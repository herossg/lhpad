#include "app.h"
#include <windowsx.h>
namespace lp {
App *app = nullptr;
static const std::vector<std::pair<int, std::wstring>> toolbarItems = {
    {New, L"새 문서 (Ctrl+N)"},
    {Open, L"열기 (Ctrl+O)"},
    {Save, L"저장 (Ctrl+S)"},
    {SaveAll, L"모두 저장 (Ctrl+Shift+S)"},
    {0, L""},
    {Undo, L"실행 취소 (Ctrl+Z)"},
    {Redo, L"다시 실행 (Ctrl+Y)"},
    {Find, L"찾기 (Ctrl+F)"},
    {Replace, L"바꾸기 (Ctrl+H)"},
    {FindInFiles, L"파일에서 찾기 (Ctrl+Shift+F)"},
    {BookmarkToggle, L"북마크 표시 / 해제 (Ctrl+F2) · 이동 F2 / Shift+F2"},
    {0, L""},
    {Source, L"원문 보기"},
    {Preview, L"미리보기 (Ctrl+Shift+V)"},
    {Split, L"미리보기 좌우 분할 (Ctrl+Alt+V)"},
    {SplitEditor, L"같은 문서 위아래 분할"},
    {OutlinePanel, L"함수 목록"},
    {0, L""},
    {Format, L"문서 정렬 (Ctrl+Alt+F)"},
    {Compare, L"두 탭 비교 (Ctrl+Alt+C)"},
    {0, L""},
    {Record, L"매크로 녹화 (Ctrl+Shift+R)"},
    {MacroStop, L"매크로 중지 (Esc)"},
    {Play, L"매크로 실행 (Ctrl+Shift+P)"},
    {Repeat, L"매크로 반복 실행"},
    {0, L""},
    {Monitor, L"로그 모니터링"},
    {FollowTail, L"끝 따라가기"},
    {Help, L"단축키 도움말"}};
Document::~Document() {
    if (secondary)
        DestroyWindow(secondary);
    if (controller)
        controller->Close();
    if (previewHost)
        DestroyWindow(previewHost);
    if (editor)
        DestroyWindow(editor);
    if (hexWindow)
        DestroyWindow(hexWindow);
}
App::~App() {
    docs.clear();
    if (findWindow)
        DestroyWindow(findWindow);
    if (toolbarImages)
        ImageList_Destroy(toolbarImages);
    if (tabImages)
        ImageList_Destroy(tabImages);
    if (font)
        DeleteObject(font);
    if (mono)
        DeleteObject(mono);
    if (menu)
        DestroyMenu(menu);
    if (gdiplusToken)
        Gdiplus::GdiplusShutdown(gdiplusToken);
}
void App::guard(const std::function<void()> &f) {
    try {
        f();
    } catch (const std::exception &e) {
        message(wide(e.what()), L"LHPad", MB_OK | MB_ICONWARNING);
    }
    update();
}
void App::buildMenu() {
    menu = CreateMenu();
    auto root = [&](const wchar_t *name) {
        auto m = CreatePopupMenu();
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(m), name);
        return m;
    };
    auto item = [](HMENU m, int id, const wchar_t *label) { AppendMenuW(m, MF_STRING, id, label); };
    auto sep = [](HMENU m) { AppendMenuW(m, MF_SEPARATOR, 0, nullptr); };
    auto f = root(L"파일(&F)");
    item(f, New, L"새 문서\tCtrl+N");
    item(f, Open, L"열기…\tCtrl+O");
    item(f, Save, L"저장\tCtrl+S");
    item(f, SaveAs, L"다른 이름으로 저장…\tCtrl+Alt+S");
    item(f, SaveAll, L"모두 저장\tCtrl+Shift+S");
    sep(f);
    recentMenu = CreatePopupMenu();
    AppendMenuW(f, MF_POPUP, reinterpret_cast<UINT_PTR>(recentMenu), L"최근 파일(&R)");
    rebuildRecentMenu();
    item(f, ReopenClosed, L"닫은 탭 다시 열기\tCtrl+Shift+T");
    sep(f);
    item(f, Close, L"현재 탭 닫기\tCtrl+W");
    item(f, CloseOthers, L"다른 탭 모두 닫기");
    item(f, CloseRight, L"오른쪽 탭 모두 닫기");
    item(f, CloseAll, L"모든 탭 닫기");
    item(f, CopyPath, L"파일 경로 복사");
    sep(f);
    item(f, Exit, L"종료\tAlt+F4");
    auto e = root(L"편집(&E)");
    item(e, Undo, L"실행 취소\tCtrl+Z");
    item(e, Redo, L"다시 실행\tCtrl+Y");
    sep(e);
    item(e, Cut, L"잘라내기\tCtrl+X");
    item(e, Copy, L"복사\tCtrl+C");
    item(e, Paste, L"붙여넣기\tCtrl+V");
    item(e, SelectAll, L"모두 선택\tCtrl+A");
    sep(e);
    item(e, Duplicate, L"선택 / 현재 줄 복제\tCtrl+D");
    item(e, DeleteLine, L"현재 줄 삭제\tCtrl+L");
    item(e, LineUp, L"줄 위로 이동\tCtrl+Shift+↑");
    item(e, LineDown, L"줄 아래로 이동\tCtrl+Shift+↓");
    item(e, SortAsc, L"선택 줄 오름차순 정렬");
    item(e, SortDesc, L"선택 줄 내림차순 정렬");
    auto caseMenu = CreatePopupMenu();
    AppendMenuW(e, MF_POPUP, reinterpret_cast<UINT_PTR>(caseMenu), L"대소문자 변환");
    item(caseMenu, UpperCase, L"대문자로\tCtrl+Shift+U");
    item(caseMenu, LowerCase, L"소문자로\tCtrl+U");
    item(caseMenu, TitleCase, L"단어 첫 글자만 대문자로");
    item(e, ToggleComment, L"주석 넣기 / 빼기\tCtrl+Q");
    item(e, BraceJump, L"짝 괄호로 이동\tCtrl+B");
    sep(e);
    item(e, AutoIndent, L"자동 들여쓰기");
    auto s = root(L"검색(&S)");
    item(s, Find, L"찾기…\tCtrl+F");
    item(s, Replace, L"바꾸기…\tCtrl+H");
    item(s, FindNext, L"다음 찾기\tF3");
    item(s, FindPrev, L"이전 찾기\tShift+F3");
    item(s, GoTo, L"줄 / 오프셋 이동…\tCtrl+G");
    sep(s);
    item(s, FindAll, L"모두 찾기 (검색 결과 창)");
    item(s, FindInFiles, L"파일에서 찾기…\tCtrl+Shift+F");
    item(s, MarkAll, L"찾은 항목 모두 표시");
    item(s, ClearMarks, L"표시 지우기");
    sep(s);
    item(s, ResultsPanel, L"검색 결과 창");
    item(s, ResultNext, L"다음 검색 결과\tF4");
    item(s, ResultPrev, L"이전 검색 결과\tShift+F4");
    auto bookmarkMenu = CreatePopupMenu();
    AppendMenuW(s, MF_POPUP, reinterpret_cast<UINT_PTR>(bookmarkMenu), L"북마크(&B)");
    item(bookmarkMenu, BookmarkToggle, L"북마크 표시 / 해제\tCtrl+F2");
    item(bookmarkMenu, BookmarkNext, L"다음 북마크\tF2");
    item(bookmarkMenu, BookmarkPrev, L"이전 북마크\tShift+F2");
    item(bookmarkMenu, BookmarkClear, L"모든 북마크 지우기");
    sep(bookmarkMenu);
    item(bookmarkMenu, BookmarkMatches, L"찾은 줄 모두 북마크");
    item(bookmarkMenu, BookmarkCopy, L"북마크한 줄 복사");
    item(bookmarkMenu, BookmarkDelete, L"북마크한 줄 삭제");
    viewMenu = root(L"보기(&V)");
    item(viewMenu, Source, L"원문 보기");
    item(viewMenu, Preview, L"미리보기 전환\tCtrl+Shift+V");
    item(viewMenu, Split, L"좌우 분할 보기\tCtrl+Alt+V");
    item(viewMenu, Refresh, L"미리보기 새로고침\tF5");
    item(viewMenu, Scripts, L"HTML 스크립트 실행");
    sep(viewMenu);
    item(viewMenu, WhiteSpace, L"공백·탭 표시");
    item(viewMenu, EolVisible, L"줄바꿈 문자 표시");
    item(viewMenu, ControlVisible, L"제어 문자 표시");
    item(viewMenu, AllSymbols, L"모든 기호 표시");
    item(viewMenu, Wrap, L"자동 줄바꿈");
    item(viewMenu, ZoomIn, L"확대\tCtrl++");
    item(viewMenu, ZoomOut, L"축소\tCtrl+-");
    item(viewMenu, ZoomReset, L"기본 배율\tCtrl+0");
    item(viewMenu, EditorFont, L"텍스트 편집기 글꼴…");
    item(viewMenu, SplitEditor, L"같은 문서 위아래 분할");
    item(viewMenu, OutlinePanel, L"함수 목록");
    auto foldMenu = CreatePopupMenu();
    AppendMenuW(viewMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(foldMenu), L"코드 접기");
    item(foldMenu, FoldCurrent, L"현재 구간 접기 / 펼치기\tCtrl+Alt+B");
    item(foldMenu, FoldAll, L"모두 접기\tAlt+0");
    item(foldMenu, UnfoldAll, L"모두 펼치기\tAlt+Shift+0");
    auto collapse = CreatePopupMenu(), expand = CreatePopupMenu();
    AppendMenuW(foldMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(collapse), L"단계별 접기");
    AppendMenuW(foldMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(expand), L"단계별 펼치기");
    for (int i = 1; i <= 8; i++) {
        auto label = std::to_wstring(i) + L"단계";
        item(collapse, FoldLevel + i, label.c_str());
        item(expand, UnfoldLevel + i, label.c_str());
    }
    sep(viewMenu);
    item(viewMenu, AsHex, L"Hex로 다시 열기");
    item(viewMenu, AsText, L"텍스트로 다시 열기");
    item(viewMenu, ExternalCheck, L"외부 파일 변경 확인");
    item(viewMenu, Monitor, L"로그 모니터링");
    item(viewMenu, FollowTail, L"끝 따라가기");
    auto multi = root(L"다중 편집(&U)");
    item(multi, Help, L"세로 선택: Alt + 마우스 드래그");
    item(multi, AddNext, L"같은 단어 다음 항목 추가\tCtrl+Alt+↓");
    item(multi, AddAll, L"같은 단어 모두 선택\tCtrl+Shift+L");
    item(multi, Column, L"열 편집기…\tAlt+C");
    auto enc = root(L"인코딩(&C)"), reopen = CreatePopupMenu(), convert = CreatePopupMenu();
    AppendMenuW(enc, MF_POPUP, reinterpret_cast<UINT_PTR>(reopen), L"이 인코딩으로 다시 열기");
    AppendMenuW(enc, MF_POPUP, reinterpret_cast<UINT_PTR>(convert), L"저장 인코딩 변환");
    for (size_t i = 0; i < encodings().size(); i++) {
        auto label = wide(encodings()[i].name);
        item(reopen, EncodingReopen + static_cast<int>(i), label.c_str());
        item(convert, EncodingConvert + static_cast<int>(i), label.c_str());
    }
    auto endings = CreatePopupMenu();
    AppendMenuW(enc, MF_POPUP, reinterpret_cast<UINT_PTR>(endings), L"줄바꿈 변환");
    item(endings, 600, L"Windows CRLF");
    item(endings, 601, L"Unix LF");
    item(endings, 602, L"Mac CR");
    languageMenu = root(L"언어(&L)");
    std::vector<size_t> order;
    for (size_t i = 0; i < languages.items.size(); i++)
        order.push_back(i);
    std::sort(order.begin(), order.end(),
              [&](size_t a, size_t b) { return languages.items[a].display < languages.items[b].display; });
    std::map<wchar_t, HMENU> groups;
    for (auto i : order) {
        auto label = wide(languages.items[i].display);
        wchar_t key = towupper(label[0]);
        if (!groups.contains(key)) {
            auto sub = CreatePopupMenu();
            wchar_t text[2] = {key, 0};
            AppendMenuW(languageMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(sub), text);
            groups[key] = sub;
        }
        item(groups[key], LanguageSelect + static_cast<int>(i), label.c_str());
    }
    macroMenu = root(L"매크로(&M)");
    item(macroMenu, Record, L"녹화 시작 / 중지\tCtrl+Shift+R");
    item(macroMenu, MacroStop, L"녹화 / 실행 중지\tEsc");
    item(macroMenu, Play, L"현재 녹화 실행\tCtrl+Shift+P");
    item(macroMenu, Repeat, L"여러 번 실행…\tCtrl+Alt+Shift+P");
    item(macroMenu, MacroSave, L"현재 녹화 저장…");
    item(macroMenu, MacroManage, L"저장된 매크로 관리…");
    auto t = root(L"도구(&T)");
    item(t, Format, L"문서 / 선택 영역 정렬\tCtrl+Alt+F");
    item(t, CompareSelect, L"이 탭을 비교 대상으로 선택");
    item(t, Compare, L"선택한 두 탭 줄 비교\tCtrl+Alt+C");
    item(t, CompareNext, L"다음 차이\tF7");
    item(t, ComparePrev, L"이전 차이\tShift+F7");
    item(t, Hash, L"Hex SHA-256…");
    sep(t);
    item(t, RegisterShell, L"탐색기에 ‘LHPad로 열기’ 등록");
    item(t, UnregisterShell, L"탐색기 ‘LHPad로 열기’ 제거");
    item(t, RecoveryFolder, L"복원 파일 폴더 열기");
    item(t, Settings, L"설정…");
    item(t, About, L"LHPad 정보");
    SetMenu(window, menu);
}
static HBITMAP pngBitmap(const fs::path &path, int size) {
    Gdiplus::Bitmap source(winPath(path).c_str());
    Gdiplus::Bitmap scaled(size, size, PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(&scaled);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.DrawImage(&source, 0, 0, size, size);
    HBITMAP bitmap = nullptr;
    scaled.GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &bitmap);
    return bitmap;
}
static HBITMAP buttonBitmap(int id, int size) {
    // data/toolbar/<Name>-<px>.png are rasterised from tools/toolbar-icons/svg for each DPI size, so
    // the closest size is used directly instead of scaling one large bitmap down (which blurs).
    static const std::map<int, std::wstring> artwork = {
        {New, L"NewFile"},       {Open, L"Open"},           {Save, L"Save"},
        {SaveAll, L"SaveAll"},   {Undo, L"Undo"},           {Redo, L"Redo"},
        {Find, L"Find"},         {Replace, L"Replace"},     {FindInFiles, L"FindInFiles"},
        {BookmarkToggle, L"Bookmark"}, {Source, L"Source"}, {Preview, L"Preview"},
        {Split, L"Split"},       {SplitEditor, L"SplitEditor"}, {OutlinePanel, L"Outline"},
        {Format, L"Format"},     {Compare, L"Compare"},     {Record, L"Record"},
        {-Record, L"RecordActive"}, {MacroStop, L"Stop"},   {Play, L"Play"},
        {Repeat, L"Repeat"},     {Monitor, L"Monitor"},     {FollowTail, L"FollowTail"},
        {Help, L"Help"}};
    if (auto it = artwork.find(id); it != artwork.end()) {
        auto folder = exeDir() / L"data" / L"toolbar";
        fs::path best;
        int bestSize = 0;
        for (int candidate : {16, 20, 24, 32, 40, 48}) {
            auto path = folder / (it->second + L"-" + std::to_wstring(candidate) + L".png");
            if (!disk::exists(path))
                continue;
            // Smallest size that is at least as large as needed; otherwise the largest available.
            if (!bestSize || (bestSize < size && candidate > bestSize) || (candidate >= size && candidate < bestSize)) {
                best = path;
                bestSize = candidate;
            }
        }
        if (bestSize)
            return pngBitmap(best, size);
    }
    Gdiplus::Bitmap bitmap(size, size, PixelFormat32bppARGB);
    Gdiplus::Graphics g(&bitmap);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.Clear(Gdiplus::Color(0, 0, 0, 0));
    Gdiplus::SolidBrush pastel(
        Gdiplus::Color(id == Record ? 169 : 190, id == Record ? 211 : 228, id == Record ? 245 : 223)),
        ink(Gdiplus::Color(255, 58, 90, 91));
    if (id == Undo || id == Redo) {
        // Draw a real curved arrow at every DPI instead of a font glyph on a tile.
        g.ScaleTransform(size / 24.0f, size / 24.0f);
        if (id == Redo) {
            g.TranslateTransform(24.0f, 0.0f);
            g.ScaleTransform(-1.0f, 1.0f);
        }
        Gdiplus::GraphicsPath arrow;
        arrow.AddLine(9.0f, 4.0f, 2.5f, 9.5f);
        arrow.AddLine(2.5f, 9.5f, 9.0f, 15.0f);
        arrow.AddLine(9.0f, 15.0f, 9.0f, 11.5f);
        arrow.AddLine(9.0f, 11.5f, 13.5f, 11.5f);
        arrow.AddBezier(13.5f, 11.5f, 17.0f, 11.5f, 19.5f, 14.0f, 19.5f, 17.5f);
        arrow.AddBezier(19.5f, 17.5f, 19.5f, 18.2f, 19.3f, 19.0f, 19.0f, 19.5f);
        arrow.AddBezier(19.0f, 19.5f, 21.2f, 18.2f, 22.0f, 16.4f, 22.0f, 14.5f);
        arrow.AddBezier(22.0f, 14.5f, 22.0f, 9.9f, 18.2f, 7.0f, 13.5f, 7.0f);
        arrow.AddLine(13.5f, 7.0f, 9.0f, 7.0f);
        arrow.AddLine(9.0f, 7.0f, 9.0f, 4.0f);
        arrow.CloseFigure();
        Gdiplus::SolidBrush fill(id == Undo ? Gdiplus::Color(255, 168, 215, 197)
                                           : Gdiplus::Color(255, 174, 204, 234));
        Gdiplus::Pen outline(Gdiplus::Color(255, 75, 103, 118), 1.1f);
        outline.SetLineJoin(Gdiplus::LineJoinRound);
        g.FillPath(&fill, &arrow);
        g.DrawPath(&outline, &arrow);
    } else if (id == Record || id == -Record) {
        Gdiplus::SolidBrush dot(id == Record ? Gdiplus::Color(255, 169, 211, 245)
                                             : Gdiplus::Color(255, 255, 179, 179));
        g.FillEllipse(&dot, 2.0f, 2.0f, size - 4.0f, size - 4.0f);
    } else {
        g.FillRectangle(&pastel, 2.0f, 3.0f, size - 4.0f, size - 6.0f);
        std::wstring glyph;
        switch (id) {
        case New:
            glyph = L"\uE8A5";
            break;
        case Open:
            glyph = L"\uE8B7";
            break;
        case Save:
        case SaveAll:
            glyph = L"\uE74E";
            break;
        case Undo:
            glyph = L"\uE7A7";
            break;
        case Redo:
            glyph = L"\uE7A6";
            break;
        case Find:
            glyph = L"\uE721";
            break;
        case Replace:
            glyph = L"\uE8AC";
            break;
        case Source:
            glyph = L"\uE943";
            break;
        case Preview:
            glyph = L"\uE890";
            break;
        case Split:
            glyph = L"\uE8A0";
            break;
        case Format:
            glyph = L"\uE8D2";
            break;
        case Compare:
            glyph = L"\uE8EF";
            break;
        case MacroStop:
            glyph = L"\uE71A";
            break;
        case Play:
            glyph = L"\uE768";
            break;
        case Repeat:
            glyph = L"\uE8EE";
            break;
        case Monitor:
            glyph = L"\uE9D9";
            break;
        case FollowTail:
            glyph = L"\uE74B";
            break;
        default:
            glyph = L"\uE897";
        }
        Gdiplus::Font font(L"Segoe MDL2 Assets", static_cast<float>(size) * .65f, Gdiplus::FontStyleRegular,
                           Gdiplus::UnitPixel);
        Gdiplus::StringFormat fmt;
        fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
        fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF r(0, 0, static_cast<float>(size), static_cast<float>(size));
        g.DrawString(glyph.c_str(), -1, &font, r, &fmt, &ink);
    }
    HBITMAP out = nullptr;
    bitmap.GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &out);
    return out;
}
void App::buildToolbar() {
    int size = MulDiv(16, dpi, 96);
    toolbarImages = ImageList_Create(size, size, ILC_COLOR32 | ILC_MASK, 24, 4);
    SendMessageW(toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(toolbar, TB_SETBITMAPSIZE, 0, MAKELONG(size, size));
    SendMessageW(toolbar, TB_SETPADDING, 0, MAKELONG(MulDiv(6, dpi, 96), MulDiv(4, dpi, 96)));
    SendMessageW(toolbar, TB_SETBUTTONSIZE, 0, MAKELONG(MulDiv(22, dpi, 96), MulDiv(22, dpi, 96)));
    int index = 0;
    for (auto &[id, label] : toolbarItems) {
        TBBUTTON b{};
        if (!id) {
            b.fsStyle = BTNS_SEP;
            b.iBitmap = MulDiv(8, dpi, 96);
        } else {
            auto bitmap = buttonBitmap(id, size);
            b.iBitmap = ImageList_Add(toolbarImages, bitmap, nullptr);
            DeleteObject(bitmap);
            b.idCommand = id;
            b.fsState = TBSTATE_ENABLED;
            b.fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
            b.iString = -1;
            index++;
        }
        SendMessageW(toolbar, TB_ADDBUTTONS, 1, reinterpret_cast<LPARAM>(&b));
    }
    auto record = buttonBitmap(-Record, size);
    ImageList_Add(toolbarImages, record, nullptr);
    DeleteObject(record);
    SendMessageW(toolbar, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(toolbarImages));
    SendMessageW(toolbar, TB_AUTOSIZE, 0, 0);
    tabImages = ImageList_Create(MulDiv(16, dpi, 96), MulDiv(16, dpi, 96), ILC_COLOR32 | ILC_MASK, 2, 0);
    for (auto file : {L"tab-saved.png", L"tab-unsaved.png"}) {
        auto bitmap = pngBitmap(exeDir() / L"data" / file, MulDiv(16, dpi, 96));
        ImageList_Add(tabImages, bitmap, nullptr);
        DeleteObject(bitmap);
    }
    TabCtrl_SetImageList(tabs, tabImages);
    TabCtrl_SetPadding(tabs, MulDiv(12, dpi, 96), MulDiv(2, dpi, 96));
}
void App::create(int show) {
    app = this;
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&gdiplusToken, &input, nullptr);
    WNDCLASSEXW cls{sizeof(cls)};
    cls.hInstance = instance;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    cls.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    cls.hIconSm = cls.hIcon;
    cls.lpszClassName = L"LHPad.Native";
    cls.lpfnWndProc = windowProc;
    RegisterClassExW(&cls);
    cls.lpszClassName = L"LHPad.Hex";
    cls.lpfnWndProc = hexProc;
    cls.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    RegisterClassExW(&cls);
    cls.lpszClassName = L"LHPad.Find";
    cls.lpfnWndProc = findProc;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&cls);
    int systemDpi = static_cast<int>(GetDpiForSystem());
    window = CreateWindowExW(0, L"LHPad.Native", L"LHPad", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, MulDiv(1320, systemDpi, 96),
                             MulDiv(850, systemDpi, 96), nullptr, nullptr, instance, this);
    if (!window)
        fail(errorText());
    dpi = GetDpiForWindow(window);
    LOGFONTW lf{};
    lf.lfHeight = -MulDiv(13, dpi, 96);
    wcscpy_s(lf.lfFaceName, L"맑은 고딕");
    font = CreateFontIndirectW(&lf);
    wcscpy_s(lf.lfFaceName, L"Consolas");
    lf.lfHeight = -MulDiv(15, dpi, 96);
    mono = CreateFontIndirectW(&lf);
    buildMenu();
    toolbar = CreateWindowExW(0, TOOLBARCLASSNAMEW, nullptr,
                              WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_NOPARENTALIGN |
                                  CCS_NORESIZE,
                              0, 0, 0, 0, window, reinterpret_cast<HMENU>(10), instance, nullptr);
    tabs = CreateWindowExW(0, WC_TABCONTROLW, nullptr,
                           WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_TOOLTIPS | TCS_FOCUSNEVER |
                               TCS_OWNERDRAWFIXED,
                           0, 0, 0, 0, window, reinterpret_cast<HMENU>(11), instance, nullptr);
    status = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0,
                             window, reinterpret_cast<HMENU>(12), instance, nullptr);
    for (auto h : {toolbar, tabs, status}) {
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        DragAcceptFiles(h, TRUE);
        SetWindowSubclass(h, childProc, 1, reinterpret_cast<DWORD_PTR>(this));
    }
    buildToolbar();
    createResultsPanel();
    createOutlinePanel();
    DragAcceptFiles(window, TRUE);
    restorePlacement(show);
    ShowWindow(window, show);
    UpdateWindow(window);
    layout();
    SetTimer(window, 1, 200, nullptr);
}
void App::start() {
    if (!demo) {
        const auto legacy = profile / L"legacy-csharp-backup";
        if (!disk::exists(legacy)) {
            disk::create_directories(legacy);
            for (auto name : {L"session.json", L"session.json.bak", L"macros.json", L"macros.json.bak",
                              L"window.json", L"window.json.bak"}) {
                const auto source = profile / name;
                if (disk::exists(source))
                    disk::copy_file(source, legacy / name, fs::copy_options::none);
            }
        }
    }
    loadMacros();
    if (!demo)
        restore();
    auto startupFiles = std::exchange(initialFiles, {});
    openPaths(startupFiles, false);
    if (docs.empty())
        newDocument();
    starting = false;
    update();
}
void App::layout() {
    if (!tabs)
        return;
    RECT client{};
    GetClientRect(window, &client);
    int width = client.right, height = client.bottom, bar = MulDiv(26, dpi, 96),
        statusHeight = MulDiv(26, dpi, 96);
    MoveWindow(toolbar, 0, 0, width, bar, TRUE);
    int panel = resultsVisible ? std::clamp(height / 3, MulDiv(120, dpi, 96), MulDiv(260, dpi, 96)) : 0;
    int outline = outlineVisible ? std::clamp(width / 5, MulDiv(200, dpi, 96), MulDiv(320, dpi, 96)) : 0;
    int tabsHeight = std::max(1, height - bar - statusHeight - panel);
    MoveWindow(tabs, 0, bar, std::max(1, width - outline), tabsHeight, TRUE);
    if (outlineList && outlineVisible) {
        MoveWindow(outlineList, width - outline, bar, outline, tabsHeight, TRUE);
        ListView_SetColumnWidth(outlineList, 0, std::max(MulDiv(80, dpi, 96), outline - MulDiv(80, dpi, 96)));
    }
    MoveWindow(status, 0, std::max(0, height - statusHeight), width, statusHeight, TRUE);
    if (resultsList && resultsVisible) {
        int top = std::max(bar, height - statusHeight - panel), label = MulDiv(24, dpi, 96);
        MoveWindow(resultsLabel, 0, top, width, label, TRUE);
        MoveWindow(resultsList, 0, top + label, width, std::max(1, panel - label), TRUE);
        ListView_SetColumnWidth(resultsList, 2, LVSCW_AUTOSIZE_USEHEADER);
    }
    int parts[] = {std::max(100, width - MulDiv(670, dpi, 96)), std::max(200, width - MulDiv(420, dpi, 96)),
                   std::max(250, width - MulDiv(270, dpi, 96)), std::max(300, width - MulDiv(150, dpi, 96)),
                   -1};
    SendMessageW(status, SB_SETPARTS, 5, reinterpret_cast<LPARAM>(parts));
    RECT r{};
    GetClientRect(tabs, &r);
    TabCtrl_AdjustRect(tabs, FALSE, &r);
    POINT p{r.left, r.top};
    MapWindowPoints(tabs, window, &p, 1);
    int x = p.x, y = p.y, w = std::max<int>(1, r.right - r.left), h = std::max<int>(1, r.bottom - r.top);
    for (size_t i = 0; i < docs.size(); i++) {
        auto &d = *docs[i];
        bool visible = static_cast<int>(i) == active;
        int dx = x, dw = w;
        if (d.editor) {
            bool show = visible && d.viewMode != 1;
            int ew = d.viewMode == 2 ? dw / 2 : dw;
            if (d.secondary) {
                HWND upper = d.topPane ? d.topPane : d.editor, lower = upper == d.editor ? d.secondary : d.editor;
                int gap = MulDiv(4, dpi, 96), half = (h - gap) / 2;
                MoveWindow(upper, dx, y, ew, std::max(1, half), TRUE);
                MoveWindow(lower, dx, y + half + gap, ew, std::max(1, h - half - gap), TRUE);
                for (auto view : {upper, lower}) {
                    ShowWindow(view, show ? SW_SHOW : SW_HIDE);
                    if (show)
                        SetWindowPos(view, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                }
            } else {
                MoveWindow(d.editor, dx, y, ew, h, TRUE);
                ShowWindow(d.editor, show ? SW_SHOW : SW_HIDE);
                if (show)
                    SetWindowPos(d.editor, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
        }
        if (d.hexWindow) {
            MoveWindow(d.hexWindow, dx, y, dw, h, TRUE);
            ShowWindow(d.hexWindow, visible ? SW_SHOW : SW_HIDE);
            if (visible)
                SetWindowPos(d.hexWindow, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        if (d.previewHost) {
            int px = d.viewMode == 2 ? dx + dw / 2 : dx, pw = d.viewMode == 2 ? dw - dw / 2 : dw;
            MoveWindow(d.previewHost, px, y, pw, h, TRUE);
            ShowWindow(d.previewHost, visible && d.viewMode != 0 ? SW_SHOW : SW_HIDE);
            if (visible && d.viewMode != 0)
                SetWindowPos(d.previewHost, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            if (d.controller) {
                RECT b{0, 0, pw, h};
                d.controller->put_Bounds(b);
                d.controller->put_IsVisible(visible && d.viewMode != 0);
            }
        }
    }
    InvalidateRect(window, nullptr, FALSE);
    refreshTabCloseHover();
}
void App::select(int index) {
    if (index < 0 || index >= static_cast<int>(docs.size()))
        return;
    active = index;
    TabCtrl_SetCurSel(tabs, index);
    layout();
    auto d = current();
    SetFocus(d->binary ? d->hexWindow : d->viewMode == 1 ? d->previewHost : d->editor);
    if (d->viewMode)
        refreshPreview(*d);
    refreshOutline(false);
    update();
}
void App::update() {
    if (!window || !status)
        return;
    bool tabChanged = false;
    for (size_t i = 0; i < docs.size(); i++) {
        auto &d = *docs[i];
        auto label = wide((compareIds.contains(d.id) ? "✓ " : "") + d.name) + L"    ×";
        TCITEMW item{};
        // Setting the same tab item still invalidates the native tab strip.
        // Read its displayed state and send only fields that really changed.
        std::wstring displayed(label.size() + 2, L'\0');
        TCITEMW previous{};
        previous.mask = TCIF_TEXT | TCIF_IMAGE;
        previous.pszText = displayed.data();
        previous.cchTextMax = static_cast<int>(displayed.size());
        bool found = TabCtrl_GetItem(tabs, static_cast<int>(i), &previous) != FALSE;
        int image = d.dirty() ? 1 : 0;
        if (!found || !previous.pszText || label != previous.pszText)
            item.mask |= TCIF_TEXT;
        if (!found || previous.iImage != image)
            item.mask |= TCIF_IMAGE;
        item.pszText = label.data();
        item.iImage = image;
        if (item.mask) {
            TabCtrl_SetItem(tabs, static_cast<int>(i), &item);
            tabChanged = true;
        }
    }
    if (tabChanged || busy || playing)
        refreshTabCloseHover();
    auto d = current();
    std::wstring title = d ? wide(d->name) + (d->dirty() ? L" *" : L"") + L" — LHPad" : L"LHPad";
    std::wstring displayedTitle(title.size() + 1, L'\0');
    GetWindowTextW(window, displayedTitle.data(), static_cast<int>(displayedTitle.size()));
    if (GetWindowTextLengthW(window) != static_cast<int>(title.size()) || title != displayedTitle.c_str())
        SetWindowTextW(window, title.c_str());
    auto setStatus = [&](int i, const std::wstring &s) {
        bool changed = static_cast<size_t>(LOWORD(SendMessageW(status, SB_GETTEXTLENGTHW, i, 0))) != s.size();
        if (!changed) {
            std::wstring displayed(s.size() + 1, L'\0');
            SendMessageW(status, SB_GETTEXTW, i, reinterpret_cast<LPARAM>(displayed.data()));
            changed = s != displayed.c_str();
        }
        if (changed)
            SendMessageW(status, SB_SETTEXTW, i, reinterpret_cast<LPARAM>(s.c_str()));
    };
    if (d) {
        setStatus(0, d->path.empty() ? L"이름 없는 문서" : d->path.wstring());
        if (d->binary) {
            setStatus(1, L"0x" + ([](Pos n) {
                             std::wstringstream s;
                             s << std::hex << std::uppercase << n;
                             return s.str();
                         })(d->caret) +
                             L" / " + std::to_wstring(d->binary->size()));
            setStatus(2, L"바이너리");
            setStatus(3, L"Hex");
            setStatus(4, d->insert ? L"INS" : L"OVR");
        } else {
            Pos caret = sci(d->editor, SCI_GETCURRENTPOS), line = sci(d->editor, SCI_LINEFROMPOSITION, caret);
            setStatus(1, L"Ln " + std::to_wstring(line + 1) + L", Col " +
                             std::to_wstring(sci(d->editor, SCI_GETCOLUMN, caret) + 1) + L" | " +
                             std::to_wstring(sci(d->editor, SCI_GETLENGTH)) + L" bytes");
            setStatus(2, wide(d->encoding.name));
            setStatus(3, wide(d->language));
            setStatus(4, d->monitor  ? wide(d->monitorStatus.empty() ? "로그 모니터링" : d->monitorStatus)
                         : recording ? L"● 녹화 중"
                         : playing   ? L"매크로 실행 중"
                                     : L"세션 자동 복원");
        }
    }
    auto check = [&](int id, bool b) {
        CheckMenuItem(menu, id, MF_BYCOMMAND | (b ? MF_CHECKED : MF_UNCHECKED));
    };
    check(AutoIndent, autoIndent);
    check(ResultsPanel, resultsVisible);
    check(OutlinePanel, outlineVisible);
    check(SplitEditor, d && d->secondary);
    check(WhiteSpace, whiteSpace);
    check(EolVisible, eolVisible);
    check(ControlVisible, controlVisible);
    check(Record, recording);
    check(Monitor, d && d->monitor);
    check(FollowTail, d && d->follow);
    check(Scripts, d && d->scripts);
    check(Wrap, d && d->editor && sci(d->editor, SCI_GETWRAPMODE));
    for (int id : {Source, Preview, Split, Monitor, FollowTail, SplitEditor, OutlinePanel}) {
        bool checked = id == OutlinePanel ? outlineVisible
                       : !d              ? false
                       : id == Source    ? d->viewMode == 0
                       : id == Preview   ? d->viewMode == 1
                       : id == Split     ? d->viewMode == 2
                       : id == Monitor   ? d->monitor
                       : id == SplitEditor ? d->secondary != nullptr
                                         : d->follow;
        if ((SendMessageW(toolbar, TB_ISBUTTONCHECKED, id, 0) != FALSE) != checked)
            SendMessageW(toolbar, TB_CHECKBUTTON, id, MAKELONG(checked, 0));
    }
    int recordImage = 0;
    for (auto &[id, label] : toolbarItems) {
        if (!id)
            continue;
        if (id == Record)
            break;
        recordImage++;
    }
    int image = recording ? ImageList_GetImageCount(toolbarImages) - 1 : recordImage;
    if (SendMessageW(toolbar, TB_GETBITMAP, Record, 0) != image)
        SendMessageW(toolbar, TB_CHANGEBITMAP, Record, image);
    for (auto &[id, label] : toolbarItems) {
        if (!id)
            continue;
        bool enabled = id == MacroStop ? recording || playing : !busy && !playing;
        if ((SendMessageW(toolbar, TB_ISBUTTONENABLED, id, 0) != FALSE) != enabled)
            SendMessageW(toolbar, TB_ENABLEBUTTON, id, MAKELONG(enabled, 0));
    }
}
void App::command(int id) {
    if (id == MacroStop) {
        cancel = true;
        if (recording)
            stopRecord();
        return;
    }
    if (busy || playing)
        return;
    guard([&] {
        auto d = current();
        if (id >= EncodingReopen && id < EncodingReopen + static_cast<int>(encodings().size())) {
            if (d)
                reload(*d, false, encodings()[id - EncodingReopen]);
            return;
        }
        if (id >= EncodingConvert && id < EncodingConvert + static_cast<int>(encodings().size())) {
            if (d && !d->binary) {
                d->encoding = encodings()[id - EncodingConvert];
                d->encodingChanged = true;
            }
            return;
        }
        if (id >= LanguageSelect && id < LanguageSelect + static_cast<int>(languages.items.size())) {
            if (d && !d->binary) {
                d->language = languages.items[id - LanguageSelect].name;
                for (auto view : views(*d))
                    languages.apply(view, d->language, d->large);
                refreshOutline(true);
                refreshPreview(*d, true);
            }
            return;
        }
        if (id >= RecentFile && id < RecentFile + static_cast<int>(recentLimit)) {
            openRecent(static_cast<size_t>(id - RecentFile));
            return;
        }
        if (id >= SavedMacro && id < SavedMacro + static_cast<int>(savedMacros.size())) {
            play(savedMacros[id - SavedMacro].steps);
            return;
        }
        if (id > FoldLevel && id <= FoldLevel + 8) {
            fold(id - FoldLevel, false);
            return;
        }
        if (id > UnfoldLevel && id <= UnfoldLevel + 8) {
            fold(id - UnfoldLevel, true);
            return;
        }
        if (id >= 600 && id <= 602) {
            if (d && d->editor) {
                int e = id == 600 ? SC_EOL_CRLF : id == 601 ? SC_EOL_LF : SC_EOL_CR;
                sci(d->editor, SCI_CONVERTEOLS, e);
                sci(d->editor, SCI_SETEOLMODE, e);
                MacroStep s;
                s.kind = 4;
                s.w = e;
                recordAction(s);
            }
            return;
        }
        switch (id) {
        case New:
            newDocument();
            break;
        case Open:
            chooseOpen();
            break;
        case Save:
            if (d)
                save(*d);
            break;
        case SaveAs:
            if (d)
                save(*d, true);
            break;
        case SaveAll:
            for (auto &doc : docs)
                if (doc->dirty() || doc->path.empty())
                    if (!save(*doc))
                        break;
            break;
        case Close:
            closeTab(active);
            break;
        case Exit:
            exit();
            break;
        case Undo:
            edit(SCI_UNDO);
            break;
        case Redo:
            edit(SCI_REDO);
            break;
        case Cut:
            edit(SCI_CUT);
            break;
        case Copy:
            edit(SCI_COPY);
            break;
        case Paste:
            edit(SCI_PASTE);
            break;
        case SelectAll:
            edit(SCI_SELECTALL);
            break;
        case Duplicate:
            edit(SCI_SELECTIONDUPLICATE);
            break;
        case DeleteLine:
            edit(SCI_LINEDELETE);
            break;
        case LineUp:
            edit(SCI_MOVESELECTEDLINESUP);
            break;
        case LineDown:
            edit(SCI_MOVESELECTEDLINESDOWN);
            break;
        case SortAsc:
            sort(false);
            break;
        case SortDesc:
            sort(true);
            break;
        case AutoIndent:
            autoIndent = !autoIndent;
            break;
        case EditorFont:
            chooseEditorFont();
            break;
        case RecentClear:
            recentFiles.clear();
            rebuildRecentMenu();
            break;
        case ReopenClosed:
            reopenClosed();
            break;
        case CloseOthers:
            closeTabs(0);
            break;
        case CloseRight:
            closeTabs(1);
            break;
        case CloseAll:
            closeTabs(2);
            break;
        case CopyPath:
            copyPath();
            break;
        case UpperCase:
            changeCase(SCI_UPPERCASE);
            break;
        case LowerCase:
            changeCase(SCI_LOWERCASE);
            break;
        case TitleCase:
            titleCase();
            break;
        case ToggleComment:
            toggleComment();
            break;
        case BraceJump:
            jumpBrace();
            break;
        case Settings:
            settings();
            break;
        case FindAll:
            if (search.query.empty())
                showFind(false);
            else
                findAll();
            break;
        case MarkAll:
            if (search.query.empty())
                showFind(false);
            else
                markAll();
            break;
        case ClearMarks:
            clearMarks();
            break;
        case FindInFiles:
            findInFiles();
            break;
        case ResultsPanel:
            showResults(!resultsVisible);
            break;
        case ResultNext:
            navigateHits(false);
            break;
        case ResultPrev:
            navigateHits(true);
            break;
        case BookmarkToggle:
            toggleBookmark();
            break;
        case BookmarkNext:
            nextBookmark(false);
            break;
        case BookmarkPrev:
            nextBookmark(true);
            break;
        case BookmarkClear:
            clearBookmarks();
            break;
        case BookmarkMatches:
            if (search.query.empty())
                showFind(false);
            else
                message(std::to_wstring(bookmarkMatches()) + L"줄에 북마크를 표시했습니다.");
            break;
        case BookmarkCopy:
            copyBookmarked();
            break;
        case BookmarkDelete:
            if (d && d->editor &&
                ask(L"북마크한 줄을 모두 삭제할까요? 실행 취소(Ctrl+Z)로 되돌릴 수 있습니다.", L"북마크한 줄 삭제",
                    MB_YESNO | MB_ICONWARNING) == IDYES)
                deleteBookmarked();
            break;
        case SplitEditor:
            toggleSplit();
            break;
        case OutlinePanel:
            showOutline(!outlineVisible);
            break;
        case Find:
            showFind(false);
            break;
        case Replace:
            showFind(true);
            break;
        case FindNext:
            findNext();
            break;
        case FindPrev:
            findNext(true);
            break;
        case GoTo:
            goTo();
            break;
        case Source:
            setView(0);
            break;
        case Preview:
            setView(d && d->viewMode == 1 ? 0 : 1);
            break;
        case Split:
            setView(2);
            break;
        case Refresh:
            if (d)
                refreshPreview(*d, true);
            break;
        case Scripts:
            if (d) {
                d->scripts = !d->scripts;
                refreshPreview(*d, true);
            }
            break;
        case WhiteSpace:
            whiteSpace = !whiteSpace;
            break;
        case EolVisible:
            eolVisible = !eolVisible;
            break;
        case ControlVisible:
            controlVisible = !controlVisible;
            break;
        case AllSymbols:
            whiteSpace = eolVisible = controlVisible = !(whiteSpace && eolVisible && controlVisible);
            break;
        case Wrap:
            if (d && d->editor)
                sci(d->editor, SCI_SETWRAPMODE, !sci(d->editor, SCI_GETWRAPMODE));
            break;
        case ZoomIn:
            edit(SCI_ZOOMIN);
            break;
        case ZoomOut:
            edit(SCI_ZOOMOUT);
            break;
        case ZoomReset:
            if (d && d->editor)
                sci(d->editor, SCI_SETZOOM, 0);
            break;
        case FoldCurrent: {
            if (d && d->editor && sci(d->editor, SCI_GETMARGINWIDTHN, 1)) {
                auto line = sci(d->editor, SCI_LINEFROMPOSITION, sci(d->editor, SCI_GETCURRENTPOS));
                sci(d->editor, SCI_COLOURISE, 0, sci(d->editor, SCI_GETLINEENDPOSITION, line));
                if (!(sci(d->editor, SCI_GETFOLDLEVEL, line) & SC_FOLDLEVELHEADERFLAG))
                    line = sci(d->editor, SCI_GETFOLDPARENT, line);
                if (line >= 0)
                    sci(d->editor, SCI_FOLDLINE, line, SC_FOLDACTION_TOGGLE);
            }
            break;
        }
        case FoldAll:
            fold(0, false);
            break;
        case UnfoldAll:
            fold(0, true);
            break;
        case AsHex:
            if (d)
                reload(*d, true);
            break;
        case AsText:
            if (d)
                reload(*d, false);
            break;
        case ExternalCheck:
            checkExternal();
            break;
        case Monitor:
            toggleMonitor();
            break;
        case FollowTail:
            if (d)
                d->follow = !d->follow;
            break;
        case Column:
            column();
            break;
        case AddNext:
            edit(SCI_MULTIPLESELECTADDNEXT);
            break;
        case AddAll:
            edit(SCI_MULTIPLESELECTADDEACH);
            break;
        case Record:
            toggleRecord();
            break;
        case Play:
            play(lastMacro);
            break;
        case Repeat:
            repeat();
            break;
        case MacroSave:
            saveMacro();
            break;
        case MacroManage:
            manageMacros();
            break;
        case Format:
            format();
            break;
        case CompareSelect:
            if (d) {
                if (!compareIds.erase(d->id))
                    compareIds.insert(d->id);
                if (compareIds.size() != 2)
                    layout();
            }
            break;
        case Compare:
            compare();
            break;
        case CompareNext:
            compareNavigate(false);
            break;
        case ComparePrev:
            compareNavigate(true);
            break;
        case Hash:
            if (d && d->binary) {
                busy = true;
                try {
                    message(wide(sha256(*d->binary)), L"SHA-256");
                    busy = false;
                } catch (...) {
                    busy = false;
                    throw;
                }
            }
            break;
        case RegisterShell:
            registerShell(exeDir() / L"LHPad.exe");
            message(L"파일 우클릭 메뉴에 ‘LHPad로 열기’를 등록했습니다.\nWindows 11 첫 우클릭 메뉴에 나타납니다. 메뉴를 닫고 다시 열어 주세요.");
            break;
        case UnregisterShell:
            unregisterShell();
            message(L"‘LHPad로 열기’를 제거했습니다.");
            break;
        case RecoveryFolder:
            ShellExecuteW(window, L"open", profile.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case About:
            about();
            break;
        case Help:
            message(
                L"Alt+드래그 / Alt+Shift+방향키: 세로 선택\nCtrl+클릭: 다중 커서\nAlt+C: 열 편집\nCtrl+F / "
                L"Ctrl+H: 찾기 / 바꾸기\nCtrl+Shift+R: 매크로 녹화\nCtrl+Shift+P: 매크로 실행\nCtrl+Alt+C: "
                L"두 탭 비교\nCtrl+Alt+B: 현재 구간 접기\nCtrl+Shift+V: 미리보기\nCtrl+Alt+V: 분할 보기\n"
                L"Ctrl+Q: 주석 넣기 / 빼기\nCtrl+B: 짝 괄호로 이동\nCtrl+Shift+U / Ctrl+U: 대문자 / 소문자\n"
                L"Ctrl+Shift+T: 닫은 탭 다시 열기\nCtrl+Shift+F: 파일에서 찾기\nF4 / Shift+F4: 다음 / 이전 검색 결과\n"
                L"Ctrl+F2 / F2 / Shift+F2: 북마크 표시 / 다음 / 이전",
                L"편집 단축키");
            break;
        }
        for (auto &doc : docs)
            for (auto view : views(*doc)) {
                sci(view, SCI_SETVIEWWS, whiteSpace ? SCWS_VISIBLEALWAYS : SCWS_INVISIBLE);
                sci(view, SCI_SETVIEWEOL, eolVisible);
                sci(view, SCI_SETCONTROLCHARSYMBOL, controlVisible ? 0 : 32);
            }
        updateFind();
    });
}
bool App::shortcut(MSG &m) {
    HWND root = GetAncestor(m.hwnd, GA_ROOT);
    if (root != window && root != findWindow)
        return false;
    if (m.message != WM_KEYDOWN && m.message != WM_SYSKEYDOWN)
        return false;
    bool ctrl = GetKeyState(VK_CONTROL) < 0, alt = GetKeyState(VK_MENU) < 0,
         shift = GetKeyState(VK_SHIFT) < 0;
    int key = static_cast<int>(m.wParam), id = 0;
    if (key == VK_ESCAPE && playing) {
        cancel = true;
        return true;
    }
    if (key == VK_TAB && ctrl) {
        if (!docs.empty())
            select((active + (shift ? -1 : 1) + static_cast<int>(docs.size())) %
                   static_cast<int>(docs.size()));
        return true;
    }
    if (alt && key >= '0' && key <= '8') {
        int depth = key - '0';
        id = depth ? (shift ? UnfoldLevel : FoldLevel) + depth : shift ? UnfoldAll : FoldAll;
    } else if (ctrl && alt && shift && key == 'P')
        id = Repeat;
    else if (ctrl && alt) {
        switch (key) {
        case 'S':
            id = SaveAs;
            break;
        case 'V':
            id = Split;
            break;
        case 'F':
            id = Format;
            break;
        case 'C':
            id = Compare;
            break;
        case 'B':
            id = FoldCurrent;
            break;
        case VK_DOWN:
            id = AddNext;
            break;
        }
    } else if (ctrl && shift) {
        switch (key) {
        case 'S':
            id = SaveAll;
            break;
        case 'V':
            id = Preview;
            break;
        case 'R':
            id = Record;
            break;
        case 'P':
            id = Play;
            break;
        case 'L':
            id = AddAll;
            break;
        case 'T':
            id = ReopenClosed;
            break;
        case 'F':
            id = FindInFiles;
            break;
        case 'U':
            id = UpperCase;
            break;
        case VK_UP:
            id = LineUp;
            break;
        case VK_DOWN:
            id = LineDown;
            break;
        }
    } else if (ctrl) {
        switch (key) {
        case 'N':
            id = New;
            break;
        case 'O':
            id = Open;
            break;
        case 'S':
            id = Save;
            break;
        case 'W':
            id = Close;
            break;
        case 'F':
            id = Find;
            break;
        case 'H':
            id = Replace;
            break;
        case 'G':
            id = GoTo;
            break;
        case 'D':
            id = Duplicate;
            break;
        case 'L':
            id = DeleteLine;
            break;
        case 'U':
            id = LowerCase;
            break;
        case 'Q':
            id = ToggleComment;
            break;
        case 'B':
            id = BraceJump;
            break;
        case VK_ADD:
        case VK_OEM_PLUS:
            id = ZoomIn;
            break;
        case VK_SUBTRACT:
        case VK_OEM_MINUS:
            id = ZoomOut;
            break;
        case '0':
        case VK_NUMPAD0:
            id = ZoomReset;
            break;
        }
    } else if (alt && key == 'C')
        id = Column;
    else if (key == VK_F3)
        id = shift ? FindPrev : FindNext;
    else if (key == VK_F5)
        id = Refresh;
    else if (key == VK_F7)
        id = shift ? ComparePrev : CompareNext;
    else if (key == VK_F4 && !ctrl && !alt)
        id = shift ? ResultPrev : ResultNext;
    else if (key == VK_F2 && !alt)
        id = ctrl ? BookmarkToggle : shift ? BookmarkPrev : BookmarkNext;
    if (id) {
        command(id);
        return true;
    }
    return false;
}
static std::vector<std::string> dropPaths(HDROP h) {
    UINT count = DragQueryFileW(h, 0xFFFFFFFF, nullptr, 0);
    std::vector<std::string> paths;
    for (UINT i = 0; i < count; i++) {
        UINT n = DragQueryFileW(h, i, nullptr, 0);
        std::wstring s(n + 1, 0);
        DragQueryFileW(h, i, s.data(), n + 1);
        s.resize(n);
        paths.push_back(narrow(s));
    }
    return paths;
}
LRESULT CALLBACK App::childProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR data) {
    auto *a = reinterpret_cast<App *>(data);
    if (h == a->tabs && a->tabCloseMessage(m, w, l))
        return m == WM_SETCURSOR ? TRUE : 0;
    if (m == WM_DROPFILES) {
        auto paths = dropPaths(reinterpret_cast<HDROP>(w));
        DragFinish(reinterpret_cast<HDROP>(w));
        PostMessageW(a->window, WM_APP + 4, 0,
                     reinterpret_cast<LPARAM>(new std::vector<std::string>(std::move(paths))));
        return 0;
    }
    if (m == WM_MBUTTONDOWN && h == a->tabs) {
        TCHITTESTINFO hit{{GET_X_LPARAM(l), GET_Y_LPARAM(l)}, 0};
        int i = TabCtrl_HitTest(h, &hit);
        if (i >= 0 && !a->busy)
            a->guard([&] { a->closeTab(i); });
        return 0;
    }
    if ((m == WM_LBUTTONDOWN || m == WM_RBUTTONDOWN) && h == a->tabs) {
        TCHITTESTINFO hit{{GET_X_LPARAM(l), GET_Y_LPARAM(l)}, 0};
        int i = TabCtrl_HitTest(h, &hit);
        if (i >= 0) {
            if (m == WM_LBUTTONDOWN && GetKeyState(VK_CONTROL) < 0) {
                auto &doc = *a->docs[i];
                if (!a->compareIds.erase(doc.id))
                    a->compareIds.insert(doc.id);
                a->update();
                return 0;
            }
            if (m == WM_LBUTTONDOWN)
                a->dragTab = a->docs[i]->id; // The tab control selects it; moving reorders.
            if (m == WM_RBUTTONDOWN) {
                a->select(i);
                auto menu = CreatePopupMenu();
                AppendMenuW(menu, MF_STRING, Save, L"저장");
                AppendMenuW(menu, MF_STRING, Close, L"닫기");
                AppendMenuW(menu, MF_STRING, CloseOthers, L"다른 탭 모두 닫기");
                AppendMenuW(menu, MF_STRING, CloseRight, L"오른쪽 탭 모두 닫기");
                AppendMenuW(menu, MF_STRING, CloseAll, L"모든 탭 닫기");
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(menu, MF_STRING | (a->docs[i]->path.empty() ? MF_GRAYED : 0), CopyPath,
                            L"파일 경로 복사");
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(menu, MF_STRING, CompareSelect, L"비교 대상으로 선택");
                AppendMenuW(menu, MF_STRING, Compare, L"선택한 두 탭 비교");
                POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
                ClientToScreen(h, &p);
                int command =
                    TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, a->window, nullptr);
                DestroyMenu(menu);
                if (command)
                    a->command(command);
                return 0;
            }
        }
    }
    if (h == a->tabs && m == WM_MOUSEMOVE && (w & MK_LBUTTON) && !a->dragTab.empty() &&
        a->pressedTabClose.empty() && !a->busy && !a->playing) {
        TCHITTESTINFO hit{{GET_X_LPARAM(l), GET_Y_LPARAM(l)}, 0};
        int target = TabCtrl_HitTest(h, &hit);
        int from = -1;
        for (size_t i = 0; i < a->docs.size(); i++)
            if (a->docs[i]->id == a->dragTab)
                from = static_cast<int>(i);
        if (target >= 0 && from >= 0 && target != from)
            a->moveTab(from, target);
    }
    if (h == a->tabs && (m == WM_LBUTTONUP || m == WM_CAPTURECHANGED))
        a->dragTab.clear();
    if (m == WM_NCDESTROY)
        RemoveWindowSubclass(h, childProc, id);
    return DefSubclassProc(h, m, w, l);
}
LRESULT App::notify(NMHDR *n) {
    if (n->hwndFrom == toolbar && n->code == NM_CUSTOMDRAW) {
        auto *draw = reinterpret_cast<NMTBCUSTOMDRAW *>(n);
        if (draw->nmcd.dwDrawStage == CDDS_PREPAINT)
            return CDRF_NOTIFYPOSTPAINT;
        if (draw->nmcd.dwDrawStage == CDDS_POSTPAINT) {
            // Keep group boundaries visible with the flat toolbar theme.
            auto pen = CreatePen(PS_SOLID, std::max(1, MulDiv(1, dpi, 96)), RGB(168, 176, 184));
            auto previous = SelectObject(draw->nmcd.hdc, pen);
            for (size_t i = 0; i < toolbarItems.size(); i++) {
                if (toolbarItems[i].first)
                    continue;
                RECT r{};
                SendMessageW(toolbar, TB_GETITEMRECT, i, reinterpret_cast<LPARAM>(&r));
                int x = (r.left + r.right) / 2, inset = MulDiv(3, dpi, 96);
                MoveToEx(draw->nmcd.hdc, x, r.top + inset, nullptr);
                LineTo(draw->nmcd.hdc, x, r.bottom - inset);
            }
            SelectObject(draw->nmcd.hdc, previous);
            DeleteObject(pen);
        }
        return CDRF_DODEFAULT;
    }
    if (resultsList && n->hwndFrom == resultsList) {
        if (n->code == LVN_GETDISPINFOW) {
            auto &item = reinterpret_cast<NMLVDISPINFOW *>(n)->item;
            if ((item.mask & LVIF_TEXT) && item.pszText && item.cchTextMax > 0 && item.iItem >= 0 &&
                item.iItem < static_cast<int>(hits.size())) {
                auto &hit = hits[static_cast<size_t>(item.iItem)];
                auto text = item.iSubItem == 0   ? hit.source
                            : item.iSubItem == 1 ? std::to_wstring(hit.line + 1)
                                                 : hit.text;
                wcsncpy_s(item.pszText, item.cchTextMax, text.c_str(), _TRUNCATE);
            }
        } else if (n->code == LVN_ITEMACTIVATE) {
            int index = reinterpret_cast<NMITEMACTIVATE *>(n)->iItem;
            if (!busy && !playing)
                guard([&] { goToHit(index); });
        }
        return 0;
    }
    if (outlineList && n->hwndFrom == outlineList) {
        if (n->code == LVN_GETDISPINFOW) {
            auto &item = reinterpret_cast<NMLVDISPINFOW *>(n)->item;
            if ((item.mask & LVIF_TEXT) && item.pszText && item.cchTextMax > 0 && item.iItem >= 0 &&
                item.iItem < static_cast<int>(outlineItems.size())) {
                auto &entry = outlineItems[static_cast<size_t>(item.iItem)];
                auto text = item.iSubItem == 0 ? std::wstring(static_cast<size_t>(entry.depth) * 3, L' ') + entry.name
                                               : std::to_wstring(entry.line + 1);
                wcsncpy_s(item.pszText, item.cchTextMax, text.c_str(), _TRUNCATE);
            }
        } else if (n->code == LVN_ITEMACTIVATE || n->code == NM_CLICK) {
            int index = reinterpret_cast<NMITEMACTIVATE *>(n)->iItem;
            if (index >= 0 && !busy && !playing)
                guard([&] { goToOutline(index); });
        }
        return 0;
    }
    if (n->hwndFrom == tabs && n->code == TCN_SELCHANGE) {
        select(TabCtrl_GetCurSel(tabs));
        return 0;
    }
    if (n->hwndFrom == toolbar && n->code == TBN_GETINFOTIPW) {
        auto *tip = reinterpret_cast<NMTBGETINFOTIPW *>(n);
        for (auto &[id, label] : toolbarItems)
            if (id == tip->iItem)
                wcsncpy_s(tip->pszText, tip->cchTextMax, label.c_str(), _TRUNCATE);
        return 0;
    }
    Document *d = nullptr;
    for (auto &doc : docs)
        if (doc->editor == n->hwndFrom) {
            d = doc.get();
            break;
        }
    if (!d)
        return 0;
    auto &s = *reinterpret_cast<SCNotification *>(n);
    switch (n->code) {
    case SCN_URIDROPPED:
        if (s.text) {
            std::istringstream stream(s.text);
            std::vector<std::string> paths;
            std::string path;
            while (std::getline(stream, path))
                if (!path.empty())
                    paths.push_back(path);
            PostMessageW(window, WM_APP + 4, 0,
                         reinterpret_cast<LPARAM>(new std::vector<std::string>(std::move(paths))));
        }
        break;
    case SCN_MODIFIED:
        if (s.modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)) {
            d->revision++;
            if (!d->loading) {
                previewTick = GetTickCount64() + 400;
                d->previewPending = true;
                update();
            }
        }
        break;
    case SCN_UPDATEUI:
        if (!d->loading) {
            if (s.updated & (SC_UPDATE_CONTENT | SC_UPDATE_SELECTION))
                highlightBrace(*d);
            update();
            if (s.updated & SC_UPDATE_V_SCROLL)
                syncCompare(*d);
        }
        break;
    case SCN_MARGINCLICK:
        if (s.margin == 2) {
            Pos line = sci(d->editor, SCI_LINEFROMPOSITION, s.position);
            if (sci(d->editor, SCI_MARKERGET, line) & (1 << bookmarkMarker))
                sci(d->editor, SCI_MARKERDELETE, line, bookmarkMarker);
            else
                sci(d->editor, SCI_MARKERADD, line, bookmarkMarker);
        }
        break;
    case SCN_CHARADDED:
        if (newlineActive && (s.ch == '\r' || s.ch == '\n'))
            newlineObserved = true;
        break;
    case SCN_MACRORECORD:
        if (recording && !macroSuppressed && !d->loading && !d->monitor)
            capture(s);
        break;
    }
    return 0;
}
LRESULT CALLBACK App::windowProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto *a = reinterpret_cast<App *>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE) {
        a = static_cast<App *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(a));
        a->window = h;
    }
    if (!a)
        return DefWindowProcW(h, m, w, l);
    try {
        switch (m) {
        case WM_SIZE:
            if (w == SIZE_MAXIMIZED)
                a->maximized = true;
            else if (w == SIZE_RESTORED)
                a->maximized = false;
            a->layout();
            return 0;
        case WM_GETMINMAXINFO: {
            auto *info = reinterpret_cast<MINMAXINFO *>(l);
            info->ptMinTrackSize = {MulDiv(800, a->dpi, 96), MulDiv(500, a->dpi, 96)};
            return 0;
        }
        case WM_DPICHANGED: {
            a->dpi = HIWORD(w);
            auto *r = reinterpret_cast<RECT *>(l);
            SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            a->layout();
            return 0;
        }
        case WM_COMMAND:
            a->command(LOWORD(w));
            return 0;
        case WM_NOTIFY:
            return a->notify(reinterpret_cast<NMHDR *>(l));
        case WM_DRAWITEM: {
            auto draw = reinterpret_cast<DRAWITEMSTRUCT *>(l);
            if (draw && draw->CtlType == ODT_TAB && draw->hwndItem == a->tabs) {
                a->drawTabItem(*draw);
                return TRUE;
            }
            break;
        }
        case WM_CLOSE:
            a->guard([&] { a->exit(); });
            return 0;
        case WM_DESTROY:
            KillTimer(h, 1);
            PostQuitMessage(0);
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(w) != WA_INACTIVE)
                PostMessageW(h, WM_APP + 2, 0, 0);
            return 0;
        case WM_APP + 2:
            if (!a->starting && !a->busy)
                a->guard([&] { a->checkExternal(); });
            return 0;
        case WM_APP + 4: {
            std::unique_ptr<std::vector<std::string>> paths(reinterpret_cast<std::vector<std::string> *>(l));
            if (a->starting || a->busy || a->playing) {
                a->initialFiles.insert(a->initialFiles.end(), paths->begin(), paths->end());
            } else
                a->guard([&] { a->openPaths(*paths); });
            return 0;
        }
        case WM_DROPFILES: {
            auto paths = dropPaths(reinterpret_cast<HDROP>(w));
            DragFinish(reinterpret_cast<HDROP>(w));
            PostMessageW(h, WM_APP + 4, 0,
                         reinterpret_cast<LPARAM>(new std::vector<std::string>(std::move(paths))));
            return 0;
        }
        case WM_COPYDATA: {
            auto *data = reinterpret_cast<COPYDATASTRUCT *>(l);
            if (data->dwData != 0x4C504144 || !data->lpData || data->cbData > 4 * 1024 * 1024)
                return FALSE;
            auto value = Json::parse(static_cast<const char *>(data->lpData),
                                     static_cast<const char *>(data->lpData) + data->cbData);
            if (!value.is_array())
                return FALSE;
            auto paths = value.get<std::vector<std::string>>();
            PostMessageW(h, WM_APP + 4, 0,
                         reinterpret_cast<LPARAM>(new std::vector<std::string>(std::move(paths))));
            return TRUE;
        }
        case WM_TIMER:
            if (w == 1)
                a->timer();
            return 0;
        case WM_QUERYENDSESSION:
            if (a->busy)
                return FALSE;
            try {
                a->backup();
                a->savePlacement();
                return TRUE;
            } catch (...) {
                return FALSE;
            }
        case WM_ENDSESSION:
            if (w)
                DestroyWindow(h);
            return 0;
        }
    } catch (const std::exception &e) {
        if (m != WM_TIMER)
            a->message(wide(e.what()), L"LHPad 오류", MB_OK | MB_ICONERROR);
    }
    return DefWindowProcW(h, m, w, l);
}
} // namespace lp
