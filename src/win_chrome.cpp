#include "win_chrome.h"

#include <algorithm>

namespace app {
namespace {

// Logical sizes at 96 dpi. The caption is the standard 32px plus the four
// pixels of breathing room this window asks for, above and below.
constexpr int kCaptionPadding = 4;
constexpr int kCaptionContent = 32;
constexpr int kButtonWidth = 46;
constexpr int kButtonHeight = 32;
constexpr int kTitleLeft = 16;   // 12 + the extra padding
constexpr int kButtonsRight = 4; // extra padding before the window edge
constexpr int kMenuHeight = 30;
constexpr int kMenuItemPadding = 12;
constexpr int kMenuStripLeft = 8;
constexpr int kGrip = 12;

void fillRect(HDC dc, const RECT& rect, COLORREF color) {
    SetDCBrushColor(dc, color);
    FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

COLORREF blend(COLORREF a, COLORREF b, int percentB) {
    int inverse = 100 - percentB;
    return RGB((GetRValue(a) * inverse + GetRValue(b) * percentB) / 100,
               (GetGValue(a) * inverse + GetGValue(b) * percentB) / 100,
               (GetBValue(a) * inverse + GetBValue(b) * percentB) / 100);
}

} // namespace

// Codicon glyphs (the VS Code icon font, embedded as a resource).
constexpr wchar_t kGlyphSearch = 0xEA6D; // codicon "search"
constexpr wchar_t kGlyphEdit = 0xEA73;   // codicon "edit"
constexpr wchar_t kGlyphSave = 0xEB4B;   // codicon "save"

WindowChrome::~WindowChrome() {
    if (captionFont_) DeleteObject(captionFont_);
    if (menuFont_) DeleteObject(menuFont_);
    if (codiconFont_) DeleteObject(codiconFont_);
    if (appIcon_) DestroyIcon(appIcon_);
}

void WindowChrome::initialize(HWND frame, HMENU fileMenu, HMENU editMenu, HMENU viewMenu,
                              HMENU aboutMenu) {
    frame_ = frame;

    items_.clear();
    items_.push_back(MenuItem{L"&File", L'f', fileMenu, {}});
    items_.push_back(MenuItem{L"&Edit", L'e', editMenu, {}});
    items_.push_back(MenuItem{L"&View", L'v', viewMenu, {}});
    items_.push_back(MenuItem{L"&About", L'a', aboutMenu, {}});

    rebuildFonts();
    layoutMenuItems();
}

void WindowChrome::setTheme(const Theme& theme) {
    theme_ = theme;
    if (frame_) InvalidateRect(frame_, nullptr, FALSE);
}

void WindowChrome::setDpi(int dpi) {
    dpi_ = dpi > 0 ? dpi : 96;
    rebuildFonts();
    layoutMenuItems();
}

void WindowChrome::setTitle(const std::wstring& title) {
    title_ = title;
    if (frame_) InvalidateRect(frame_, nullptr, FALSE);
}

void WindowChrome::rebuildFonts() {
    if (captionFont_) DeleteObject(captionFont_);
    if (menuFont_) DeleteObject(menuFont_);
    if (codiconFont_) DeleteObject(codiconFont_);

    NONCLIENTMETRICSW metrics = {sizeof(metrics)};
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0,
                               static_cast<UINT>(dpi_));
    captionFont_ = CreateFontIndirectW(&metrics.lfCaptionFont);
    menuFont_ = CreateFontIndirectW(&metrics.lfMenuFont);

    // Codicon is designed on a 16px grid; the em box is the icon box.
    LOGFONTW lf = {};
    lf.lfHeight = -scale(16);
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcsncpy_s(lf.lfFaceName, L"codicon", _TRUNCATE);
    codiconFont_ = CreateFontIndirectW(&lf);

    // The application icon (resource id 1), sized for the caption strip.
    if (appIcon_) DestroyIcon(appIcon_);
    int iconSize = scale(16);
    appIcon_ = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1),
                                             IMAGE_ICON, iconSize, iconSize, 0));
}

int WindowChrome::captionHeight() const {
    return scale(kCaptionContent + kCaptionPadding * 2);
}

