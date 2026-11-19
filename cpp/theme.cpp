// Dark mode for the main window: title bar, menu bar and menus, toolbar, tabs, status bar and side
// panels. Editors and the Hex view take their colours from palette() (core.cpp).
#include "app.h"
#include <uxtheme.h>
#include <vssym32.h>
namespace lp {
namespace {
// Undocumented but stable since Windows 10 1903 (used by Notepad++, Explorer and others). Every call
// is optional: when an entry point is missing the window simply keeps the light system look.
enum class PreferredAppMode { Default, AllowDark, ForceDark, ForceLight };
using SetPreferredAppModeFn = PreferredAppMode(WINAPI *)(PreferredAppMode);
using FlushMenuThemesFn = void(WINAPI *)();
using DwmSetWindowAttributeFn = HRESULT(WINAPI *)(HWND, DWORD, LPCVOID, DWORD);
DWORD windowsBuild() {
    using RtlGetVersionFn = LONG(WINAPI *)(OSVERSIONINFOW *);
    static DWORD build = [] {
        OSVERSIONINFOW info{sizeof(info)};
        if (auto get = reinterpret_cast<RtlGetVersionFn>(
                reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"))))
            get(&info);
        return info.dwBuildNumber;
    }();
    return build;
}
void darkMenus(bool dark) {
    if (windowsBuild() < 18362)
        return;
    auto theme = GetModuleHandleW(L"uxtheme.dll");
    if (!theme)
        theme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!theme)
        return;
    auto setMode = reinterpret_cast<SetPreferredAppModeFn>(
        reinterpret_cast<void *>(GetProcAddress(theme, MAKEINTRESOURCEA(135))));
    auto flush =
        reinterpret_cast<FlushMenuThemesFn>(reinterpret_cast<void *>(GetProcAddress(theme, MAKEINTRESOURCEA(136))));
    if (setMode)
        setMode(dark ? PreferredAppMode::ForceDark : PreferredAppMode::ForceLight);
    if (flush)
        flush();
}
void darkTitleBar(HWND window, bool dark) {
    auto dwm = LoadLibraryExW(L"dwmapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!dwm)
        return;
    if (auto set = reinterpret_cast<DwmSetWindowAttributeFn>(
            reinterpret_cast<void *>(GetProcAddress(dwm, "DwmSetWindowAttribute")))) {
        BOOL value = dark;
        // DWMWA_USE_IMMERSIVE_DARK_MODE is 20 since Windows 10 20H1 and 19 before that.
        if (FAILED(set(window, 20, &value, sizeof(value))))
            set(window, 19, &value, sizeof(value));
    }
}
void themeList(HWND list) {
    if (!list)
        return;
    auto &p = palette();
    COLORREF back = p.dark ? p.panel : GetSysColor(COLOR_WINDOW), text = p.dark ? p.text : GetSysColor(COLOR_WINDOWTEXT);
    ListView_SetBkColor(list, back);
    ListView_SetTextBkColor(list, back);
    ListView_SetTextColor(list, text);
    SetWindowTheme(list, p.dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    if (auto header = ListView_GetHeader(list))
        SetWindowTheme(header, p.dark ? L"DarkMode_ItemsView" : nullptr, nullptr);
    InvalidateRect(list, nullptr, TRUE);
}
// Menu bar drawing messages sent by Windows when the bar is themed (see Notepad++ UAHMenuBar).
constexpr UINT WM_UAHDRAWMENU = 0x0091, WM_UAHDRAWMENUITEM = 0x0092;
struct UAHMENU {
    HMENU hmenu;
    HDC hdc;
    DWORD dwFlags;
};
struct UAHMENUITEMMETRICS {
    union {
        struct {
            DWORD cx, cy;
        } rgsizeBar[2];
        struct {
            DWORD cx, cy;
        } rgsizePopup[4];
    };
};
struct UAHMENUPOPUPMETRICS {
    DWORD rgcx[4];
    DWORD fUpdateMaxWidths : 2;
};
struct UAHMENUITEM {
    int iPosition;
    UAHMENUITEMMETRICS umim;
    UAHMENUPOPUPMETRICS umpm;
};
struct UAHDRAWMENUITEM {
    DRAWITEMSTRUCT dis;
    UAHMENU um;
    UAHMENUITEM umi;
};
} // namespace
HBRUSH paletteBrush(COLORREF colour) {
    static std::map<COLORREF, HBRUSH> brushes; // A handful of palette colours; kept for the process.
    auto &brush = brushes[colour];
    if (!brush)
        brush = CreateSolidBrush(colour);
    return brush;
}
void themeDialog(HWND dialog) {
    if (!dialog || !IsWindow(dialog))
        return;
    bool dark = palette().dark;
    darkTitleBar(dialog, dark);
    EnumChildWindows(
        dialog,
        [](HWND child, LPARAM value) -> BOOL {
            bool darkChild = value != 0;
            wchar_t cls[64]{};
            GetClassNameW(child, cls, 64);
            auto style = GetWindowLongPtrW(child, GWL_STYLE) & BS_TYPEMASK;
            if (!_wcsicmp(cls, L"Button")) {
                bool box = style == BS_CHECKBOX || style == BS_AUTOCHECKBOX || style == BS_RADIOBUTTON ||
                           style == BS_AUTORADIOBUTTON || style == BS_3STATE || style == BS_AUTO3STATE ||
                           style == BS_GROUPBOX;
                // Themed check boxes ignore WM_CTLCOLOR text colours; the classic look honours them.
                if (box)
                    SetWindowTheme(child, darkChild ? L"" : nullptr, darkChild ? L"" : nullptr);
                else
                    SetWindowTheme(child, darkChild ? L"DarkMode_Explorer" : nullptr, nullptr);
            } else if (!_wcsicmp(cls, L"Edit") || !_wcsicmp(cls, L"ComboBox"))
                SetWindowTheme(child, darkChild ? L"DarkMode_CFD" : nullptr, nullptr);
            else if (!_wcsicmp(cls, WC_LISTVIEWW))
                themeList(child);
            return TRUE;
        },
        dark);
    RedrawWindow(dialog, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}
bool dialogColours(UINT message, WPARAM w, LPARAM l, LRESULT &result) {
    auto &p = palette();
    if (!p.dark)
        return false;
    auto dc = reinterpret_cast<HDC>(w);
    switch (message) {
    case WM_ERASEBKGND: {
        RECT r{};
        GetClipBox(dc, &r); // Works for window and memory DCs alike.
        FillRect(dc, &r, paletteBrush(p.window));
        result = 1;
        return true;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG:
        SetTextColor(dc, p.text);
        SetBkColor(dc, p.window);
        result = reinterpret_cast<LRESULT>(paletteBrush(p.window));
        return true;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        SetTextColor(dc, p.text);
        SetBkColor(dc, p.editorBack);
        result = reinterpret_cast<LRESULT>(paletteBrush(p.editorBack));
        return true;
    }
    (void)l;
    return false;
}
bool App::darkMenuMessage(UINT message, WPARAM w, LPARAM l, LRESULT &result) {
    if (!palette().dark)
        return false;
    auto &p = palette();
    if (message == WM_UAHDRAWMENU) {
        auto *menuInfo = reinterpret_cast<UAHMENU *>(l);
        MENUBARINFO bar{sizeof(bar)};
        GetMenuBarInfo(window, OBJID_MENU, 0, &bar);
        RECT frame{};
        GetWindowRect(window, &frame);
        RECT r = bar.rcBar;
        OffsetRect(&r, -frame.left, -frame.top);
        r.top -= 1;
        auto brush = CreateSolidBrush(p.window);
        FillRect(menuInfo->hdc, &r, brush);
        DeleteObject(brush);
        result = 0;
        return true;
    }
    if (message == WM_UAHDRAWMENUITEM) {
        auto *item = reinterpret_cast<UAHDRAWMENUITEM *>(l);
        wchar_t text[256]{};
        MENUITEMINFOW info{sizeof(info)};
        info.fMask = MIIM_STRING;
        info.dwTypeData = text;
        info.cch = static_cast<UINT>(std::size(text) - 1);
        GetMenuItemInfoW(item->um.hmenu, static_cast<UINT>(item->umi.iPosition), TRUE, &info);
        auto state = item->dis.itemState;
        bool hot = state & (ODS_HOTLIGHT | ODS_SELECTED), disabled = state & (ODS_INACTIVE | ODS_DISABLED | ODS_GRAYED);
        auto brush = CreateSolidBrush(hot ? p.hot : p.window);
        FillRect(item->um.hdc, &item->dis.rcItem, brush);
        DeleteObject(brush);
        SetBkMode(item->um.hdc, TRANSPARENT);
        SetTextColor(item->um.hdc, disabled ? p.muted : p.text);
        DrawTextW(item->um.hdc, text, -1, &item->dis.rcItem,
                  DT_CENTER | DT_SINGLELINE | DT_VCENTER | (state & ODS_NOACCEL ? DT_HIDEPREFIX : 0));
        result = 0;
        return true;
    }
    if (message == WM_NCPAINT || message == WM_NCACTIVATE) {
        result = DefWindowProcW(window, message, w, l);
        // Cover the light separator line Windows draws under the menu bar.
        MENUBARINFO bar{sizeof(bar)};
        if (!GetMenuBarInfo(window, OBJID_MENU, 0, &bar))
            return true;
        RECT client{}, frame{};
        GetClientRect(window, &client);
        MapWindowPoints(window, nullptr, reinterpret_cast<POINT *>(&client), 2);
        GetWindowRect(window, &frame);
        OffsetRect(&client, -frame.left, -frame.top);
        RECT line = client;
        line.bottom = line.top;
        line.top--;
        auto dc = GetWindowDC(window);
        auto brush = CreateSolidBrush(p.window);
        FillRect(dc, &line, brush);
        DeleteObject(brush);
        ReleaseDC(window, dc);
        return true;
    }
    return false;
}
void App::paintTabs(HDC dc) {
    RECT client{};
    GetClientRect(tabs, &client);
    auto brush = CreateSolidBrush(palette().window);
    FillRect(dc, &client, brush);
    DeleteObject(brush);
    for (int i = 0; i < TabCtrl_GetItemCount(tabs); i++) {
        DRAWITEMSTRUCT draw{};
        draw.CtlType = ODT_TAB;
        draw.itemID = static_cast<UINT>(i);
        draw.hDC = dc;
        draw.hwndItem = tabs;
        TabCtrl_GetItemRect(tabs, i, &draw.rcItem);
        drawTabItem(draw);
    }
}
void App::paintStatus(HDC dc) {
    auto &p = palette();
    RECT client{};
    GetClientRect(status, &client);
    auto brush = CreateSolidBrush(p.window);
    FillRect(dc, &client, brush);
    DeleteObject(brush);
    int parts[8]{};
    int count = static_cast<int>(SendMessageW(status, SB_GETPARTS, 8, reinterpret_cast<LPARAM>(parts)));
    auto old = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    auto pen = CreatePen(PS_SOLID, 1, p.border);
    auto oldPen = SelectObject(dc, pen);
    MoveToEx(dc, 0, 0, nullptr);
    LineTo(dc, client.right, 0);
    int left = 0;
    for (int i = 0; i < count; i++) {
        int right = parts[i] < 0 ? client.right : parts[i];
        auto length = LOWORD(SendMessageW(status, SB_GETTEXTLENGTHW, i, 0));
        std::wstring text(length + 1, L'\0');
        SendMessageW(status, SB_GETTEXTW, i, reinterpret_cast<LPARAM>(text.data()));
        text.resize(length);
        RECT r{left + MulDiv(6, dpi, 96), client.top, right - MulDiv(4, dpi, 96), client.bottom};
        SetTextColor(dc, i == 0 ? p.text : p.muted);
        DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &r, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        if (i + 1 < count) {
            MoveToEx(dc, right - 1, client.top + MulDiv(5, dpi, 96), nullptr);
            LineTo(dc, right - 1, client.bottom - MulDiv(5, dpi, 96));
        }
        left = right;
    }
    SelectObject(dc, oldPen);
    DeleteObject(pen);
    SelectObject(dc, old);
}
void App::applyDarkMode() {
    setDarkMode(darkMode);
    darkMenus(darkMode);
    darkTitleBar(window, darkMode);
    rebuildToolbarImages();
    themeList(resultsList);
    themeList(outlineList);
    for (auto &doc : docs) {
        for (auto view : views(*doc)) {
            languages.recolor(view, doc->language, doc->large);
            applyEditorFont(view);
        }
        if (doc->hexWindow)
            InvalidateRect(doc->hexWindow, nullptr, TRUE);
    }
    themeDialog(findWindow);
    themeDialog(compareWindow);
    if (compareWindow)
        for (int id : {501, 502})
            if (auto editor = GetDlgItem(compareWindow, id)) {
                sci(editor, SCI_STYLESETFORE, STYLE_DEFAULT, palette().editorText);
                sci(editor, SCI_STYLESETBACK, STYLE_DEFAULT, palette().editorBack);
                sci(editor, SCI_STYLECLEARALL);
                Languages::theme(editor);
                applyEditorFont(editor);
            }
    DrawMenuBar(window);
    SetWindowPos(window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
    update();
}
} // namespace lp
