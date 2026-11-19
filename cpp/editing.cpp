#include "app.h"
#include "macro_commands.h"
#include <cstring>
#include <windowsx.h>
namespace lp {
static std::string eol(HWND h) {
    auto m = sci(h, SCI_GETEOLMODE);
    return m == SC_EOL_CRLF ? "\r\n" : m == SC_EOL_CR ? "\r" : "\n";
}
static void replaceRange(HWND h, Pos a, Pos b, const std::string &s, bool regex = false) {
    sci(h, SCI_SETTARGETSTART, a);
    sci(h, SCI_SETTARGETEND, b);
    sci(h, regex ? SCI_REPLACETARGETRE : SCI_REPLACETARGET, s.size(), reinterpret_cast<LPARAM>(s.data()));
}
void App::edit(int message) {
    auto d = current();
    if (!d)
        return;
    if (d->binary) {
        if (message == SCI_UNDO)
            d->binary->undoEdit();
        else if (message == SCI_REDO)
            d->binary->redoEdit();
        else if (message == SCI_SELECTALL) {
            d->anchor = 0;
            d->caret = d->binary->size();
        } else if (message == SCI_CLEAR || message == SCI_CUT)
            hexInput(*d, WM_KEYDOWN, message == SCI_CUT ? 'X' : VK_DELETE, 0);
        else if (message == SCI_COPY || message == SCI_PASTE)
            hexInput(*d, WM_KEYDOWN, message == SCI_COPY ? 'C' : 'V', 1);
        hexChanged(*d);
    } else
        sci(d->editor, message);
}
void App::newline(Document &d, UINT msg, WPARAM w, LPARAM l) {
    auto h = d.editor;
    if (!h)
        return;
    std::vector<std::string> prefixes;
    int count = static_cast<int>(sci(h, SCI_GETSELECTIONS));
    if (autoIndent && !sci(h, SCI_GETREADONLY)) {
        bool onlyMain = sci(h, SCI_GETSELECTIONMODE) == SC_SEL_RECTANGLE ||
                        sci(h, SCI_GETSELECTIONMODE) == SC_SEL_THIN ||
                        !sci(h, SCI_GETADDITIONALSELECTIONTYPING);
        int main = static_cast<int>(sci(h, SCI_GETMAINSELECTION));
        for (int i = 0; i < (onlyMain ? 1 : count); i++) {
            auto start = sci(h, SCI_GETSELECTIONNSTART, onlyMain ? main : i),
                 line = sci(h, SCI_LINEFROMPOSITION, start), pos = sci(h, SCI_POSITIONFROMLINE, line);
            std::string prefix;
            while (pos < start) {
                auto bytes = textRange(h, pos, std::min<Pos>(65536, start - pos));
                size_t n = 0;
                while (n < bytes.size() && (bytes[n] == '\t' || bytes[n] == ' '))
                    n++;
                prefix.append(bytes, 0, n);
                pos += n;
                if (n < bytes.size())
                    break;
            }
            prefixes.push_back(std::move(prefix));
        }
    }
    newlineActive = true;
    newlineObserved = false;
    bool suppression = macroSuppressed;
    macroSuppressed = true;
    sci(h, SCI_BEGINUNDOACTION);
    DefSubclassProc(h, msg, w, l);
    if (newlineObserved && sci(h, SCI_GETSELECTIONS) == static_cast<LRESULT>(prefixes.size())) {
        struct Caret {
            Pos caret, anchor, cs, as;
        };
        std::vector<Caret> carets;
        std::map<Pos, std::string> insertions;
        for (int i = 0; i < count; i++) {
            Pos caret = sci(h, SCI_GETSELECTIONNCARET, i);
            carets.push_back({caret, sci(h, SCI_GETSELECTIONNANCHOR, i),
                              sci(h, SCI_GETSELECTIONNCARETVIRTUALSPACE, i),
                              sci(h, SCI_GETSELECTIONNANCHORVIRTUALSPACE, i)});
            if (!prefixes[i].empty())
                insertions.emplace(caret, prefixes[i]);
        }
        Pos targetStart = sci(h, SCI_GETTARGETSTART), targetEnd = sci(h, SCI_GETTARGETEND);
        for (auto it = insertions.rbegin(); it != insertions.rend(); ++it)
            replaceRange(h, it->first, it->first, it->second);
        auto shifted = [&](Pos position) {
            Pos shift = 0;
            for (auto &[p, s] : insertions) {
                if (p > position)
                    break;
                shift += s.size();
            }
            return position + shift;
        };
        for (int i = 0; i < count; i++) {
            sci(h, SCI_SETSELECTIONNCARET, i, shifted(carets[i].caret));
            sci(h, SCI_SETSELECTIONNANCHOR, i, shifted(carets[i].anchor));
            sci(h, SCI_SETSELECTIONNCARETVIRTUALSPACE, i, carets[i].cs);
            sci(h, SCI_SETSELECTIONNANCHORVIRTUALSPACE, i, carets[i].as);
        }
        sci(h, SCI_SETTARGETSTART, shifted(targetStart));
        sci(h, SCI_SETTARGETEND, shifted(targetEnd));
        sci(h, SCI_CHOOSECARETX);
        sci(h, SCI_SCROLLCARET);
    }
    sci(h, SCI_ENDUNDOACTION);
    newlineActive = false;
    macroSuppressed = suppression;
    if (recording && newlineObserved && !suppression) {
        MacroStep step;
        step.message = SCI_NEWLINE;
        recordAction(step);
    }
    newlineObserved = false;
}
static std::wstring controlText(HWND h, int id) {
    auto c = GetDlgItem(h, id);
    int n = GetWindowTextLengthW(c);
    std::wstring s(n + 1, 0);
    GetWindowTextW(c, s.data(), n + 1);
    s.resize(n);
    return s;
}
static void gatherSearch(App &a) {
    auto h = a.findWindow;
    if (!h)
        return;
    a.search.query = narrow(controlText(h, 100));
    a.search.replacement = narrow(controlText(h, 101));
    auto checked = [&](int id) { return SendDlgItemMessageW(h, id, BM_GETCHECK, 0, 0) == BST_CHECKED; };
    a.search.flags = (checked(102) ? SCFIND_MATCHCASE : 0) | (checked(103) ? SCFIND_WHOLEWORD : 0) |
                     (checked(104) ? SCFIND_REGEXP | SCFIND_CXX11REGEX : 0);
    a.search.allTabs = checked(105);
    a.search.hexMode = static_cast<int>(SendDlgItemMessageW(h, 106, CB_GETCURSEL, 0, 0));
}
LRESULT CALLBACK App::findProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto *a = reinterpret_cast<App *>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE) {
        a = static_cast<App *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(a));
        a->findWindow = h;
    }
    if (!a)
        return DefWindowProcW(h, m, w, l);
    switch (m) {
    case WM_CREATE: {
        int dpi = GetDpiForWindow(h);
        auto create = [&](const wchar_t *cls, const wchar_t *text, int id, int x, int y, int width,
                          int height, DWORD style = 0) {
            auto c = CreateWindowExW(cls == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0, cls, text,
                                     WS_CHILD | WS_VISIBLE | style, MulDiv(x, dpi, 96), MulDiv(y, dpi, 96),
                                     MulDiv(width, dpi, 96), MulDiv(height, dpi, 96), h,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), a->instance, nullptr);
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(a->font), TRUE);
            return c;
        };
        create(L"STATIC", L"찾을 내용", 0, 20, 20, 100, 24);
        create(L"EDIT", wide(a->search.query).c_str(), 100, 130, 17, 540, 30, WS_TABSTOP | ES_AUTOHSCROLL);
        create(L"STATIC", L"바꿀 내용", 10, 20, 65, 100, 24);
        create(L"EDIT", wide(a->search.replacement).c_str(), 101, 130, 62, 540, 30,
               WS_TABSTOP | ES_AUTOHSCROLL);
        create(L"BUTTON", L"대소문자 구분", 102, 20, 110, 150, 30, WS_TABSTOP | BS_AUTOCHECKBOX);
        create(L"BUTTON", L"단어 단위", 103, 180, 110, 130, 30, WS_TABSTOP | BS_AUTOCHECKBOX);
        create(L"BUTTON", L"정규식", 104, 330, 110, 130, 30, WS_TABSTOP | BS_AUTOCHECKBOX);
        create(L"BUTTON", L"모든 텍스트 탭", 105, 480, 110, 180, 30, WS_TABSTOP | BS_AUTOCHECKBOX);
        create(L"STATIC", L"Hex 검색 형식", 11, 20, 155, 120, 24);
        auto combo = create(L"COMBOBOX", L"", 106, 150, 150, 260, 200, WS_TABSTOP | CBS_DROPDOWNLIST);
        for (auto text : {L"Hex 바이트 (70 72 6F)", L"텍스트 UTF-8", L"텍스트 UTF-16 LE", L"텍스트 ASCII"})
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
        SendMessageW(combo, CB_SETCURSEL, a->search.hexMode, 0);
        create(L"STATIC", L"", 300, 20, 197, 650, 45);
        create(L"BUTTON", L"다음 찾기", 200, 20, 255, 98, 32, WS_TABSTOP | BS_DEFPUSHBUTTON);
        create(L"BUTTON", L"이전 찾기", 201, 130, 255, 98, 32, WS_TABSTOP);
        create(L"BUTTON", L"바꾸기", 202, 240, 255, 98, 32, WS_TABSTOP);
        create(L"BUTTON", L"모두 바꾸기", 203, 350, 255, 98, 32, WS_TABSTOP);
        create(L"BUTTON", L"일치 개수", 204, 460, 255, 98, 32, WS_TABSTOP);
        create(L"BUTTON", L"중지", 205, 570, 255, 98, 32, WS_TABSTOP);
        for (auto [id, flag] : {std::pair{102, SCFIND_MATCHCASE}, std::pair{103, SCFIND_WHOLEWORD},
                                std::pair{104, SCFIND_REGEXP}})
            SendDlgItemMessageW(h, id, BM_SETCHECK, a->search.flags & flag ? BST_CHECKED : BST_UNCHECKED, 0);
        SendDlgItemMessageW(h, 105, BM_SETCHECK, a->search.allTabs ? BST_CHECKED : BST_UNCHECKED, 0);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(w) == 205) {
            a->cancel = true;
            return 0;
        }
        if (LOWORD(w) == IDCANCEL) {
            a->cancel = true;
            ShowWindow(h, SW_HIDE);
            return 0;
        }
        if (LOWORD(w) == IDOK) {
            a->searchAction(200);
            return 0;
        }
        if (LOWORD(w) >= 200 && LOWORD(w) <= 204) {
            a->searchAction(LOWORD(w));
            return 0;
        }
        break;
    case WM_CLOSE:
        ShowWindow(h, SW_HIDE);
        return 0;
    case WM_DESTROY:
        a->findWindow = nullptr;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