int WindowChrome::menuHeight() const { return scale(kMenuHeight); }

int WindowChrome::resizeBorder() const {
    return GetSystemMetricsForDpi(SM_CYFRAME, static_cast<UINT>(dpi_)) +
           GetSystemMetricsForDpi(SM_CXPADDEDBORDER, static_cast<UINT>(dpi_));
}

int WindowChrome::gripSize() const { return scale(kGrip); }

bool WindowChrome::maximized() const {
    if (!frame_) return false;
    WINDOWPLACEMENT placement = {sizeof(placement)};
    return GetWindowPlacement(frame_, &placement) && placement.showCmd == SW_SHOWMAXIMIZED;
}

void WindowChrome::layoutMenuItems() {
    if (!frame_ || items_.empty()) return;

    HDC dc = GetDC(frame_);
    HGDIOBJ previous = SelectObject(dc, menuFont_);

    int x = scale(kMenuStripLeft);
    int top = captionHeight();
    int bottom = top + menuHeight();
    for (MenuItem& item : items_) {
        std::wstring plain;
        for (wchar_t c : item.label) {
            if (c != L'&') plain.push_back(c);
        }
        SIZE size = {};
        GetTextExtentPoint32W(dc, plain.c_str(), static_cast<int>(plain.size()), &size);
        int width = static_cast<int>(size.cx) + scale(kMenuItemPadding) * 2;
        item.bounds = RECT{x, top, x + width, bottom};
        x += width;
    }

    SelectObject(dc, previous);
    ReleaseDC(frame_, dc);
}

RECT WindowChrome::buttonRect(int indexFromRight, const RECT& client) const {
    int width = scale(kButtonWidth);
    int height = scale(kButtonHeight);
    int top = (captionHeight() - height) / 2;
    int right = client.right - scale(kButtonsRight) - indexFromRight * width;
    return RECT{right - width, top, right, top + height};
}

LRESULT WindowChrome::hitTest(POINT point) const {
    RECT client = {};
    GetClientRect(frame_, &client);

    const bool isMaximized = maximized();
    const int border = resizeBorder();

    if (!isMaximized && point.y < border) {
        if (point.x < border * 2) return HTTOPLEFT;
        if (point.x > client.right - border * 2) return HTTOPRIGHT;
        return HTTOP;
    }

    RECT close = buttonRect(0, client);
    RECT maximize = buttonRect(1, client);
    RECT minimize = buttonRect(2, client);
    if (PtInRect(&close, point)) return HTCLOSE;
    if (PtInRect(&maximize, point)) return HTMAXBUTTON;
    if (PtInRect(&minimize, point)) return HTMINBUTTON;

    if (point.y < captionHeight()) {
        // The menu strip and title share the caption row's drag behaviour.
        return HTCAPTION;
    }

    if (!isMaximized) {
        RECT grip = gripRect(client);
        if (PtInRect(&grip, point)) return HTBOTTOMRIGHT;
    }
    return HTNOWHERE;
}

RECT WindowChrome::gripRect(const RECT& client) const {
    int size = gripSize();
    return RECT{client.right - size, client.bottom - size, client.right, client.bottom};
}

void WindowChrome::setHot(LRESULT hit) {
    if (hot_ == hit) return;
    hot_ = hit;
    if (frame_) InvalidateRect(frame_, nullptr, FALSE);
}

void WindowChrome::setPressed(LRESULT hit) {
    if (pressed_ == hit) return;
    pressed_ = hit;
    if (frame_) InvalidateRect(frame_, nullptr, FALSE);
}

void WindowChrome::clearHot() {
    if (hot_ == HTNOWHERE && pressed_ == HTNOWHERE) return;
    hot_ = HTNOWHERE;
    pressed_ = HTNOWHERE;
    if (frame_) InvalidateRect(frame_, nullptr, FALSE);
}

