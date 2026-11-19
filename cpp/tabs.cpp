#include "app.h"
#include <windowsx.h>
#include <uxtheme.h>
#include <vssym32.h>
namespace lp {
static RECT visibleTabStrip(HWND tabs) {
    RECT visible{};
    GetClientRect(tabs, &visible);
    auto arrows = FindWindowExW(tabs, nullptr, UPDOWN_CLASSW, nullptr);
    if (arrows && IsWindowVisible(arrows)) {
        RECT r{};
        GetWindowRect(arrows, &r);
        MapWindowPoints(nullptr, tabs, reinterpret_cast<POINT *>(&r), 2);
        visible.right = std::min(visible.right, r.left);
    }
    return visible;
}
RECT App::tabCloseBounds(RECT item, int dpi) {
    int size = std::min<int>(MulDiv(22, dpi, 96), std::max(0L, item.bottom - item.top - MulDiv(4, dpi, 96)));
    int right = item.right - MulDiv(8, dpi, 96);
    int top = item.top + (item.bottom - item.top - size) / 2;
    return {right - size, top, right, top + size};
}
RECT App::tabCloseRect(int index) const {
    RECT item{};
    if (index < 0 || index >= static_cast<int>(docs.size()) || !TabCtrl_GetItemRect(tabs, index, &item))
        return {};
    auto close = tabCloseBounds(item, GetDpiForWindow(tabs));
    auto visible = visibleTabStrip(tabs);
    if (close.left < visible.left || close.right > visible.right || close.top < visible.top ||
        close.bottom > visible.bottom)
        return {};
    return close;
}
int App::tabCloseHit(POINT point) const {
    TCHITTESTINFO hit{point, 0};
    int index = TabCtrl_HitTest(tabs, &hit);
    if (index < 0)
        return -1;
    auto r = tabCloseRect(index);
    return PtInRect(&r, point) ? index : -1;
}
static void invalidateClose(App &a, int index) {
    auto r = a.tabCloseRect(index);
    if (!IsRectEmpty(&r))
        InvalidateRect(a.tabs, &r, FALSE);
}
void App::updateTabCloseHover(POINT point) {
    int index = busy || playing ? -1 : tabCloseHit(point);
    if (index == hotTabClose)
        return;
    int previous = hotTabClose;
    hotTabClose = index;
    invalidateClose(*this, previous);
    invalidateClose(*this, index);
}
void App::refreshTabCloseHover() {
    POINT point{};
    GetCursorPos(&point);
    if (WindowFromPoint(point) != tabs && GetCapture() != tabs) {
        updateTabCloseHover({-1, -1});
        return;
    }
    ScreenToClient(tabs, &point);
    updateTabCloseHover(point);
}
void App::drawTabItem(const DRAWITEMSTRUCT &draw) {
    int index = static_cast<int>(draw.itemID);
    if (index < 0 || index >= static_cast<int>(docs.size()))
        return;
    RECT r{};
    if (!TabCtrl_GetItemRect(tabs, index, &r))
        return;
    auto visible = visibleTabStrip(tabs);
    HDC dc = draw.hDC;
    int saved = SaveDC(dc);
    IntersectClipRect(dc, r.left, r.top, std::min(r.right, visible.right), r.bottom);
    bool selected = TabCtrl_GetCurSel(tabs) == index;
    auto theme = OpenThemeData(tabs, L"TAB");
    if (theme) {
        DrawThemeBackground(theme, dc, TABP_TABITEM, selected ? TIS_SELECTED : TIS_NORMAL, &r, nullptr);
        CloseThemeData(theme);
    } else
        FillRect(dc, &r, GetSysColorBrush(selected ? COLOR_WINDOW : COLOR_BTNFACE));
    int scale = GetDpiForWindow(tabs), inset = MulDiv(8, scale, 96), gap = MulDiv(8, scale, 96);
    int iconWidth = 0, iconHeight = 0;
    ImageList_GetIconSize(tabImages, &iconWidth, &iconHeight);
    int iconX = r.left + inset, iconY = r.top + (r.bottom - r.top - iconHeight) / 2;
    ImageList_Draw(tabImages, docs[index]->dirty() ? 1 : 0, dc, iconX, iconY, ILD_TRANSPARENT);
    auto close = tabCloseRect(index);
    RECT title{iconX + iconWidth + gap, r.top, IsRectEmpty(&close) ? r.right - inset : close.left - gap,
               r.bottom};
    auto name = wide((compareIds.contains(docs[index]->id) ? "✓ " : "") + docs[index]->name);
    SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    DrawTextW(dc, name.c_str(), static_cast<int>(name.size()), &title,
              DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (!IsRectEmpty(&close)) {
        bool hot = hotTabClose == index && !busy && !playing;
        bool pressed = hot && pressedTabClose == docs[index]->id;
        if (hot) {
            auto brush = CreateSolidBrush(pressed ? RGB(247, 181, 188) : RGB(255, 228, 231));
            auto pen = CreatePen(PS_SOLID, 1, pressed ? RGB(199, 92, 108) : RGB(230, 166, 174));
            auto oldBrush = SelectObject(dc, brush), oldPen = SelectObject(dc, pen);
            RoundRect(dc, close.left, close.top, close.right, close.bottom, MulDiv(6, scale, 96),
                      MulDiv(6, scale, 96));
            SelectObject(dc, oldBrush);
            SelectObject(dc, oldPen);
            DeleteObject(brush);
            DeleteObject(pen);
        }
        SetTextColor(dc, busy || playing ? RGB(168, 174, 181) : hot ? RGB(157, 45, 64) : RGB(82, 103, 117));
        DrawTextW(dc, L"×", 1, &close, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
    }
    RestoreDC(dc, saved);
}
bool App::tabCloseMessage(UINT message, WPARAM w, LPARAM l) {
    POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
    if (message == WM_MOUSEMOVE) {
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, tabs, 0};
        TrackMouseEvent(&tracking);
        updateTabCloseHover(point);
    } else if (message == WM_MOUSELEAVE) {
        int previous = hotTabClose;
        hotTabClose = -1;
        invalidateClose(*this, previous);
    } else if (message == WM_SETCURSOR && LOWORD(l) == HTCLIENT) {
        GetCursorPos(&point);
        ScreenToClient(tabs, &point);
        if (!busy && !playing && tabCloseHit(point) >= 0) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return true;
        }
    } else if (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK) {
        int index = tabCloseHit(point);
        if (index >= 0) {
            if (!busy && !playing) {
                pressedTabClose = docs[index]->id;
                SetCapture(tabs);
                updateTabCloseHover(point);
                invalidateClose(*this, index);
            }
            return true;
        }
    } else if (message == WM_LBUTTONUP && !pressedTabClose.empty()) {
        auto pressed = std::exchange(pressedTabClose, {});
        int index = tabCloseHit(point);
        int previous = hotTabClose;
        hotTabClose = -1;
        invalidateClose(*this, previous);
        if (GetCapture() == tabs)
            ReleaseCapture();
        if (index >= 0 && docs[index]->id == pressed && !busy && !playing)
            guard([&] { closeTab(index); });
        refreshTabCloseHover();
        return true;
    } else if (message == WM_CAPTURECHANGED || message == WM_CANCELMODE) {
        if (!pressedTabClose.empty()) {
            auto pressed = std::exchange(pressedTabClose, {});
            for (size_t i = 0; i < docs.size(); i++)
                if (docs[i]->id == pressed)
                    invalidateClose(*this, static_cast<int>(i));
            if (message == WM_CANCELMODE && GetCapture() == tabs)
                ReleaseCapture();
        }
    }
    (void)w;
    return false;
}
} // namespace lp
