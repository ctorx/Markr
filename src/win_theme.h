// Colour palette and OS dark/light-mode integration.
#pragma once

#include "layout.h"

#include <windows.h>

namespace app {

struct Theme {
    bool dark = false;

    COLORREF background = RGB(255, 255, 255);
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

    // Menu bar, only custom drawn in dark mode.
    COLORREF menuBackground = RGB(243, 243, 243);
    COLORREF menuText = RGB(31, 35, 40);
    COLORREF menuHot = RGB(224, 224, 224);

    COLORREF roles[32] = {0};

    COLORREF role(view::ColorRole r) const { return roles[static_cast<int>(r)]; }
};

Theme makeTheme(bool dark);

bool systemUsesDarkMode();

// Opts the process into dark common controls / menus where the OS supports it.
void enableDarkModeSupport();

void applyWindowTheme(HWND hwnd, bool dark);

} // namespace app
