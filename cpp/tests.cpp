#include "app.h"
#include "macro_commands.h"
#include <random>
#include <winioctl.h>
namespace lp {
static Json evaluate(Document &d, const wchar_t *script) {
    struct Result {
        bool done = false;
        HRESULT hr = E_PENDING;
        std::string value;
    };
    auto result = std::make_shared<Result>();
    auto hr =
        d.browser->ExecuteScript(script, Microsoft::WRL::Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                                             [result](HRESULT hr, LPCWSTR text) -> HRESULT {
                                                 result->hr = hr;
                                                 result->value = text ? narrow(text) : "null";
                                                 result->done = true;
                                                 return S_OK;
                                             })
                                             .Get());
    if (FAILED(hr))
        fail(L"미리보기 스크립트 요청 실패");
    auto until = GetTickCount64() + 5000;
    while (!result->done && GetTickCount64() < until) {
        pump();
        Sleep(5);
    }
    if (!result->done || FAILED(result->hr))
        fail(L"미리보기 스크립트 응답 실패");
    return Json::parse(result->value);
}
static bool untilValue(Document &d, const wchar_t *script, const Json &expected) {
    auto until = GetTickCount64() + 10000;
    while (GetTickCount64() < until) {
        if (evaluate(d, script) == expected)
            return true;
        pump();
        Sleep(20);
    }
    return false;
}
class TestFileData final : public IDataObject {
    std::atomic<ULONG> refs{1};
    HGLOBAL memory = nullptr;

  public:
    explicit TestFileData(const std::vector<fs::path> &files) {
        std::wstring paths;
        for (auto &p : files) {
            paths += p.wstring();
            paths.push_back(0);
        }
        paths.push_back(0);
        memory = GlobalAlloc(GHND, sizeof(DROPFILES) + paths.size() * sizeof(wchar_t));
        auto p = static_cast<DROPFILES *>(GlobalLock(memory));
        p->pFiles = sizeof(DROPFILES);
        p->fWide = TRUE;
        memcpy(reinterpret_cast<BYTE *>(p) + sizeof(DROPFILES), paths.data(), paths.size() * sizeof(wchar_t));
        GlobalUnlock(memory);
    }
    ~TestFileData() { GlobalFree(memory); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
        *out = nullptr;
        if (id == IID_IUnknown || id == IID_IDataObject) {
            *out = this;
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        auto n = --refs;
        if (!n)
            delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC *f, STGMEDIUM *s) override {
        if (FAILED(QueryGetData(f)))
            return DV_E_FORMATETC;
        s->tymed = TYMED_HGLOBAL;
        s->hGlobal = static_cast<HGLOBAL>(OleDuplicateData(memory, CF_HDROP, 0));
        s->pUnkForRelease = nullptr;
        return s->hGlobal ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC *f) override {
        return f->cfFormat == CF_HDROP && (f->tymed & TYMED_HGLOBAL) ? S_OK : DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC *, STGMEDIUM *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC *, FORMATETC *f) override {
        f->ptd = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC *, STGMEDIUM *, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD, IEnumFORMATETC **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC *, DWORD, IAdviseSink *, DWORD *) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA **) override { return OLE_E_ADVISENOTSUPPORTED; }
};
class TestDropSource final : public IDropSource {
    std::atomic<ULONG> refs{1};
    ULONGLONG started = GetTickCount64();

