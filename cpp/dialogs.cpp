#include "app.h"
#include <dlgs.h>
namespace lp {
namespace {
constexpr int lineSpacingControl = 1800;
constexpr UINT refreshFontSample = WM_APP + 16;
constexpr auto fontDialogProperty = L"LHPad.FontDialog";
struct FontDialogState {
    int lineSpacing;
};
}
static UINT_PTR CALLBACK editorFontHook(HWND dialog, UINT message, WPARAM w, LPARAM l) {
    auto *state = reinterpret_cast<FontDialogState *>(GetPropW(dialog, fontDialogProperty));
    if (message == WM_INITDIALOG) {
        auto *choice = reinterpret_cast<CHOOSEFONTW *>(l);
        state = reinterpret_cast<FontDialogState *>(choice->lCustData);
        SetPropW(dialog, fontDialogProperty, state);
        // Syntax highlighting owns bold/italic; the picker changes family and size.
        EnableWindow(GetDlgItem(dialog, cmb2), FALSE);
        EnableWindow(GetDlgItem(dialog, stc2), FALSE);
        int dpi = GetDpiForWindow(dialog);
        auto scale = [dpi](int value) { return MulDiv(value, dpi, 96); };
        RECT field{};
        GetWindowRect(GetDlgItem(dialog, cmb1), &field);
        MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT *>(&field), 2);
        auto font = SendMessageW(GetDlgItem(dialog, cmb1), WM_GETFONT, 0, 0);
        auto label = CreateWindowExW(0, L"STATIC", L"추가 줄 간격 (&L):", WS_CHILD | WS_VISIBLE,
                                     field.left, field.bottom + scale(14), field.right - field.left,
                                     scale(18), dialog, nullptr, nullptr, nullptr);
        auto spacing = CreateWindowExW(0, WC_COMBOBOXW, nullptr,
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                                       field.left, field.bottom + scale(36), field.right - field.left,
                                       scale(180), dialog,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(lineSpacingControl)),
                                       nullptr, nullptr);
        auto hint = CreateWindowExW(0, L"STATIC", L"0 = 글꼴 기본 간격", WS_CHILD | WS_VISIBLE,
                                    field.left, field.bottom + scale(66), field.right - field.left,
                                    scale(20), dialog, nullptr, nullptr, nullptr);
        for (auto control : {label, spacing, hint})
            SendMessageW(control, WM_SETFONT, font, TRUE);
        for (int extra = 0; extra <= 24; extra++) {
            auto text = std::to_wstring(extra) + L" px";
            SendMessageW(spacing, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
        }
        SendMessageW(spacing, CB_SETCURSEL, state->lineSpacing, 0);
        // Use the built-in sample area for two rows so the spacing is visible.
        auto sample = GetDlgItem(dialog, stc6);
        SetWindowLongPtrW(sample, GWL_STYLE,
                          (GetWindowLongPtrW(sample, GWL_STYLE) & ~SS_TYPEMASK) | SS_OWNERDRAW);
        RECT group{};
        GetWindowRect(GetDlgItem(dialog, grp2), &group);
        MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT *>(&group), 2);
        MoveWindow(sample, group.left + scale(8), group.top + scale(20),
                   group.right - group.left - scale(16), group.bottom - group.top - scale(28), TRUE);
    } else if (state && message == WM_COMMAND) {
        if (LOWORD(w) == lineSpacingControl && HIWORD(w) == CBN_SELCHANGE)
            state->lineSpacing = static_cast<int>(SendDlgItemMessageW(dialog, lineSpacingControl,
                                                                      CB_GETCURSEL, 0, 0));
        if (HIWORD(w) == CBN_SELCHANGE || HIWORD(w) == CBN_EDITCHANGE)
            PostMessageW(dialog, refreshFontSample, 0, 0);
    } else if (state && message == refreshFontSample) {
        InvalidateRect(GetDlgItem(dialog, stc6), nullptr, TRUE);
    } else if (state && message == WM_DRAWITEM && w == stc6) {
        auto *draw = reinterpret_cast<DRAWITEMSTRUCT *>(l);
        LOGFONTW selected{};
        SendMessageW(dialog, WM_CHOOSEFONT_GETLOGFONT, 0, reinterpret_cast<LPARAM>(&selected));
        auto font = CreateFontIndirectW(&selected);
        int saved = SaveDC(draw->hDC);
        SelectObject(draw->hDC, font);
        FillRect(draw->hDC, &draw->rcItem, GetSysColorBrush(COLOR_BTNFACE));
        SetBkMode(draw->hDC, TRANSPARENT);
        SetTextColor(draw->hDC, GetSysColor(COLOR_BTNTEXT));
        TEXTMETRICW metrics{};
        GetTextMetricsW(draw->hDC, &metrics);
        int extra = MulDiv(state->lineSpacing, GetDpiForWindow(dialog), 96);
        int height = metrics.tmHeight * 2 + extra;
        int y = draw->rcItem.top + std::max(0L, (draw->rcItem.bottom - draw->rcItem.top - height) / 2);
        IntersectClipRect(draw->hDC, draw->rcItem.left, draw->rcItem.top,
                          draw->rcItem.right, draw->rcItem.bottom);
        for (int row = 0; row < 2; row++) {
            RECT line{draw->rcItem.left, y, draw->rcItem.right, y + metrics.tmHeight};
            DrawTextW(draw->hDC, L"가나다 AaBb 0123", -1, &line,
                       DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
            y += metrics.tmHeight + extra;
        }
        RestoreDC(draw->hDC, saved);
        DeleteObject(font);
        return TRUE;
    } else if (message == WM_DESTROY) {
        RemovePropW(dialog, fontDialogProperty);
    }
    return 0;
}
void App::chooseEditorFont() {
    LOGFONTW selected{};
    selected.lfHeight = -MulDiv(editorFontSize, GetDpiForWindow(window), 7200);
    selected.lfWeight = FW_NORMAL;
    selected.lfCharSet = DEFAULT_CHARSET;
    wcsncpy_s(selected.lfFaceName, wide(editorFont).c_str(), _TRUNCATE);
    CHOOSEFONTW choice{sizeof(choice)};
    choice.hwndOwner = window;
    choice.lpLogFont = &selected;
    choice.iPointSize = editorFontSize / 10;
    choice.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_FORCEFONTEXIST | CF_LIMITSIZE |
                   CF_NOVERTFONTS | CF_NOSCRIPTSEL | CF_ENABLEHOOK;
    choice.lpfnHook = editorFontHook;
    FontDialogState state{editorLineSpacing};
    choice.lCustData = reinterpret_cast<LPARAM>(&state);
    choice.nSizeMin = 6;
    choice.nSizeMax = 72;
    if (!ChooseFontW(&choice)) {
        if (auto error = CommDlgExtendedError())
            fail(L"글꼴 선택 창을 열지 못했습니다. 오류: " + std::to_wstring(error));
        return;
    }
    editorFont = narrow(selected.lfFaceName);
    editorFontSize = std::clamp(choice.iPointSize * 10, 600, 7200);
    editorLineSpacing = std::clamp(state.lineSpacing, 0, 24);
    for (const auto &d : docs)
        if (d->editor)
            applyEditorFont(d->editor);
    if (compareWindow)
        for (int id : {501, 502})
            if (auto editor = GetDlgItem(compareWindow, id))
                applyEditorFont(editor);
    backup();
}
struct InputState {
    HWND window = nullptr;
    HFONT font = nullptr;
    std::vector<Field> fields;
    std::vector<HWND> controls;
    std::wstring title, explanation;
    bool done = false, ok = false;
    int dpi = 96;
};
static LRESULT CALLBACK inputProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto *s = reinterpret_cast<InputState *>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE) {
        s = static_cast<InputState *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
        s->window = h;
    }
    if (!s)
        return DefWindowProcW(h, m, w, l);
    switch (m) {
    case WM_CTLCOLORSTATIC:
        SetBkMode(reinterpret_cast<HDC>(w), TRANSPARENT);
        SetTextColor(reinterpret_cast<HDC>(w), RGB(43, 52, 63));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    case WM_CREATE: {
        s->dpi = GetDpiForWindow(h);
        auto scale = [&](int x) { return MulDiv(x, s->dpi, 96); };
        LOGFONTW lf{};
        lf.lfHeight = -scale(13);
        wcscpy_s(lf.lfFaceName, L"맑은 고딕");
        s->font = CreateFontIndirectW(&lf);
        int y = 20;
        auto control = [&](const wchar_t *cls, const std::wstring &text, DWORD style, int id, int x, int y,
                           int width, int height) {
            auto c = CreateWindowExW(cls == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0, cls, text.c_str(),
                                     WS_CHILD | WS_VISIBLE | style, scale(x), scale(y), scale(width),
                                     scale(height), h, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                     GetModuleHandleW(nullptr), nullptr);
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(s->font), TRUE);
            return c;
        };
        if (!s->explanation.empty()) {
            control(L"EDIT", s->explanation, ES_READONLY | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL, 0, 20,
                    y, 500, 104);
            y += 114;
        }
        int id = 100;
        for (auto &f : s->fields) {
            f.id = id++;
            if (f.check) {
                s->controls.push_back(
                    control(L"BUTTON", f.label, WS_TABSTOP | BS_AUTOCHECKBOX, f.id, 20, y, 500, 30));
                SendMessageW(s->controls.back(), BM_SETCHECK, f.checked ? BST_CHECKED : BST_UNCHECKED, 0);
                y += 40;
            } else {
                control(L"STATIC", f.label, SS_LEFT, 0, 20, y, 500, 23);
                y += 25;
                s->controls.push_back(
                    control(L"EDIT", f.value, WS_TABSTOP | ES_AUTOHSCROLL, f.id, 20, y, 500, 30));
                SendMessageW(s->controls.back(), EM_SETLIMITTEXT, 4 * 1024 * 1024, 0);
                y += 42;
            }
        }
        control(L"BUTTON", L"확인", WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK, 332, y + 8, 88, 32);
        control(L"BUTTON", L"취소", WS_TABSTOP | BS_PUSHBUTTON, IDCANCEL, 432, y + 8, 88, 32);
        RECT r{0, 0, scale(540), scale(y + 60)};
        AdjustWindowRectExForDpi(&r, static_cast<DWORD>(GetWindowLongPtrW(h, GWL_STYLE)), FALSE,
                                 WS_EX_DLGMODALFRAME, s->dpi);
        RECT owner{};
        GetWindowRect(GetWindow(h, GW_OWNER), &owner);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
        int width = r.right - r.left, height = r.bottom - r.top;
        int x = std::clamp((owner.left + owner.right - width) / 2, mi.rcWork.left,
                           std::max(mi.rcWork.left, mi.rcWork.right - width));
        int yy = std::clamp((owner.top + owner.bottom - height) / 2, mi.rcWork.top,
                            std::max(mi.rcWork.top, mi.rcWork.bottom - height));
        SetWindowPos(h, nullptr, x, yy, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(w) == IDOK) {
            for (size_t i = 0; i < s->fields.size(); i++) {
                auto &f = s->fields[i];
                if (f.check)
                    f.checked = SendMessageW(s->controls[i], BM_GETCHECK, 0, 0) == BST_CHECKED;
                else {
                    int n = GetWindowTextLengthW(s->controls[i]);
                    f.value.resize(n + 1);
                    GetWindowTextW(s->controls[i], f.value.data(), n + 1);
                    f.value.resize(n);
                }
            }
            s->ok = true;
            s->done = true;
            ShowWindow(h, SW_HIDE);
            return 0;
        }
        if (LOWORD(w) == IDCANCEL) {
            s->done = true;
            ShowWindow(h, SW_HIDE);
            return 0;
        }
        break;
    case WM_CLOSE:
        s->done = true;
        ShowWindow(h, SW_HIDE);
        return 0;
    case WM_DESTROY:
        if (s->font)
            DeleteObject(s->font);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
std::optional<std::vector<Field>> inputDialog(HWND owner, const std::wstring &title,
                                              std::vector<Field> fields, const std::wstring &explanation) {
    static bool registered = [] {
        WNDCLASSEXW c{sizeof(c)};
        c.lpfnWndProc = inputProc;
        c.hInstance = GetModuleHandleW(nullptr);
        c.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        c.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        c.lpszClassName = L"LHPad.Input";
        RegisterClassExW(&c);
        return true;
    }();
    (void)registered;
    InputState state;
    state.fields = std::move(fields);
    state.explanation = explanation;
    auto h = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, L"LHPad.Input", title.c_str(),
                             WS_POPUP | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 600, 400,
                             owner, nullptr, GetModuleHandleW(nullptr), &state);
    if (!h)
        fail(errorText());
    EnableWindow(owner, FALSE);
    ShowWindow(h, SW_SHOW);
    if (!state.controls.empty()) {
        SetFocus(state.controls[0]);
        if (!state.fields[0].check)
            SendMessageW(state.controls[0], EM_SETSEL, 0, -1);
    }
    MSG msg;
    while (!state.done) {
        auto result = GetMessageW(&msg, nullptr, 0, 0);
        if (result <= 0) {
            state.done = true;
            if (result == 0)
                PostQuitMessage(static_cast<int>(msg.wParam));
            break;
        }
        if (!IsDialogMessageW(h, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    DestroyWindow(h);
    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    if (state.ok)
        return state.fields;
    return {};
}
void App::message(const std::wstring &text, const std::wstring &title, UINT flags) {
    ask(text, title, flags);
}
int App::ask(const std::wstring &text, const std::wstring &title, UINT flags) {
    TASKDIALOGCONFIG c{sizeof(c)};
    c.hwndParent = window;
    c.hInstance = instance;
    c.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
    c.pszWindowTitle = title.c_str();
    c.pszContent = text.c_str();
    switch (flags & MB_TYPEMASK) {
    case MB_YESNOCANCEL:
        c.dwCommonButtons = TDCBF_YES_BUTTON | TDCBF_NO_BUTTON | TDCBF_CANCEL_BUTTON;
        c.nDefaultButton = IDCANCEL;
        break;
    case MB_YESNO:
        c.dwCommonButtons = TDCBF_YES_BUTTON | TDCBF_NO_BUTTON;
        c.nDefaultButton = IDNO;
        break;
    default:
        c.dwCommonButtons = TDCBF_OK_BUTTON;
        break;
    }
    UINT icon = flags & MB_ICONMASK;
    c.pszMainIcon = icon == MB_ICONERROR                                ? TD_ERROR_ICON
                    : icon == MB_ICONWARNING || icon == MB_ICONQUESTION ? TD_WARNING_ICON
                                                                        : TD_INFORMATION_ICON;
    int button = IDCANCEL;
    auto hr = TaskDialogIndirect(&c, &button, nullptr, nullptr);
    if (FAILED(hr))
        button = MessageBoxW(window, text.c_str(), title.c_str(), flags);
    return button;
}
struct AboutState {
    App *app;
    bool done = false;
    HICON icon = nullptr;
    HFONT heading = nullptr;
};
static LRESULT CALLBACK aboutProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto state = reinterpret_cast<AboutState *>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE) {
        state = static_cast<AboutState *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (!state)
        return DefWindowProcW(h, m, w, l);
    if (m == WM_CTLCOLORSTATIC) {
        SetBkMode(reinterpret_cast<HDC>(w), TRANSPARENT);
        SetTextColor(reinterpret_cast<HDC>(w), RGB(43, 52, 63));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }
    if (m == WM_CREATE) {
        int dpi = GetDpiForWindow(h);
        auto scale = [&](int n) { return MulDiv(n, dpi, 96); };
        auto control = [&](const wchar_t *cls, const wchar_t *text, DWORD style, int id, int x, int y,
                           int width, int height) {
            auto child = CreateWindowExW(
                0, cls, text, WS_CHILD | WS_VISIBLE | style, scale(x), scale(y), scale(width), scale(height),
                h, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), state->app->instance, nullptr);
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(state->app->font), TRUE);
            return child;
        };
        state->icon = static_cast<HICON>(
            LoadImageW(state->app->instance, MAKEINTRESOURCEW(1), IMAGE_ICON, scale(128), scale(128), 0));
        auto icon = control(L"STATIC", L"", SS_ICON | SS_CENTERIMAGE, 10, 176, 20, 128, 128);
        SendMessageW(icon, STM_SETIMAGE, IMAGE_ICON, reinterpret_cast<LPARAM>(state->icon));
        auto heading =
            control(L"STATIC", L"LHPad 1.1.2 · C++ 네이티브 x64", SS_CENTER, 11, 20, 168, 440, 32);
        LOGFONTW font{};
        GetObjectW(state->app->font, sizeof(font), &font);
        font.lfHeight = -scale(17);
        font.lfWeight = FW_BOLD;
        state->heading = CreateFontIndirectW(&font);
        SendMessageW(heading, WM_SETFONT, reinterpret_cast<WPARAM>(state->heading), TRUE);
        control(L"STATIC",
                L"Scintilla + Lexilla 텍스트·Hex 편집기\n.NET 런타임 설치가 필요하지 "
                L"않습니다.\nMarkdown·HTML·SVG 미리보기: WebView2\n\nGPL-3.0-or-later\nCtrl+N 새 문서 · "
                L"Ctrl+O 열기 · Ctrl+S 저장\nAlt+드래그 세로 선택 · Ctrl+클릭 다중 커서",
                SS_CENTER, 12, 20, 216, 440, 164);
        control(L"BUTTON", L"확인", WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK, 196, 398, 88, 32);
        RECT size{0, 0, scale(480), scale(450)};
        AdjustWindowRectExForDpi(&size, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME, dpi);
        RECT owner{};
        GetWindowRect(state->app->window, &owner);
        int width = size.right - size.left, height = size.bottom - size.top;
        SetWindowPos(h, nullptr, (owner.left + owner.right - width) / 2,
                     (owner.top + owner.bottom - height) / 2, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    if (m == WM_COMMAND && (LOWORD(w) == IDOK || LOWORD(w) == IDCANCEL) || m == WM_CLOSE) {
        state->done = true;
        return 0;
    }
    if (m == WM_DESTROY) {
        if (state->icon)
            DestroyIcon(state->icon);
        if (state->heading)
            DeleteObject(state->heading);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
void App::about() {
    WNDCLASSEXW cls{sizeof(cls)};
    cls.hInstance = instance;
    cls.lpfnWndProc = aboutProc;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    cls.lpszClassName = L"LHPad.About";
    RegisterClassExW(&cls);
    AboutState state{this};
    auto h = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, cls.lpszClassName, L"LHPad 정보",
                             WS_POPUP | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 600, 600,
                             window, nullptr, instance, &state);
    if (!h)
        fail(errorText());
    EnableWindow(window, FALSE);
    ShowWindow(h, SW_SHOW);
    SetFocus(GetDlgItem(h, IDOK));
    MSG message;
    while (!state.done) {
        auto messageResult = GetMessageW(&message, nullptr, 0, 0);
        if (messageResult <= 0) {
            if (messageResult == 0)
                PostQuitMessage(static_cast<int>(message.wParam));
            break;
        }
        if (!IsDialogMessageW(h, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    DestroyWindow(h);
    EnableWindow(window, TRUE);
    SetActiveWindow(window);
}
} // namespace lp
