#include "win_outline.h"

#include <algorithm>
#include <windowsx.h>

namespace app {
namespace {

const wchar_t* const kOutlineClass = L"SmvOutlinePanel";

// Padding used both when collapsed and expanded.
constexpr int kPadding = 10;
constexpr int kButtonSize = 28;
constexpr int kExpandedWidth = 280;
constexpr int kIndentPerLevel = 14;

void fillRect(HDC dc, const RECT& rect, COLORREF color) {
    SetDCBrushColor(dc, color);
    FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

} // namespace

bool OutlinePanel::registerWindowClass(HINSTANCE instance) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &OutlinePanel::windowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kOutlineClass;
    return RegisterClassExW(&wc) != 0;
}

HWND OutlinePanel::create(HWND parent, HINSTANCE instance) {
    hwnd_ = CreateWindowExW(0, kOutlineClass, nullptr, WS_CHILD | WS_VISIBLE, 0, 0, 10, 10,
                            parent, nullptr, instance, this);
    return hwnd_;
}

LRESULT CALLBACK OutlinePanel::windowProc(HWND hwnd, UINT message, WPARAM wParam,
                                          LPARAM lParam) {
    OutlinePanel* self =
        reinterpret_cast<OutlinePanel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<OutlinePanel*>(cs->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(hwnd, message, wParam, lParam);
    return self->handleMessage(message, wParam, lParam);
}

void OutlinePanel::setTheme(const Theme& theme) {
    theme_ = theme;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, TRUE);
}

void OutlinePanel::setUiFont(HFONT font) {
    font_ = font;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, TRUE);
}

void OutlinePanel::setDpi(int dpi) {
    dpi_ = dpi > 0 ? dpi : 96;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, TRUE);
}

void OutlinePanel::setEntries(const std::vector<view::OutlineEntry>& entries) {
    entries_ = entries;
    activeIndex_ = -1;
    hotItem_ = -1;
    scrollY_ = 0;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, TRUE);
}

