#include "app.h"
#include <shlwapi.h>
namespace lp {
namespace {
std::wstring hitLine(HWND h, Pos line) {
    Pos start = sci(h, SCI_POSITIONFROMLINE, line), end = sci(h, SCI_GETLINEENDPOSITION, line);
    // A partial UTF-8 sequence at the cut is replaced, not rejected.
    auto text = wide(textRange(h, start, std::min<Pos>(end - start, 1000)), CP_UTF8, false);
    auto first = text.find_first_not_of(L" \t");
    text = first == std::wstring::npos ? std::wstring{} : text.substr(first);
    for (auto &c : text)
        if (c == L'\t')
            c = L' ';
    return text;
}
// Walks every match of `options` in `h`. Returns false when `limit` matches were already reported.
bool eachMatch(App &a, HWND h, const SearchOptions &options, size_t limit, size_t &count,
               const std::function<void(Pos, Pos)> &visit) {
    Pos length = sci(h, SCI_GETLENGTH), pos = 0;
    while (pos <= length && !a.cancel) {
        auto found = findTarget(h, options, pos, length);
        if (found < 0)
            break;
        Pos end = sci(h, SCI_GETTARGETEND);
        if (count >= limit)
            return false;
        visit(found, end);
        count++;
        pos = end;
        if (found == end) {
            if (pos >= length)
                break;
            pos = sci(h, SCI_POSITIONAFTER, pos);
        }
        if (count % 256 == 0)
            pump();
    }
    return true;
}
// Restores the target range, search flags and macro suppression that a search borrowed.
struct SearchScope {
    App &app;
    HWND h;
    Pos start, end;
    LRESULT flags;
    bool suppressed;
    SearchScope(App &a, HWND editor)
        : app(a), h(editor), start(sci(editor, SCI_GETTARGETSTART)), end(sci(editor, SCI_GETTARGETEND)),
          flags(sci(editor, SCI_GETSEARCHFLAGS)), suppressed(a.macroSuppressed) {
        app.macroSuppressed = true;
    }
    ~SearchScope() {
        sci(h, SCI_SETTARGETSTART, start);
        sci(h, SCI_SETTARGETEND, end);
        sci(h, SCI_SETSEARCHFLAGS, flags);
        app.macroSuppressed = suppressed;
    }
    SearchScope(const SearchScope &) = delete;
    SearchScope &operator=(const SearchScope &) = delete;
};
struct Searching {
    App &app;
    bool busy;
    Searching(App &a) : app(a), busy(a.busy) {
        app.busy = app.searching = true;
        app.cancel = false;
        app.updateFind();
    }
    ~Searching() {
        app.busy = busy;
        app.searching = false;
        app.updateFind();
        app.update();
    }
    Searching(const Searching &) = delete;
    Searching &operator=(const Searching &) = delete;
};
std::wstring quotedQuery(const std::string &query) {
    auto q = wide(query, CP_UTF8, false);
    if (q.size() > 60)
        q = q.substr(0, 60) + L"…";
    return L"“" + q + L"”";
}
std::wstring countText(size_t n) {
    auto s = std::to_wstring(n);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3)
        s.insert(static_cast<size_t>(i), 1, L',');
    return s;
}
} // namespace
void App::createResultsPanel() {
    resultsLabel = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_LEFTNOWORDWRAP | SS_CENTERIMAGE | SS_NOPREFIX,
                                   0, 0, 0, 0, window, reinterpret_cast<HMENU>(13), instance, nullptr);
    resultsList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                  WS_CHILD | WS_TABSTOP | LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL |
                                      LVS_SHOWSELALWAYS,
                                  0, 0, 0, 0, window, reinterpret_cast<HMENU>(14), instance, nullptr);
    if (!resultsLabel || !resultsList)
        fail(L"검색 결과 창을 만들 수 없습니다.");
    for (auto h : {resultsLabel, resultsList})
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    ListView_SetExtendedListViewStyle(resultsList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    struct Column {
        const wchar_t *title;
        int width, format;
    };
    int index = 0;
    for (auto c : {Column{L"파일 / 탭", 240, LVCFMT_LEFT}, Column{L"줄", 70, LVCFMT_RIGHT},
                   Column{L"내용", 800, LVCFMT_LEFT}}) {
        LVCOLUMNW header{};
        header.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        header.pszText = const_cast<wchar_t *>(c.title);
        header.cx = MulDiv(c.width, dpi, 96);
        header.fmt = c.format;
        ListView_InsertColumn(resultsList, index++, &header);
    }
}
void App::showResults(bool visible) {
    resultsVisible = visible && resultsList;
    if (resultsList) {
        ShowWindow(resultsLabel, resultsVisible ? SW_SHOW : SW_HIDE);
        ShowWindow(resultsList, resultsVisible ? SW_SHOW : SW_HIDE);
    }
    layout();
    update();
}
void App::setResults(std::vector<SearchHit> found, std::wstring title) {
    hits = std::move(found);
    hitIndex = -1;
    resultsTitle = std::move(title);
    if (!resultsList)
        return;
    auto label = L"  " + resultsTitle + L"     더블클릭·Enter: 이동 · F4 / Shift+F4: 다음 / 이전";
    SetWindowTextW(resultsLabel, label.c_str());
    ListView_SetItemState(resultsList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemCountEx(resultsList, static_cast<int>(hits.size()), 0);
    if (!hits.empty())
        ListView_EnsureVisible(resultsList, 0, FALSE);
    InvalidateRect(resultsList, nullptr, TRUE);
    showResults(true);
}
size_t App::findAll() {
    auto d = current();
    if (!d || search.query.empty())
        return 0;
    if (!d->editor)
        fail(L"모두 찾기는 텍스트 탭에서 사용할 수 있습니다.");
    std::vector<Document *> targets;
    if (search.allTabs) {
        for (auto &p : docs)
            if (p->editor)
                targets.push_back(p.get());
    } else
        targets.push_back(d);
    std::vector<SearchHit> found;
    size_t count = 0;
    bool complete = true;
    {
        Searching scope(*this);
        for (auto *target : targets) {
            auto h = target->editor;
            const auto revision = target->revision;
            {
                SearchScope borrowed(*this, h);
                complete = eachMatch(*this, h, search, hitLimit, count, [&](Pos a, Pos b) {
                    Pos line = sci(h, SCI_LINEFROMPOSITION, a);
                    found.push_back({target->id, target->path, wide(target->name), line,
                                     a - sci(h, SCI_POSITIONFROMLINE, line), b - a, hitLine(h, line)});
                });
            }
            if (target->revision != revision)
                fail(L"검색 중 문서가 변경되었습니다. 다시 검색해 주세요.");
            if (!complete || cancel)
                break;
        }
    }
    auto title = quotedQuery(search.query) + L" 검색 결과 " + countText(count) + L"개";
    if (targets.size() > 1)
        title += L" · 열린 텍스트 탭 " + countText(targets.size()) + L"개";
    if (!complete)
        title += L" · 처음 " + countText(hitLimit) + L"개만 표시";
    if (cancel)
        title += L" · 중지됨";
    searchMessage = std::to_string(count) + "개 찾았습니다. 아래 검색 결과 창에서 이동하세요.";
    setResults(std::move(found), title);
    updateFind();
    return count;
}
size_t App::markAll() {
    auto d = current();
    if (!d || !d->editor)
        fail(L"모두 표시는 텍스트 탭에서 사용할 수 있습니다.");
    if (search.query.empty())
        return 0;
    auto h = d->editor;
    clearMarks();
    size_t count = 0;
    {
        Searching scope(*this);
        SearchScope borrowed(*this, h);
        sci(h, SCI_SETINDICATORCURRENT, markIndicator);
        eachMatch(*this, h, search, SIZE_MAX, count, [&](Pos a, Pos b) {
            if (b > a) {
                sci(h, SCI_SETINDICATORCURRENT, markIndicator);
                sci(h, SCI_INDICATORFILLRANGE, a, b - a);
            }
        });
    }
    searchMessage = std::to_string(count) + "개 표시했습니다. 검색 → 표시 지우기로 없앱니다.";
    updateFind();
    return count;
}
size_t App::bookmarkMatches() {
    auto d = current();
    if (!d || !d->editor)
        fail(L"찾은 줄 북마크는 텍스트 탭에서 사용할 수 있습니다.");
    if (search.query.empty())
        return 0;
    auto h = d->editor;
    size_t count = 0, lines = 0;
    Pos previous = -1;
    {
        Searching scope(*this);
        SearchScope borrowed(*this, h);
        eachMatch(*this, h, search, SIZE_MAX, count, [&](Pos a, Pos) {
            Pos line = sci(h, SCI_LINEFROMPOSITION, a);
            if (line != previous) {
                previous = line;
                lines++;
                if (!(sci(h, SCI_MARKERGET, line) & (1 << bookmarkMarker)))
                    sci(h, SCI_MARKERADD, line, bookmarkMarker);
            }
        });
    }
    searchMessage = std::to_string(lines) + "줄에 북마크를 표시했습니다.";
    updateFind();
    return lines;
}
void App::clearMarks() {
    auto d = current();
    if (!d || !d->editor)
        return;
    sci(d->editor, SCI_SETINDICATORCURRENT, markIndicator);
    sci(d->editor, SCI_INDICATORCLEARRANGE, 0, sci(d->editor, SCI_GETLENGTH));
}
void App::findInFiles() {
    if (busy || playing)
        return;
    fs::path folder = findFolder;
    if (folder.empty())
        if (auto d = current(); d && !d->path.empty())
            folder = d->path.parent_path();
    auto fields =
        inputDialog(window, L"파일에서 찾기",
                    {{L"찾을 내용", wide(search.query, CP_UTF8, false)},
                     {L"폴더", folder.wstring()},
                     {L"파일 형식 (; 로 구분)", findFilter},
                     {L"하위 폴더 포함", L"", true, findSubfolders},
                     {L"대소문자 구분", L"", true, (search.flags & SCFIND_MATCHCASE) != 0},
                     {L"단어 단위", L"", true, (search.flags & SCFIND_WHOLEWORD) != 0},
                     {L"정규식", L"", true, (search.flags & SCFIND_REGEXP) != 0}},
                    L"디스크에 저장된 내용을 검색합니다. 바이너리 파일, 256 MiB 초과 파일과 .git·.svn·.hg 폴더는 "
                    L"건너뜁니다. 검색 중 Esc 또는 찾기 창의 중지로 멈춥니다.");
    if (!fields)
        return;
    auto &f = *fields;
    SearchOptions options;
    options.query = narrow(f[0].value);
    if (options.query.empty())
        fail(L"찾을 내용을 입력하세요.");
    auto text = f[1].value;
    while (!text.empty() && (text.back() == L' ' || text.back() == L'"'))
        text.pop_back();
    while (!text.empty() && (text.front() == L' ' || text.front() == L'"'))
        text.erase(0, 1);
    fs::path directory = text;
    if (text.empty() || !disk::is_directory(directory))
        fail(L"검색할 폴더를 찾을 수 없습니다: " + text);
    options.flags = (f[4].checked ? SCFIND_MATCHCASE : 0) | (f[5].checked ? SCFIND_WHOLEWORD : 0) |
                    (f[6].checked ? SCFIND_REGEXP | SCFIND_CXX11REGEX : 0);
    findFolder = directory;
    findFilter = f[2].value.empty() ? L"*.*" : f[2].value;
    findSubfolders = f[3].checked;
    search.query = options.query;
    search.flags = options.flags;
    if (findWindow) {
        SetDlgItemTextW(findWindow, 100, f[0].value.c_str());
        for (auto [id, flag] : {std::pair{102, SCFIND_MATCHCASE}, std::pair{103, SCFIND_WHOLEWORD},
                                std::pair{104, SCFIND_REGEXP}})
            SendDlgItemMessageW(findWindow, id, BM_SETCHECK, options.flags & flag ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    findInFolder(directory, findFilter, findSubfolders, options);
}
size_t App::findInFolder(const fs::path &folder, const std::wstring &filter, bool subfolders,
                         const SearchOptions &options) {
    std::vector<std::wstring> patterns;
    std::wstring part;
    for (auto c : filter + L";") {
        if (c == L';' || c == L',' || c == L' ') {
            if (!part.empty())
                patterns.push_back(part);
            part.clear();
        } else
            part += c;
    }
    if (patterns.empty())
        patterns.push_back(L"*");
    auto wanted = [&](const fs::path &p) {
        auto name = p.filename().wstring();
        return std::any_of(patterns.begin(), patterns.end(),
                           [&](const std::wstring &pattern) { return PathMatchSpecW(name.c_str(), pattern.c_str()); });
    };
    if (!searchBuffer) {
        searchBuffer = CreateWindowExW(0, L"Scintilla", nullptr, WS_CHILD, 0, 0, 1, 1, window, nullptr, instance,
                                       nullptr);
        if (!searchBuffer)
            fail(L"검색 버퍼를 만들 수 없습니다.");
        auto doc = sci(searchBuffer, SCI_CREATEDOCUMENT, 0, SC_DOCUMENTOPTION_TEXT_LARGE);
        sci(searchBuffer, SCI_SETDOCPOINTER, 0, doc);
        sci(searchBuffer, SCI_RELEASEDOCUMENT, 0, doc);
        sci(searchBuffer, SCI_SETCODEPAGE, SC_CP_UTF8);
        sci(searchBuffer, SCI_SETUNDOCOLLECTION, 0);
        sci(searchBuffer, SCI_SETMODEVENTMASK, 0);
    }
    std::vector<SearchHit> found;
    size_t count = 0, searched = 0, skipped = 0, matchedFiles = 0;
    bool complete = true;
    {
        Searching scope(*this);
        auto stopRequested = [&] {
            if (GetForegroundWindow() == window && (GetAsyncKeyState(VK_ESCAPE) & 0x8000))
                cancel = true;
            return cancel.load();
        };
        auto progress = [&](const std::wstring &text) {
            SendMessageW(status, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(text.c_str()));
            pump();
        };
        const auto root = winPath(folder);
        std::vector<fs::path> files;
        std::error_code error;
        auto consider = [&](const fs::directory_entry &entry) {
            std::error_code e;
            if (entry.is_regular_file(e) && wanted(entry.path()))
                files.push_back(entry.path().lexically_relative(root));
        };
        if (subfolders) {
            fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, error), end;
            for (; !error && it != end; it.increment(error)) {
                std::error_code e;
                if (it->is_directory(e)) {
                    auto name = it->path().filename().wstring();
                    if (!_wcsicmp(name.c_str(), L".git") || !_wcsicmp(name.c_str(), L".svn") ||
                        !_wcsicmp(name.c_str(), L".hg"))
                        it.disable_recursion_pending();
                    continue;
                }
                consider(*it);
                if (files.size() % 500 == 0) {
                    progress(L"파일 목록 만드는 중: " + countText(files.size()) + L"개 (Esc: 중지)");
                    if (stopRequested())
                        break;
                }
            }
        } else {
            fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, error), end;
            for (; !error && it != end; it.increment(error))
                consider(*it);
        }
        if (error && files.empty())
            fail(L"폴더를 읽을 수 없습니다: " + folder.wstring());
        std::sort(files.begin(), files.end());
        for (size_t i = 0; i < files.size() && complete; i++) {
            if (i % 16 == 0) {
                progress(L"파일에서 찾기: " + countText(i) + L" / " + countText(files.size()) + L" (Esc: 중지)");
                if (stopRequested())
                    break;
            }
            const fs::path real = folder / files[i];
            try {
                std::error_code e;
                if (fs::file_size(winPath(real), e) > 256ull * 1024 * 1024 || e) {
                    skipped++;
                    continue;
                }
                auto detected = detect(real);
                if (detected.binary) {
                    skipped++;
                    continue;
                }
                sci(searchBuffer, SCI_CLEARALL);
                readText(real, detected.encoding, detected.skip, [&](std::string_view s) {
                    sci(searchBuffer, SCI_APPENDTEXT, s.size(), reinterpret_cast<LPARAM>(s.data()));
                });
                searched++;
                const auto before = count;
                complete = eachMatch(*this, searchBuffer, options, hitLimit, count, [&](Pos a, Pos b) {
                    Pos line = sci(searchBuffer, SCI_LINEFROMPOSITION, a);
                    found.push_back({"", real, files[i].wstring(), line,
                                     a - sci(searchBuffer, SCI_POSITIONFROMLINE, line), b - a,
                                     hitLine(searchBuffer, line)});
                });
                if (count > before)
                    matchedFiles++;
            } catch (const std::exception &) {
                skipped++; // Unreadable or undecodable; reported in the summary.
            }
            if (cancel)
                break;
        }
        sci(searchBuffer, SCI_CLEARALL);
    }
    auto title = quotedQuery(options.query) + L" 파일에서 찾기 " + countText(count) + L"개 · 파일 " + countText(matchedFiles) +
                 L"/" + countText(searched) + L"개 · " + folder.wstring();
    if (skipped)
        title += L" · 건너뜀 " + countText(skipped) + L"개";
    if (!complete)
        title += L" · 처음 " + countText(hitLimit) + L"개만 표시";
    if (cancel)
        title += L" · 중지됨";
    setResults(std::move(found), title);
    return count;
}
void App::goToHit(int index) {
    if (index < 0 || index >= static_cast<int>(hits.size()))
        return;
    hitIndex = index;
    const auto hit = hits[static_cast<size_t>(index)];
    int target = -1;
    for (size_t i = 0; i < docs.size() && target < 0; i++)
        if (!hit.docId.empty() && docs[i]->id == hit.docId)
            target = static_cast<int>(i);
    for (size_t i = 0; i < docs.size() && target < 0; i++)
        if (!hit.path.empty() && !docs[i]->path.empty() && !_wcsicmp(docs[i]->path.c_str(), hit.path.c_str()))
            target = static_cast<int>(i);
    if (target < 0 && !hit.path.empty()) {
        open(hit.path);
        addRecent(hit.path);
        target = active;
    }
    if (target < 0)
        fail(L"검색한 탭이 닫혀 있어 이동할 수 없습니다.");
    select(target);
    auto d = docs[static_cast<size_t>(target)];
    if (!d->editor)
        fail(L"Hex 탭으로 열려 있어 검색 위치로 이동할 수 없습니다.");
    if (d->viewMode == 1)
        setView(0);
    auto h = d->editor;
    Pos line = std::clamp<Pos>(hit.line, 0, sci(h, SCI_GETLINECOUNT) - 1);
    Pos start = sci(h, SCI_POSITIONFROMLINE, line), lineEnd = sci(h, SCI_GETLINEENDPOSITION, line);
    Pos a = std::min(start + hit.column, lineEnd), b = std::min(a + hit.length, sci(h, SCI_GETLENGTH));
    bool suppressed = macroSuppressed;
    macroSuppressed = true;
    sci(h, SCI_ENSUREVISIBLEENFORCEPOLICY, line);
    sci(h, SCI_SETSEL, a, b);
    macroSuppressed = suppressed;
    if (resultsList) {
        ListView_SetItemState(resultsList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_SetItemState(resultsList, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(resultsList, index, FALSE);
    }
    SetFocus(h);
}
void App::navigateHits(bool backwards) {
    if (hits.empty())
        return;
    if (!resultsVisible)
        showResults(true);
    int n = static_cast<int>(hits.size());
    int next = hitIndex < 0 ? (backwards ? n - 1 : 0) : (hitIndex + (backwards ? n - 1 : 1)) % n;
    goToHit(next);
}
} // namespace lp
