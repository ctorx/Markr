// Persists window placement and panel state between runs (HKCU registry).
#pragma once

#include <windows.h>

namespace app {

struct WindowState {
    RECT bounds = {0, 0, 0, 0}; // restored (non-maximised) position
    bool maximized = false;
    bool outlineExpanded = false;
    int zoomPercent = 100;
    int editorZoomPercent = 100;
    bool editorWordWrap = false;
    bool editorLineNumbers = true;
    // PageWidthMode as an int (0 full, 1 reading, 2 adjustable), and the
    // adjustable text width in logical pixels.
    int pageWidthMode = 1;
    int adjustablePageWidth = 880;
    bool valid = false; // false when nothing usable was stored
};

// Reads the saved state. `valid` is false when there is none, or when the saved
// rectangle no longer lands on a connected monitor.
WindowState loadWindowState();

void saveWindowState(const WindowState& state);

} // namespace app