void App::showFind(bool replace) {
    if (!findWindow) {
        int width = MulDiv(710, dpi, 96), height = MulDiv(350, dpi, 96);
        RECT r{};
        GetWindowRect(window, &r);
        findWindow = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT, L"LHPad.Find",
                                     L"찾기 / 바꾸기", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, r.left + 50,
                                     r.top + 100, width, height, window, nullptr, instance, this);
    }
    auto d = current();
    if (d && d->editor) {
        auto a = sci(d->editor, SCI_GETSELECTIONSTART), b = sci(d->editor, SCI_GETSELECTIONEND);
        if (b > a && b - a < 4096)
            SetDlgItemTextW(findWindow, 100, wide(textRange(d->editor, a, b - a)).c_str());
    }
    ShowWindow(findWindow, SW_SHOW);
    SetForegroundWindow(findWindow);
    updateFind();
    EnableWindow(GetDlgItem(findWindow, 101), replace);
    EnableWindow(GetDlgItem(findWindow, 202), replace);
    EnableWindow(GetDlgItem(findWindow, 203), replace && d && !d->binary);
    SetFocus(GetDlgItem(findWindow, 100));
    SendDlgItemMessageW(findWindow, 100, EM_SETSEL, 0, -1);
}
void App::updateFind() {
    if (!findWindow)
        return;
    auto d = current();
    bool hex = d && d->binary;
    for (int id : {102, 103, 104, 105})
        EnableWindow(GetDlgItem(findWindow, id), !hex && !busy && !playing);
    for (int id : {11, 106})
        ShowWindow(GetDlgItem(findWindow, id), hex ? SW_SHOW : SW_HIDE);
    EnableWindow(GetDlgItem(findWindow, 201), !hex && !busy);
    EnableWindow(GetDlgItem(findWindow, 203), !hex && !busy);
    EnableWindow(GetDlgItem(findWindow, 205), busy);
    SetDlgItemTextW(findWindow, 300, wide(searchMessage).c_str());
}
Bytes App::searchBytes() const {
    if (search.hexMode == 0) {
        Bytes b;
        int high = -1;
        for (unsigned char c : search.query) {
            if (isspace(c))
                continue;
            int v = c >= '0' && c <= '9'   ? c - '0'
                    : c >= 'a' && c <= 'f' ? c - 'a' + 10
                    : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                           : -1;
            if (v < 0)
                fail(L"Hex 바이트는 0~9, A~F로 입력하세요. 예: 70 72 6F");
            if (high < 0)
                high = v;
            else {
                b.push_back(static_cast<uint8_t>(high * 16 + v));
                high = -1;
            }
        }
        if (high >= 0)
            fail(L"Hex 바이트는 두 자리씩 입력하세요.");
        return b;
    }
    Encoding e;
    e.cp = search.hexMode == 2 ? 1200 : search.hexMode == 3 ? 20127 : 65001;
    return encode(search.query, e);
}
static Pos findTarget(HWND h, const SearchOptions &options, Pos a, Pos b) {
    sci(h, SCI_SETSTATUS, SC_STATUS_OK);
    // Notepad++'s bundled Boost.Regex build requires its empty-match/CRLF flags.
    uint32_t flags = static_cast<uint32_t>(options.flags);
    if (flags & SCFIND_REGEXP)
        flags |= 0xC8000000u;
    sci(h, SCI_SETSEARCHFLAGS, flags);
    sci(h, SCI_SETTARGETSTART, a);
    sci(h, SCI_SETTARGETEND, b);
    Pos found =
        sci(h, SCI_SEARCHINTARGET, options.query.size(), reinterpret_cast<LPARAM>(options.query.data()));
    if (found == -2 || sci(h, SCI_GETSTATUS) == SC_STATUS_WARN_REGEX)
        fail(L"정규식 구문이 올바르지 않습니다.");
    if (found == -3)
        fail(L"정규식이 너무 복잡하거나 실행 한도를 초과했습니다.");
    return found;
}
bool App::findNext(bool backwards, bool wrapSearch) {
    auto d = current();
    if (!d || search.query.empty())
        return false;
    if (d->binary) {
        struct RestoreBusy {
            App &app;
            bool previous;
            ~RestoreBusy() {
                app.busy = previous;
                app.updateFind();
            }
        } restore{*this, busy};
        busy = true;
        cancel = false;
        updateFind();
        auto needle = searchBytes();
        if (needle.empty())
            return false;
        Pos start =
            std::min(d->binary->size(), std::max(d->caret, d->anchor) + (d->caret == d->anchor ? 0 : 0));
        auto found = d->binary->find(needle, start, &cancel);
        if (found < 0 && wrapSearch && start && !cancel)
            found = d->binary->find(needle, 0, &cancel);
        if (found < 0) {
            searchMessage = cancel ? "검색을 중지했습니다." : "일치하는 항목이 없습니다.";
            return false;
        }
        d->anchor = found;
        d->caret = found + needle.size();
        hexEnsureVisible(*d);
        InvalidateRect(d->hexWindow, nullptr, FALSE);
        searchMessage = "일치 항목을 찾았습니다.";
        return true;
    }
    auto h = d->editor;
    Pos length = sci(h, SCI_GETLENGTH), a = sci(h, SCI_GETSELECTIONSTART), b = sci(h, SCI_GETSELECTIONEND),
        start = backwards ? a : b;
    static HWND previousWindow = nullptr;
    static Pos previousEmpty = -1;
    static uint64_t previousRevision = 0;
    static std::string previousQuery;
    if (previousRevision != d->revision || previousQuery != search.query)
        previousEmpty = -1;
    if (a == b && previousWindow == h && previousEmpty == start) {
        auto next = sci(h, backwards ? SCI_POSITIONBEFORE : SCI_POSITIONAFTER, start);
        if (next == start) {
            if (!wrapSearch)
                return false;
            start = backwards ? length : 0;
        } else
            start = next;
    }
    Pos found = findTarget(h, search, start, backwards ? 0 : length);
    if (found < 0 && wrapSearch)
        found = findTarget(h, search, backwards ? length : 0, start);
    if (found < 0) {
        previousEmpty = -1;
        searchMessage = "일치하는 항목이 없습니다.";
        return false;
    }
    Pos end = sci(h, SCI_GETTARGETEND);
    sci(h, SCI_SETSEL, found, end);
    sci(h, SCI_ENSUREVISIBLE, sci(h, SCI_LINEFROMPOSITION, found));
    sci(h, SCI_SCROLLCARET);
    previousWindow = h;
    previousEmpty = found == end ? found : -1;
    previousRevision = d->revision;
    previousQuery = search.query;
    searchMessage = "일치 항목을 찾았습니다.";
    MacroStep step;
    step.kind = 1;
    step.search = search;
    step.backwards = backwards;
    recordAction(step);
    return true;
}
Pos App::countMatches(bool replacing) {
    auto d = current();
    if (!d || search.query.empty())
        return 0;
    if (replacing && d->monitor)
        fail(L"로그 모니터링 중에는 편집할 수 없습니다.");
    bool wasBusy = busy;
    busy = true;
    cancel = false;
    updateFind();
    Pos total = 0;
    try {
        if (d->binary) {
            if (replacing)
                fail(L"Hex에서는 같은 길이의 개별 바꾸기를 사용하세요.");
            auto needle = searchBytes();
            if (needle.empty()) {
                busy = wasBusy;
                return 0;
            }
            Pos pos = 0;
            while (!cancel) {
                auto found = d->binary->find(needle, pos, &cancel);
                if (found < 0)
                    break;
                total++;
                pos = found + needle.size();
                pump();
            }
        } else {
            std::vector<Document *> targets;
            if (replacing && search.allTabs) {
                if (recording)
                    fail(L"모든 탭 바꾸기는 녹화를 중지한 후 사용하세요.");
                for (auto &p : docs)
                    if (p->editor && !p->monitor)
                        targets.push_back(p.get());
            } else
                targets.push_back(d);
            for (auto *target : targets) {
                auto h = target->editor;
                const auto revision = target->revision;
                Pos length = sci(h, SCI_GETLENGTH), pos = 0;
                Pos ta = sci(h, SCI_GETTARGETSTART), tb = sci(h, SCI_GETTARGETEND);
                int flags = static_cast<int>(sci(h, SCI_GETSEARCHFLAGS));
                if (replacing)
                    sci(h, SCI_BEGINUNDOACTION);
                bool suppress = macroSuppressed;
                macroSuppressed = true;
                try {
                    while (pos <= length && !cancel) {
                        if (!replacing && target->revision != revision)
                            fail(L"검색 중 문서가 변경되었습니다. 다시 검색해 주세요.");
                        auto found = findTarget(h, search, pos, length);
                        if (found < 0)
                            break;
                        auto end = sci(h, SCI_GETTARGETEND);
                        bool empty = found == end;
                        total++;
                        if (replacing) {
                            auto n =
                                sci(h, search.flags & SCFIND_REGEXP ? SCI_REPLACETARGETRE : SCI_REPLACETARGET,
                                    search.replacement.size(),
                                    reinterpret_cast<LPARAM>(search.replacement.data()));
                            if (n < 0)
                                fail(L"바꾸기에 실패했습니다.");
                            length += n - (end - found);
                            pos = found + n;
                        } else
                            pos = end;
                        if (empty) {
                            if (pos >= length)
                                break;
                            pos = sci(h, SCI_POSITIONAFTER, pos);
                        }
                        if (total % 128 == 0) {
                            searchMessage = std::to_string(total) + "개 처리 중…";
                            updateFind();
                            pump();
                        }
                    }
                } catch (...) {
                    macroSuppressed = suppress;
                    if (replacing)
                        sci(h, SCI_ENDUNDOACTION);
                    throw;
                }
                macroSuppressed = suppress;
                if (replacing)
                    sci(h, SCI_ENDUNDOACTION);
                else {
                    sci(h, SCI_SETTARGETSTART, ta);
                    sci(h, SCI_SETTARGETEND, tb);
                    sci(h, SCI_SETSEARCHFLAGS, flags);
                }
                if (cancel)
                    break;
            }
        }
        busy = wasBusy;
        if (replacing) {
            MacroStep step;
            step.kind = 3;
            step.search = search;
            recordAction(step);
        }
        searchMessage = std::string(cancel ? "중지: " : "") + std::to_string(total) +
                        (replacing ? "개 바꿨습니다." : "개 일치합니다.");
        updateFind();
        return total;
    } catch (...) {
        busy = wasBusy;
        updateFind();
        throw;
    }
}
bool App::replaceOne() {
    auto d = current();
    if (!d || d->monitor || search.query.empty())
        return false;
    if (d->binary) {
        auto needle = searchBytes();
        auto original = search.query;
        search.query = search.replacement;
        Bytes replacement;
        try {
            replacement = searchBytes();
        } catch (...) {
            search.query = original;
            throw;
        }
        search.query = original;
        if (replacement.size() != needle.size())
            fail(L"Hex 바꾸기는 원본과 같은 바이트 길이만 지원합니다.");
        auto a = std::min(d->caret, d->anchor), b = std::max(d->caret, d->anchor);
        if (b - a == static_cast<Pos>(needle.size()) && d->binary->read(a, b - a) == needle) {
            d->binary->replace(a, b - a, replacement);
            hexChanged(*d);
        }
        return findNext();
    }
    auto h = d->editor;
    auto a = sci(h, SCI_GETSELECTIONSTART), b = sci(h, SCI_GETSELECTIONEND);
    bool suppress = macroSuppressed;
    macroSuppressed = true;
    bool replaced = false;
    try {
        auto found = findTarget(h, search, a, b);
        if (found == a && sci(h, SCI_GETTARGETEND) == b) {
            auto n = sci(h, search.flags & SCFIND_REGEXP ? SCI_REPLACETARGETRE : SCI_REPLACETARGET,
                         search.replacement.size(), reinterpret_cast<LPARAM>(search.replacement.data()));
            sci(h, SCI_SETSEL, a, a + n);
            replaced = true;
        }
        bool result = findNext(false, !playing);
        macroSuppressed = suppress;
        MacroStep step;
        step.kind = 2;
        step.search = search;
        recordAction(step);
        return result || replaced;
    } catch (...) {
        macroSuppressed = suppress;
        throw;
    }
}
void App::searchAction(int action) {
    if (busy || playing)
        return;
    try {
        gatherSearch(*this);
        if (search.query.empty())
            fail(L"찾을 내용을 입력하세요.");
        if (action == 200)
            findNext();
        else if (action == 201)
            findNext(true);
        else if (action == 202)
            replaceOne();
        else if (action == 203)
            countMatches(true);
        else if (action == 204)
            countMatches();
    } catch (const std::exception &e) {
        searchMessage = e.what();
    }
    updateFind();
    update();
}
void App::goTo() {
    auto d = current();
    if (!d)
        return;
    auto fields = inputDialog(
        window, d->binary ? L"오프셋 이동" : L"줄 이동",
        {{d->binary ? L"바이트 오프셋 (0x: 16진수)" : L"줄 번호",
          d->binary ? std::to_wstring(d->caret)
                    : std::to_wstring(
                          sci(d->editor, SCI_LINEFROMPOSITION, sci(d->editor, SCI_GETCURRENTPOS)) + 1)}});
    if (!fields)
        return;
    auto s = (*fields)[0].value;
    size_t used = 0;
    Pos value = std::stoll(s, &used, s.starts_with(L"0x") || s.starts_with(L"0X") ? 16 : 10);
    if (used != s.size() || value < 0)
        fail(L"올바른 위치를 입력하세요.");
    if (d->binary) {
        d->anchor = d->caret = std::clamp<Pos>(value, 0, d->binary->size());
        hexEnsureVisible(*d);
        InvalidateRect(d->hexWindow, nullptr, FALSE);
    } else {
        sci(d->editor, SCI_GOTOLINE, std::max<Pos>(0, value - 1));
        SetFocus(d->editor);
    }
}
static void applyColumn(HWND h, bool number, const std::string &text, Pos start, Pos increment) {
    int count = static_cast<int>(sci(h, SCI_GETSELECTIONS));
    if (count < 2)
        fail(L"Alt+드래그로 세로 선택하거나 Ctrl+클릭으로 커서를 여러 개 만들어 주세요.");
    std::vector<std::pair<Pos, Pos>> ranges;
    for (int i = 0; i < count; i++) {
        auto a = sci(h, SCI_GETSELECTIONNANCHOR, i), b = sci(h, SCI_GETSELECTIONNCARET, i);
        ranges.emplace_back(std::min(a, b), std::max(a, b));
    }
    std::sort(ranges.begin(), ranges.end());
    sci(h, SCI_BEGINUNDOACTION);
    try {
        for (int i = count - 1; i >= 0; i--) {
            Pos n = start;
            if (number) {
                const Pos index = static_cast<Pos>(i);
                if (index && (increment > 0 && increment > INT64_MAX / index ||
                              increment < 0 && increment < INT64_MIN / index))
                    fail(L"연속 숫자가 64비트 범위를 초과합니다.");
                const Pos delta = index * increment;
                if (delta > 0 && start > INT64_MAX - delta || delta < 0 && start < INT64_MIN - delta)
                    fail(L"연속 숫자가 64비트 범위를 초과합니다.");
                n += delta;
            }
            replaceRange(h, ranges[i].first, ranges[i].second, number ? std::to_string(n) : text);
        }
    } catch (...) {
        sci(h, SCI_ENDUNDOACTION);
        throw;
    }
    sci(h, SCI_ENDUNDOACTION);
}
void App::column() {
    auto d = current();
    if (!d || !d->editor || d->monitor)
        return;
    auto fields =
        inputDialog(window, L"열 편집",
                    {{L"삽입 문자열", L""},
                     {L"연속 숫자 사용", L"", true, false},
                     {L"시작 값", L"1"},
                     {L"증가 값", L"1"}},
                    L"Alt+드래그로 세로 선택하거나 Ctrl+클릭으로 커서를 여러 개 만든 후 사용하세요.");
    if (!fields)
        return;
    MacroStep step;
    step.kind = 7;
    step.number = (*fields)[1].checked;
    step.data = narrow((*fields)[0].value);
    step.start = std::stoll((*fields)[2].value);
    step.increment = std::stoll((*fields)[3].value);
    bool suppression = macroSuppressed;
    macroSuppressed = true;
    try {
        applyColumn(d->editor, step.number, step.data, step.start, step.increment);
    } catch (...) {
        macroSuppressed = suppression;
        throw;
    }
    macroSuppressed = suppression;
    recordAction(step);
}
void App::format() {
    auto d = current();
    if (!d || !d->editor || d->monitor)
        return;
    auto h = d->editor;
    Pos a = sci(h, SCI_GETSELECTIONSTART), b = sci(h, SCI_GETSELECTIONEND);
    if (a == b) {
        a = 0;
        b = sci(h, SCI_GETLENGTH);
    }
    if (b - a > 32 * 1024 * 1024)
        fail(L"32 MiB 이하 영역을 선택해 정렬해 주세요.");
    auto output = formatText(textRange(h, a, b - a), d->language, eol(h));
    bool suppression = macroSuppressed;
    macroSuppressed = true;
    sci(h, SCI_BEGINUNDOACTION);
    replaceRange(h, a, b, output);
    sci(h, SCI_ENDUNDOACTION);
    sci(h, SCI_SETSEL, a, a + output.size());
    macroSuppressed = suppression;
    MacroStep step;
    step.kind = 5;
    step.search.query = d->language;
    recordAction(step);
}
void App::sort(bool descending) {
    auto d = current();
    if (!d || !d->editor || d->monitor)
        return;
    auto h = d->editor;
    Pos a = sci(h, SCI_GETSELECTIONSTART), b = sci(h, SCI_GETSELECTIONEND);
    if (a == b)
        fail(L"정렬할 줄을 선택해 주세요.");
    Pos first = sci(h, SCI_LINEFROMPOSITION, a), last = sci(h, SCI_LINEFROMPOSITION, b);
    a = sci(h, SCI_POSITIONFROMLINE, first);
    if (b == sci(h, SCI_POSITIONFROMLINE, last))
        last--;
    b = sci(h, SCI_GETLINEENDPOSITION, last);
    if (b - a > 32 * 1024 * 1024)
        fail(L"32 MiB 이하를 선택해 주세요.");
    auto rows = lines(textRange(h, a, b - a));
    std::sort(rows.begin(), rows.end());
    if (descending)
        std::reverse(rows.begin(), rows.end());
    std::string out;
    for (size_t i = 0; i < rows.size(); i++) {
        if (i)
            out += eol(h);
        out += rows[i];
    }
    bool suppression = macroSuppressed;
    macroSuppressed = true;
    sci(h, SCI_BEGINUNDOACTION);
    replaceRange(h, a, b, out);
    sci(h, SCI_ENDUNDOACTION);
    macroSuppressed = suppression;
    MacroStep step;
    step.kind = 6;
    step.descending = descending;
    recordAction(step);
}
void App::changeCase(int message) {
    auto d = current();
    if (!d || !d->editor || d->monitor)
        return;
    sci(d->editor, message); // SCI_UPPERCASE / SCI_LOWERCASE are recorded by Scintilla itself.
}
static std::wstring properCase(std::wstring text) {
    bool start = true;
    for (auto &c : text) {
        if (IsCharAlphaNumericW(c)) {
            wchar_t one[2] = {c, 0};
            if (start)
                CharUpperBuffW(one, 1);
            else
                CharLowerBuffW(one, 1);
            c = one[0];
            start = false;
        } else if (c != L'\'' && c != L'’')
            start = true; // Apostrophes stay inside a word: "don't" -> "Don't".
    }
    return text;
}
void App::titleCase() {
    auto d = current();
    if (!d || !d->editor || d->monitor)
        return;
    auto h = d->editor;
    int count = static_cast<int>(sci(h, SCI_GETSELECTIONS));
    std::vector<std::pair<Pos, Pos>> ranges;
    for (int i = 0; i < count; i++) {
        Pos a = sci(h, SCI_GETSELECTIONNSTART, i), b = sci(h, SCI_GETSELECTIONNEND, i);
        if (b > a)
            ranges.emplace_back(a, b);
    }
    if (ranges.empty())
        return;
    std::sort(ranges.begin(), ranges.end());
    Pos total = 0;
    for (auto &[a, b] : ranges)
        total += b - a;
    if (total > 32 * 1024 * 1024)
        fail(L"32 MiB 이하를 선택해 주세요.");
    std::vector<Pos> lengths(ranges.size());
    bool suppression = macroSuppressed;
    macroSuppressed = true;
    sci(h, SCI_BEGINUNDOACTION);
    try {
        for (size_t i = ranges.size(); i-- > 0;) {
            auto [a, b] = ranges[i];
            auto out = narrow(properCase(wide(textRange(h, a, b - a))));
            lengths[i] = static_cast<Pos>(out.size());
            if (out != textRange(h, a, b - a))
                replaceRange(h, a, b, out);
        }
    } catch (...) {
        sci(h, SCI_ENDUNDOACTION);
        macroSuppressed = suppression;
        throw;
    }
    sci(h, SCI_ENDUNDOACTION);
    // Restore every selection; earlier replacements shift later ranges by the length change.
    Pos shift = 0;
    for (size_t i = 0; i < ranges.size(); i++) {
        Pos a = ranges[i].first + shift, b = a + lengths[i];
        if (!i)
            sci(h, SCI_SETSELECTION, b, a);
        else
            sci(h, SCI_ADDSELECTION, b, a);
        shift += lengths[i] - (ranges[i].second - ranges[i].first);
    }
    macroSuppressed = suppression;
    MacroStep step;
    step.kind = 9;
    recordAction(step);
}
void App::toggleComment() {
    auto d = current();
    if (!d || !d->editor || d->monitor)
        return;
    auto h = d->editor;
    auto tokens = languages.comment(d->language);
    if (tokens.line.empty() && tokens.start.empty())
        fail(L"이 문서의 언어에는 주석 기호가 없습니다. 언어 메뉴에서 언어를 지정해 주세요.");
    Pos a = sci(h, SCI_GETSELECTIONSTART), b = sci(h, SCI_GETSELECTIONEND), caret = sci(h, SCI_GETCURRENTPOS);
    if (b - a > 32 * 1024 * 1024)
        fail(L"32 MiB 이하를 선택해 주세요.");
    Pos first = sci(h, SCI_LINEFROMPOSITION, a), last = sci(h, SCI_LINEFROMPOSITION, b);
    if (last > first && b == sci(h, SCI_POSITIONFROMLINE, last))
        last--; // A selection ending at column 0 does not include that line.
    bool suppression = macroSuppressed;
    macroSuppressed = true;
    sci(h, SCI_BEGINUNDOACTION);
    try {
        if (!tokens.line.empty()) {
            auto &token = tokens.line;
            struct Row {
                Pos indent, end;
                bool blank, commented;
            };
            std::vector<Row> rows;
            bool anyText = false, allCommented = true;
            for (Pos line = first; line <= last; line++) {
                Row r{sci(h, SCI_GETLINEINDENTPOSITION, line), sci(h, SCI_GETLINEENDPOSITION, line), false, false};
                r.blank = r.indent == r.end;
                r.commented = !r.blank && r.end - r.indent >= static_cast<Pos>(token.size()) &&
                              textRange(h, r.indent, static_cast<Pos>(token.size())) == token;
                if (!r.blank) {
                    anyText = true;
                    allCommented = allCommented && r.commented;
                }
                rows.push_back(r);
            }
            bool uncomment = anyText && allCommented;
            Pos caretDelta = 0;
            for (size_t i = rows.size(); i-- > 0;) {
                auto &r = rows[i];
                if (r.blank && anyText)
                    continue; // Leave empty lines alone inside a commented block.
                Pos change = 0;
                if (uncomment) {
                    Pos length = static_cast<Pos>(token.size());
                    if (r.indent + length < r.end && sci(h, SCI_GETCHARAT, r.indent + length) == ' ')
                        length++;
                    replaceRange(h, r.indent, r.indent + length, "");
                    change = -length;
                } else {
                    replaceRange(h, r.indent, r.indent, token + " ");
                    change = static_cast<Pos>(token.size()) + 1;
                }
                if (first + static_cast<Pos>(i) == sci(h, SCI_LINEFROMPOSITION, caret) && caret >= r.indent)
                    caretDelta = change;
            }
            if (a == b)
                sci(h, SCI_GOTOPOS, std::max(sci(h, SCI_POSITIONFROMLINE, first), caret + caretDelta));
            else
                sci(h, SCI_SETSEL, sci(h, SCI_POSITIONFROMLINE, first), sci(h, SCI_GETLINEENDPOSITION, last));
        } else {
            // Block comment: wrap the selection, or the current line's text when nothing is selected.
            Pos s = a, e = b;
            if (a == b) {
                s = sci(h, SCI_GETLINEINDENTPOSITION, first);
                e = sci(h, SCI_GETLINEENDPOSITION, first);
            }
            auto text = textRange(h, s, e - s);
            const auto &opener = tokens.start, &closer = tokens.end;
            if (text.size() >= opener.size() + closer.size() && text.starts_with(opener) &&
                text.ends_with(closer)) {
                auto inner = text.substr(opener.size(), text.size() - opener.size() - closer.size());
                if (inner.starts_with(' '))
                    inner.erase(0, 1);
                if (inner.ends_with(' '))
                    inner.pop_back();
                replaceRange(h, s, e, inner);
                sci(h, SCI_SETSEL, s, s + static_cast<Pos>(inner.size()));
            } else {
                auto wrapped = opener + " " + text + " " + closer;
                replaceRange(h, s, e, wrapped);
                sci(h, SCI_SETSEL, s, s + static_cast<Pos>(wrapped.size()));
            }
        }
    } catch (...) {
        sci(h, SCI_ENDUNDOACTION);
        macroSuppressed = suppression;
        throw;
    }
    sci(h, SCI_ENDUNDOACTION);
    macroSuppressed = suppression;
    MacroStep step;
    step.kind = 8;
    recordAction(step);
}
static bool isBrace(HWND h, Pos p) {
    if (p < 0 || p >= sci(h, SCI_GETLENGTH))
        return false;
    auto c = static_cast<char>(sci(h, SCI_GETCHARAT, p));
    return c && std::strchr("()[]{}", c);
}
static Pos braceAtCaret(HWND h, Pos caret) {
    // Like Notepad++: the brace before the caret wins over the one after it.
    return isBrace(h, caret - 1) ? caret - 1 : isBrace(h, caret) ? caret : -1;
}
void App::highlightBrace(Document &d) {
    auto h = d.editor;
    if (!h)
        return;
    Pos pos = braceMatch && !d.large ? braceAtCaret(h, sci(h, SCI_GETCURRENTPOS)) : -1;
    if (pos < 0) {
        sci(h, SCI_BRACEHIGHLIGHT, INVALID_POSITION, INVALID_POSITION);
        return;
    }
    Pos match = sci(h, SCI_BRACEMATCH, pos, 0);
    if (match < 0)
        sci(h, SCI_BRACEBADLIGHT, pos);
    else
        sci(h, SCI_BRACEHIGHLIGHT, pos, match);
}
void App::jumpBrace() {
    auto d = current();
    if (!d || !d->editor || d->large)
        return;
    auto h = d->editor;
    Pos caret = sci(h, SCI_GETCURRENTPOS), pos = braceAtCaret(h, caret);
    if (pos < 0)
        return;
    Pos match = sci(h, SCI_BRACEMATCH, pos, 0);
    if (match < 0)
        return;
    // Brace after the caret -> land after its partner; brace before the caret -> land before it.
    bool suppression = macroSuppressed;
    macroSuppressed = true;
    sci(h, SCI_GOTOPOS, pos == caret ? match + 1 : match);
    macroSuppressed = suppression;
    MacroStep step;
    step.kind = 10;
    recordAction(step);
}
void App::fold(int depth, bool expanded) {
    auto d = current();
    if (!d || !d->editor || !sci(d->editor, SCI_GETMARGINWIDTHN, 1) || d->viewMode == 1)
        return;
    auto h = d->editor;
    sci(h, SCI_COLOURISE, 0, -1);
    if (!depth)
        sci(h, SCI_FOLDALL,
            expanded ? SC_FOLDACTION_EXPAND : SC_FOLDACTION_CONTRACT | SC_FOLDACTION_CONTRACT_EVERY_LEVEL);
    else {
        std::vector<int> parents;
        for (Pos line = 0; line < sci(h, SCI_GETLINECOUNT); line++) {
            int level = static_cast<int>(sci(h, SCI_GETFOLDLEVEL, line));
            if (!(level & SC_FOLDLEVELHEADERFLAG))
                continue;
            int raw = level & SC_FOLDLEVELNUMBERMASK;
            while (!parents.empty() && parents.back() >= raw)
                parents.pop_back();
            parents.push_back(raw);
            if (static_cast<int>(parents.size()) == depth) {
                if (sci(h, SCI_GETLINEVISIBLE, line))
                    sci(h, SCI_FOLDLINE, line, expanded ? SC_FOLDACTION_EXPAND : SC_FOLDACTION_CONTRACT);
                else if (expanded)
                    sci(h, SCI_SETFOLDEXPANDED, line, 1);
            }
        }
    }
}
static std::string base64(const std::string &data) {
    static const char *table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    uint32_t value = 0;
    int bits = -6;
    for (unsigned char c : data) {
        value = (value << 8) | c;
        bits += 8;
        while (bits >= 0) {
            out += table[(value >> bits) & 63];
            bits -= 6;
        }
    }
    if (bits > -6)
        out += table[((value << 8) >> (bits + 8)) & 63];
    while (out.size() % 4)
        out += '=';
    return out;
}
static std::string unbase64(const std::string &s) {
    std::string out;
    uint32_t value = 0;
    int bits = -8;
    std::string table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (unsigned char c : s) {
        if (c == '=')
            break;
        auto pos = table.find(c);
        if (pos == std::string::npos)
            fail(L"잘못된 매크로 문자열입니다.");
        value = (value << 6) | static_cast<uint32_t>(pos);
        bits += 6;
        if (bits >= 0) {
            out += static_cast<char>(value >> bits);
            bits -= 8;
        }
    }
    return out;
}
void to_json(Json &j, const MacroStep &s) {
    j = {{"Kind", s.kind},
         {"Message", s.message},
         {"WParam", s.w},
         {"LParam", s.l},
         {"Data", s.kind == 0 && macroStrings.contains(s.message) ? Json(base64(s.data)) : Json(nullptr)},
         {"Query", s.kind == 7 ? s.data : s.search.query},
         {"Replacement", s.search.replacement},
         {"Flags", s.search.flags},
         {"Backwards", s.backwards},
         {"Wrap", true},
         {"Descending", s.descending},
         {"Number", s.number},
         {"Start", s.start},
         {"Increment", s.increment}};
}
void from_json(const Json &j, MacroStep &s) {
    s.kind = j.value("Kind", 0);
    s.message = j.value("Message", 0);
    s.w = j.value("WParam", 0ll);
    s.l = j.value("LParam", 0ll);
    if (j.contains("Data") && !j["Data"].is_null())
        s.data = unbase64(j["Data"]);
    s.search.query = j.value("Query", "");
    s.search.replacement = j.value("Replacement", "");
    s.search.flags = j.value("Flags", 0);
    s.backwards = j.value("Backwards", false);
    s.descending = j.value("Descending", false);
    s.number = j.value("Number", false);
    s.start = j.value("Start", 0ll);
    s.increment = j.value("Increment", 0ll);
    if (s.kind == 7)
        s.data = s.search.query;
    if (s.kind < 0 || s.kind > 7 || s.data.size() > 4 * 1024 * 1024 ||
        s.search.query.size() > 4 * 1024 * 1024 || s.search.replacement.size() > 4 * 1024 * 1024)
        fail(L"잘못된 매크로 동작입니다.");
    if (s.kind == 0) {
        if (!macroAllowed.contains(s.message) || s.l != 0)
            fail(L"허용되지 않는 매크로 명령입니다.");
        if (!macroStrings.contains(s.message) && j.contains("Data") && !j["Data"].is_null())
            fail(L"잘못된 매크로 매개변수입니다.");
        if ((s.message == SCI_ADDTEXT || s.message == SCI_APPENDTEXT) &&
            (s.w < 0 || static_cast<uint64_t>(s.w) != s.data.size()))
            fail(L"매크로 문자열 길이가 일치하지 않습니다.");
    }
    if (s.kind == 4 && s.w > 2)
        fail(L"잘못된 줄바꿈 매크로입니다.");
}
void App::recordAction(MacroStep s) {
    if (!recording || playing || macroSuppressed)
        return;
    if (recorded.size() >= 100000) {
        stopRecord();
        return;
    }
    recorded.push_back(std::move(s));
    update();
}
void App::capture(const SCNotification &n) {
    if (!macroAllowed.contains(n.message))
        return;
    MacroStep s;
    s.message = n.message;
    s.w = n.wParam;
    if (macroStrings.contains(n.message) && n.lParam) {
        const char *data = reinterpret_cast<const char *>(n.lParam);
        size_t size = n.message == SCI_ADDTEXT || n.message == SCI_APPENDTEXT
                          ? n.wParam
                          : strnlen(data, 4 * 1024 * 1024 + 1);
        if (size > 4 * 1024 * 1024) {
            stopRecord();
            return;
        }
        s.data.assign(data, size);
    }
    if (newlineActive)
        return;
    recordAction(s);
}
void App::toggleRecord() {
    if (recording) {
        stopRecord();
        return;
    }
    auto d = current();
    if (!d || !d->editor || d->monitor)
        fail(L"편집 가능한 텍스트 탭에서 녹화를 시작하세요.");
    recorded.clear();
    recording = true;
    for (auto &p : docs)
        if (p->editor)
            sci(p->editor, SCI_STARTRECORD);
    update();
}
void App::stopRecord() {
    for (auto &p : docs)
        if (p->editor)
            sci(p->editor, SCI_STOPRECORD);
    recording = false;
    lastMacro = recorded;
    persistMacros();
    update();
}
void App::runStep(const MacroStep &s) {
    auto d = current();
    if (!d || !d->editor)
        return;
    auto h = d->editor;
    switch (s.kind) {
    case 0:
        if (macroStrings.contains(s.message))
            str(h, s.message, s.data, s.w);
        else
            sci(h, s.message, s.w, s.l);
        break;
    case 1:
        search = s.search;
        if (!findNext(s.backwards, !playing))
            cancel = true;
        break;
    case 2:
        search = s.search;
        if (!replaceOne())
            cancel = true;
        break;
    case 3:
        search = s.search;
        search.allTabs = false;
        countMatches(true);
        break;
    case 4:
        sci(h, SCI_CONVERTEOLS, s.w);
        sci(h, SCI_SETEOLMODE, s.w);
        break;
    case 5: {
        auto language = d->language;
        d->language = s.search.query;
        try {
            format();
        } catch (...) {
            d->language = language;
            throw;
        }
        d->language = language;
        break;
    }
    case 6:
        sort(s.descending);
        break;
    case 7:
        applyColumn(h, s.number, s.data, s.start, s.increment);
        break;
    case 8:
        toggleComment();
        break;
    case 9:
        titleCase();
        break;
    case 10:
        jumpBrace();
        break;
    }
}
void App::play(const std::vector<MacroStep> &input, int64_t times, bool toEnd) {
    auto d = current();
    if (recording || playing || !d || !d->editor || d->monitor)
        fail(L"편집 가능한 텍스트 탭에서 녹화를 중지한 후 실행하세요.");
    if (input.empty())
        fail(L"먼저 매크로를 녹화하거나 저장된 매크로를 선택하세요.");
    if (times < 1 || times > 1000000 || input.size() > 100000)
        fail(L"반복 횟수는 1~1,000,000입니다.");
    auto steps = input;
    for (auto &s : steps) {
        Json j = s;
        auto validate = j.get<MacroStep>();
        (void)validate;
    }
    auto h = d->editor;
    playing = true;
    cancel = false;
    bool suppression = macroSuppressed;
    macroSuppressed = true;
    EnableWindow(h, FALSE);
    sci(h, SCI_BEGINUNDOACTION);
    int64_t done = 0;
    auto started = GetTickCount64();
    try {
        while ((toEnd || done < times) && !cancel) {
            Pos beforeLength = sci(h, SCI_GETLENGTH), beforeCaret = sci(h, SCI_GETCURRENTPOS),
                beforeLine = sci(h, SCI_LINEFROMPOSITION, beforeCaret),
                beforeLines = sci(h, SCI_GETLINECOUNT);
            for (auto &s : steps) {
                if (cancel)
                    break;
                runStep(s);
                if (GetTickCount64() - started >= 12) {
                    update();
                    pump();
                    started = GetTickCount64();
                }
            }
            done++;
            if (toEnd) {
                Pos caret = sci(h, SCI_GETCURRENTPOS), line = sci(h, SCI_LINEFROMPOSITION, caret),
                    lineCount = sci(h, SCI_GETLINECOUNT), length = sci(h, SCI_GETLENGTH);
                if (line < beforeLine && lineCount >= beforeLines ||
                    lineCount >= beforeLines && line <= beforeLine && length >= beforeLength ||
                    caret >= length)
                    break;
            }
            if (GetTickCount64() - started >= 12) {
                update();
                pump();
                started = GetTickCount64();
            }
        }
    } catch (...) {
        sci(h, SCI_ENDUNDOACTION);
        EnableWindow(h, TRUE);
        playing = false;
        macroSuppressed = suppression;
        throw;
    }
    sci(h, SCI_ENDUNDOACTION);
    EnableWindow(h, TRUE);
    SetFocus(h);
    sci(h, SCI_SCROLLCARET);
    playing = false;
    macroSuppressed = suppression;
    searchMessage = std::to_string(done) + (cancel ? "회 실행 후 중지했습니다." : "회 실행했습니다.");
    updateFind();
    update();
}
void App::repeat() {
    auto fields =
        inputDialog(window, L"매크로 반복 실행",
                    {{L"반복 횟수 (1~1,000,000)", L"1"}, {L"현재 위치부터 문서 끝까지", L"", true, false}});
    if (fields)
        play(lastMacro, std::stoll((*fields)[0].value), (*fields)[1].checked);
}
void App::saveMacro() {
    if (lastMacro.empty())
        fail(L"먼저 매크로를 녹화하세요.");
    auto fields = inputDialog(window, L"매크로 저장", {{L"이름", L""}});
    if (!fields)
        return;
    auto name = narrow((*fields)[0].value);
    if (name.empty() || (*fields)[0].value.size() > 100)
        fail(L"1~100자의 이름을 입력하세요.");
    for (auto &m : savedMacros)
        if (!_stricmp(m.name.c_str(), name.c_str()))
            fail(L"중복되지 않는 이름을 입력하세요.");
    savedMacros.push_back({guid(), name, lastMacro});
    persistMacros();
    loadMacros();
}
void App::manageMacros() {
    if (savedMacros.empty()) {
        message(L"저장된 매크로가 없습니다.");
        return;
    }
    std::wstring list;
    for (size_t i = 0; i < savedMacros.size(); i++)
        list += std::to_wstring(i + 1) + L". " + wide(savedMacros[i].name) + L"\n";
    auto fields = inputDialog(window, L"저장된 매크로 관리",
                              {{L"매크로 번호", L"1"},
                               {L"새 이름 (비우면 기존 이름 유지)", L""},
                               {L"선택한 매크로 삭제", L"", true, false}},
                              list);
    if (!fields)
        return;
    int index = std::stoi((*fields)[0].value) - 1;
    if (index < 0 || index >= static_cast<int>(savedMacros.size()))
        fail(L"올바른 번호를 입력하세요.");
    if ((*fields)[2].checked)
        savedMacros.erase(savedMacros.begin() + index);
    else if (!(*fields)[1].value.empty()) {
        auto name = narrow((*fields)[1].value);
        for (size_t i = 0; i < savedMacros.size(); i++)
            if (static_cast<int>(i) != index && !_stricmp(savedMacros[i].name.c_str(), name.c_str()))
                fail(L"중복되지 않는 이름을 입력하세요.");
        savedMacros[index].name = name;
    }
    persistMacros();
    loadMacros();
}
void App::persistMacros() {
    Json saved = Json::array();
    for (auto &m : savedMacros)
        saved.push_back({{"Id", m.id}, {"Name", m.name}, {"Steps", m.steps}});
    writeJson(profile / L"macros.json", {{"Version", 1}, {"Last", lastMacro}, {"Saved", saved}});
}
void App::loadMacros() {
    for (auto p : {profile / L"macros.json", profile / L"macros.json.bak"}) {
        if (!disk::exists(p))
            continue;
        try {
            auto j = readJson(p);
            if (j.value("Version", 0) != 1)
                continue;
            auto last = j["Last"].get<std::vector<MacroStep>>();
            if (last.size() > 100000)
                fail(L"매크로 단계가 너무 많습니다.");
            std::vector<SavedMacroEntry> saved;
            for (auto &m : j["Saved"]) {
                auto steps = m["Steps"].get<std::vector<MacroStep>>();
                if (steps.size() > 100000)
                    fail(L"매크로 단계가 너무 많습니다.");
                saved.push_back({m["Id"], m["Name"], std::move(steps)});
            }
            lastMacro = std::move(last);
            savedMacros = std::move(saved);
            break;
        } catch (...) {
        }
    }
    if (macroMenu) {
        while (GetMenuItemCount(macroMenu) > 6)
            DeleteMenu(macroMenu, 6, MF_BYPOSITION);
        if (!savedMacros.empty())
            AppendMenuW(macroMenu, MF_SEPARATOR, 0, nullptr);
        for (size_t i = 0; i < savedMacros.size(); i++)
            AppendMenuW(macroMenu, MF_STRING, SavedMacro + i, wide(savedMacros[i].name).c_str());
    }
}
} // namespace lp