void WindowChrome::drawGlyph(HDC dc, const RECT& button, LRESULT which, COLORREF color) const {
    int centreX = (button.left + button.right) / 2;
    int centreY = (button.top + button.bottom) / 2;
    int half = std::max(4, scale(5));
    int thickness = std::max(1, scale(1));

    HPEN pen = CreatePen(PS_SOLID, thickness, color);
    HGDIOBJ previousPen = SelectObject(dc, pen);
    HGDIOBJ previousBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));

    if (which == HTMINBUTTON) {
        MoveToEx(dc, centreX - half, centreY, nullptr);
        LineTo(dc, centreX + half + 1, centreY);
    } else if (which == HTMAXBUTTON) {
        if (maximized()) {
            int offset = std::max(2, scale(2));
            Rectangle(dc, centreX - half, centreY - half + offset, centreX + half - offset,
                      centreY + half);
            MoveToEx(dc, centreX - half + offset, centreY - half + offset, nullptr);
            LineTo(dc, centreX - half + offset, centreY - half);
            LineTo(dc, centreX + half, centreY - half);
            LineTo(dc, centreX + half, centreY + half - offset);
            LineTo(dc, centreX + half - offset, centreY + half - offset);
        } else {
            Rectangle(dc, centreX - half, centreY - half, centreX + half, centreY + half);
        }
    } else if (which == HTCLOSE) {
        MoveToEx(dc, centreX - half, centreY - half, nullptr);
        LineTo(dc, centreX + half + 1, centreY + half + 1);
        MoveToEx(dc, centreX + half, centreY - half, nullptr);
        LineTo(dc, centreX - half - 1, centreY + half + 1);
    }

    SelectObject(dc, previousBrush);
    SelectObject(dc, previousPen);
    DeleteObject(pen);
}

void WindowChrome::drawCaptionButtons(HDC dc, const RECT& client, bool active) const {
    const LRESULT kButtons[3] = {HTCLOSE, HTMAXBUTTON, HTMINBUTTON};
    for (int i = 0; i < 3; ++i) {
        LRESULT which = kButtons[i];
        RECT bounds = buttonRect(i, client);

        COLORREF glyph = active ? theme_.captionText : theme_.captionTextInactive;
        bool isHot = hot_ == which;
        bool isPressed = pressed_ == which;

        if (isHot || isPressed) {
            if (which == HTCLOSE) {
                COLORREF red = RGB(196, 43, 28);
                fillRect(dc, bounds, isPressed ? blend(red, RGB(0, 0, 0), 20) : red);
                glyph = RGB(255, 255, 255);
            } else {
                fillRect(dc, bounds,
                         isPressed ? theme_.captionButtonPressed : theme_.captionButtonHot);
                glyph = theme_.captionText;
            }
        }
        drawGlyph(dc, bounds, which, glyph);
    }
}

