// Custom window caption and menu strip.
//
// Windows owns the metrics of a standard title bar, so the only way to control
// its padding is to draw it ourselves: WM_NCCALCSIZE hands the caption strip to
// the client area, and this class paints and hit-tests it. The menu bar has to
// come along, because the system draws it in the same non-client space.
#pragma once

#include "win_theme.h"

#include <string>
#include <vector>
#include <windows.h>

namespace app {

class WindowChrome {
public:
    ~WindowChrome();

    WindowChrome(const WindowChrome&) = delete;
    WindowChrome& operator=(const WindowChrome&) = delete;
    WindowChrome() = default;

    void initialize(HWND frame, HMENU fileMenu, HMENU editMenu, HMENU aboutMenu);
    void setTheme(const Theme& theme);
    void setDpi(int dpi);
    void setTitle(const std::wstring& title);

    int captionHeight() const;
    int menuHeight() const;
    int height() const { return captionHeight() + menuHeight(); }
    int resizeBorder() const;
    int gripSize() const;

    // Window-relative hit testing. Returns HTNOWHERE when the point is not ours.
    LRESULT hitTest(POINT point) const;

    void paint(HDC dc, const RECT& client) const;
    void paintResizeGrip(HDC dc, const RECT& client) const;
    RECT gripRect(const RECT& client) const;

    // Non-client mouse tracking for the caption buttons.
    void setHot(LRESULT hit);
    void setPressed(LRESULT hit);
    LRESULT pressed() const { return pressed_; }
    void clearHot();

    // Returns true when the hovered menu item or search button changed.
    bool updateMenuHover(POINT clientPoint);

    // Magnifier button at the right end of the menu strip.
    RECT searchButtonRect() const;
    bool hitSearchButton(POINT clientPoint) const;
    void setSearchActive(bool active);

    // Menu strip interaction. Both return true when the click/key was consumed.
    bool openMenuAt(POINT clientPoint);
    bool openMenuForMnemonic(wchar_t character);
    bool openFirstMenu();

private:
    struct MenuItem {
        std::wstring label;
        wchar_t mnemonic = 0;
        HMENU popup = nullptr;
        RECT bounds = {0, 0, 0, 0};
    };

    void rebuildFonts();
    void layoutMenuItems();
    RECT buttonRect(int indexFromRight, const RECT& client) const;
    void drawCaptionButtons(HDC dc, const RECT& client, bool active) const;
    void drawGlyph(HDC dc, const RECT& button, LRESULT which, COLORREF color) const;
    void openMenu(size_t index);
    bool maximized() const;
    int scale(int value) const { return MulDiv(value, dpi_, 96); }

    HWND frame_ = nullptr;
    Theme theme_;
    int dpi_ = 96;
    std::wstring title_;

    HFONT captionFont_ = nullptr;
    HFONT menuFont_ = nullptr;

    void drawMagnifier(HDC dc, const RECT& button, COLORREF color) const;

    std::vector<MenuItem> items_;
    int openItem_ = -1;
    int hotItem_ = -1;
    bool hotSearch_ = false;
    bool searchActive_ = false;
    LRESULT hot_ = HTNOWHERE;
    LRESULT pressed_ = HTNOWHERE;
};

} // namespace app
