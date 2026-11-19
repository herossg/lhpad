#include "app.h"
#include <windowsx.h>
namespace lp {
struct BusyScope {
    App &a;
    bool previous;
    BusyScope(App &a, bool showBusy = true) : a(a), previous(a.busy) {
        a.busy = true;
        if (showBusy)
            a.update();
    }
    ~BusyScope() {
        a.busy = previous;
        a.update();
    }
};
void App::applyEditorFont(HWND h) {
    // Change only the font so language colours, emphasis and undo history survive.
    for (int style = 0; style <= STYLE_MAX; style++) {
        str(h, SCI_STYLESETFONT, editorFont, style);
        sci(h, SCI_STYLESETSIZEFRACTIONAL, style, editorFontSize);
    }
    int extra = MulDiv(editorLineSpacing, GetDpiForWindow(h), 96);
    sci(h, SCI_SETEXTRAASCENT, (extra + 1) / 2);
    sci(h, SCI_SETEXTRADESCENT, extra / 2);
    auto digits = std::to_string(std::max<LRESULT>(9999, sci(h, SCI_GETLINECOUNT))) + "  ";
    sci(h, SCI_SETMARGINWIDTHN, 0,
        std::max<LRESULT>(MulDiv(62, dpi, 96),
                          sci(h, SCI_TEXTWIDTH, STYLE_LINENUMBER, reinterpret_cast<LPARAM>(digits.c_str()))));
    InvalidateRect(h, nullptr, FALSE);
}
void App::configureEditor(HWND h) {
    sci(h, SCI_SETCODEPAGE, SC_CP_UTF8);
    sci(h, SCI_STYLESETFORE, STYLE_DEFAULT, RGB(34, 42, 53));
    sci(h, SCI_STYLESETBACK, STYLE_DEFAULT, RGB(253, 253, 252));
    str(h, SCI_STYLESETFONT, editorFont, STYLE_DEFAULT);
    sci(h, SCI_STYLESETSIZEFRACTIONAL, STYLE_DEFAULT, editorFontSize);
    sci(h, SCI_STYLECLEARALL);
    sci(h, SCI_SETCARETFORE, RGB(0, 117, 111));
    sci(h, SCI_SETCARETLINEVISIBLE, 1);
    sci(h, SCI_SETCARETLINEBACK, RGB(242, 247, 246));
    sci(h, SCI_SETSELFORE, 1, RGB(20, 47, 57));
    sci(h, SCI_SETSELBACK, 1, RGB(192, 226, 220));
    sci(h, SCI_SETMARGINTYPEN, 0, SC_MARGIN_NUMBER);
    sci(h, SCI_SETMARGINWIDTHN, 0, MulDiv(62, dpi, 96));
    sci(h, SCI_STYLESETFORE, STYLE_LINENUMBER, RGB(136, 145, 149));
    sci(h, SCI_SETMARGINTYPEN, 1, SC_MARGIN_SYMBOL);
    sci(h, SCI_SETMARGINMASKN, 1, SC_MASK_FOLDERS);
    sci(h, SCI_SETMARGINWIDTHN, 1, 0);
    sci(h, SCI_SETMARGINCURSORN, 1, SC_CURSORARROW);
    sci(h, SCI_SETFOLDMARGINCOLOUR, 1, RGB(242, 245, 245));
    sci(h, SCI_SETFOLDMARGINHICOLOUR, 1, RGB(242, 245, 245));
    int markers[] = {
        SC_MARK_BOXPLUSCONNECTED, SC_MARK_BOXMINUSCONNECTED, SC_MARK_TCORNER, SC_MARK_LCORNER, SC_MARK_VLINE,
        SC_MARK_BOXPLUS,          SC_MARK_BOXMINUS};
    for (int i = 25; i <= 31; i++) {
        sci(h, SCI_MARKERDEFINE, i, markers[i - 25]);
        sci(h, SCI_MARKERSETFORE, i, RGB(242, 245, 245));
        sci(h, SCI_MARKERSETBACK, i, RGB(132, 132, 132));
        sci(h, SCI_MARKERSETBACKSELECTED, i, RGB(255, 0, 0));
    }
    sci(h, SCI_MARKERENABLEHIGHLIGHT, 1);
    sci(h, SCI_SETFOLDFLAGS, SC_FOLDFLAG_LINEAFTER_CONTRACTED);
    sci(h, SCI_SETAUTOMATICFOLD, SC_AUTOMATICFOLD_SHOW | SC_AUTOMATICFOLD_CLICK | SC_AUTOMATICFOLD_CHANGE);
    sci(h, SCI_SETMULTIPLESELECTION, 1);
    sci(h, SCI_SETADDITIONALSELECTIONTYPING, 1);
    sci(h, SCI_SETMULTIPASTE, SC_MULTIPASTE_EACH);
    sci(h, SCI_SETRECTANGULARSELECTIONMODIFIER, SCMOD_ALT);
    sci(h, SCI_SETVIRTUALSPACEOPTIONS, SCVS_RECTANGULARSELECTION);
    // Brace highlights use container indicators so language changes (STYLECLEARALL) keep them.
    sci(h, SCI_INDICSETSTYLE, 8, INDIC_ROUNDBOX);
    sci(h, SCI_INDICSETFORE, 8, RGB(0, 150, 136));
    sci(h, SCI_INDICSETALPHA, 8, 70);
    sci(h, SCI_INDICSETOUTLINEALPHA, 8, 200);
    sci(h, SCI_INDICSETUNDER, 8, 1);
    sci(h, SCI_INDICSETSTYLE, 9, INDIC_ROUNDBOX);
    sci(h, SCI_INDICSETFORE, 9, RGB(220, 50, 47));
    sci(h, SCI_INDICSETALPHA, 9, 70);
    sci(h, SCI_INDICSETOUTLINEALPHA, 9, 200);
    sci(h, SCI_INDICSETUNDER, 9, 1);
    sci(h, SCI_INDICSETSTYLE, markIndicator, INDIC_ROUNDBOX);
    sci(h, SCI_INDICSETFORE, markIndicator, RGB(255, 183, 0));
    sci(h, SCI_INDICSETALPHA, markIndicator, 90);
    sci(h, SCI_INDICSETOUTLINEALPHA, markIndicator, 180);
    sci(h, SCI_INDICSETUNDER, markIndicator, 1);
    sci(h, SCI_BRACEHIGHLIGHTINDICATOR, 1, 8);
    sci(h, SCI_BRACEBADLIGHTINDICATOR, 1, 9);
    applyEditorSettings(h);
    sci(h, SCI_SETINDENTATIONGUIDES, SC_IV_LOOKBOTH);
    sci(h, SCI_SETEOLMODE, SC_EOL_CRLF);
    sci(h, SCI_SETSCROLLWIDTH, 1);
    sci(h, SCI_SETSCROLLWIDTHTRACKING, 1);
    sci(h, SCI_SETMODEVENTMASK, SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT);
    sci(h, SCI_SETVIEWWS, whiteSpace ? SCWS_VISIBLEALWAYS : SCWS_INVISIBLE);
    sci(h, SCI_SETVIEWEOL, eolVisible);
    sci(h, SCI_SETCONTROLCHARSYMBOL, controlVisible ? 0 : 32);
    for (int i = 0; i < 3; i++) {
        sci(h, SCI_MARKERDEFINE, 20 + i, SC_MARK_BACKGROUND);
        sci(h, SCI_MARKERSETBACK, 20 + i,
            i == 0   ? RGB(208, 242, 221)
            : i == 1 ? RGB(255, 219, 219)
                     : RGB(255, 239, 191));
    }
    applyEditorFont(h);
}
void App::applyEditorSettings(HWND h) {
    sci(h, SCI_SETTABWIDTH, tabWidth);
    sci(h, SCI_SETINDENT, 0); // Indent size follows the tab width.
    sci(h, SCI_SETUSETABS, useTabs);
    if (!braceMatch)
        sci(h, SCI_BRACEHIGHLIGHT, INVALID_POSITION, INVALID_POSITION);
}
void App::settings() {
    auto fields = inputDialog(window, L"설정",
                              {{L"탭 너비 (1~16)", std::to_wstring(tabWidth)},
                               {L"Tab 키로 탭 문자 넣기 (끄면 공백)", L"", true, useTabs},
                               {L"괄호 짝 강조", L"", true, braceMatch},
                               {L"자동 들여쓰기", L"", true, autoIndent}},
                              L"모든 텍스트 탭에 바로 적용되고 다음 실행에도 유지됩니다.");
    if (!fields)
        return;
    int width = 0;
    try {
        size_t used = 0;
        width = std::stoi((*fields)[0].value, &used);
        if (used != (*fields)[0].value.size())
            width = 0;
    } catch (...) {
        width = 0;
    }
    if (width < 1 || width > 16)
        fail(L"탭 너비는 1~16 사이의 숫자로 입력해 주세요.");
    tabWidth = width;
    useTabs = (*fields)[1].checked;
    braceMatch = (*fields)[2].checked;
    autoIndent = (*fields)[3].checked;
    for (auto &doc : docs)
        if (doc->editor) {
            applyEditorSettings(doc->editor);
            if (braceMatch)
                highlightBrace(*doc);
        }
}
void App::addRecent(const fs::path &path) {
    if (path.empty())
        return;
    std::erase_if(recentFiles, [&](const fs::path &p) { return !_wcsicmp(p.c_str(), path.c_str()); });
    recentFiles.insert(recentFiles.begin(), path);
    if (recentFiles.size() > recentLimit)
        recentFiles.resize(recentLimit);
    rebuildRecentMenu();
}
void App::rebuildRecentMenu() {
    if (!recentMenu)
        return;
    while (GetMenuItemCount(recentMenu) > 0)
        DeleteMenu(recentMenu, 0, MF_BYPOSITION);
    if (recentFiles.empty()) {
        AppendMenuW(recentMenu, MF_STRING | MF_GRAYED, 0, L"(최근 파일 없음)");
        return;
    }
    for (size_t i = 0; i < recentFiles.size(); i++) {
        std::wstring path = recentFiles[i].wstring(), escaped;
        for (auto c : path) {
            if (c == L'&')
                escaped += L'&';
            escaped += c;
        }
        auto label = (i < 9 ? L"&" + std::to_wstring(i + 1) : std::to_wstring(i + 1)) + L"  " + escaped;
        AppendMenuW(recentMenu, MF_STRING, RecentFile + static_cast<int>(i), label.c_str());
    }
    AppendMenuW(recentMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(recentMenu, MF_STRING, RecentClear, L"목록 지우기");
}
void App::openRecent(size_t index) {
    if (index >= recentFiles.size())
        return;
    auto path = recentFiles[index];
    if (!disk::exists(path)) {
        if (ask(path.wstring() + L"\n\n파일을 찾을 수 없습니다. 최근 파일 목록에서 제거할까요?", L"최근 파일",
                MB_YESNO | MB_ICONWARNING) == IDYES) {
            recentFiles.erase(recentFiles.begin() + static_cast<std::ptrdiff_t>(index));
            rebuildRecentMenu();
        }
        return;
    }
    openPaths({narrow(path.wstring())});
}
void App::reopenClosed() {
    while (!closedTabs.empty()) {
        auto tab = closedTabs.back();
        closedTabs.pop_back();
        if (!disk::exists(tab.path))
            continue;
        open(tab.path, tab.binary);
        addRecent(tab.path);
        return;
    }
    message(L"다시 열 수 있는 닫은 탭이 없습니다.");
}
HWND App::createEditor(Document *d, HWND parent) {
    HWND h = CreateWindowExW(0, L"Scintilla", nullptr,
                             WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP | WS_HSCROLL | WS_VSCROLL, 0, 0, 100,
                             100, parent ? parent : window, nullptr, instance, nullptr);
    if (!h)
        fail(L"Scintilla 편집기를 만들 수 없습니다.");
    auto doc = sci(h, SCI_CREATEDOCUMENT, 0, SC_DOCUMENTOPTION_TEXT_LARGE);
    sci(h, SCI_SETDOCPOINTER, 0, doc);
    sci(h, SCI_RELEASEDOCUMENT, 0, doc);
    configureEditor(h);
    SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(d));
    SetWindowSubclass(h, editorProc, 1, reinterpret_cast<DWORD_PTR>(this));
    DragAcceptFiles(h, TRUE);
    return h;
}
std::shared_ptr<Document> App::newDocument(std::string name, bool binary) {
    auto d = std::make_shared<Document>();
    d->name = name.empty() ? "새 문서 " + std::to_string(++untitled) : name;
    if (binary)
        d->hexWindow =
            CreateWindowExW(0, L"LHPad.Hex", nullptr, WS_CHILD | WS_CLIPSIBLINGS | WS_VSCROLL | WS_TABSTOP,
                            0, 0, 100, 100, window, nullptr, instance, d.get());
    else {
        d->editor = createEditor(d.get());
        languages.apply(d->editor, "normal");
        if (recording)
            sci(d->editor, SCI_STARTRECORD);
    }
    docs.push_back(d);
    TCITEMW item{};
    auto label = wide(d->name);
    item.mask = TCIF_TEXT | TCIF_IMAGE;
    item.pszText = label.data();
    item.iImage = 0;
    TabCtrl_InsertItem(tabs, static_cast<int>(docs.size() - 1), &item);
    select(static_cast<int>(docs.size() - 1));
    return d;
}
void App::open(const fs::path &input, std::optional<bool> forceBinary,
               std::optional<Encoding> forceEncoding) {
    auto raw = input.wstring();
    if (raw.starts_with(L"\\\\?\\UNC\\"))
        raw = L"\\\\" + raw.substr(8);
    else if (raw.starts_with(L"\\\\?\\"))
        raw = raw.substr(4);
    auto path = fs::absolute(fs::path(raw)).lexically_normal();
    if (!disk::is_regular_file(path))
        fail(L"파일을 찾을 수 없습니다: " + path.wstring());
    for (size_t i = 0; i < docs.size(); i++)
        if (!docs[i]->path.empty() && !_wcsicmp(docs[i]->path.c_str(), path.c_str())) {
            select(static_cast<int>(i));
            return;
        }
    BusyScope scope(*this);
    auto detected = detect(path);
    bool binary = forceBinary.value_or(detected.binary);
    auto d = newDocument(narrow(path.filename().wstring()), binary);
    d->path = path;
    d->encoding = forceEncoding.value_or(detected.encoding);
    d->language = languages.detect(path);
    d->loading = true;
    try {
        if (binary)
            d->binary = std::make_unique<BinaryBuffer>(path);
        else {
            d->large = disk::file_size(path) > 200ull * 1024 * 1024;
            SendMessageW(d->editor, WM_SETREDRAW, FALSE, 0);
            sci(d->editor, SCI_SETSCROLLWIDTHTRACKING, 0);
            sci(d->editor, SCI_SETUNDOCOLLECTION, 0);
            int skip = detected.skip;
            if (forceEncoding) {
                auto test = detect(path);
                skip = test.encoding.cp == forceEncoding->cp ? test.skip : 0;
            }
            Pos loaded = 0;
            readText(path, d->encoding, skip, [&](std::string_view s) {
                sci(d->editor, SCI_APPENDTEXT, s.size(), reinterpret_cast<LPARAM>(s.data()));
                loaded += s.size();
                auto message = L"여는 중: " + std::to_wstring(loaded / 1048576) + L" MiB";
                SendMessageW(status, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(message.c_str()));
                pump();
            });
            sci(d->editor, SCI_SETUNDOCOLLECTION, 1);
            sci(d->editor, SCI_EMPTYUNDOBUFFER);
            sci(d->editor, SCI_SETSAVEPOINT);
            sci(d->editor, SCI_GOTOPOS, 0);
            auto sample = textRange(d->editor, 0, std::min<Pos>(sci(d->editor, SCI_GETLENGTH), 65536));
            auto first = sample.find('\n');
            sci(d->editor, SCI_SETEOLMODE,
                first == std::string::npos
                    ? (sample.find('\r') != std::string::npos ? SC_EOL_CR : SC_EOL_CRLF)
                : first && sample[first - 1] == '\r' ? SC_EOL_CRLF
                                                     : SC_EOL_LF);
            languages.apply(d->editor, d->language, d->large);
            sci(d->editor, SCI_SETSCROLLWIDTHTRACKING, 1);
            SendMessageW(d->editor, WM_SETREDRAW, TRUE, 0);
            InvalidateRect(d->editor, nullptr, TRUE);
        }
        d->disk = stamp(path);
        d->loading = false;
        select(static_cast<int>(docs.size() - 1));
    } catch (...) {
        d->loading = false;
        auto i = std::find(docs.begin(), docs.end(), d);
        if (i != docs.end()) {
            int index = static_cast<int>(i - docs.begin());
            TabCtrl_DeleteItem(tabs, index);
            docs.erase(i);
            active = -1;
            if (!docs.empty())
                select(std::min(index, static_cast<int>(docs.size() - 1)));
        }
        throw;
    }
}
void App::openPaths(const std::vector<std::string> &paths, bool activate) {
    for (auto &s : paths) {
        try {
            fs::path p = wide(s);
            if (disk::is_directory(p))
                continue;
            open(p);
            addRecent(p);
        } catch (const std::exception &e) {
            message(wide(e.what()), L"파일 열기", MB_OK | MB_ICONWARNING);
        }
    }
    if (activate) {
        if (IsIconic(window))
            ShowWindow(window, maximized ? SW_MAXIMIZE : SW_RESTORE);
        SetForegroundWindow(window);
        if (auto d = current())
            SetFocus(d->binary ? d->hexWindow : d->editor);
    }
}
void App::chooseOpen() {
    std::wstring buffer(65536, 0);
    OPENFILENAMEW f{sizeof(f)};
    f.hwndOwner = window;
    f.lpstrFilter = L"모든 파일\0*.*\0텍스트 파일\0*.txt;*.log;*.json;*.xml;*.html;*.sql\0\0";
    f.lpstrFile = buffer.data();
    f.nMaxFile = static_cast<DWORD>(buffer.size());
    f.Flags = OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&f))
        return;
    fs::path first(buffer.c_str());
    wchar_t *next = buffer.data() + wcslen(buffer.c_str()) + 1;
    std::vector<std::string> paths;
    if (!*next)
        paths.push_back(narrow(first.wstring()));
    else
        while (*next) {
            paths.push_back(narrow((first / next).wstring()));
            next += wcslen(next) + 1;
        }
    openPaths(paths);
}
bool App::save(Document &d, bool saveAs) {
    if (d.monitor)
        fail(L"로그 모니터링을 먼저 꺼 주세요.");
    fs::path path = d.path;
    if (saveAs || path.empty()) {
        std::wstring buffer(32768, 0);
        auto name = path.empty() ? wide(d.name) : path.wstring();
        std::copy_n(name.data(), std::min(name.size(), buffer.size() - 1), buffer.data());
        OPENFILENAMEW f{sizeof(f)};
        f.hwndOwner = window;
        f.lpstrFilter = L"모든 파일\0*.*\0\0";
        f.lpstrFile = buffer.data();
        f.nMaxFile = static_cast<DWORD>(buffer.size());
        f.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST;
        if (!GetSaveFileNameW(&f))
            return false;
        path = buffer.c_str();
    }
    for (auto &other : docs)
        if (other.get() != &d && !other->path.empty() && !_wcsicmp(other->path.c_str(), path.c_str()))
            fail(L"다른 탭에서 이미 열려 있는 파일입니다.");
    auto state = stamp(path);
    bool same = !d.path.empty() && !_wcsicmp(d.path.c_str(), path.c_str());
    if (same && state != d.disk && state.exists &&
        ask(L"디스크의 파일이 변경되었습니다. 덮어쓸까요?", L"외부 변경", MB_YESNO | MB_ICONWARNING) != IDYES)
        return false;
    BusyScope scope(*this);
    if (d.binary) {
        d.binary->save(path);
        d.binary->savedRevision = d.binary->revision;
    } else {
        auto h = d.editor;
        const auto revision = d.revision;
        saveText(path, sci(h, SCI_GETLENGTH), d.encoding, [&](Pos start, Pos count) {
            auto s = textRange(h, start, count);
            pump();
            if (d.revision != revision)
                fail(L"저장 중 문서가 변경되어 저장을 중단했습니다. 원본 파일은 보존됩니다.");
            return s;
        });
        sci(h, SCI_SETSAVEPOINT);
    }
    d.path = path;
    d.name = narrow(path.filename().wstring());
    d.encodingChanged = d.recovered = false;
    d.disk = stamp(path);
    d.notifiedValid = false;
    d.snapshotRevision = ~uint64_t{};
    addRecent(path);
    return true;
}
bool App::closeTab(int index) {
    if (index < 0 || index >= static_cast<int>(docs.size()) || busy || playing)
        return false;
    auto d = docs[index];
    if (d->dirty()) {
        int answer = ask(wide(d->name) + L"의 변경 내용을 저장할까요?");
        if (answer == IDCANCEL)
            return false;
        if (answer == IDYES && !save(*d))
            return false;
    }
    compareIds.erase(d->id);
    if (!d->path.empty()) {
        closedTabs.push_back({d->path, d->binary != nullptr});
        if (closedTabs.size() > closedLimit)
            closedTabs.erase(closedTabs.begin());
        addRecent(d->path);
    }
    if (dragTab == d->id)
        dragTab.clear();
    TabCtrl_DeleteItem(tabs, index);
    docs.erase(docs.begin() + index);
    active = -1;
    if (docs.empty())
        newDocument();
    else
        select(std::min(index, static_cast<int>(docs.size() - 1)));
    return true;
}
void App::reload(Document &d, std::optional<bool> binary, std::optional<Encoding> encoding) {
    if (d.path.empty())
        fail(L"파일을 먼저 저장해 주세요.");
    if (d.dirty() && !checking &&
        ask(L"다시 열면 현재 편집 내용이 사라집니다. 계속할까요?", L"다시 열기", MB_YESNO | MB_ICONWARNING) !=
            IDYES)
        return;
    BusyScope scope(*this);
    bool asBinary = binary.value_or(d.binary != nullptr);
    auto path = d.path;
    auto staged = std::make_shared<Document>();
    staged->loading = true;
    staged->path = path;
    staged->encoding = encoding.value_or(d.encoding);
    staged->language = d.language;
    Pos anchor = d.editor ? sci(d.editor, SCI_GETANCHOR) : d.anchor,
        caret = d.editor ? sci(d.editor, SCI_GETCURRENTPOS) : d.caret,
        first = d.editor ? sci(d.editor, SCI_GETFIRSTVISIBLELINE) : 0;
    if (asBinary) {
        staged->binary = std::make_unique<BinaryBuffer>(path);
        staged->hexWindow =
            CreateWindowExW(0, L"LHPad.Hex", nullptr, WS_CHILD | WS_CLIPSIBLINGS | WS_VSCROLL | WS_TABSTOP,
                            0, 0, 100, 100, window, nullptr, instance, &d);
    } else {
        staged->editor = createEditor(&d);
        staged->large = disk::file_size(path) > 200ull * 1024 * 1024;
        auto detected = detect(path);
        int skip = detected.encoding.cp == staged->encoding.cp ? detected.skip : 0;
        sci(staged->editor, SCI_SETUNDOCOLLECTION, 0);
        readText(path, staged->encoding, skip, [&](std::string_view s) {
            sci(staged->editor, SCI_ADDTEXT, s.size(), reinterpret_cast<LPARAM>(s.data()));
            pump();
        });
        sci(staged->editor, SCI_SETUNDOCOLLECTION, 1);
        sci(staged->editor, SCI_SETSAVEPOINT);
        languages.apply(staged->editor, d.language, staged->large);
    }
    if (d.controller)
        d.controller->Close();
    d.controller.Reset();
    d.browser.Reset();
    if (d.previewHost)
        DestroyWindow(std::exchange(d.previewHost, nullptr));
    if (d.editor)
        DestroyWindow(d.editor);
    if (d.hexWindow)
        DestroyWindow(d.hexWindow);
    d.editor = std::exchange(staged->editor, nullptr);
    d.hexWindow = std::exchange(staged->hexWindow, nullptr);
    d.binary = std::move(staged->binary);
    d.large = staged->large;
    d.encoding = staged->encoding;
    d.viewMode = 0;
    d.recovered = d.encodingChanged = d.monitor = false;
    d.disk = stamp(path);
    d.notifiedValid = false;
    d.snapshotRevision = ~uint64_t{};
    d.revision++;
    if (d.editor) {
        sci(d.editor, SCI_SETSEL, anchor, caret);
        sci(d.editor, SCI_SETFIRSTVISIBLELINE, first);
    } else {
        d.anchor = std::min(anchor, d.binary->size());
        d.caret = std::min(caret, d.binary->size());
    }
    layout();
}
void App::backup() {
    if (starting || busy || playing || closing)
        return;
    BusyScope scope(*this, false);
    disk::create_directories(profile);
    Json states = Json::array();
    for (auto &item : docs) {
        auto &d = *item;
        if (d.loading)
            fail(L"문서를 읽는 중입니다.");
        bool needs = d.dirty() || d.path.empty();
        if (needs && (d.snapshot.empty() || d.snapshotRevision != d.revision || !disk::exists(d.snapshot))) {
            auto snapshot = profile / wide(d.id + "-" + guid() + ".snapshot");
            if (d.binary)
                d.binary->save(snapshot);
            else
                saveText(snapshot, sci(d.editor, SCI_GETLENGTH), Encoding{},
                         [&](Pos p, Pos n) { return textRange(d.editor, p, n); });
            d.snapshot = snapshot;
            d.snapshotRevision = d.revision;
        }
        Json selections = Json::array();
        if (d.editor)
            for (int i = 0; i < sci(d.editor, SCI_GETSELECTIONS); i++)
                selections.push_back({{"Anchor", sci(d.editor, SCI_GETSELECTIONNANCHOR, i)},
                                      {"Caret", sci(d.editor, SCI_GETSELECTIONNCARET, i)}});
        Json s = {{"Id", d.id},
                  {"Path", d.path.empty() ? Json(nullptr) : Json(narrow(d.path.wstring()))},
                  {"Name", d.name},
                  {"Encoding", d.encoding},
                  {"Language", d.language},
                  {"Binary", d.binary != nullptr},
                  {"Dirty", d.dirty()},
                  {"Snapshot", needs ? Json(narrow(d.snapshot.wstring())) : Json(nullptr)},
                  {"Caret", d.editor ? sci(d.editor, SCI_GETCURRENTPOS) : d.caret},
                  {"Anchor", d.editor ? sci(d.editor, SCI_GETANCHOR) : d.anchor},
                  {"FirstLine", d.editor ? sci(d.editor, SCI_GETFIRSTVISIBLELINE) : d.firstRow},
                  {"Selections", selections},
                  {"Wrap", d.editor && sci(d.editor, SCI_GETWRAPMODE) != 0},
                  {"Eol", d.editor ? sci(d.editor, SCI_GETEOLMODE) : 0},
                  {"Zoom", d.editor ? sci(d.editor, SCI_GETZOOM) : 0},
                  {"ViewMode", d.viewMode},
                  {"Stamp", "0001-01-01T00:00:00"},
                  {"OriginalLength", d.disk.size}};
        states.push_back(s);
    }
    for (auto &j : unavailable)
        states.push_back(j);
    Json session = {{"Version", 1},
                    {"Active", active},
                    {"Documents", states},
                    {"WhiteSpace", whiteSpace},
                    {"EolVisible", eolVisible},
                    {"ControlsVisible", controlVisible},
                    {"AutoIndent", autoIndent},
                    {"EditorFont", editorFont},
                    {"EditorFontSize", editorFontSize},
                    {"EditorLineSpacing", editorLineSpacing},
                    {"TabWidth", tabWidth},
                    {"UseTabs", useTabs},
                    {"BraceMatch", braceMatch},
                    {"FindFolder", narrow(findFolder.wstring())},
                    {"FindFilter", narrow(findFilter)},
                    {"FindSubfolders", findSubfolders}};
    Json recent = Json::array();
    for (auto &p : recentFiles)
        recent.push_back(narrow(p.wstring()));
    session["RecentFiles"] = recent;
    writeJson(profile / L"session.json", session);
}
void App::restore() {
    Json session;
    bool found = false;
    std::string failures;
    for (auto path : {profile / L"session.json", profile / L"session.json.bak"}) {
        if (!disk::exists(path))
            continue;
        try {
            session = readJson(path);
            if (session.value("Version", 0) != 1 || !session.contains("Documents") ||
                !session["Documents"].is_array())
                throw std::runtime_error("Unsupported session format");
            found = true;
            break;
        } catch (const std::exception &e) {
            failures = e.what();
        }
    }
    if (!found) {
        if (!failures.empty()) {
            profile /= wide("new-session-" + guid());
            disk::create_directories(profile);
            message(L"기존 세션 파일을 읽지 못했습니다. 원본 백업을 보존하고 별도 프로필을 사용합니다.\n" +
                        wide(failures),
                    L"세션 복원", MB_OK | MB_ICONWARNING);
        }
        return;
    }
    autoIndent = session.value("AutoIndent", true);
    whiteSpace = session.value("WhiteSpace", false);
    eolVisible = session.value("EolVisible", false);
    controlVisible = session.value("ControlsVisible", false);
    if (session.contains("EditorFont") && session["EditorFont"].is_string()) {
        auto name = session["EditorFont"].get<std::string>();
        if (!name.empty() && name.size() <= 128 && name.find('\0') == std::string::npos)
            editorFont = std::move(name);
    }
    if (session.contains("EditorFontSize") && session["EditorFontSize"].is_number_integer())
        editorFontSize = std::clamp(session["EditorFontSize"].get<int>(), 600, 7200);
    if (session.contains("EditorLineSpacing") && session["EditorLineSpacing"].is_number_integer())
        editorLineSpacing = std::clamp(session["EditorLineSpacing"].get<int>(), 0, 24);
    if (session.contains("TabWidth") && session["TabWidth"].is_number_integer())
        tabWidth = std::clamp(session["TabWidth"].get<int>(), 1, 16);
    useTabs = session.value("UseTabs", false);
    braceMatch = session.value("BraceMatch", true);
    if (session.contains("FindFolder") && session["FindFolder"].is_string())
        findFolder = wide(session["FindFolder"].get<std::string>());
    if (session.contains("FindFilter") && session["FindFilter"].is_string() &&
        !session["FindFilter"].get<std::string>().empty())
        findFilter = wide(session["FindFilter"].get<std::string>());
    findSubfolders = session.value("FindSubfolders", true);
    recentFiles.clear();
    if (session.contains("RecentFiles") && session["RecentFiles"].is_array())
        for (auto &item : session["RecentFiles"])
            if (item.is_string() && !item.get<std::string>().empty() && recentFiles.size() < recentLimit)
                recentFiles.push_back(wide(item.get<std::string>()));
    rebuildRecentMenu();
    for (auto &doc : docs)
        if (doc->editor)
            applyEditorSettings(doc->editor);
    std::string errors;
    for (auto &s : session["Documents"]) {
        std::shared_ptr<Document> d;
        try {
            bool binary = s.value("Binary", false), dirty = s.value("Dirty", false);
            fs::path path =
                s.contains("Path") && !s["Path"].is_null() ? wide(s["Path"].get<std::string>()) : L"";
            fs::path snapshot = s.contains("Snapshot") && !s["Snapshot"].is_null()
                                    ? wide(s["Snapshot"].get<std::string>())
                                    : L"";
            bool recover = (dirty || path.empty()) && !snapshot.empty();
            if (recover && !disk::exists(snapshot))
                fail(L"복구 스냅샷을 찾을 수 없습니다.");
            auto source = recover ? snapshot : path;
            if (source.empty())
                d = newDocument(s.value("Name", "새 문서"), binary);
            else {
                open(source, binary,
                     recover && !binary ? std::optional<Encoding>(Encoding{})
                                        : std::optional<Encoding>(s.value("Encoding", Encoding{})));
                d = docs.back();
            }
            d->id = s.value("Id", guid());
            d->name = s.value("Name", d->name);
            d->path = path;
            d->encoding = s.value("Encoding", Encoding{});
            d->language = s.value("Language", "normal");
            d->recovered = dirty;
            d->snapshot = snapshot;
            d->snapshotRevision = d->revision;
            d->disk = path.empty() ? Stamp{} : stamp(path);
            if (d->editor) {
                sci(d->editor, SCI_SETWRAPMODE, s.value("Wrap", false) ? SC_WRAP_WORD : SC_WRAP_NONE);
                sci(d->editor, SCI_SETEOLMODE, s.value("Eol", SC_EOL_CRLF));
                sci(d->editor, SCI_SETZOOM, s.value("Zoom", 0));
                auto selections = s.value("Selections", Json::array());
                if (selections.empty())
                    sci(d->editor, SCI_SETSEL, s.value("Anchor", 0ll), s.value("Caret", 0ll));
                else {
                    sci(d->editor, SCI_CLEARSELECTIONS);
                    bool first = true;
                    for (auto &selection : selections) {
                        auto anchor = selection.value("Anchor", 0ll), caret = selection.value("Caret", 0ll);
                        if (first) {
                            sci(d->editor, SCI_SETSELECTION, caret, anchor);
                            first = false;
                        } else
                            sci(d->editor, SCI_ADDSELECTION, caret, anchor);
                    }
                }
                sci(d->editor, SCI_SETFIRSTVISIBLELINE, s.value("FirstLine", 0ll));
                languages.apply(d->editor, d->language, d->large);
                int mode = s.value("ViewMode", 0);
                if (mode >= 0 && mode <= 2)
                    d->viewMode = mode;
            } else {
                d->caret = std::min<Pos>(s.value("Caret", 0ll), d->binary->size());
                d->anchor = std::min<Pos>(s.value("Anchor", 0ll), d->binary->size());
                d->firstRow = s.value("FirstLine", 0ll);
            }
        } catch (const std::exception &e) {
            unavailable.push_back(s);
            errors += s.value("Name", "문서") + ": " + e.what() + "\n";
        }
    }
    if (!docs.empty()) {
        select(std::clamp(session.value("Active", 0), 0, static_cast<int>(docs.size() - 1)));
        for (auto &d : docs)
            if (d->viewMode)
                refreshPreview(*d, true);
    }
    if (!errors.empty())
        message(L"일부 탭 복원을 보류했습니다. 기존 복구 자료는 보존됩니다.\n" + wide(errors), L"세션 복원",
                MB_OK | MB_ICONWARNING);
}
void App::savePlacement() {
    WINDOWPLACEMENT p{sizeof(p)};
    if (!GetWindowPlacement(window, &p))
        fail(errorText());
    RECT r = p.rcNormalPosition;
    writeJson(profile / L"window.json", {{"Version", 1},
                                         {"X", r.left},
                                         {"Y", r.top},
                                         {"Width", r.right - r.left},
                                         {"Height", r.bottom - r.top},
                                         {"Maximized", IsZoomed(window) || IsIconic(window) && maximized}});
}
void App::restorePlacement(int &show) {
    for (auto path : {profile / L"window.json", profile / L"window.json.bak"}) {
        try {
            if (!disk::exists(path))
                continue;
            auto j = readJson(path);
            if (j.value("Version", 0) != 1)
                continue;
            RECT r{j.value("X", 100), j.value("Y", 100), 0, 0};
            int w = j.value("Width", 1320), h = j.value("Height", 850);
            if (w < 200 || w > 100000 || h < 100 || h > 100000)
                continue;
            r.right = r.left + w;
            r.bottom = r.top + h;
            MONITORINFO mi{sizeof(mi)};
            GetMonitorInfoW(MonitorFromRect(&r, MONITOR_DEFAULTTOPRIMARY), &mi);
            w = std::clamp<int>(w, std::min<int>(MulDiv(800, dpi, 96), mi.rcWork.right - mi.rcWork.left),
                                mi.rcWork.right - mi.rcWork.left);
            h = std::clamp<int>(h, std::min<int>(MulDiv(500, dpi, 96), mi.rcWork.bottom - mi.rcWork.top),
                                mi.rcWork.bottom - mi.rcWork.top);
            int x = std::clamp<int>(r.left, mi.rcWork.left, mi.rcWork.right - w),
                y = std::clamp<int>(r.top, mi.rcWork.top, mi.rcWork.bottom - h);
            SetWindowPos(window, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
            if (j.value("Maximized", false)) {
                show = SW_MAXIMIZE;
                maximized = true;
            }
            break;
        } catch (...) {
        }
    }
}
void App::exit() {
    if (busy || starting)
        return;
    if (playing) {
        cancel = true;
        return;
    }
    if (recording)
        stopRecord();
    backup();
    savePlacement();
    persistMacros();
    closing = true;
    DestroyWindow(window);
}
void App::timer() {
    if (starting || closing || busy || playing || checking)
        return;
    auto now = GetTickCount64();
    if (!initialFiles.empty()) {
        auto files = std::exchange(initialFiles, {});
        guard([&] { openPaths(files); });
    }
    if (now >= previewTick)
        for (auto &d : docs)
            if (d->previewPending && d->viewMode)
                guard([&] { refreshPreview(*d); });
    static uint64_t monitorTick = 0;
    if (now - monitorTick >= 1000) {
        monitorTick = now;
        try {
            pollMonitors();
        } catch (...) {
        }
    }
    if (now - backupTick >= 3000) {
        backupTick = now;
        try {
            backup();
        } catch (...) {
            SendMessageW(status, SB_SETTEXTW, 4, reinterpret_cast<LPARAM>(L"백업 재시도 대기"));
        }
    }
}
void App::checkExternal() {
    if (checking || starting || busy || playing || closing)
        return;
    checking = true;
    try {
        for (auto &d : docs) {
            if (d->path.empty() || d->monitor || d->loading)
                continue;
            auto state = stamp(d->path);
            if (state == d->disk || d->notifiedValid && state == d->notified)
                continue;
            d->notified = state;
            d->notifiedValid = true;
            if (!state.exists)
                continue;
            if (ask(wide(d->name) +
                        (d->dirty() ? L"가 외부에서 변경되었습니다. 미저장 편집을 버리고 다시 불러올까요?"
                                    : L"가 외부에서 변경되었습니다. 다시 불러올까요?"),
                    L"외부 파일 변경", MB_YESNO | MB_ICONWARNING) == IDYES) {
                reload(*d);
            }
        }
        checking = false;
    } catch (...) {
        checking = false;
        throw;
    }
}
static Bytes readSegment(const fs::path &path, Pos offset, size_t count) {
    Handle f(CreateFileW(winPath(path).c_str(), GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!f)
        fail(errorText());
    LARGE_INTEGER pos{};
    pos.QuadPart = offset;
    SetFilePointerEx(f.h, pos, nullptr, FILE_BEGIN);
    Bytes b(count);
    DWORD got = 0;
    if (!ReadFile(f.h, b.data(), static_cast<DWORD>(count), &got, nullptr))
        fail(errorText());
    b.resize(got);
    return b;
}
void App::toggleMonitor() {
    auto d = current();
    if (!d || d->binary || d->path.empty())
        fail(L"저장된 텍스트 파일에서 로그 모니터링을 사용할 수 있습니다.");
    if (d->monitor) {
        d->monitor = false;
        sci(d->editor, SCI_SETREADONLY, 0);
        return;
    }
    if (d->dirty() && !save(*d))
        return;
    d->monitorOffset = disk::file_size(d->path);
    d->monitorPending.clear();
    d->monitorHead = readSegment(d->path, 0, static_cast<size_t>(std::min<Pos>(64, d->monitorOffset)));
    d->monitorBoundary = readSegment(d->path, std::max<Pos>(0, d->monitorOffset - 64),
                                     static_cast<size_t>(std::min<Pos>(64, d->monitorOffset)));
    d->disk = stamp(d->path);
    d->monitor = true;
    d->monitorStatus = "로그 모니터링";
    sci(d->editor, SCI_SETREADONLY, 1);
}
void App::pollMonitors() {
    if (std::none_of(docs.begin(), docs.end(), [](const auto &d) { return d->monitor; }))
        return;
    BusyScope scope(*this, false);
    for (auto &ptr : docs) {
        auto &d = *ptr;
        if (!d.monitor)
            continue;
        try {
            auto state = stamp(d.path);
            if (!state.exists) {
                d.monitorStatus = "파일 재생성 대기";
                continue;
            }
            bool reset = state.id != d.disk.id || state.volume != d.disk.volume ||
                         state.size < static_cast<uint64_t>(d.monitorOffset);
            if (!reset && !d.monitorHead.empty())
                reset = readSegment(d.path, 0, d.monitorHead.size()) != d.monitorHead;
            if (!reset && !d.monitorBoundary.empty())
                reset = readSegment(d.path, d.monitorOffset - static_cast<Pos>(d.monitorBoundary.size()),
                                    d.monitorBoundary.size()) != d.monitorBoundary;
            Pos oldCaret = sci(d.editor, SCI_GETCURRENTPOS), oldAnchor = sci(d.editor, SCI_GETANCHOR),
                first = sci(d.editor, SCI_GETFIRSTVISIBLELINE);
            if (reset) {
                auto changedEncoding = detect(d.path);
                if (changedEncoding.skip && changedEncoding.encoding.cp != d.encoding.cp) {
                    d.monitor = false;
                    sci(d.editor, SCI_SETREADONLY, 0);
                    d.monitorStatus = "인코딩 변경 — 모니터링 중지";
                    continue;
                }
                d.monitorOffset = changedEncoding.skip;
                d.monitorPending.clear();
            }
            auto b = readSegment(
                d.path, d.monitorOffset,
                static_cast<size_t>(std::min<uint64_t>(256 * 1024, state.size - d.monitorOffset)));
            d.monitorOffset += b.size();
            d.monitorPending.insert(d.monitorPending.end(), b.begin(), b.end());
            size_t usable = d.monitorPending.size();
            std::string text;
            bool decoded = false;
            for (size_t trim = 0; trim <= std::min<size_t>(8, usable); trim++) {
                try {
                    Bytes part(d.monitorPending.begin(), d.monitorPending.begin() + usable - trim);
                    text = decode(part, d.encoding);
                    usable -= trim;
                    decoded = true;
                    break;
                } catch (...) {
                }
            }
            if (!decoded) {
                d.monitor = false;
                sci(d.editor, SCI_SETREADONLY, 0);
                d.monitorStatus = "인코딩 오류 — 모니터링 중지";
                continue;
            }
            d.monitorPending.erase(d.monitorPending.begin(), d.monitorPending.begin() + usable);
            d.loading = true;
            sci(d.editor, SCI_SETREADONLY, 0);
            sci(d.editor, SCI_SETUNDOCOLLECTION, 0);
            if (reset)
                sci(d.editor, SCI_CLEARALL);
            if (!text.empty())
                sci(d.editor, SCI_APPENDTEXT, text.size(), reinterpret_cast<LPARAM>(text.data()));
            sci(d.editor, SCI_SETSAVEPOINT);
            sci(d.editor, SCI_SETUNDOCOLLECTION, 1);
            sci(d.editor, SCI_SETREADONLY, 1);
            d.loading = false;
            if (d.follow)
                sci(d.editor, SCI_GOTOPOS, sci(d.editor, SCI_GETLENGTH));
            else {
                sci(d.editor, SCI_SETSEL, oldAnchor, oldCaret);
                sci(d.editor, SCI_SETFIRSTVISIBLELINE, first);
            }
            d.disk = state;
            d.monitorHead = readSegment(d.path, 0, static_cast<size_t>(std::min<Pos>(64, d.monitorOffset)));
            d.monitorBoundary = readSegment(d.path, std::max<Pos>(0, d.monitorOffset - 64),
                                            static_cast<size_t>(std::min<Pos>(64, d.monitorOffset)));
            d.monitorStatus = "로그 모니터링";
            if (reset || !text.empty()) {
                d.previewPending = true;
                previewTick = GetTickCount64() + 400;
            }
        } catch (...) {
            d.loading = false;
            d.monitorStatus = "파일 접근 재시도 대기";
        }
    }
}
LRESULT CALLBACK App::editorProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR data) {
    auto *a = reinterpret_cast<App *>(data);
    auto *d = reinterpret_cast<Document *>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!d)
        return DefSubclassProc(h, m, w, l);
    if ((a->busy || a->playing) && (m == WM_CHAR || m == WM_KEYDOWN || m == WM_PASTE || m == WM_CUT))
        return 0;
    if (!a->newlineActive &&
        (m == SCI_NEWLINE ||
         m == WM_KEYDOWN && w == VK_RETURN && GetKeyState(VK_CONTROL) >= 0 && GetKeyState(VK_MENU) >= 0)) {
        a->guard([&] { a->newline(*d, m, w, l); });
        return 0;
    }
    if (m == WM_SETFOCUS)
        for (size_t i = 0; i < a->docs.size(); i++)
            if (a->docs[i].get() == d) {
                if (a->active != static_cast<int>(i)) {
                    a->active = static_cast<int>(i);
                    TabCtrl_SetCurSel(a->tabs, a->active);
                    a->update();
                }
                break;
            }
    if (m == WM_CONTEXTMENU) {
        auto menu = CreatePopupMenu();
        for (auto &item :
             {std::pair{Undo, L"실행 취소"}, std::pair{Redo, L"다시 실행"}, std::pair{Cut, L"잘라내기"},
              std::pair{Copy, L"복사"}, std::pair{Paste, L"붙여넣기"}, std::pair{SelectAll, L"모두 선택"}})
            AppendMenuW(menu, MF_STRING, item.first, item.second);
        POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        if (p.x == -1)
            GetCursorPos(&p);
        int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, a->window, nullptr);
        DestroyMenu(menu);
        if (cmd)
            a->command(cmd);
        return 0;
    }
    if (m == WM_DROPFILES)
        return childProc(h, m, w, l, id, data);
    if (m == WM_NCDESTROY)
        RemoveWindowSubclass(h, editorProc, id);
    return DefSubclassProc(h, m, w, l);
}
} // namespace lp
