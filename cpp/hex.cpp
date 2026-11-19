#include "app.h"
#include <windowsx.h>
#include <iomanip>
namespace lp {
struct HexMetrics {
    int cell, row, header, hexX, asciiX, inspectorX, rows;
};
static HexMetrics metrics(HWND h, HFONT font) {
    HDC dc = GetDC(h);
    auto old = SelectObject(dc, font);
    TEXTMETRICW t{};
    GetTextMetricsW(dc, &t);
    SelectObject(dc, old);
    ReleaseDC(h, dc);
    RECT r{};
    GetClientRect(h, &r);
    int cell = std::max(6L, t.tmAveCharWidth), row = std::max(18L, t.tmHeight + 5),
        header = MulDiv(38, GetDpiForWindow(h), 96), hexX = cell * 19, asciiX = hexX + cell * 49;
    return {cell,
            row,
            header,
            hexX,
            asciiX,
            std::max<int>(asciiX + cell * 18, r.right - MulDiv(290, GetDpiForWindow(h), 96)),
            std::max<int>(1, (r.bottom - header) / row)};
}
void App::hexEnsureVisible(Document &d) {
    auto m = metrics(d.hexWindow, mono);
    Pos row = d.caret / 16;
    if (row < d.firstRow)
        d.firstRow = row;
    if (row >= d.firstRow + m.rows)
        d.firstRow = row - m.rows + 1;
    Pos max = std::max<Pos>(0, (d.binary->size() + 15) / 16 - m.rows + 1);
    d.firstRow = std::clamp<Pos>(d.firstRow, 0, max);
    SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS};
    info.nMin = 0;
    info.nMax = max ? 1000000 : 0;
    info.nPage = 1;
    info.nPos = max ? static_cast<int>(static_cast<double>(d.firstRow) / max * 1000000) : 0;
    SetScrollInfo(d.hexWindow, SB_VERT, &info, TRUE);
}
void App::hexChanged(Document &d) {
    d.revision++;
    d.caret = std::clamp<Pos>(d.caret, 0, d.binary->size());
    d.anchor = std::clamp<Pos>(d.anchor, 0, d.binary->size());
    hexEnsureVisible(d);
    InvalidateRect(d.hexWindow, nullptr, FALSE);
    update();
}
void App::paintHex(Document &d, HDC dc) {
    RECT area{};
    GetClientRect(d.hexWindow, &area);
    HBRUSH bg = CreateSolidBrush(palette().hexBack);
    FillRect(dc, &area, bg);
    DeleteObject(bg);
    if (!d.binary)
        return;
    auto m = metrics(d.hexWindow, mono);
    auto old = SelectObject(dc, mono);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, palette().hexText);
    auto text = [&](int x, int y, const std::wstring &s) {
        TextOutW(dc, x, y, s.c_str(), static_cast<int>(s.size()));
    };
    HBRUSH header = CreateSolidBrush(palette().hexHeader);
    RECT top{0, 0, area.right, m.header - 3};
    FillRect(dc, &top, header);
    DeleteObject(header);
    text(m.cell, 8, L"Offset");
    for (int col = 0; col < 16; col++) {
        wchar_t s[4];
        swprintf_s(s, L"%02X", col);
        text(m.hexX + col * 3 * m.cell, 8, s);
    }
    text(m.asciiX, 8, L"Decoded text");
    Pos offset = d.firstRow * 16;
    auto bytes = d.binary->read(
        std::min(offset, d.binary->size()),
        std::min<Pos>(static_cast<Pos>(m.rows) * 16, std::max<Pos>(0, d.binary->size() - offset)));
    auto a = std::min(d.caret, d.anchor), b = std::max(d.caret, d.anchor);
    for (int row = 0; row < m.rows; row++) {
        Pos pos = offset + row * 16;
        if (pos > d.binary->size())
            break;
        int y = m.header + row * m.row;
        wchar_t addr[32];
        swprintf_s(addr, L"%016llX", static_cast<unsigned long long>(pos));
        SetTextColor(dc, palette().hexOffset);
        text(m.cell, y, addr);
        for (int col = 0; col < 16; col++) {
            Pos index = pos + col;
            if (index > d.binary->size())
                break;
            bool selected = index >= a && index < b, caret = index == d.caret && GetFocus() == d.hexWindow;
            int x = m.hexX + col * 3 * m.cell, ax = m.asciiX + col * m.cell;
            if (selected || caret) {
                HBRUSH selection = CreateSolidBrush(selected ? palette().hexSelection : palette().hexSelectionOther);
                RECT r{x - 1, y - 1, x + 2 * m.cell + 1, y + m.row - 2},
                    c{ax - 1, y - 1, ax + m.cell, y + m.row - 2};
                FillRect(dc, &r, selection);
                FillRect(dc, &c, selection);
                DeleteObject(selection);
            }
            if (index == d.binary->size())
                continue;
            size_t at = static_cast<size_t>(row * 16 + col);
            if (at >= bytes.size())
                break;
            auto v = bytes[at];
            wchar_t hex[4];
            swprintf_s(hex, L"%02X", v);
            SetTextColor(dc, d.binary->changed(index) ? palette().hexChanged : palette().hexText);
            text(x, y, hex);
            std::wstring chr;
            if (v < 32 || v == 127)
                chr = L".";
            else {
                char c = static_cast<char>(v);
                chr = wide(std::string_view(&c, 1), 1252, false);
            }
            text(ax, y, chr);
        }
    }
    HBRUSH inspector = CreateSolidBrush(palette().hexInspector);
    RECT ir{m.inspectorX, 0, area.right, area.bottom};
    FillRect(dc, &ir, inspector);
    DeleteObject(inspector);
    SetTextColor(dc, palette().hexText);
    int x = m.inspectorX + 12, y = 12;
    text(x, y, d.bigEndian ? L"☑ Big endian" : L"☐ Big endian");
    y += m.row * 2;
    text(x, y, L"Offset: " + std::to_wstring(d.caret));
    y += m.row * 2;
    auto values =
        d.binary->read(std::min(d.caret, d.binary->size()), std::min<Pos>(8, d.binary->size() - d.caret));
    auto integer = [&](int n) {
        uint64_t v = 0;
        for (int i = 0; i < n; i++)
            v |= static_cast<uint64_t>(values[i]) << (d.bigEndian ? (n - 1 - i) * 8 : i * 8);
        return v;
    };
    for (int n : {1, 2, 4, 8}) {
        std::wstring value = values.size() < static_cast<size_t>(n) ? L"—" : std::to_wstring(integer(n));
        text(x, y, L"UInt" + std::to_wstring(n * 8) + L"  " + value);
        y += m.row;
        if (values.size() >= static_cast<size_t>(n)) {
            uint64_t v = integer(n);
            int64_t signedValue =
                n == 8 ? static_cast<int64_t>(v) : static_cast<int64_t>(v << (64 - n * 8)) >> (64 - n * 8);
            text(x, y, L"Int" + std::to_wstring(n * 8) + L"   " + std::to_wstring(signedValue));
        } else
            text(x, y, L"Int" + std::to_wstring(n * 8) + L"   —");
        y += m.row;
    }
    for (int n : {4, 8}) {
        std::wstring value = L"—";
        if (values.size() >= static_cast<size_t>(n)) {
            auto bits = integer(n);
            if (n == 4) {
                uint32_t bits32 = static_cast<uint32_t>(bits);
                float f;
                memcpy(&f, &bits32, 4);
                value = std::to_wstring(f);
            } else {
                double f;
                memcpy(&f, &bits, 8);
                value = std::to_wstring(f);
            }
        }
        text(x, y, (n == 4 ? L"Float32 " : L"Float64 ") + value);
        y += m.row;
    }
    y += m.row;
    text(x, y, d.insert ? L"삽입 모드 (Insert)" : L"덮어쓰기 모드 (Insert)");
    y += m.row;
    text(x, y, d.ascii ? L"문자 영역 (Tab)" : L"Hex 영역 (Tab)");
    y += m.row * 2;
    text(x, y, L"Ctrl+G 오프셋 이동");
    y += m.row;
    text(x, y, L"Ctrl+F 검색");
    SelectObject(dc, old);
}
static void clipboard(const std::wstring &text, HWND owner) {
    if (!OpenClipboard(owner))
        fail(L"클립보드를 열 수 없습니다.");
    EmptyClipboard();
    auto memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    if (!memory) {
        CloseClipboard();
        fail(L"메모리가 부족합니다.");
    }
    auto p = GlobalLock(memory);
    memcpy(p, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
    GlobalUnlock(memory);
    if (!SetClipboardData(CF_UNICODETEXT, memory))
        GlobalFree(memory);
    CloseClipboard();
}
static std::wstring paste(HWND owner) {
    if (!OpenClipboard(owner))
        fail(L"클립보드를 열 수 없습니다.");
    std::wstring text;
    auto memory = GetClipboardData(CF_UNICODETEXT);
    if (memory) {
        auto p = static_cast<const wchar_t *>(GlobalLock(memory));
        if (p)
            text = p;
        GlobalUnlock(memory);
    }
    CloseClipboard();
    return text;
}
void App::hexInput(Document &d, UINT message, WPARAM w, LPARAM l) {
    if (!d.binary)
        return;
    auto m = metrics(d.hexWindow, mono);
    auto move = [&](Pos p, bool extend) {
        d.caret = std::clamp<Pos>(p, 0, d.binary->size());
        if (!extend)
            d.anchor = d.caret;
        d.lowNibble = false;
        hexEnsureVisible(d);
        InvalidateRect(d.hexWindow, nullptr, FALSE);
        update();
    };
    bool ctrl = GetKeyState(VK_CONTROL) < 0 || l == 1, shift = GetKeyState(VK_SHIFT) < 0;
    auto a = std::min(d.caret, d.anchor), b = std::max(d.caret, d.anchor);
    if (message == WM_KEYDOWN) {
        if (ctrl) {
            if (w == 'Z') {
                d.binary->undoEdit();
                hexChanged(d);
                return;
            }
            if (w == 'Y') {
                d.binary->redoEdit();
                hexChanged(d);
                return;
            }
            if (w == 'A') {
                d.anchor = 0;
                d.caret = d.binary->size();
                InvalidateRect(d.hexWindow, nullptr, FALSE);
                return;
            }
            if (w == 'C' || w == 'X') {
                if (b - a > 32 * 1024 * 1024)
                    fail(L"복사는 32 MiB 이하 영역에서 지원합니다.");
                auto bytes = d.binary->read(a, b - a);
                std::wstring text;
                if (d.ascii)
                    text = wide(std::string_view(reinterpret_cast<char *>(bytes.data()), bytes.size()), 1252,
                                false);
                else {
                    for (auto v : bytes) {
                        wchar_t s[4];
                        swprintf_s(s, L"%02X", v);
                        if (!text.empty())
                            text += L' ';
                        text += s;
                    }
                }
                clipboard(text, window);
                if (w == 'X' && b > a) {
                    d.binary->replace(a, b - a, {});
                    d.anchor = d.caret = a;
                    hexChanged(d);
                }
                return;
            }
            if (w == 'V') {
                auto text = paste(window);
                Bytes bytes;
                if (d.ascii) {
                    auto narrowText = narrow(text, 1252);
                    bytes.assign(narrowText.begin(), narrowText.end());
                } else {
                    SearchOptions previous = search;
                    search.query = narrow(text);
                    search.hexMode = 0;
                    try {
                        bytes = searchBytes();
                    } catch (...) {
                        search = previous;
                        throw;
                    }
                    search = previous;
                }
                Pos remove = b - a;
                if (!remove && !d.insert)
                    remove = std::min<Pos>(bytes.size(), d.binary->size() - a);
                d.binary->replace(a, remove, bytes);
                d.anchor = d.caret = a + bytes.size();
                hexChanged(d);
                return;
            }
        }
        switch (w) {
        case VK_LEFT:
            move(d.caret - 1, shift);
            return;
        case VK_RIGHT:
            move(d.caret + 1, shift);
            return;
        case VK_UP:
            move(d.caret - 16, shift);
            return;
        case VK_DOWN:
            move(d.caret + 16, shift);
            return;
        case VK_PRIOR:
            move(d.caret - m.rows * 16, shift);
            return;
        case VK_NEXT:
            move(d.caret + m.rows * 16, shift);
            return;
        case VK_HOME:
            move(ctrl ? 0 : d.caret / 16 * 16, shift);
            return;
        case VK_END:
            move(ctrl ? d.binary->size() : std::min<Pos>(d.binary->size(), d.caret / 16 * 16 + 15), shift);
            return;
        case VK_INSERT:
            d.insert = !d.insert;
            d.lowNibble = false;
            update();
            return;
        case VK_TAB:
            d.ascii = !d.ascii;
            d.lowNibble = false;
            InvalidateRect(d.hexWindow, nullptr, FALSE);
            update();
            return;
        case VK_DELETE:
        case VK_BACK:
            if (a == b) {
                if (w == VK_BACK && a > 0)
                    a--;
                else if (w == VK_DELETE && b < d.binary->size())
                    b++;
            }
            if (b > a) {
                d.binary->replace(a, b - a, {});
                d.caret = d.anchor = a;
                hexChanged(d);
            }
            return;
        }
    } else if (message == WM_CHAR && !ctrl && w >= 32) {
        if (d.ascii) {
            wchar_t c = static_cast<wchar_t>(w);
            auto encoded = narrow(std::wstring_view(&c, 1), 1252);
            Bytes bytes(encoded.begin(), encoded.end());
            d.binary->replace(a,
                              b > a      ? b - a
                              : d.insert ? 0
                                         : std::min<Pos>(bytes.size(), d.binary->size() - a),
                              bytes);
            d.caret = d.anchor = a + bytes.size();
            d.lowNibble = false;
        } else {
            int value = w >= '0' && w <= '9'   ? static_cast<int>(w - '0')
                        : w >= 'a' && w <= 'f' ? static_cast<int>(w - 'a' + 10)
                        : w >= 'A' && w <= 'F' ? static_cast<int>(w - 'A' + 10)
                                               : -1;
            if (value < 0)
                return;
            uint8_t old = a < d.binary->size() ? d.binary->read(a, 1)[0] : 0;
            if (d.insert && !d.lowNibble) {
                d.binary->replace(a, b - a, {static_cast<uint8_t>(value << 4)});
                d.anchor = d.caret = a;
                d.lowNibble = true;
            } else {
                uint8_t next =
                    static_cast<uint8_t>(d.lowNibble ? (old & 0xF0) | value : (value << 4) | (old & 15));
                d.binary->replace(a, b > a ? b - a : a < d.binary->size() ? 1 : 0, {next});
                if (d.lowNibble) {
                    d.caret = d.anchor = a + 1;
                    d.lowNibble = false;
                } else {
                    d.caret = d.anchor = a;
                    d.lowNibble = true;
                }
            }
        }
        hexChanged(d);
    }
}
LRESULT CALLBACK App::hexProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto *d = reinterpret_cast<Document *>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE) {
        d = static_cast<Document *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(d));
        d->hexWindow = h;
        DragAcceptFiles(h, TRUE);
    }
    if (!d || !app)
        return DefWindowProcW(h, m, w, l);
    try {
        switch (m) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            app->paintHex(*d, dc);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_GETDLGCODE:
            return DLGC_WANTALLKEYS;
        case WM_SIZE:
            if (d->binary)
                app->hexEnsureVisible(*d);
            return 0;
        case WM_KEYDOWN:
        case WM_CHAR:
            if (!app->busy && !app->playing)
                app->hexInput(*d, m, w, l);
            return 0;
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_MOUSEWHEEL:
            if (d->binary) {
                auto metricsValue = metrics(h, app->mono);
                Pos max = std::max<Pos>(0, (d->binary->size() + 15) / 16 - metricsValue.rows + 1);
                d->firstRow = std::clamp<Pos>(d->firstRow - GET_WHEEL_DELTA_WPARAM(w) / 120 * 3, 0, max);
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_VSCROLL:
            if (d->binary) {
                auto metricsValue = metrics(h, app->mono);
                Pos max = std::max<Pos>(0, (d->binary->size() + 15) / 16 - metricsValue.rows + 1);
                switch (LOWORD(w)) {
                case SB_LINEUP:
                    d->firstRow--;
                    break;
                case SB_LINEDOWN:
                    d->firstRow++;
                    break;
                case SB_PAGEUP:
                    d->firstRow -= metricsValue.rows;
                    break;
                case SB_PAGEDOWN:
                    d->firstRow += metricsValue.rows;
                    break;
                case SB_THUMBPOSITION:
                case SB_THUMBTRACK: {
                    SCROLLINFO info{sizeof(info), SIF_TRACKPOS};
                    GetScrollInfo(h, SB_VERT, &info);
                    d->firstRow = static_cast<Pos>(static_cast<double>(info.nTrackPos) / 1000000 * max);
                    break;
                }
                }
                d->firstRow = std::clamp<Pos>(d->firstRow, 0, max);
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONDOWN:
        case WM_MOUSEMOVE:
            if (d->binary && !app->busy) {
                auto met = metrics(h, app->mono);
                int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
                if (m == WM_LBUTTONDOWN && x >= met.inspectorX) {
                    if (y < met.row * 2) {
                        d->bigEndian = !d->bigEndian;
                        InvalidateRect(h, nullptr, FALSE);
                    }
                    return 0;
                }
                if (m == WM_LBUTTONDOWN) {
                    SetFocus(h);
                    SetCapture(h);
                    d->hexSelecting = true;
                    d->ascii = x >= met.asciiX;
                    for (size_t i = 0; i < app->docs.size(); i++)
                        if (app->docs[i].get() == d) {
                            app->active = static_cast<int>(i);
                            TabCtrl_SetCurSel(app->tabs, static_cast<int>(i));
                        }
                }
                if (!d->hexSelecting)
                    return 0;
                int col = std::clamp(
                    (x - (d->ascii ? met.asciiX : met.hexX)) / (met.cell * (d->ascii ? 1 : 3)), 0, 15);
                Pos row = d->firstRow + std::max(0, (y - met.header) / met.row);
                d->caret = std::min<Pos>(row * 16 + col, d->binary->size());
                if (m == WM_LBUTTONDOWN && GetKeyState(VK_SHIFT) >= 0)
                    d->anchor = d->caret;
                d->lowNibble = false;
                app->hexEnsureVisible(*d);
                InvalidateRect(h, nullptr, FALSE);
                app->update();
            }
            return 0;
        case WM_LBUTTONUP:
            d->hexSelecting = false;
            ReleaseCapture();
            return 0;
        case WM_DROPFILES:
            return App::childProc(h, m, w, l, 0, reinterpret_cast<DWORD_PTR>(app));
        }
    } catch (const std::exception &e) {
        app->message(wide(e.what()), L"Hex 편집", MB_OK | MB_ICONWARNING);
    }
    return DefWindowProcW(h, m, w, l);
}
} // namespace lp
