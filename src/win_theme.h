// Colour palette and OS dark/light-mode integration.
#pragma once

#include "layout.h"

#include <windows.h>

namespace app {

// Widest the text column grows, in logical pixels at 100% zoom, in both the
// reading view and the editor. A wide (typically maximised) window centres the
// column instead of stretching lines across the screen.
constexpr int kReadableWidth = 880;

// Margin between the text and the edge of the page, in logical pixels at 100%
// zoom. Once the window is wide enough, the page stands out on the canvas
// colour, the way Word shows a page on a grey background.
constexpr int kPagePadding = 48;

// Narrowest an adjustable page's text column may be dragged, in logical pixels.
constexpr int kMinPageWidth = 320;

// How the document width is constrained, chosen from the View menu.
enum class PageWidthMode {
    Full = 0,       // text runs the full width of the window
    Reading = 1,    // capped at kReadableWidth, on a centred page
    Adjustable = 2, // capped at a width the user drags with the page edges
};

// The document width setting shared by the reading view and the editor.
struct PageLayout {
    PageWidthMode mode = PageWidthMode::Reading;
    // The text column width in Adjustable mode, logical pixels at 100% zoom.
    int adjustableWidth = kReadableWidth;

    // Cap on the text column in logical pixels at 100% zoom; 0 means no cap.
    int textWidth() const {
        switch (mode) {
            case PageWidthMode::Full: return 0;
            case PageWidthMode::Adjustable: return adjustableWidth;
            default: return kReadableWidth;
        }
    }
    bool adjustable() const { return mode == PageWidthMode::Adjustable; }
};

struct Theme {
    bool dark = false;

    COLORREF background = RGB(255, 255, 255);
    // The area around the page in a wide window, and the page's edge line.
    COLORREF canvas = RGB(233, 233, 233);
    COLORREF pageBorder = RGB(214, 214, 214);
    // The drag grips on the page edges in Adjustable mode.
    COLORREF pageHandle = RGB(150, 150, 150);
    COLORREF selection = RGB(180, 213, 254);
    COLORREF searchHighlight = RGB(255, 243, 176);
    COLORREF searchCurrent = RGB(255, 190, 60);

    // Search bar chrome.
    COLORREF barBackground = RGB(243, 243, 243);
    COLORREF barBorder = RGB(208, 208, 208);
    COLORREF fieldBackground = RGB(255, 255, 255);
    COLORREF fieldBorder = RGB(190, 190, 190);
    COLORREF fieldText = RGB(31, 35, 40);
    COLORREF fieldError = RGB(196, 43, 28);
    COLORREF buttonFace = RGB(252, 252, 252);
    COLORREF buttonHot = RGB(240, 240, 240);
    COLORREF buttonPressed = RGB(228, 228, 228);
    COLORREF buttonBorder = RGB(190, 190, 190);
    COLORREF buttonText = RGB(31, 35, 40);

    // Menu strip and caption, both drawn by WindowChrome.
    COLORREF menuBackground = RGB(243, 243, 243);
    COLORREF menuText = RGB(31, 35, 40);
    COLORREF menuHot = RGB(224, 224, 224);

    COLORREF captionBackground = RGB(250, 250, 250);
    COLORREF captionInactive = RGB(243, 243, 243);
    COLORREF captionText = RGB(26, 26, 26);
    COLORREF captionTextInactive = RGB(120, 120, 120);
    COLORREF captionButtonHot = RGB(232, 232, 232);
    COLORREF captionButtonPressed = RGB(218, 218, 218);

    // Indexed by view::ColorRole; sized with room for new roles.
    COLORREF roles[40] = {0};

    COLORREF role(view::ColorRole r) const { return roles[static_cast<int>(r)]; }
};

Theme makeTheme(bool dark);

bool systemUsesDarkMode();

// Opts the process into dark common controls / menus where the OS supports it.
void enableDarkModeSupport();

void applyWindowTheme(HWND hwnd, bool dark);

// Paints the thin edge lines down both sides of a page, and the drag grips
// on them when the width is adjustable. `page` is the page in client
// coordinates; the grips sit midway down `visible`.
void paintPageEdges(HDC dc, const RECT& page, const RECT& visible, const Theme& theme, int dpi,
                    bool handles);

// Half-width of the zone around a page edge that grabs it for resizing.
inline int pageEdgeGrabZone(int dpi) { return MulDiv(5, dpi, 96); }

} // namespace app