  public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
        *out = nullptr;
        if (id == IID_IUnknown || id == IID_IDropSource) {
            *out = this;
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        auto n = --refs;
        if (!n)
            delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape, DWORD) override {
        return escape ? DRAGDROP_S_CANCEL : GetTickCount64() - started >= 120 ? DRAGDROP_S_DROP : S_OK;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }
};
static HRESULT oleFileDrop(HWND target, const std::vector<fs::path> &files) {
    POINT before{}, point{100, 100};
    GetCursorPos(&before);
    auto root = GetAncestor(target, GA_ROOT);
    ShowWindow(root, SW_SHOWNORMAL);
    // A background build runner may not take foreground activation. Keep this
    // isolated test window above other apps while the real OLE drop hit-tests.
    SetWindowPos(root, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetForegroundWindow(root);
    pump();
    ClientToScreen(target, &point);
    SetCursorPos(point.x, point.y);
    pump();
    ComPtr<IDataObject> data;
    data.Attach(new TestFileData(files));
    ComPtr<IDropSource> source;
    source.Attach(new TestDropSource());
    auto thread = GetCurrentThreadId();
    std::thread events([thread, point] {
        for (int i = 0; i < 6; i++) {
            Sleep(60);
            PostThreadMessageW(thread, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(point.x, point.y));
        }
        PostThreadMessageW(thread, WM_LBUTTONUP, 0, MAKELPARAM(point.x, point.y));
    });
    DWORD effect = 0;
    auto result = DoDragDrop(data.Get(), source.Get(), DROPEFFECT_COPY, &effect);
    if (effect == DROPEFFECT_NONE) {
        wchar_t hitClass[128]{};
        POINT cursor{}; GetCursorPos(&cursor);
        GetClassNameW(WindowFromPoint(cursor), hitClass, 128);
        std::ofstream(exeDir() / L"test-results/ole-diagnosis.txt")
            << "visible=" << IsWindowVisible(target) << " target=" << point.x << ',' << point.y
            << " cursor=" << cursor.x << ',' << cursor.y << " hit=" << narrow(hitClass);
    }
    events.join();
    SetWindowPos(root, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetCursorPos(before.x, before.y);
    pump();
    return result;
}
static void setText(Document &d, const std::string &text) {
    sci(d.editor, SCI_SETREADONLY, 0);
    sci(d.editor, SCI_CLEARALL);
    sci(d.editor, SCI_ADDTEXT, text.size(), reinterpret_cast<LPARAM>(text.data()));
    sci(d.editor, SCI_EMPTYUNDOBUFFER);
    sci(d.editor, SCI_SETSAVEPOINT);
    d.recovered = false;
}
static std::string all(Document &d) {
    return textRange(d.editor, 0, sci(d.editor, SCI_GETLENGTH));
}
static void screenshot(HWND window, const fs::path &path) {
    ShowWindow(window, SW_SHOWNORMAL);
    SetForegroundWindow(window);
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
    RECT bounds{};
    GetWindowRect(window, &bounds);
    int width = std::min(bounds.right - bounds.left, monitor.rcWork.right - monitor.rcWork.left);
    int height = std::min(bounds.bottom - bounds.top, monitor.rcWork.bottom - monitor.rcWork.top);
    int left = std::clamp(bounds.left, monitor.rcWork.left, monitor.rcWork.right - width);
    int top = std::clamp(bounds.top, monitor.rcWork.top, monitor.rcWork.bottom - height);
    SetWindowPos(window, nullptr, left, top, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    UpdateWindow(window);
    auto presented = GetTickCount64() + 150;
    while (GetTickCount64() < presented) {
        pump();
        Sleep(5);
    }
    RECT r{};
    GetWindowRect(window, &r);
    HDC screen = GetDC(window), memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, r.right - r.left, r.bottom - r.top);
    auto previous = SelectObject(memory, bitmap);
    if (GetForegroundWindow() == window) {
        auto desktop = GetDC(nullptr);
        BitBlt(memory, 0, 0, r.right - r.left, r.bottom - r.top, desktop, r.left, r.top,
               SRCCOPY | CAPTUREBLT);
        ReleaseDC(nullptr, desktop);
    } else
        PrintWindow(window, memory, 2);
    SelectObject(memory, previous);
    Gdiplus::Bitmap image(bitmap, nullptr);
    CLSID png{};
    CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}", &png);
    image.Save(path.c_str(), &png, nullptr);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(window, screen);
}
struct AboutProbe {
    HWND owner;
    fs::path image;
    bool checked = false, valid = false;
};
static AboutProbe *aboutProbe = nullptr;
struct TabUiProbe {
    int writes = 0, textWrites = 0, imageWrites = 0, paints = 0, erases = 0;
};
static LRESULT CALLBACK tabUiProbeProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
    auto &probe = *reinterpret_cast<TabUiProbe *>(data);
    if (m == TCM_SETITEMW || m == TCM_SETITEMA) {
        probe.writes++;
        auto item = reinterpret_cast<TCITEMW *>(l);
        if (item->mask & TCIF_TEXT)
            probe.textWrites++;
        if (item->mask & TCIF_IMAGE)
            probe.imageWrites++;
    }
    if (m == WM_PAINT)
        probe.paints++;
    if (m == WM_ERASEBKGND)
        probe.erases++;
    return DefSubclassProc(h, m, w, l);
}
static void tabUiTests(App &a, Document &doc, const fs::path &output,
                       const std::function<void(bool, const std::string &)> &check) {
    auto originalName = doc.name;
    auto extra = a.newDocument("background.txt");
    a.select(0);
    setText(doc, "unchanged\nsecond line");
    a.update();
    ShowWindow(a.window, SW_SHOWNORMAL);
    SetForegroundWindow(a.window);
    POINT cursor{}, inside{150, 150};
    GetCursorPos(&cursor);
    ClientToScreen(doc.editor, &inside);
    SetCursorPos(inside.x, inside.y);
    TabUiProbe probe;
    SetWindowSubclass(a.tabs, tabUiProbeProc, 0x5155, reinterpret_cast<DWORD_PTR>(&probe));
    struct Cleanup {
        App &a;
        POINT cursor;
        ~Cleanup() {
            KillTimer(a.window, 1);
            RemoveWindowSubclass(a.tabs, tabUiProbeProc, 0x5155);
            SetCursorPos(cursor.x, cursor.y);
        }
    } cleanup{a, cursor};
    auto settle = GetTickCount64() + 250;
    while (GetTickCount64() < settle) {
        pump();
        Sleep(5);
    }
    UpdateWindow(a.tabs);
    probe = {};
    a.backupTick = GetTickCount64();
    SetTimer(a.window, 1, 200, nullptr);
    auto idle = GetTickCount64() + 3400;
    while (GetTickCount64() < idle) {
        pump();
        Sleep(5);
    }
    KillTimer(a.window, 1);
    writeJson(output / L"tab-idle-observation.json",
              {{"IdleMilliseconds", 3400},
               {"TabWrites", probe.writes},
               {"TextWrites", probe.textWrites},
               {"ImageWrites", probe.imageWrites},
               {"Paints", probe.paints},
               {"BackgroundErases", probe.erases}},
              false);
    check(probe.writes == 0, "Idle timers and automatic backup do not rewrite any tab header (writes=" +
                                 std::to_string(probe.writes) + ")");
    check(disk::exists(a.profile / L"session.json"),
          "Tab redraw suppression keeps the automatic session backup running");
    probe = {};
    for (int i = 0; i < 100; i++) {
        sci(doc.editor, SCI_GOTOPOS, i % 10);
        a.update();
    }
    check(probe.writes == 0, "Repeated cursor movement and UI updates do not rewrite unchanged tab headers");
    probe = {};
    str(doc.editor, SCI_REPLACESEL, "X");
    a.update();
    check(probe.writes == 1 && probe.textWrites == 0 && probe.imageWrites == 1,
          "First unsaved edit updates only that tab's status icon once");
    probe = {};
    str(doc.editor, SCI_REPLACESEL, "Y");
    a.update();
    check(probe.writes == 0, "Further edits preserve an already-unsaved tab header without repaint requests");
    probe = {};
    sci(doc.editor, SCI_SETSAVEPOINT);
    a.update();
    check(probe.writes == 1 && probe.textWrites == 0 && probe.imageWrites == 1,
          "Saving changes the tab icon back without rewriting its title");
    probe = {};
    doc.name = "renamed document.txt";
    a.update();
    check(probe.writes == 1 && probe.textWrites == 1 && probe.imageWrites == 0,
          "Renaming updates only the changed tab title");
    probe = {};
    a.compareIds.insert(doc.id);
    a.update();
    check(probe.writes == 1 && probe.textWrites == 1 && probe.imageWrites == 0,
          "Comparison selection refreshes its title marker only once");
    a.compareIds.clear();
    a.update();
    probe = {};
    str(extra->editor, SCI_REPLACESEL, "background edit");
    a.update();
    check(probe.writes == 1 && probe.textWrites == 0 && probe.imageWrites == 1,
          "Editing an inactive document updates only that background tab icon");
    TabCtrl_DeleteItem(a.tabs, 1);
    a.docs.erase(a.docs.begin() + 1);
    a.active = 0;
    doc.name = originalName;
    setText(doc, "");
    a.select(0);
    probe = {};
    a.update();
    check(probe.writes == 0, "Closing another tab leaves surviving tab state synchronized");
}
static VOID CALLBACK inspectAbout(HWND, UINT, UINT_PTR timer, DWORD) {
    if (!aboutProbe) {
        KillTimer(nullptr, timer);
        return;
    }
    HWND dialog = FindWindowExW(nullptr, nullptr, L"LHPad.About", nullptr);
    if (!dialog || GetWindow(dialog, GW_OWNER) != aboutProbe->owner)
        return;
    RECT icon{}, button{}, client{};
    GetWindowRect(GetDlgItem(dialog, 10), &icon);
    GetWindowRect(GetDlgItem(dialog, IDOK), &button);
    GetClientRect(dialog, &client);
    POINT corner{button.right, button.bottom};
    ScreenToClient(dialog, &corner);
    auto dpi = GetDpiForWindow(dialog);
    aboutProbe->valid = icon.right - icon.left == MulDiv(128, dpi, 96) &&
                        button.bottom - button.top == MulDiv(32, dpi, 96) && corner.x <= client.right &&
                        corner.y <= client.bottom;
    KillTimer(nullptr, timer);
    screenshot(dialog, aboutProbe->image);
    aboutProbe->checked = true;
    SendMessageW(dialog, WM_COMMAND, IDOK, 0);
}
static void tabCloseTests(App &a, const fs::path &output,
                          const std::function<void(bool, const std::string &)> &check) {
    auto primary = a.docs[0]->id;
    for (int dpi : {96, 120, 144, 192}) {
        RECT item{0, 0, MulDiv(240, dpi, 96), MulDiv(40, dpi, 96)};
        auto close = App::tabCloseBounds(item, dpi);
        check(close.right - close.left == MulDiv(22, dpi, 96) &&
                  close.bottom - close.top == close.right - close.left &&
                  close.right == item.right - MulDiv(8, dpi, 96),
              "Close button has one square drawing/hit rectangle at DPI " + std::to_string(dpi));
    }
    auto target = a.newDocument("hover-button.cpp");
    a.update();
    POINT originalCursor{};
    GetCursorPos(&originalCursor);
    struct RestoreCursor {
        POINT point;
        ~RestoreCursor() { SetCursorPos(point.x, point.y); }
    } restore{originalCursor};
    auto cursor = [&](POINT point, WPARAM buttons = 0) {
        POINT screen = point;
        ClientToScreen(a.tabs, &screen);
        SetCursorPos(screen.x, screen.y);
        SendMessageW(a.tabs, WM_MOUSEMOVE, buttons, MAKELPARAM(point.x, point.y));
        UpdateWindow(a.tabs);
    };
    auto sample = [&](RECT r) {
        auto dc = GetDC(a.tabs);
        auto color = GetPixel(dc, r.left + 5, r.top + 5);
        ReleaseDC(a.tabs, dc);
        return color;
    };
    auto close = a.tabCloseRect(1);
    POINT center{(close.left + close.right) / 2, (close.top + close.bottom) / 2};
    check(!IsRectEmpty(&close) && a.tabCloseHit(center) == 1,
          "The visible X center is exactly the close-button hit target");
    POINT padding{close.right + 2, center.y};
    check(a.tabCloseHit(padding) == -1, "Padding to the right of X is outside the close-button target");
    SendMessageW(a.tabs, WM_MOUSELEAVE, 0, 0);
    UpdateWindow(a.tabs);
    auto normal = sample(close);
    cursor(center);
    auto hover = sample(close);
    check(a.hotTabClose == 1 && hover != CLR_INVALID && hover != normal,
          "Hovering the actual X displays a distinct button background");
    SendMessageW(a.tabs, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(center.x, center.y));
    UpdateWindow(a.tabs);
    check(a.pressedTabClose == target->id && GetCapture() == a.tabs && sample(close) != hover,
          "Pressing X captures the mouse and displays its pressed background");
    RECT item{};
    TabCtrl_GetItemRect(a.tabs, 1, &item);
    POINT title{item.left + MulDiv(12, a.dpi, 96), center.y};
    cursor(title, MK_LBUTTON);
    SendMessageW(a.tabs, WM_LBUTTONUP, 0, MAKELPARAM(title.x, title.y));
    check(a.docs.size() == 2 && a.pressedTabClose.empty() && GetCapture() != a.tabs,
          "Dragging out of X before release cancels closing and releases capture");
    close = a.tabCloseRect(1);
    center = {(close.left + close.right) / 2, (close.top + close.bottom) / 2};
    padding = {close.right + 2, center.y};
    SendMessageW(a.tabs, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(padding.x, padding.y));
    SendMessageW(a.tabs, WM_LBUTTONUP, 0, MAKELPARAM(padding.x, padding.y));
    check(a.docs.size() == 2, "Clicking to the right of X selects the tab without closing it");
    cursor(center);
    a.update();
    screenshot(a.window, output / L"tab-close-hover.png");
    SendMessageW(a.tabs, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(center.x, center.y));
    SendMessageW(a.tabs, WM_LBUTTONUP, 0, MAKELPARAM(center.x, center.y));
    check(a.docs.size() == 1 && a.docs[0]->id == primary,
          "Click and release on the drawn X closes the intended document");
    auto background = a.newDocument("background-close.txt");
    a.select(0);
    close = a.tabCloseRect(1);
    center = {(close.left + close.right) / 2, (close.top + close.bottom) / 2};
    cursor(center);
    SendMessageW(a.tabs, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(center.x, center.y));
    SendMessageW(a.tabs, WM_LBUTTONUP, 0, MAKELPARAM(center.x, center.y));
    check(a.docs.size() == 1 && a.current()->id == primary,
          "Closing an inactive tab does not activate it or close the current document");
    for (int i = 0; i < 12; i++)
        a.newDocument("scrolling-document-" + std::to_string(i) + ".cpp");
    int last = static_cast<int>(a.docs.size() - 1);
    a.select(last);
    close = a.tabCloseRect(last);
    center = {(close.left + close.right) / 2, (close.top + close.bottom) / 2};
    check(!IsRectEmpty(&close) && a.tabCloseHit(center) == last,
          "Close-button geometry follows horizontally scrolled tabs");
    auto arrows = FindWindowExW(a.tabs, nullptr, UPDOWN_CLASSW, nullptr);
    if (arrows && IsWindowVisible(arrows)) {
        RECT arrow{};
        GetWindowRect(arrows, &arrow);
        MapWindowPoints(nullptr, a.tabs, reinterpret_cast<POINT *>(&arrow), 2);
        check(a.tabCloseHit({(arrow.left + arrow.right) / 2, (arrow.top + arrow.bottom) / 2}) == -1,
              "Tab scroll arrows cannot be mistaken for a close button");
    }
    while (a.docs.size() > 1) {
        TabCtrl_DeleteItem(a.tabs, static_cast<int>(a.docs.size() - 1));
        a.docs.pop_back();
    }
    a.pressedTabClose.clear();
    a.hotTabClose = -1;
    a.active = 0;
    a.select(0);
    cursor({-1, -1});
}
int runTests(App &a, bool full) {
    KillTimer(a.window, 1);
    std::vector<std::string> results;
    int exit = 1;
    auto output = exeDir() / L"test-results";
    disk::create_directories(output);
    auto fixture = a.profile / L"fixtures";
    disk::create_directories(fixture);
    auto check = [&](bool condition, const std::string &name) {
        if (!condition)
            throw std::runtime_error(name);
        results.push_back("PASS " + name);
    };
    auto write = [&](const wchar_t *name, const std::string &text) {
        auto path = fixture / name;
        writeAtomic(path, Bytes(text.begin(), text.end()));
        return path;
    };
    try {
        a.starting = false;
        auto text = a.newDocument();
        tabUiTests(a, *text, output, check);
        tabCloseTests(a, output, check);
        check(GetModuleHandleW(L"coreclr.dll") == nullptr && GetModuleHandleW(L"clr.dll") == nullptr,
              "Process runs without .NET CLR");
        check(a.languages.items.size() > 80, "Native language catalog retains Notepad++ mappings");
        for (auto encoding : std::vector<Encoding>{{"UTF8", 65001},
                                                   {"UTF8 BOM", 65001, true},
                                                   {"UTF16LE", 1200, true},
                                                   {"UTF16BE", 1201, true},
                                                   {"UTF32LE", 12000, true},
                                                   {"UTF32BE", 12001, true},
                                                   {"CP949", 949},
                                                   {"EUC-KR", 51949}}) {
            std::string source = "한글 테스트\r\nABC 123";
            auto bytes = encode(source, encoding);
            check(decode(bytes, encoding) == source, "Encoding roundtrip " + encoding.name);
            auto path = fixture / wide(encoding.name + ".txt");
            saveText(path, source.size(), encoding, [&](Pos p, Pos n) {
                return source.substr(static_cast<size_t>(p), static_cast<size_t>(n));
            });
            auto detected = detect(path);
            std::string loaded;
            readText(path, encoding, encoding.bom ? detected.skip : 0,
                     [&](std::string_view s) { loaded += s; });
            check(loaded == source, "Native file stream and BOM " + encoding.name);
        }
        bool rejected = false;
        try {
            encode("한글😀", {"ASCII", 20127});
        } catch (...) {
            rejected = true;
        }
        check(rejected, "Unrepresentable text rejects lossy encoding");
        for (auto encoding :
             std::vector<Encoding>{{"UTF8", 65001}, {"UTF16", 1200}, {"CP949", 949}, {"GB18030", 54936}}) {
            std::string source(256 * 1024 - 1, 'a');
            source += "한글𠮷";
            if (encoding.cp == 949)
                source.resize(source.size() - 4);
            auto path = fixture / wide(encoding.name + "-boundary.txt");
            saveText(path, source.size(), encoding, [&](Pos p, Pos n) {
                return source.substr(static_cast<size_t>(p), static_cast<size_t>(n));
            });
            std::string loaded;
            readText(path, encoding, 0, [&](std::string_view s) { loaded += s; });
            check(loaded == source, "Multibyte character across streaming boundary " + encoding.name);
        }
        auto atomic = write(L"atomic.txt", "original");
        rejected = false;
        try {
            std::string bad = "한글";
            saveText(atomic, bad.size(), {"ASCII", 20127}, [&](Pos p, Pos n) {
                return bad.substr(static_cast<size_t>(p), static_cast<size_t>(n));
            });
        } catch (...) {
            rejected = true;
        }
        check(rejected && readAll(atomic) == Bytes({'o', 'r', 'i', 'g', 'i', 'n', 'a', 'l'}),
              "Failed encoding save preserves original file atomically");
        auto deep = fixture;
        for (int i = 0; i < 8; i++)
            deep /= L"한글 긴 폴더 이름 012345678901234567890123456789";
        disk::create_directories(deep);
        auto longFile = deep / L"260자를 초과하는 경로.txt";
        std::string longText = "긴 경로 한글 문서\n";
        saveText(longFile, longText.size(), Encoding{}, [&](Pos p, Pos n) {
            return longText.substr(static_cast<size_t>(p), static_cast<size_t>(n));
        });
        std::string longLoaded;
        readText(longFile, Encoding{}, 0, [&](std::string_view text) { longLoaded += text; });
        check(longFile.wstring().size() > 260 && longLoaded == longText,
              "Unicode file read and atomic save work beyond MAX_PATH without OS policy changes");
        writeJson(deep / L"session.json", {{"Version", 1}, {"Value", "first"}});
        writeJson(deep / L"session.json", {{"Version", 1}, {"Value", "second"}});
        check(readJson(deep / L"session.json")["Value"] == "second" &&
                  readJson(deep / L"session.json.bak")["Value"] == "first",
              "Recovery JSON and its atomic backup work beyond MAX_PATH");
        check(stamp(longFile).exists && disk::file_size(longFile) == longText.size(),
              "Long Unicode paths retain file identity and size metadata");
        auto h = text->editor;
        setText(*text, "한글 첫 줄\r\nsecond\r\nthird");
        str(h, SCI_REPLACESEL, "X");
        check(text->dirty(), "Native edit notification sets dirty tab state");
        sci(h, SCI_UNDO);
        check(all(*text) == "한글 첫 줄\r\nsecond\r\nthird", "Undo preserves Korean UTF8");
        for (auto eolMode : {SC_EOL_CRLF, SC_EOL_LF, SC_EOL_CR}) {
            setText(*text, "\t\t한글");
            sci(h, SCI_GOTOPOS, sci(h, SCI_GETLENGTH));
            sci(h, SCI_SETEOLMODE, eolMode);
            sci(h, SCI_NEWLINE);
            std::string e = eolMode == SC_EOL_CRLF ? "\r\n" : eolMode == SC_EOL_LF ? "\n" : "\r";
            check(all(*text) == "\t\t한글" + e + "\t\t",
                  "Auto-indent literal tabs EOL " + std::to_string(eolMode));
            sci(h, SCI_UNDO);
            check(all(*text) == "\t\t한글", "Newline and indentation undo as one action");
        }
        a.autoIndent = false;
        setText(*text, "  x");
        sci(h, SCI_GOTOPOS, 3);
        sci(h, SCI_NEWLINE);
        check(!all(*text).ends_with("  "), "Auto-indent preference disables continuation");
        a.autoIndent = true;
        setText(*text, "  a\n\tb");
        sci(h, SCI_SETSELECTION, 3, 3);
        sci(h, SCI_ADDSELECTION, 6, 6);
        sci(h, SCI_SETEOLMODE, SC_EOL_LF);
        sci(h, SCI_NEWLINE);
        check(all(*text) == "  a\n  \n\tb\n\t", "Multi-caret Enter preserves each original indentation");
        sci(h, SCI_UNDO);
        check(all(*text) == "  a\n\tb", "Multi-caret auto-indent grouped undo");
        {
            // 1.2 editing basics: comments, case, braces, recent files and tab management.
            auto indexOf = [&](const Document *doc) {
                for (size_t i = 0; i < a.docs.size(); i++)
                    if (a.docs[i].get() == doc)
                        return static_cast<int>(i);
                return -1;
            };
            a.select(indexOf(text.get()));
            auto useLanguage = [&](const char *name) {
                text->language = name;
                a.languages.apply(h, name);
            };
            useLanguage("cpp");
            setText(*text, "  a\n\n  b");
            sci(h, SCI_SETSEL, 0, sci(h, SCI_GETLENGTH));
            a.toggleComment();
            check(all(*text) == "  // a\n\n  // b", "Line comment toggle keeps indentation and skips blank lines");
            a.toggleComment();
            check(all(*text) == "  a\n\n  b", "Line comment toggle removes its own marker");
            sci(h, SCI_UNDO);
            check(all(*text) == "  // a\n\n  // b", "Comment toggle undoes as one action");
            setText(*text, "x();");
            sci(h, SCI_GOTOPOS, 2);
            a.toggleComment();
            check(all(*text) == "// x();" && sci(h, SCI_GETCURRENTPOS) == 5,
                  "Comment toggle without selection keeps the caret on the same character");
            useLanguage("html");
            setText(*text, "  <p>x</p>");
            sci(h, SCI_GOTOPOS, 4);
            a.toggleComment();
            check(all(*text) == "  <!-- <p>x</p> -->", "HTML block comment wraps the current line text");
            a.toggleComment();
            check(all(*text) == "  <p>x</p>", "HTML block comment toggle unwraps the selection");
            useLanguage("normal");
            bool refused = false;
            try {
                a.toggleComment();
            } catch (const std::exception &) {
                refused = true;
            }
            check(refused && all(*text) == "  <p>x</p>", "Comment toggle refuses languages without markers");
            setText(*text, "hello wORLD don't");
            sci(h, SCI_SETSEL, 0, 5);
            a.command(UpperCase);
            check(all(*text) == "HELLO wORLD don't", "Upper case converts only the selection");
            sci(h, SCI_SETSEL, 0, sci(h, SCI_GETLENGTH));
            a.command(LowerCase);
            check(all(*text) == "hello world don't", "Lower case converts the selection");
            a.titleCase();
            check(all(*text) == "Hello World Don't" && sci(h, SCI_GETSELECTIONSTART) == 0 &&
                      sci(h, SCI_GETSELECTIONEND) == sci(h, SCI_GETLENGTH),
                  "Title case keeps apostrophes inside words and the selection");
            setText(*text, "한글 abc");
            sci(h, SCI_SETSEL, 0, sci(h, SCI_GETLENGTH));
            a.titleCase();
            check(all(*text) == "한글 Abc", "Title case leaves Korean text unchanged");
            sci(h, SCI_UNDO);
            check(all(*text) == "한글 abc", "Title case undoes as one action");
            setText(*text, "f(a[b]c)");
            sci(h, SCI_GOTOPOS, 1);
            a.jumpBrace();
            check(sci(h, SCI_GETCURRENTPOS) == 8, "Brace jump from an opening brace lands after its partner");
            a.jumpBrace();
            check(sci(h, SCI_GETCURRENTPOS) == 1, "Brace jump returns before the opening brace");
            auto savedRecent = a.recentFiles;
            a.recentFiles.clear();
            for (int i = 0; i < 20; i++)
                a.addRecent(fixture / (L"recent-" + std::to_wstring(i) + L".txt"));
            a.addRecent(fixture / L"RECENT-5.txt");
            check(a.recentFiles.size() == recentLimit && a.recentFiles.front().filename() == L"RECENT-5.txt" &&
                      std::none_of(a.recentFiles.begin() + 1, a.recentFiles.end(),
                                   [](const fs::path &p) { return !_wcsicmp(p.filename().c_str(), L"recent-5.txt"); }),
                  "Recent files drop case-insensitive duplicates and keep the newest 15");
            check(GetMenuItemCount(a.recentMenu) == static_cast<int>(recentLimit) + 2, "Recent file menu lists entries and a clear command");
            a.recentFiles = savedRecent;
            a.rebuildRecentMenu();
            auto count = a.docs.size();
            auto first = a.newDocument(), second = a.newDocument();
            int left = indexOf(first.get()), right = indexOf(second.get());
            a.select(left);
            a.moveTab(left, right);
            check(indexOf(first.get()) == right && indexOf(second.get()) == left && a.active == right &&
                      TabCtrl_GetCurSel(a.tabs) == right && TabCtrl_GetItemCount(a.tabs) == static_cast<int>(a.docs.size()),
                  "Moving a tab reorders documents and keeps the moved tab active");
            a.select(left);
            a.closeTabs(1);
            check(a.docs.size() == count + 1 && indexOf(second.get()) >= 0 && indexOf(first.get()) < 0,
                  "Close tabs to the right keeps the active tab and those on its left");
            a.closeTab(indexOf(second.get()));
            auto reopenPath = write(L"reopen.txt", "reopen");
            a.open(reopenPath);
            a.closeTab(static_cast<int>(a.docs.size() - 1));
            check(!a.closedTabs.empty() && a.closedTabs.back().path == reopenPath, "Closing a saved tab remembers it");
            a.reopenClosed();
            check(a.current() && a.current()->path == reopenPath && a.docs.size() == count + 1,
                  "Reopen closed tab restores the last closed file");
            a.closeTab(a.active);
            a.recentFiles = savedRecent;
            a.rebuildRecentMenu();
            a.select(indexOf(text.get()));
            sci(h, SCI_SETEOLMODE, SC_EOL_LF);
            // 1.3 search: find all, results panel, mark all and find in files.
            setText(*text, "alpha\nbeta alpha\n");
            a.search = {"alpha", "", 0};
            check(a.findAll() == 2 && a.hits.size() == 2 && a.hits[1].line == 1 && a.hits[1].column == 5 &&
                      a.hits[1].text == L"beta alpha" && a.resultsVisible &&
                      ListView_GetItemCount(a.resultsList) == 2 && IsWindowVisible(a.resultsList),
                  "Find all lists each match with line, column and text in the results panel");
            sci(h, SCI_GOTOPOS, 0);
            a.goToHit(1);
            check(sci(h, SCI_GETSELECTIONSTART) == 11 && sci(h, SCI_GETSELECTIONEND) == 16,
                  "Activating a search result selects the match");
            a.navigateHits(false);
            check(sci(h, SCI_GETSELECTIONSTART) == 0 && a.hitIndex == 0, "Next result wraps to the first match");
            check(a.markAll() == 2 && (sci(h, SCI_INDICATORALLONFOR, 12) & (1 << markIndicator)) &&
                      !(sci(h, SCI_INDICATORALLONFOR, 8) & (1 << markIndicator)),
                  "Mark all highlights only the matches");
            a.clearMarks();
            check(!(sci(h, SCI_INDICATORALLONFOR, 12) & (1 << markIndicator)), "Clear marks removes highlights");
            check(all(*text) == "alpha\nbeta alpha\n" && !text->dirty(), "Find all and mark all never edit the text");
            auto searchRoot = fixture / L"find-in-files";
            disk::create_directories(searchRoot / L"sub");
            disk::create_directories(searchRoot / L".git");
            write(L"find-in-files\\a.txt", "needle\r\nx");
            write(L"find-in-files\\sub\\b.cpp", "int needle = 1;");
            write(L"find-in-files\\c.log", "needle");
            write(L"find-in-files\\.git\\d.txt", "needle");
            writeAtomic(searchRoot / L"e.txt", Bytes{'n', 'e', 'e', 'd', 'l', 'e', 0, 0, 1, 2, 0, 3});
            SearchOptions fileSearch;
            fileSearch.query = "needle";
            check(a.findInFolder(searchRoot, L"*.txt;*.cpp", true, fileSearch) == 2 && a.hits.size() == 2 &&
                      a.hits[0].source == L"a.txt" && a.hits[1].source == L"sub\\b.cpp" && a.hits[1].column == 4,
                  "Find in files filters names, recurses, skips binary files and .git");
            check(a.findInFolder(searchRoot, L"*.txt", false, fileSearch) == 1, "Find in files can stay in one folder");
            auto savedFiles = a.recentFiles;
            a.findInFolder(searchRoot, L"*.cpp", true, fileSearch);
            auto docCount = a.docs.size();
            a.goToHit(0);
            check(a.docs.size() == docCount + 1 && a.current() && a.current()->path.filename() == L"b.cpp" &&
                      sci(a.current()->editor, SCI_GETSELECTIONSTART) == 4 &&
                      sci(a.current()->editor, SCI_GETSELECTIONEND) == 10,
                  "Opening a find-in-files result opens the file at the match");
            a.closeTab(a.active);
            a.recentFiles = savedFiles;
            a.rebuildRecentMenu();
            a.showResults(false);
            check(!IsWindowVisible(a.resultsList), "Results panel can be hidden");
            a.search = {};
            a.select(indexOf(text.get()));
            // 1.4 navigation: bookmarks, same-document split and outline.
            auto marked = [&](Pos line) { return (sci(h, SCI_MARKERGET, line) & (1 << bookmarkMarker)) != 0; };
            setText(*text, "a\nb\nc\nd");
            sci(h, SCI_GOTOLINE, 1);
            a.toggleBookmark();
            sci(h, SCI_GOTOLINE, 3);
            a.toggleBookmark();
            check(marked(1) && marked(3) && !marked(0) && !marked(2), "Ctrl+F2 toggles a bookmark on the caret line");
            sci(h, SCI_GOTOPOS, 0);
            a.nextBookmark(false);
            bool forward = sci(h, SCI_LINEFROMPOSITION, sci(h, SCI_GETCURRENTPOS)) == 1;
            a.nextBookmark(false);
            forward = forward && sci(h, SCI_LINEFROMPOSITION, sci(h, SCI_GETCURRENTPOS)) == 3;
            a.nextBookmark(false);
            forward = forward && sci(h, SCI_LINEFROMPOSITION, sci(h, SCI_GETCURRENTPOS)) == 1;
            a.nextBookmark(true);
            check(forward && sci(h, SCI_LINEFROMPOSITION, sci(h, SCI_GETCURRENTPOS)) == 3,
                  "F2 / Shift+F2 move between bookmarks and wrap around");
            check(a.copyBookmarked() == "b\nd", "Copy bookmarked lines joins them with the document line break");
            check(a.deleteBookmarked() == 2 && all(*text) == "a\nc" && !marked(0) && !marked(1),
                  "Delete bookmarked lines removes them without moving bookmarks to neighbours");
            sci(h, SCI_UNDO);
            check(all(*text) == "a\nb\nc\nd", "Deleting bookmarked lines undoes as one action");
            a.clearBookmarks();
            setText(*text, "x1\ny\nx2 x3");
            a.search = {"x", "", 0};
            check(a.bookmarkMatches() == 2 && marked(0) && !marked(1) && marked(2),
                  "Bookmark matches marks each matching line once");
            a.clearBookmarks();
            check(!marked(0) && !marked(2), "Clear bookmarks removes all bookmarks");
            a.search = {};
            setText(*text, "shared");
            a.toggleSplit();
            auto lower = text->secondary;
            check(lower && sci(lower, SCI_GETDOCPOINTER) == sci(h, SCI_GETDOCPOINTER) && IsWindowVisible(lower) &&
                      IsWindowVisible(h),
                  "Split view shows a second editor on the same document");
            str(lower, SCI_APPENDTEXT, " text", 5);
            check(all(*text) == "shared text" && text->dirty(), "Edits in either split view change the one document");
            SendMessageW(lower, WM_SETFOCUS, 0, 0);
            check(text->editor == lower && text->secondary == h, "Focusing the other split view makes commands target it");
            SendMessageW(h, WM_SETFOCUS, 0, 0);
            check(text->editor == h && text->secondary == lower, "Focus returns to the first split view");
            a.toggleSplit();
            check(!text->secondary && IsWindow(h) && !IsWindow(lower) && all(*text) == "shared text",
                  "Closing the split keeps the document and its active editor");
            auto names = [](const std::vector<OutlineItem> &items) {
                std::wstring s;
                for (auto &item : items)
                    s += std::to_wstring(item.line) + L":" + std::to_wstring(item.depth) + L":" + item.name + L"|";
                return s;
            };
            auto cppOutline = buildOutline("namespace lp {\nclass App {\n    void run();\n};\nint App::start(int x) const {\n"
                                           "    if (x) return 1;\n    call(x);\n}\nstatic void helper()\n{\n}\n",
                                           "cpp");
            check(names(cppOutline) == L"0:0:namespace lp|1:0:class App|4:0:App::start()|8:0:helper()|",
                  "Outline finds C++ namespaces, classes and definitions but not declarations or calls");
            check(names(buildOutline("class A:\n    def f(self):\n        pass\ndef g():\n    pass\n", "python")) ==
                      L"0:0:class A|1:1:f()|3:0:g()|",
                  "Outline lists Python classes and functions with nesting");
            check(names(buildOutline("# 제목\n```\n# not\n```\n## Section ##\n", "markdown")) == L"0:0:제목|4:1:Section|",
                  "Outline lists Markdown headings outside code fences");
            check(names(buildOutline("CREATE OR REPLACE PROCEDURE dbo.Load AS\nselect 1;\n", "sql")) ==
                      L"0:0:PROCEDURE dbo.Load|",
                  "Outline lists SQL objects");
            check(names(buildOutline("export async function load(a) {\nconst run = async (x) => x;\n}\n", "javascript")) ==
                      L"0:0:load()|1:0:run()|",
                  "Outline lists JavaScript functions and arrow functions");
            check(buildOutline("anything", "normal").empty() && !outlineSupported("normal"),
                  "Outline is empty for languages without rules");
            useLanguage("cpp");
            setText(*text, "int main()\n{\n}\n");
            a.showOutline(true);
            check(a.outlineVisible && IsWindowVisible(a.outlineList) && ListView_GetItemCount(a.outlineList) == 1,
                  "Outline panel shows the active document's symbols");
            sci(h, SCI_GOTOPOS, sci(h, SCI_GETLENGTH));
            a.goToOutline(0);
            check(sci(h, SCI_LINEFROMPOSITION, sci(h, SCI_GETCURRENTPOS)) == 0, "Clicking an outline entry moves to its line");
            a.showOutline(false);
            useLanguage("normal");
            check(!IsWindowVisible(a.outlineList), "Outline panel can be hidden");
            // 1.5 dark mode: palette, editor recolouring without touching text, panels and menu state.
            constexpr int cppKeyword = 5; // SCE_C_WORD in Lexilla's SciLexer.h
            auto luminance = [](COLORREF c) { return (GetRValue(c) * 299 + GetGValue(c) * 587 + GetBValue(c) * 114) / 1000; };
            useLanguage("cpp");
            setText(*text, "int main() { return 0; }");
            sci(h, SCI_COLOURISE, 0, -1);
            auto keywordLight = static_cast<COLORREF>(sci(h, SCI_STYLEGETFORE, cppKeyword));
            a.darkMode = true;
            a.applyDarkMode();
            auto keywordDark = static_cast<COLORREF>(sci(h, SCI_STYLEGETFORE, cppKeyword));
            check(palette().dark && sci(h, SCI_STYLEGETBACK, STYLE_DEFAULT) == static_cast<LRESULT>(palette().editorBack) &&
                      luminance(static_cast<COLORREF>(sci(h, SCI_STYLEGETBACK, STYLE_DEFAULT))) < 60,
                  "Dark mode gives editors a dark background");
            check(luminance(keywordDark) > 140 && luminance(keywordLight) < 140 && keywordDark != keywordLight,
                  "Dark mode turns dark language colours into light ones");
            check(all(*text) == "int main() { return 0; }" && !text->dirty() &&
                      sci(h, SCI_GETSTYLEAT, 0) == cppKeyword,
                  "Switching theme keeps text, saved state and lexer styling");
            check(ListView_GetBkColor(a.outlineList) == palette().panel && ListView_GetBkColor(a.resultsList) == palette().panel,
                  "Dark mode recolours the side panels");
            check((GetMenuState(a.menu, DarkMode, MF_BYCOMMAND) & MF_CHECKED) != 0, "Dark mode menu item shows its state");
            check(luminance(adaptForeground(RGB(0, 0, 128))) > 140 && adaptBackground(RGB(255, 255, 255)) == palette().editorBack,
                  "Colour adaptation lightens text and maps white backgrounds to the editor background");
            check(contrastRatio(adaptForeground(RGB(0, 0, 255)), palette().editorBack) >= 4.5 &&
                      contrastRatio(adaptForeground(RGB(255, 0, 0)), palette().editorBack) >= 4.5,
                  "Adapted pure blue and red text keep at least 4.5:1 contrast");
            a.darkMode = false;
            a.applyDarkMode();
            check(!palette().dark && sci(h, SCI_STYLEGETBACK, STYLE_DEFAULT) == static_cast<LRESULT>(RGB(253, 253, 252)) &&
                      static_cast<COLORREF>(sci(h, SCI_STYLEGETFORE, cppKeyword)) == keywordLight &&
                      ListView_GetBkColor(a.outlineList) == GetSysColor(COLOR_WINDOW),
                  "Turning dark mode off restores the original light colours");
            useLanguage("normal");
        }
        setText(*text, "alpha beta alpha\nalpha");
        a.search = {"alpha", "가나다", 0};
        sci(h, SCI_SETSEL, 2, 5);
        Pos caret = sci(h, SCI_GETCURRENTPOS), anchor = sci(h, SCI_GETANCHOR);
        check(a.countMatches() == 3 && sci(h, SCI_GETCURRENTPOS) == caret && sci(h, SCI_GETANCHOR) == anchor,
              "Count matches preserves cursor and selection");
        check(a.countMatches(true) == 3 && all(*text) == "가나다 beta 가나다\n가나다", "Unicode replace-all");
        sci(h, SCI_UNDO);
        check(all(*text) == "alpha beta alpha\nalpha", "Replace-all grouped undo");
        setText(*text, "a1 b2 c3");
        a.search = {"([a-z])(\\d)", "\\2\\1", SCFIND_REGEXP | SCFIND_CXX11REGEX};
        check(a.countMatches(true) == 3 && all(*text) == "1a 2b 3c", "Boost regex captures and replacement");
        setText(*text, "a\nb");
        a.search = {"^", ">", SCFIND_REGEXP | SCFIND_CXX11REGEX};
        check(a.countMatches(true) == 2 && all(*text) == ">a\n>b", "Zero-width replace-all terminates");
        a.search.query = "[";
        rejected = false;
        try {
            a.countMatches();
        } catch (...) {
            rejected = true;
        }
        check(rejected, "Invalid regex rejected with validation");
        setText(*text, "{\"key\":true}");
        text->language = "json";
        a.languages.apply(h, "json");
        sci(h, SCI_COLOURISE, 0, -1);
        check(sci(h, SCI_GETSTYLEAT, 1) != sci(h, SCI_GETSTYLEAT, 7),
              "JSON native lexer applies syntax colors");
        setText(*text, "int main() {\n  if (true) {\n    return 1;\n  }\n}\n");
        text->language = "cpp";
        a.languages.apply(h, "cpp");
        a.fold(0, false);
        check(sci(h, SCI_GETLINEVISIBLE, 2) == 0, "Native code folding collapses nested blocks");
        a.fold(0, true);
        check(sci(h, SCI_GETLINEVISIBLE, 2) != 0, "Native code folding expands all levels");
        check(a.languages.detect(L"a.md") == "markdown" && a.languages.detect(L"a.json") == "json",
              "Extension auto-detection");
        auto formatted = formatText("{\"한글\":[1,2]}", "json", "\r\n");
        check(Json::parse(formatted)["한글"].size() == 2 && formatted.find("\r\n") != std::string::npos,
              "JSON formatting semantic roundtrip");
        auto xml =
            formatText("<?xml version=\"1.0\" encoding=\"UTF-8\"?><root><a>한글</a></root>", "xml", "\n");
        check(xml.find("encoding=\"UTF-8\"") != std::string::npos && xml.find('\n') != std::string::npos,
              "XML formatting preserves declaration");
        rejected = false;
        try {
            formatText("<!DOCTYPE foo [<!ENTITY xxe SYSTEM 'file:///C:/Windows/win.ini'>]><foo>&xxe;</foo>",
                       "xml", "\n");
        } catch (...) {
            rejected = true;
        }
        check(rejected, "XML formatter prohibits external entity DTDs");
        std::string sql = "WITH t AS (SELECT 'a; -- quote' AS x /* outer /* nested */ c */ FROM foo) SELECT "
                          "[id], $$a\n'b$$ FROM t WHERE x = N'한글' -- comment\n AND id >= 1;";
        check(sqlTokens(formatText(sql, "sql", "\n")) == sqlTokens(sql),
              "SQL formatting preserves strings comments and tokens");
        auto html = formatText(
            "<div><span>A</span> <span>B</span><script>if(a < b) x = '<tag>';</script></div>", "html", "\n");
        check(html.find("</span> <span>") != std::string::npos &&
                  html.find("if(a < b) x = '<tag>';") != std::string::npos,
              "HTML formatting preserves inline spaces and raw scripts");
        auto md = markdown("# 미리보기\n\n**굵게**\n\n|a|b|\n|---|---|\n|1|2|\n\n- [x] "
                           "완료\n\n```html\n<b>literal</b>\n```\n");
        check(md.find("<h1>") != std::string::npos && md.find("<table>") != std::string::npos &&
                  md.find("checkbox") != std::string::npos && md.find("&lt;b&gt;") != std::string::npos,
              "Native Markdown headings tables tasks and fenced code");
        auto binPath = fixture / L"sample.bin";
        Bytes bytes(256);
        for (int i = 0; i < 256; i++)
            bytes[i] = static_cast<uint8_t>(i);
        writeAtomic(binPath, bytes);
        BinaryBuffer buffer(binPath);
        check(detect(binPath).binary, "Binary content detection");
        buffer.replace(14, 3, {0xFE, 0xFF, 0xAA});
        check(buffer.read(14, 3) == Bytes({0xFE, 0xFF, 0xAA}) && buffer.dirty(), "Hex overwrite piece table");
        buffer.undoEdit();
        check(!buffer.dirty() && buffer.read(14, 1)[0] == 14, "Hex undo returns clean original");
        buffer.redoEdit();
        check(buffer.find({0xFF, 0xAA}, 0) == 15, "Hex streaming search sees dirty bytes");
        auto patched = fixture / L"patched.bin";
        buffer.save(patched);
        check(readAll(patched)[14] == 0xFE, "Hex atomic file save");
        buffer.replace(0, 0, {1, 2, 3});
        check(buffer.size() == 259 && buffer.read(3, 1)[0] == 0, "Hex insertion shifts original bytes");
        buffer.replace(1, 4, {});
        check(buffer.size() == 255, "Hex deletion updates 64-bit length");
        auto shaPath = write(L"sha.txt", "abc");
        BinaryBuffer abc(shaPath);
        check(sha256(abc) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              "SHA256 native BCrypt known vector");
        a.open(binPath);
        auto hex = a.current();
        a.hexInput(*hex, WM_KEYDOWN, VK_END, 0);
        hex->caret = hex->anchor = 0;
        a.hexInput(*hex, WM_CHAR, 'A', 0);
        a.hexInput(*hex, WM_CHAR, 'F', 0);
        check(hex->binary->read(0, 1)[0] == 0xAF && hex->caret == 1, "Actual Hex window nibble editing");
        a.select(0);
        setText(*text, "");
        a.toggleRecord();
        str(h, SCI_REPLACESEL, "한글");
        sci(h, SCI_NEWLINE);
        str(h, SCI_REPLACESEL, "next");
        a.stopRecord();
        check(!a.lastMacro.empty(), "Native SCN_MACRORECORD captures edits");
        auto macro = a.lastMacro;
        setText(*text, "");
        a.play(macro, 2);
        check(all(*text) == "한글\nnext한글\nnext", "Native Unicode macro repeat");
        sci(h, SCI_UNDO);
        check(all(*text).empty(), "Whole macro repeat grouped undo");
        a.savedMacros.push_back({guid(), "테스트 매크로", macro});
        a.persistMacros();
        a.savedMacros.clear();
        a.lastMacro.clear();
        a.loadMacros();
        check(a.savedMacros.size() == 1 && a.lastMacro.size() == macro.size(),
              "C# compatible macro JSON save/reload");
        rejected = false;
        try {
            Json malicious = {{"Kind", 0}, {"Message", SCI_SETTEXT}, {"LParam", 1234}};
            malicious.get<MacroStep>();
        } catch (...) {
            rejected = true;
        }
        check(rejected, "Macro loader rejects pointer commands");
        setText(*text, "\t\tone\n\t\ttwo");
        sci(h, SCI_GOTOPOS, 5);
        a.toggleRecord();
        sci(h, SCI_NEWLINE);
        a.stopRecord();
        check(a.lastMacro.size() == 1 && a.lastMacro[0].message == SCI_NEWLINE,
              "Newline macro captures one semantic step");
        std::mt19937 rng(41);
        for (int test = 0; test < 30; test++) {
            std::vector<std::string> left, right;
            for (int i = 0; i < static_cast<int>(rng() % 30 + 1); i++)
                left.push_back(std::to_string(rng() % 8));
            for (int i = 0; i < static_cast<int>(rng() % 30 + 1); i++)
                right.push_back(std::to_string(rng() % 8));
            auto rows = diffLines(left, right);
            size_t li = 0, ri = 0;
            bool valid = true;
            for (auto r : rows) {
                if (r.left >= 0)
                    valid &= r.left == static_cast<Pos>(li++);
                if (r.right >= 0)
                    valid &= r.right == static_cast<Pos>(ri++);
                if (!r.kind)
                    valid &= left[r.left] == right[r.right];
            }
            check(valid && li == left.size() && ri == right.size(),
                  "Line diff coverage invariant " + std::to_string(test));
        }
        auto logPath = write(L"log.txt", "first\n");
        a.open(logPath);
        auto log = a.current();
        a.toggleMonitor();
        {
            std::ofstream f(logPath, std::ios::binary | std::ios::app);
            f << "한글 추가\n";
        }
        a.pollMonitors();
        check(all(*log) == "first\n한글 추가\n" && sci(log->editor, SCI_GETREADONLY),
              "Log tail appends Korean while editor stays read-only");
        writeAtomic(logPath, Bytes({'n', 'e', 'w', '\n'}));
        a.pollMonitors();
        check(all(*log) == "new\n", "Log rotation resets replaced file");
        a.toggleMonitor();
        check(!sci(log->editor, SCI_GETREADONLY), "Log stop restores normal editing");
        auto dropPath = write(L"한글 공백 drop.txt", "drop file\n");
        auto count = a.docs.size();
        std::wstring drop = dropPath.wstring();
        size_t dropBytes = sizeof(DROPFILES) + (drop.size() + 2) * sizeof(wchar_t);
        HGLOBAL memory = GlobalAlloc(GHND, dropBytes);
        auto ptr = static_cast<DROPFILES *>(GlobalLock(memory));
        ptr->pFiles = sizeof(DROPFILES);
        ptr->fWide = TRUE;
        memcpy(reinterpret_cast<uint8_t *>(ptr) + sizeof(DROPFILES), drop.c_str(),
               (drop.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(memory);
        SendMessageW(a.tabs, WM_DROPFILES, reinterpret_cast<WPARAM>(memory), 0);
        pump();
        check(a.docs.size() == count + 1 && a.current()->path == dropPath,
              "Shell file drop opens Korean path with spaces from tab area");
        SCNotification notice{};
        notice.nmhdr.hwndFrom = h;
        notice.nmhdr.code = SCN_URIDROPPED;
        auto list = narrow(dropPath.wstring()) + "\n" + narrow(binPath.wstring()) + "\n";
        notice.text = list.c_str();
        SendMessageW(a.window, WM_NOTIFY, 0, reinterpret_cast<LPARAM>(&notice));
        pump();
        check(a.docs.size() == count + 1,
              "Native editor file drop handles multiple paths and deduplicates tabs");
        auto ipcPath = write(L"ipc file.txt", "IPC\n");
        std::string payload = Json(std::vector<std::string>{narrow(ipcPath.wstring())}).dump();
        COPYDATASTRUCT copy{0x4C504144, static_cast<DWORD>(payload.size()), payload.data()};
        check(SendMessageW(a.window, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&copy)) == TRUE,
              "Running-instance WM_COPYDATA accepts UTF8 file list");
        pump();
        check(a.current()->path == ipcPath, "Running-instance opens delivered file in new tab");
        a.select(0);
        auto oleFirst = write(L"OLE 한글 첫 파일.txt", "OLE first\n");
        auto oleSecond = write(L"OLE second file.txt", "OLE second\n");
        count = a.docs.size();
        auto dropResult = oleFileDrop(h, {oleFirst, oleSecond});
        check(dropResult == DRAGDROP_S_DROP && a.docs.size() == count + 2,
              "Actual OLE CF_HDROP opens multiple Unicode files over native Scintilla (result=" +
                  std::to_string(dropResult) + ", added=" + std::to_string(a.docs.size() - count) + ")");
        auto childPath = write(L"second process 한글.txt", "second process\n");
        auto exe = exeDir() / L"LHPad.exe";
        std::wstring childCommand = L"\"" + exe.wstring() + L"\" --profile \"" + a.profile.wstring() +
                                    L"\" \"" + childPath.wstring() + L"\"";
        STARTUPINFOW startup{sizeof(startup)};
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION process{};
        check(CreateProcessW(exe.c_str(), childCommand.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                             &startup, &process) != FALSE,
              "Second native process starts with quoted profile and Unicode file path");
        Handle processHandle(process.hProcess), threadHandle(process.hThread);
        auto childTimeout = GetTickCount64() + 10000;
        while (WaitForSingleObject(processHandle.h, 0) == WAIT_TIMEOUT && GetTickCount64() < childTimeout) {
            pump();
            Sleep(5);
        }
        DWORD childExit = 1;
        GetExitCodeProcess(processHandle.h, &childExit);
        pump();
        check(childExit == 0 && a.current()->path == childPath,
              "Second process forwards file to existing instance and exits successfully");
        auto compareLeft = a.newDocument("left.txt");
        setText(*compareLeft, "a\nb\nc");
        auto compareRight = a.newDocument("right.txt");
        setText(*compareRight, "a\ninsert\nb\nchanged");
        a.compareIds = {compareLeft->id, compareRight->id};
        a.compare();
        auto leftView = GetDlgItem(a.compareWindow, 501), rightView = GetDlgItem(a.compareWindow, 502);
        check(sci(leftView, SCI_GETREADONLY) && sci(rightView, SCI_GETREADONLY) &&
                  sci(leftView, SCI_GETLINECOUNT) == sci(rightView, SCI_GETLINECOUNT),
              "Native comparison creates aligned read-only snapshot views");
        str(leftView, SCI_REPLACESEL, "blocked");
        check(all(*compareLeft) == "a\nb\nc" && all(*compareRight) == "a\ninsert\nb\nchanged",
              "Comparison cannot alter original editable tabs");
        screenshot(a.compareWindow, output / L"native-compare.png");
        DestroyWindow(a.compareWindow);
        a.compareIds.clear();
        if (full) {
            auto largePath = fixture / L"large.txt";
            {
                std::ofstream f(largePath, std::ios::binary);
                std::string chunk(1024 * 1024, 'a');
                chunk.back() = '\n';
                for (int i = 0; i < 256; i++)
                    f.write(chunk.data(), chunk.size());
            }
            static int ticks = 0;
            ticks = 0;
            auto responsivenessTimer = SetTimer(nullptr, 0, 15, [](HWND, UINT, UINT_PTR, DWORD) { ticks++; });
            auto started = GetTickCount64();
            a.open(largePath);
            KillTimer(nullptr, responsivenessTimer);
            auto large = a.current();
            check(sci(large->editor, SCI_GETLENGTH) == 256ll * 1024 * 1024 && large->large,
                  "256 MiB text loads in large-document mode");
            check(ticks > 0, "UI timer continues while loading 256 MiB text with long lines");
            results.push_back("METRIC largeLoadMs=" + std::to_string(GetTickCount64() - started) +
                              " uiTicks=" + std::to_string(ticks));
            auto savePath = fixture / L"large-saved.txt";
            saveText(savePath, sci(large->editor, SCI_GETLENGTH), large->encoding,
                     [&](Pos p, Pos n) { return textRange(large->editor, p, n); });
            BinaryBuffer original(largePath), saved(savePath);
            check(sha256(original) == sha256(saved), "256 MiB native text save preserves SHA256");
            auto sparse = fixture / L"three-gib.bin";
            Handle file(CreateFileW(winPath(sparse).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                    CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
            DWORD sparseBytes = 0;
            DeviceIoControl(file.h, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &sparseBytes, nullptr);
            LARGE_INTEGER end{};
            end.QuadPart = 3ll * 1024 * 1024 * 1024;
            SetFilePointerEx(file.h, end, nullptr, FILE_BEGIN);
            SetEndOfFile(file.h);
            file = Handle{};
            BinaryBuffer big(sparse);
            big.replace(3ll * 1024 * 1024 * 1024 - 2, 2, {0xAB, 0xCD});
            check(big.size() == 3ll * 1024 * 1024 * 1024 &&
                      big.read(big.size() - 2, 2) == Bytes({0xAB, 0xCD}),
                  "3 GiB Hex edit uses 64-bit offsets without whole-file loading");
            a.open(sparse);
            a.search = {"7F", "", 0, 0};
            auto cancelTimer =
                SetTimer(nullptr, 0, 15, [](HWND, UINT, UINT_PTR, DWORD) { app->cancel = true; });
            bool found = a.findNext();
            KillTimer(nullptr, cancelTimer);
            check(!found && a.cancel && !a.busy,
                  "Real UI timer cancels 3 GiB native Hex search and restores the application busy state");
        }
        a.select(0);
        setText(*text, "# LHPad C++\n\n한글 **미리보기**\n\n- [x] .NET 없이 실행\n- [x] 드래그 앤 "
                       "드롭\n\n|기능|상태|\n|---|---|\n|네이티브|완료|\n");
        text->name = "README.md";
        text->language = "markdown";
        a.languages.apply(h, "markdown");
        ShowWindow(a.window, SW_SHOWNORMAL);
        a.layout();
        pump();
        a.setView(2);
        auto until = GetTickCount64() + 20000;
        while (!text->browser && GetTickCount64() < until) {
            pump();
            Sleep(20);
        }
        check(text->browser != nullptr, "Native WebView2 COM preview initializes");
        bool completed = false;
        std::string evaluated;
        for (int attempt = 0; attempt < 50 && !completed; attempt++) {
            text->browser->ExecuteScript(L"document.querySelector('h1')?.textContent",
                                         Microsoft::WRL::Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                                             [&](HRESULT hr, LPCWSTR value) -> HRESULT {
                                                 if (SUCCEEDED(hr) && value) {
                                                     auto j = Json::parse(narrow(value), nullptr, false);
                                                     if (j.is_string()) {
                                                         evaluated = j.get<std::string>();
                                                         completed = true;
                                                     }
                                                 }
                                                 return S_OK;
                                             })
                                             .Get());
            auto wait = GetTickCount64() + 100;
            while (GetTickCount64() < wait) {
                pump();
                Sleep(5);
            }
        }
        LPWSTR previewSource = nullptr;
        text->browser->get_Source(&previewSource);
        Json previewDiagnostic = {{"Source", previewSource ? narrow(previewSource) : ""},
                                  {"Html", text->previewHtml},
                                  {"Visible", IsWindowVisible(text->previewHost) != FALSE}};
        CoTaskMemFree(previewSource);
        auto diagnostic = std::make_shared<std::string>();
        auto hr = text->browser->ExecuteScript(
            L"JSON.stringify({url:location.href,ready:document.readyState,html:document.documentElement."
            L"outerHTML})",
            Microsoft::WRL::Callback<ICoreWebView2ExecuteScriptCompletedHandler>([diagnostic](HRESULT hr,
                                                                                              LPCWSTR value)
                                                                                     -> HRESULT {
                *diagnostic = std::to_string(hr) + ":" + (value ? narrow(value) : "");
                return S_OK;
            }).Get());
        auto diagWait = GetTickCount64() + 500;
        while (GetTickCount64() < diagWait) {
            pump();
            Sleep(5);
        }
        previewDiagnostic["ExecuteScriptResult"] = hr;
        previewDiagnostic["Document"] = *diagnostic;
        writeJson(output / L"preview-diagnostic.json", previewDiagnostic, false);
        screenshot(a.window, output / L"native-markdown-split.png");
        check(evaluated == "LHPad C++",
              "WebView2 renders actual native Markdown HTML (heading=" + evaluated + ")");
        check(evaluate(*text, L"document.querySelectorAll('table tbody tr').length") == 1,
              "Actual Markdown preview renders pipe table rows");
        str(h, SCI_REPLACESEL, "\n\n실시간 변경");
        a.refreshPreview(*text);
        check(untilValue(*text, L"document.body.textContent.includes('실시간 변경')", true),
              "Native preview renders unsaved editor changes");
        a.setView(0);
        a.select(1);
        screenshot(a.window, output / L"native-hex.png");
        auto imagePath =
            write(L"그림 테스트.svg", "<svg xmlns='http://www.w3.org/2000/svg' width='240' "
                                      "height='120'><rect width='240' height='120' fill='#b5decd'/></svg>");
        (void)imagePath;
        write(L"sample.css", "body {background:rgb(12,34,56);color:white;padding:24px}");
        auto htmlPath =
            write(L"preview.html",
                  "<!doctype html><meta charset='utf-8'><link rel='stylesheet' href='sample.css'><h1>HTML "
                  "테스트</h1><p "
                  "id='state'>initial</p><script>document.getElementById('state').textContent='executed'</"
                  "script><img src='그림%20테스트.svg'>");
        a.open(htmlPath);
        auto htmlDocument = a.docs.back();
        a.setView(1);
        until = GetTickCount64() + 20000;
        while (!htmlDocument->browser && GetTickCount64() < until) {
            pump();
            Sleep(10);
        }
        check(htmlDocument->browser &&
                  untilValue(*htmlDocument, L"document.querySelector('h1')?.textContent", "HTML 테스트"),
              "Native WebView2 renders actual HTML source");
        check(
            untilValue(*htmlDocument, L"getComputedStyle(document.body).backgroundColor", "rgb(12, 34, 56)"),
            "HTML preview resolves relative CSS through confined local resources");
        check(untilValue(*htmlDocument, L"document.querySelector('img').naturalWidth", 240),
              "Preview resolves Korean relative image filenames with URL-encoded spaces");
        check(evaluate(*htmlDocument, L"document.getElementById('state').textContent") == "initial",
              "Document JavaScript stays disabled by default");
        htmlDocument->scripts = true;
        a.refreshPreview(*htmlDocument, true);
        check(untilValue(*htmlDocument, L"document.getElementById('state').textContent", "executed"),
              "Explicit per-tab HTML script option enables execution");
        screenshot(a.window, output / L"native-html-preview.png");
        a.open(fixture / L"그림 테스트.svg");
        auto svgDocument = a.docs.back();
        a.setView(1);
        until = GetTickCount64() + 20000;
        while (!svgDocument->browser && GetTickCount64() < until) {
            pump();
            Sleep(10);
        }
        check(svgDocument->browser &&
                  untilValue(*svgDocument, L"document.querySelector('svg')?.getAttribute('width')", "240"),
              "Native SVG preview renders the vector document");
        a.select(0);
        text->recovered = true;
        a.backup();
        auto session = readJson(a.profile / L"session.json");
        check(session["Version"] == 1 && session["Documents"][0]["Dirty"] == true &&
                  disk::exists(wide(session["Documents"][0]["Snapshot"].get<std::string>())),
              "Session manifest and unsaved snapshot retain C# compatible schema");
        a.savePlacement();
        check(readJson(a.profile / L"window.json")["Version"] == 1,
              "Native window placement retains existing schema");
        AboutProbe probe{a.window, output / L"native-about.png"};
        aboutProbe = &probe;
        auto aboutTimer = SetTimer(nullptr, 0, 80, inspectAbout);
        a.about();
        KillTimer(nullptr, aboutTimer);
        aboutProbe = nullptr;
        check(probe.checked && probe.valid,
              "Native About dialog keeps a 128px typewriter and correctly sized unclipped button");
        auto migration = a.profile / L"csharp-migration-profile";
        disk::create_directories(migration);
        auto legacySnapshot = write(L"legacy.snapshot", "미저장 복구 문서\nsecond");
        auto legacyOriginal = write(L"legacy-original.txt", "original on disk");
        Json legacyDocument = {
            {"Id", "legacy-document"},
            {"Path", narrow(legacyOriginal.wstring())},
            {"Name", "복구 문서.cpp"},
            {"Encoding", Encoding{"한국어 Windows-949 (CP949)", 949}},
            {"Language", "cpp"},
            {"Binary", false},
            {"Dirty", true},
            {"Snapshot", narrow(legacySnapshot.wstring())},
            {"Caret", 6},
            {"Anchor", 0},
            {"FirstLine", 0},
            {"Selections", Json::array({{{"Anchor", 0}, {"Caret", 6}}, {{"Anchor", 10}, {"Caret", 10}}})},
            {"Wrap", true},
            {"Eol", SC_EOL_LF},
            {"Zoom", 2},
            {"ViewMode", 0}};
        writeJson(migration / L"session.json",
                  {{"Version", 1},
                   {"Active", 0},
                   {"AutoIndent", false},
                   {"WhiteSpace", true},
                   {"EolVisible", true},
                   {"ControlsVisible", false},
                   {"Documents", Json::array({legacyDocument})}},
                  false);
        Json legacyStep = {
            {"Kind", 0}, {"Message", SCI_REPLACESEL}, {"WParam", 0}, {"LParam", 0}, {"Data", "WA=="}};
        writeJson(migration / L"macros.json",
                  {{"Version", 1},
                   {"Last", Json::array({legacyStep})},
                   {"Saved", Json::array({{{"Id", "legacy-macro"},
                                           {"Name", "기존 매크로"},
                                           {"Steps", Json::array({legacyStep})}}})}},
                  false);
        writeJson(migration / L"window.json",
                  {{"Version", 1},
                   {"X", 50000},
                   {"Y", 50000},
                   {"Width", 20000},
                   {"Height", 20000},
                   {"Maximized", false}},
                  false);
        {
            struct RestoreGlobal {
                App *original;
                ~RestoreGlobal() { app = original; }
            } global{&a};
            App restored(a.instance, migration);
            restored.create(SW_HIDE);
            KillTimer(restored.window, 1);
            restored.start();
            auto recovered = restored.current();
            check(recovered && recovered->dirty() && all(*recovered) == "미저장 복구 문서\nsecond" &&
                      recovered->encoding.cp == 949 && recovered->language == "cpp",
                  "Native startup imports C# unsaved UTF8 snapshot while retaining original file encoding");
            check(sci(recovered->editor, SCI_GETSELECTIONS) == 2 && sci(recovered->editor, SCI_GETWRAPMODE) &&
                      sci(recovered->editor, SCI_GETZOOM) == 2 && !restored.autoIndent &&
                      restored.whiteSpace && restored.eolVisible,
                  "C# selections wrap zoom and editor preferences migrate intact");
            check(restored.lastMacro.size() == 1 && restored.lastMacro[0].data == "X" &&
                      restored.savedMacros.size() == 1,
                  "Native startup imports C# base64 macro strings and saved macro list");
            check(disk::exists(migration / L"legacy-csharp-backup/session.json") &&
                      readAll(legacyOriginal) == Bytes({'o', 'r', 'i', 'g', 'i', 'n', 'a', 'l', ' ', 'o', 'n',
                                                        ' ', 'd', 'i', 's', 'k'}),
                  "Migration preserves original manifest backup and never overwrites saved source file");
            RECT rectangle{};
            GetWindowRect(restored.window, &rectangle);
            MONITORINFO monitor{sizeof(monitor)};
            GetMonitorInfoW(MonitorFromWindow(restored.window, MONITOR_DEFAULTTONEAREST), &monitor);
            check(rectangle.left >= monitor.rcWork.left && rectangle.top >= monitor.rcWork.top &&
                      rectangle.right <= monitor.rcWork.right && rectangle.bottom <= monitor.rcWork.bottom,
                  "Off-screen oversized C# window settings are clamped to the current monitor");
            restored.backup();
            check(readJson(migration / L"session.json")["Documents"][0]["Dirty"] == true,
                  "Recovered unsaved state survives the next native session backup");
            auto before = all(*recovered);
            writeAtomic(legacyOriginal, {0xFF, 0x00, 0xFE});
            restored.checking = true;
            bool reloadFailed = false;
            try {
                restored.reload(*recovered, false, Encoding{});
            } catch (...) {
                reloadFailed = true;
            }
            restored.checking = false;
            check(reloadFailed && recovered->dirty() && all(*recovered) == before,
                  "Failed staged reload preserves existing unsaved text and dirty state");
            DestroyWindow(restored.window);
        }
        results.push_back("ALL NATIVE TESTS PASSED");
        exit = 0;
    } catch (const std::exception &e) {
        results.push_back("FAIL " + std::string(e.what()));
    }
    // These fixed filenames are created only inside this run's isolated fixture directory.
    for (auto name : {L"large.txt", L"large-saved.txt", L"three-gib.bin"}) {
        std::error_code error;
        disk::remove(fixture / name, error);
        if (error) {
            results.push_back("FAIL temporary large-file cleanup: " + error.message());
            exit = 1;
        }
    }
    std::ofstream out(output / L"native-results.txt", std::ios::binary);
    for (auto &line : results)
        out << line << "\n";
    return exit;
}
} // namespace lp