void WindowChrome::paint(HDC dc, const RECT& client) const {
    const bool active = GetActiveWindow() == frame_;

    RECT caption = {0, 0, client.right, captionHeight()};
    fillRect(dc, caption, active ? theme_.captionBackground : theme_.captionInactive);

    SetBkMode(dc, TRANSPARENT);

    // App icon, then the title, with its extra left padding.
    RECT titleRect = caption;
    titleRect.left = scale(kTitleLeft);
    if (appIcon_) {
        int iconSize = scale(16);
        int iconY = (captionHeight() - iconSize) / 2;
        DrawIconEx(dc, titleRect.left, iconY, appIcon_, iconSize, iconSize, 0, nullptr,
                   DI_NORMAL);
        titleRect.left += iconSize + scale(8);
    }
    titleRect.right = buttonRect(2, client).left - scale(8);
    if (titleRect.right > titleRect.left) {
        HGDIOBJ previous = SelectObject(dc, captionFont_);
        SetTextColor(dc, active ? theme_.captionText : theme_.captionTextInactive);
        DrawTextW(dc, title_.c_str(), -1, &titleRect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(dc, previous);
    }

    drawCaptionButtons(dc, client, active);

    // Menu strip.
    RECT strip = {0, captionHeight(), client.right, captionHeight() + menuHeight()};
    fillRect(dc, strip, theme_.menuBackground);

    LRESULT uiState = SendMessageW(frame_, WM_QUERYUISTATE, 0, 0);
    UINT textFlags = DT_CENTER | DT_VCENTER | DT_SINGLELINE;
    if (uiState & UISF_HIDEACCEL) textFlags |= DT_HIDEPREFIX;

    HGDIOBJ previousFont = SelectObject(dc, menuFont_);
    for (size_t i = 0; i < items_.size(); ++i) {
        RECT bounds = items_[i].bounds;
        bool open = static_cast<int>(i) == openItem_;
        bool hovered = static_cast<int>(i) == hotItem_;
        if (open || hovered) fillRect(dc, bounds, theme_.menuHot);

        SetTextColor(dc, theme_.menuText);
        RECT text = bounds;
        DrawTextW(dc, items_[i].label.c_str(), -1, &text, textFlags);
    }
    SelectObject(dc, previousFont);

    RECT search = searchButtonRect();
    if (searchActive_ || hotSearch_) {
        fillRect(dc, search, searchActive_ ? theme_.menuHot : theme_.captionButtonHot);
    }
    drawCodicon(dc, search, kGlyphSearch,
                searchActive_ ? theme_.role(view::ColorRole::Link) : theme_.menuText);

    if (editButtonVisible_) {
        RECT edit = editButtonRect();
        if (editActive_ || hotEdit_) {
            fillRect(dc, edit, editActive_ ? theme_.menuHot : theme_.captionButtonHot);
        }
        drawCodicon(dc, edit, kGlyphEdit,
                    editActive_ ? theme_.role(view::ColorRole::Link) : theme_.menuText);
    }

    if (saveButtonVisible_) {
        RECT save = saveButtonRect();
        if (hotSave_) fillRect(dc, save, theme_.captionButtonHot);
        drawCodicon(dc, save, kGlyphSave, theme_.menuText);
        // Disk = prompt to save on exit; slashed disk = close silently. There
        // is no "save-slash" codicon, so the slash is drawn on top.
        if (!saveActive_) {
            const int cx = (save.left + save.right) / 2;
            const int cy = (save.top + save.bottom) / 2;
            const int radius = scale(8);
            HPEN pen = CreatePen(PS_SOLID, (std::max)(1, scale(1)), theme_.menuText);
            HGDIOBJ previous = SelectObject(dc, pen);
            MoveToEx(dc, cx - radius, cy - radius, nullptr);
            LineTo(dc, cx + radius, cy + radius);
            SelectObject(dc, previous);
            DeleteObject(pen);
        }
    }
}

void WindowChrome::paintResizeGrip(HDC dc, const RECT& client) const {
    if (maximized()) return;

    RECT grip = gripRect(client);
    int dot = std::max(2, scale(2));
    int gap = std::max(1, scale(1));
    int step = dot + gap;

    COLORREF colour = theme_.role(view::ColorRole::Muted);
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column + row < 3; ++column) {
            RECT cell;
            cell.right = grip.right - gap - column * step;
            cell.bottom = grip.bottom - gap - row * step;
            cell.left = cell.right - dot;
            cell.top = cell.bottom - dot;
            fillRect(dc, cell, colour);
        }
    }
}

void WindowChrome::openMenu(size_t index) {
    if (index >= items_.size() || !items_[index].popup) return;

    openItem_ = static_cast<int>(index);
    InvalidateRect(frame_, nullptr, FALSE);
    UpdateWindow(frame_);

    RECT bounds = items_[index].bounds;
    POINT corner = {bounds.left, bounds.bottom};
    ClientToScreen(frame_, &corner);

    TPMPARAMS params = {sizeof(params)};
    params.rcExclude = bounds;
    MapWindowPoints(frame_, nullptr, reinterpret_cast<POINT*>(&params.rcExclude), 2);

    TrackPopupMenuEx(items_[index].popup, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON,
                     corner.x, corner.y, frame_, &params);

    openItem_ = -1;
    InvalidateRect(frame_, nullptr, FALSE);
}

RECT WindowChrome::searchButtonRect() const {
    RECT client = {};
    GetClientRect(frame_, &client);

    int inset = scale(kMenuStripLeft) / 2;
    int top = captionHeight() + inset;
    int bottom = captionHeight() + menuHeight() - inset;
    int right = client.right - scale(kMenuStripLeft);
    int width = bottom - top;
    // The save button owns the rightmost slot when it is shown.
    if (saveButtonVisible_) right -= width + scale(4);
    return RECT{right - width, top, right, bottom};
}

