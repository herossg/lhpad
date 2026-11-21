#include "app.h"
namespace lp {
// Indicators for the characters that differ inside a changed line pair.
constexpr int inlineRemoved = 12, inlineAdded = 13;
struct Comparison {
    App *app;
    std::string leftId, rightId;
    HWND window = nullptr, left = nullptr, right = nullptr;
    std::vector<Pos> changes;
    int index = -1;
    bool syncing = false;
    void navigate(bool backwards) {
        if (changes.empty())
            return;
        index = (index + (backwards ? -1 : 1) + static_cast<int>(changes.size())) %
                static_cast<int>(changes.size());
        for (auto h : {left, right}) {
            sci(h, SCI_GOTOLINE, changes[index]);
            sci(h, SCI_SETFIRSTVISIBLELINE, std::max<Pos>(0, changes[index] - 4));
        }
    }
};
static LRESULT CALLBACK compareEditorProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR data) {
    auto state = reinterpret_cast<Comparison *>(data);
    if (m == WM_KEYDOWN && w == VK_F7) {
        state->navigate(GetKeyState(VK_SHIFT) < 0);
        return 0;
    }
    if (m == WM_NCDESTROY)
        RemoveWindowSubclass(h, compareEditorProc, id);
    return DefSubclassProc(h, m, w, l);
}
static LRESULT CALLBACK compareProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto state = reinterpret_cast<Comparison *>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE) {
        state = static_cast<Comparison *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
        state->window = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (!state)
        return DefWindowProcW(h, m, w, l);
    if (LRESULT coloured = 0; dialogColours(m, w, l, coloured))
        return coloured;
    switch (m) {
    case WM_SIZE: {
        RECT r{};
        GetClientRect(h, &r);
        int bar = MulDiv(44, GetDpiForWindow(h), 96);
        MoveWindow(GetDlgItem(h, 510), 8, 6, MulDiv(180, GetDpiForWindow(h), 96), bar - 10, TRUE);
        MoveWindow(GetDlgItem(h, 511), MulDiv(190, GetDpiForWindow(h), 96), 6,
                   MulDiv(180, GetDpiForWindow(h), 96), bar - 10, TRUE);
        MoveWindow(GetDlgItem(h, 513), MulDiv(385, GetDpiForWindow(h), 96), 6,
                   MulDiv(110, GetDpiForWindow(h), 96), bar - 10, TRUE);
        MoveWindow(GetDlgItem(h, 514), MulDiv(500, GetDpiForWindow(h), 96), 6,
                   MulDiv(130, GetDpiForWindow(h), 96), bar - 10, TRUE);
        MoveWindow(GetDlgItem(h, 512), MulDiv(640, GetDpiForWindow(h), 96), 10,
                   std::max(1L, r.right - MulDiv(645, GetDpiForWindow(h), 96)), bar - 10, TRUE);
        MoveWindow(state->left, 0, bar, r.right / 2 - 2, std::max(1L, r.bottom - bar), TRUE);
        MoveWindow(state->right, r.right / 2 + 2, bar, r.right - r.right / 2 - 2,
                   std::max(1L, r.bottom - bar), TRUE);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(w) == CompareNext || LOWORD(w) == 510)
            state->navigate(false);
        else if (LOWORD(w) == ComparePrev || LOWORD(w) == 511)
            state->navigate(true);
        else if ((LOWORD(w) == 513 || LOWORD(w) == 514) && HIWORD(w) == BN_CLICKED) {
            // Re-run the comparison with the new option in a window at the same place, then close this one.
            auto &owner = *state->app;
            (LOWORD(w) == 513 ? owner.compareIgnoreSpace : owner.compareIgnoreCase) =
                SendDlgItemMessageW(h, LOWORD(w), BM_GETCHECK, 0, 0) == BST_CHECKED;
            RECT place{};
            GetWindowRect(h, &place);
            auto previous = owner.compareIds;
            owner.compareIds = {state->leftId, state->rightId};
            owner.guard([&] { owner.compare(); });
            owner.compareIds = previous;
            if (owner.compareWindow && owner.compareWindow != h) {
                SetWindowPos(owner.compareWindow, nullptr, place.left, place.top, place.right - place.left,
                             place.bottom - place.top, SWP_NOZORDER);
                PostMessageW(h, WM_CLOSE, 0, 0);
            }
        }
        return 0;
    case WM_NOTIFY: {
        auto n = reinterpret_cast<SCNotification *>(l);
        if (n->nmhdr.code == SCN_UPDATEUI && (n->updated & SC_UPDATE_V_SCROLL) && !state->syncing) {
            state->syncing = true;
            HWND from = reinterpret_cast<HWND>(n->nmhdr.hwndFrom);
            HWND to = from == state->left ? state->right : state->left;
            sci(to, SCI_SETFIRSTVISIBLELINE, sci(from, SCI_GETFIRSTVISIBLELINE));
            state->syncing = false;
        }
        if (n->nmhdr.code == SCN_URIDROPPED && n->text) {
            std::istringstream stream(n->text);
            std::string path;
            std::vector<std::string> paths;
            while (std::getline(stream, path))
                if (!path.empty())
                    paths.push_back(path);
            PostMessageW(state->app->window, WM_APP + 4, 0,
                         reinterpret_cast<LPARAM>(new std::vector<std::string>(std::move(paths))));
        }
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_NCDESTROY:
        if (state->app->compareWindow == h)
            state->app->compareWindow = nullptr;
        delete state;
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        return DefWindowProcW(h, m, w, l);
    }
    return DefWindowProcW(h, m, w, l);
}
void App::compare() {
    std::vector<Document *> pair;
    for (auto &d : docs)
        if (compareIds.contains(d->id))
            pair.push_back(d.get());
    if (pair.size() != 2 || !pair[0]->editor || !pair[1]->editor)
        fail(L"Ctrl을 누르고 텍스트 탭 두 개를 클릭해 비교 대상을 선택하세요.");
    for (auto d : pair)
        if (sci(d->editor, SCI_GETLENGTH) > 32 * 1024 * 1024)
            fail(L"비교는 각각 32 MiB 이하 문서에서 지원합니다.");
    auto left = lines(textRange(pair[0]->editor, 0, sci(pair[0]->editor, SCI_GETLENGTH)));
    auto right = lines(textRange(pair[1]->editor, 0, sci(pair[1]->editor, SCI_GETLENGTH)));
    if (left.size() + right.size() > 400000)
        fail(L"비교는 두 문서 합계 400,000줄 이하에서 지원합니다.");
    // Match lines on their normalised form, but always show the original text.
    std::vector<std::string> leftKeys, rightKeys;
    if (compareIgnoreSpace || compareIgnoreCase) {
        for (auto &line : left)
            leftKeys.push_back(compareKey(line, compareIgnoreSpace, compareIgnoreCase));
        for (auto &line : right)
            rightKeys.push_back(compareKey(line, compareIgnoreSpace, compareIgnoreCase));
    }
    auto rows = compareIgnoreSpace || compareIgnoreCase ? diffLines(leftKeys, rightKeys) : diffLines(left, right);
    auto state = new Comparison{this, pair[0]->id, pair[1]->id};
    WNDCLASSEXW cls{sizeof(cls)};
    cls.hInstance = instance;
    cls.lpfnWndProc = compareProc;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    cls.lpszClassName = L"LHPad.Compare";
    RegisterClassExW(&cls);
    auto title = L"줄 비교 · " + wide(pair[0]->name) + L" ↔ " + wide(pair[1]->name);
    auto h = CreateWindowExW(0, cls.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, MulDiv(1350, dpi, 96), MulDiv(820, dpi, 96),
                             window, nullptr, instance, state);
    if (!h) {
        delete state;
        fail(errorText());
    }
    compareWindow = h;
    for (auto item : {std::pair{510, L"다음 차이  F7"}, std::pair{511, L"이전 차이  Shift+F7"},
                      std::pair{513, L"공백 무시"}, std::pair{514, L"대소문자 무시"},
                      std::pair{512, L"비교 시점의 읽기 전용 스냅샷"}}) {
        bool box = item.first == 513 || item.first == 514;
        auto control = CreateWindowExW(
            0, item.first == 512 ? L"STATIC" : L"BUTTON", item.second,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | (box ? BS_AUTOCHECKBOX : 0), 0, 0, 1, 1, h,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(item.first)), instance, nullptr);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        if (box)
            SendMessageW(control, BM_SETCHECK,
                         (item.first == 513 ? compareIgnoreSpace : compareIgnoreCase) ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    state->left = CreateWindowExW(0, L"Scintilla", nullptr,
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_HSCROLL | WS_VSCROLL, 0, 0, 1, 1, h,
                                  reinterpret_cast<HMENU>(501), instance, nullptr);
    state->right = CreateWindowExW(0, L"Scintilla", nullptr,
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_HSCROLL | WS_VSCROLL, 0, 0, 1, 1,
                                   h, reinterpret_cast<HMENU>(502), instance, nullptr);
    std::string a, b;
    for (size_t i = 0; i < rows.size(); i++) {
        auto row = rows[i];
        if (i) {
            a += '\n';
            b += '\n';
        }
        if (row.left >= 0)
            a += left[row.left];
        if (row.right >= 0)
            b += right[row.right];
        if (row.kind)
            state->changes.push_back(static_cast<Pos>(i));
    }
    for (auto editor : {state->left, state->right}) {
        configureEditor(editor);
        str(editor, SCI_SETTEXT, editor == state->left ? a : b);
        sci(editor, SCI_EMPTYUNDOBUFFER);
        sci(editor, SCI_SETREADONLY, 1);
        sci(editor, SCI_SETCARETLINEVISIBLE, 0);
        sci(editor, SCI_SETMARGINTYPEN, 0, SC_MARGIN_RTEXT);
        SetWindowSubclass(editor, compareEditorProc, 1, reinterpret_cast<DWORD_PTR>(state));
    }
    for (size_t i = 0; i < rows.size(); i++) {
        auto row = rows[i];
        str(state->left, SCI_MARGINSETTEXT, row.left < 0 ? "" : std::to_string(row.left + 1), i);
        str(state->right, SCI_MARGINSETTEXT, row.right < 0 ? "" : std::to_string(row.right + 1), i);
        if (row.kind) {
            sci(state->left, SCI_MARKERADD, i, row.left < 0 ? 22 : 21);
            sci(state->right, SCI_MARKERADD, i, row.right < 0 ? 22 : 20);
        }
    }
    // Mark the characters that differ inside each changed pair (kind 3: a line on both sides).
    auto &colours = palette();
    for (auto [editor, indicator, colour] :
         {std::tuple{state->left, inlineRemoved, colours.dark ? RGB(190, 70, 80) : RGB(255, 120, 120)},
          std::tuple{state->right, inlineAdded, colours.dark ? RGB(60, 165, 95) : RGB(70, 190, 100)}}) {
        sci(editor, SCI_INDICSETSTYLE, indicator, INDIC_FULLBOX);
        sci(editor, SCI_INDICSETFORE, indicator, colour);
        sci(editor, SCI_INDICSETALPHA, indicator, 110);
        sci(editor, SCI_INDICSETOUTLINEALPHA, indicator, 160);
        sci(editor, SCI_INDICSETUNDER, indicator, 1);
    }
    size_t marked = 0;
    for (size_t i = 0; i < rows.size() && marked < 20000; i++) {
        auto row = rows[i];
        if (row.kind != 3 || left[row.left].size() > 10000 || right[row.right].size() > 10000)
            continue;
        marked++;
        auto parts = inlineDiff(left[row.left], right[row.right]);
        for (auto [editor, indicator, ranges] : {std::tuple{state->left, inlineRemoved, &parts.left},
                                                 std::tuple{state->right, inlineAdded, &parts.right}}) {
            Pos lineStart = sci(editor, SCI_POSITIONFROMLINE, i);
            sci(editor, SCI_SETINDICATORCURRENT, indicator);
            for (auto [from, to] : *ranges)
                sci(editor, SCI_INDICATORFILLRANGE, lineStart + static_cast<Pos>(from), static_cast<Pos>(to - from));
        }
    }
    auto info = L"변경 " + std::to_wstring(state->changes.size()) + L"줄 · 읽기 전용 스냅샷" +
                (compareIgnoreSpace || compareIgnoreCase ? L" · 무시 옵션 적용" : L"");
    SetWindowTextW(GetDlgItem(h, 512), info.c_str());
    themeDialog(h);
    ShowWindow(h, SW_SHOWNORMAL);
    SendMessageW(h, WM_SIZE, 0, 0);
    state->navigate(false);
}
void App::compareNavigate(bool backwards) {
    if (compareWindow && IsWindow(compareWindow))
        SendMessageW(compareWindow, WM_COMMAND, backwards ? ComparePrev : CompareNext, 0);
}
void App::syncCompare(Document &) {}
} // namespace lp