void OutlinePanel::setActiveIndex(int index) {
    if (index == activeIndex_) return;
    activeIndex_ = index;
    if (hwnd_ && expanded_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void OutlinePanel::setExpanded(bool expanded) {
    if (expanded_ == expanded) return;
    expanded_ = expanded;
    scrollY_ = 0;
    hotItem_ = -1;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, TRUE);
}

int OutlinePanel::collapsedWidth() const {
    return scale(kPadding) * 2 + scale(kButtonSize);
}

int OutlinePanel::panelWidth() const {
    return expanded_ ? scale(kExpandedWidth) : collapsedWidth();
}

RECT OutlinePanel::buttonRect() const {
    int pad = scale(kPadding);
    int size = scale(kButtonSize);
    return RECT{pad, pad, pad + size, pad + size};
}

int OutlinePanel::itemHeight() const {
    return scale(kButtonSize) - scale(4);
}

int OutlinePanel::listTop() const {
    RECT button = buttonRect();
    return button.bottom + scale(kPadding);
}

int OutlinePanel::contentHeight() const {
    return static_cast<int>(entries_.size()) * itemHeight();
}

void OutlinePanel::clampScroll() {
    RECT client = {};
    GetClientRect(hwnd_, &client);
    int visible = std::max(0, static_cast<int>(client.bottom) - listTop() - scale(kPadding));
    int maxScroll = std::max(0, contentHeight() - visible);
    scrollY_ = std::max(0, std::min(scrollY_, maxScroll));
}

int OutlinePanel::hitTestItem(int x, int y) const {
    if (!expanded_ || entries_.empty()) return -1;
    RECT client = {};
    GetClientRect(hwnd_, &client);
    if (x < 0 || x > client.right) return -1;
    int top = listTop();
    if (y < top || y > client.bottom - scale(kPadding)) return -1;
    int index = (y - top + scrollY_) / itemHeight();
    if (index < 0 || index >= static_cast<int>(entries_.size())) return -1;
    return index;
}

LRESULT OutlinePanel::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps = {};
            HDC dc = BeginPaint(hwnd_, &ps);
            RECT client = {};
            GetClientRect(hwnd_, &client);

            HDC memory = CreateCompatibleDC(dc);
            HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
            HGDIOBJ previous = SelectObject(memory, bitmap);
            paint(memory, client);
            BitBlt(dc, 0, 0, client.right, client.bottom, memory, 0, 0, SRCCOPY);
            SelectObject(memory, previous);
            DeleteObject(bitmap);
            DeleteDC(memory);

            EndPaint(hwnd_, &ps);
            return 0;
        }

        case WM_SIZE:
            clampScroll();
            return 0;

        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
            RECT button = buttonRect();
            POINT p = {x, y};
            bool overButton = PtInRect(&button, p) != FALSE;
            int item = overButton ? -1 : hitTestItem(x, y);
            if (overButton != hotButton_ || item != hotItem_) {
                hotButton_ = overButton;
                hotItem_ = item;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            if (!tracking_) {
                TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, hwnd_, 0};
                TrackMouseEvent(&track);
                tracking_ = true;
            }
            return 0;
        }

        case WM_MOUSELEAVE:
            tracking_ = false;
            if (hotButton_ || hotItem_ >= 0) {
                hotButton_ = false;
                hotItem_ = -1;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;

        case WM_LBUTTONDOWN: {
            int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
            RECT button = buttonRect();
            POINT p = {x, y};
            if (PtInRect(&button, p)) {
                if (onToggle_) onToggle_();
                return 0;
            }
            int item = hitTestItem(x, y);
            if (item >= 0 && onSelect_) onSelect_(static_cast<size_t>(item));
            return 0;
        }

        case WM_MOUSEWHEEL: {
            if (!expanded_) return 0;
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            scrollY_ -= delta * itemHeight() * 3 / WHEEL_DELTA;
            clampScroll();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }

        case WM_SETCURSOR: {
            POINT p = {};
            GetCursorPos(&p);
            ScreenToClient(hwnd_, &p);
            RECT button = buttonRect();
            if (PtInRect(&button, p) || hitTestItem(p.x, p.y) >= 0) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            break;
        }

        default:
            break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

void OutlinePanel::drawChevron(HDC dc, const RECT& button, bool pointingRight) const {
    int centreX = (button.left + button.right) / 2;
    int centreY = (button.top + button.bottom) / 2;
    int reach = std::max(3, scale(5));
    int thickness = std::max(1, scale(2));

    HPEN pen = CreatePen(PS_SOLID, thickness, theme_.role(view::ColorRole::Text));
    HGDIOBJ previous = SelectObject(dc, pen);

    int tipX = pointingRight ? centreX + reach / 2 : centreX - reach / 2;
    int backX = pointingRight ? centreX - reach / 2 : centreX + reach / 2;
    POINT points[3] = {{backX, centreY - reach}, {tipX, centreY}, {backX, centreY + reach}};
    Polyline(dc, points, 3);

    SelectObject(dc, previous);
    DeleteObject(pen);
}

void OutlinePanel::paint(HDC dc, const RECT& client) {
    fillRect(dc, client, theme_.barBackground);

    // Separator on the edge that faces the document.
    RECT border = {client.right - std::max(1, scale(1)), 0, client.right, client.bottom};
    fillRect(dc, border, theme_.barBorder);

    RECT button = buttonRect();
    if (hotButton_) {
        fillRect(dc, button, theme_.buttonHot);
    }
    drawChevron(dc, button, !expanded_);

    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ previousFont = font_ ? SelectObject(dc, font_) : nullptr;

    if (expanded_) {
        RECT title = {button.right + scale(kPadding), button.top, client.right - scale(kPadding),
                      button.bottom};
        SetTextColor(dc, theme_.role(view::ColorRole::Muted));
        DrawTextW(dc, L"Outline", -1, &title,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        int top = listTop();
        RECT separator = {scale(kPadding), top - scale(kPadding) / 2,
                          client.right - scale(kPadding),
                          top - scale(kPadding) / 2 + std::max(1, scale(1))};
        fillRect(dc, separator, theme_.barBorder);

        if (entries_.empty()) {
            RECT empty = {scale(kPadding), top, client.right - scale(kPadding),
                          top + itemHeight()};
            SetTextColor(dc, theme_.role(view::ColorRole::Muted));
            DrawTextW(dc, L"No headings", -1, &empty,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }

        int bottomLimit = client.bottom - scale(kPadding);
        for (size_t i = 0; i < entries_.size(); ++i) {
            int y = top + static_cast<int>(i) * itemHeight() - scrollY_;
            if (y + itemHeight() < top || y > bottomLimit) continue;

            RECT row = {scale(kPadding) / 2, y, client.right - scale(kPadding) / 2,
                        y + itemHeight()};
            bool active = static_cast<int>(i) == activeIndex_;
            if (static_cast<int>(i) == hotItem_) {
                fillRect(dc, row, theme_.buttonHot);
            } else if (active) {
                fillRect(dc, row, theme_.role(view::ColorRole::CodeBg));
            }
            if (active) {
                RECT marker = {row.left, y + scale(3), row.left + std::max(2, scale(3)),
                               y + itemHeight() - scale(3)};
                fillRect(dc, marker, theme_.role(view::ColorRole::Link));
            }

            int indent = scale(kPadding) + (entries_[i].level - 1) * scale(kIndentPerLevel);
            RECT text = {indent, y, client.right - scale(kPadding), y + itemHeight()};
            SetTextColor(dc, active ? theme_.role(view::ColorRole::Link)
                                    : theme_.role(view::ColorRole::Text));
            DrawTextW(dc, entries_[i].text.c_str(), -1, &text,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
    }

    if (previousFont) SelectObject(dc, previousFont);
}

} // namespace app