bool WindowChrome::hitSearchButton(POINT clientPoint) const {
    RECT bounds = searchButtonRect();
    return PtInRect(&bounds, clientPoint) != FALSE;
}

void WindowChrome::setSearchActive(bool active) {
    if (searchActive_ == active) return;
    searchActive_ = active;
    if (frame_) InvalidateRect(frame_, nullptr, FALSE);
}

RECT WindowChrome::editButtonRect() const {
    RECT search = searchButtonRect();
    int width = search.right - search.left;
    int gap = scale(4);
    return RECT{search.left - gap - width, search.top, search.left - gap, search.bottom};
}

bool WindowChrome::hitEditButton(POINT clientPoint) const {
    if (!editButtonVisible_) return false;
    RECT bounds = editButtonRect();
    return PtInRect(&bounds, clientPoint) != FALSE;
}

void WindowChrome::setEditActive(bool active) {
    if (editActive_ == active) return;
    editActive_ = active;
    if (frame_) InvalidateRect(frame_, nullptr, FALSE);
}

void WindowChrome::setEditButtonVisible(bool visible) {
    if (editButtonVisible_ == visible) return;
    editButtonVisible_ = visible;
    if (!visible) hotEdit_ = false;
    if (frame_) InvalidateRect(frame_, nullptr, FALSE);
}

RECT WindowChrome::saveButtonRect() const {
    RECT search = searchButtonRect();
    int width = search.right - search.left;
    int gap = scale(4);
    return RECT{search.right + gap, search.top, search.right + gap + width, search.bottom};
}

bool WindowChrome::hitSaveButton(POINT clientPoint) const {
    if (!saveButtonVisible_) return false;
    RECT bounds = saveButtonRect();
    return PtInRect(&bounds, clientPoint) != FALSE;
}

void WindowChrome::setSaveActive(bool active) {
    if (saveActive_ == active) return;
    saveActive_ = active;
    if (frame_) InvalidateRect(frame_, nullptr, FALSE);
}

void WindowChrome::setSaveButtonVisible(bool visible) {
    if (saveButtonVisible_ == visible) return;
    saveButtonVisible_ = visible;
    if (!visible) hotSave_ = false;
    if (frame_) InvalidateRect(frame_, nullptr, FALSE);
}

bool WindowChrome::updateMenuHover(POINT clientPoint) {
    int found = -1;
    for (size_t i = 0; i < items_.size(); ++i) {
        if (PtInRect(&items_[i].bounds, clientPoint)) {
            found = static_cast<int>(i);
            break;
        }
    }
    bool overSearch = hitSearchButton(clientPoint);
    bool overEdit = hitEditButton(clientPoint);
    bool overSave = hitSaveButton(clientPoint);
    if (found == hotItem_ && overSearch == hotSearch_ && overEdit == hotEdit_ &&
        overSave == hotSave_) {
        return false;
    }
    hotItem_ = found;
    hotSearch_ = overSearch;
    hotEdit_ = overEdit;
    hotSave_ = overSave;
    return true;
}

void WindowChrome::drawCodicon(HDC dc, const RECT& button, wchar_t glyph,
                               COLORREF color) const {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    HGDIOBJ previous = SelectObject(dc, codiconFont_);
    RECT bounds = button;
    wchar_t text[2] = {glyph, 0};
    DrawTextW(dc, text, 1, &bounds, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, previous);
}

bool WindowChrome::openMenuAt(POINT clientPoint) {
    for (size_t i = 0; i < items_.size(); ++i) {
        if (PtInRect(&items_[i].bounds, clientPoint)) {
            openMenu(i);
            return true;
        }
    }
    return false;
}

bool WindowChrome::openMenuForMnemonic(wchar_t character) {
    wchar_t lower = static_cast<wchar_t>(towlower(character));
    for (size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].mnemonic == lower) {
            openMenu(i);
            return true;
        }
    }
    return false;
}

bool WindowChrome::openFirstMenu() {
    if (items_.empty()) return false;
    openMenu(0);
    return true;
}

} // namespace app
