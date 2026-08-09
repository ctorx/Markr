#include "win_settings.h"

namespace app {
namespace {

const wchar_t* const kKeyPath = L"Software\\Simple Markdown Viewer";

bool readDword(HKEY key, const wchar_t* name, DWORD* value) {
    DWORD size = sizeof(DWORD);
    DWORD type = 0;
    return RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(value),
                            &size) == ERROR_SUCCESS &&
           type == REG_DWORD;
}

void writeDword(HKEY key, const wchar_t* name, DWORD value) {
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value),
                   sizeof(value));
}

// Guards against restoring onto a monitor that is no longer attached.
bool isOnScreen(const RECT& bounds) {
    if (bounds.right - bounds.left < 200 || bounds.bottom - bounds.top < 150) return false;
    return MonitorFromRect(&bounds, MONITOR_DEFAULTTONULL) != nullptr;
}

} // namespace

WindowState loadWindowState() {
    WindowState state;

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kKeyPath, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return state;
    }

    DWORD left = 0, top = 0, width = 0, height = 0, maximized = 0, outline = 0, zoom = 0;
    if (readDword(key, L"ZoomPercent", &zoom) && zoom >= 50 && zoom <= 300) {
        state.zoomPercent = static_cast<int>(zoom);
    }
    bool haveAll = readDword(key, L"WindowLeft", &left) && readDword(key, L"WindowTop", &top) &&
                   readDword(key, L"WindowWidth", &width) &&
                   readDword(key, L"WindowHeight", &height);
    readDword(key, L"WindowMaximized", &maximized);
    if (readDword(key, L"OutlineExpanded", &outline)) state.outlineExpanded = outline != 0;
    RegCloseKey(key);

    if (!haveAll) return state;

    state.bounds.left = static_cast<LONG>(static_cast<int>(left));
    state.bounds.top = static_cast<LONG>(static_cast<int>(top));
    state.bounds.right = state.bounds.left + static_cast<LONG>(width);
    state.bounds.bottom = state.bounds.top + static_cast<LONG>(height);
    state.maximized = maximized != 0;
    state.valid = isOnScreen(state.bounds);
    return state;
}

void saveWindowState(const WindowState& state) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kKeyPath, 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return;
    }

    writeDword(key, L"WindowLeft", static_cast<DWORD>(state.bounds.left));
    writeDword(key, L"WindowTop", static_cast<DWORD>(state.bounds.top));
    writeDword(key, L"WindowWidth", static_cast<DWORD>(state.bounds.right - state.bounds.left));
    writeDword(key, L"WindowHeight", static_cast<DWORD>(state.bounds.bottom - state.bounds.top));
    writeDword(key, L"WindowMaximized", state.maximized ? 1 : 0);
    writeDword(key, L"OutlineExpanded", state.outlineExpanded ? 1 : 0);
    writeDword(key, L"ZoomPercent", static_cast<DWORD>(state.zoomPercent));
    RegCloseKey(key);
}

} // namespace app
