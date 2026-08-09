#include "win_theme.h"

#include <dwmapi.h>
#include <uxtheme.h>

namespace app {
namespace {

using view::ColorRole;

void set(Theme& t, ColorRole role, COLORREF color) {
    t.roles[static_cast<int>(role)] = color;
}

// Undocumented but widely used uxtheme entry points for dark menus/scrollbars.
enum PreferredAppMode { AppModeDefault = 0, AppModeAllowDark = 1, AppModeForceDark = 2 };
using SetPreferredAppModeFn = PreferredAppMode(WINAPI*)(PreferredAppMode);
using FlushMenuThemesFn = void(WINAPI*)();

} // namespace

Theme makeTheme(bool dark) {
    Theme t;
    t.dark = dark;

    if (!dark) {
        t.background = RGB(255, 255, 255);
        set(t, ColorRole::None, RGB(255, 255, 255));
        set(t, ColorRole::Text, RGB(31, 35, 40));
        set(t, ColorRole::Muted, RGB(101, 109, 118));
        set(t, ColorRole::Heading, RGB(20, 24, 30));
        set(t, ColorRole::Link, RGB(9, 105, 218));
        set(t, ColorRole::CodeText, RGB(31, 35, 40));
        set(t, ColorRole::CodeBg, RGB(246, 248, 250));
        set(t, ColorRole::QuoteText, RGB(87, 96, 106));
        set(t, ColorRole::QuoteBar, RGB(208, 215, 222));
        set(t, ColorRole::Rule, RGB(216, 222, 228));
        set(t, ColorRole::TableBorder, RGB(208, 215, 222));
        set(t, ColorRole::TableHeaderBg, RGB(246, 248, 250));
        set(t, ColorRole::MarkBg, RGB(255, 248, 197));
        set(t, ColorRole::MarkText, RGB(31, 35, 40));
        set(t, ColorRole::InlineCodeBg, RGB(239, 241, 243));
        set(t, ColorRole::Checkbox, RGB(101, 109, 118));

        set(t, ColorRole::CodeKeyword, RGB(207, 34, 46));
        set(t, ColorRole::CodeType, RGB(149, 56, 0));
        set(t, ColorRole::CodeString, RGB(10, 48, 105));
        set(t, ColorRole::CodeNumber, RGB(5, 80, 174));
        set(t, ColorRole::CodeComment, RGB(110, 119, 129));
        set(t, ColorRole::CodeDirective, RGB(130, 80, 223));
        set(t, ColorRole::CodeTag, RGB(17, 99, 41));
        set(t, ColorRole::CodeAttribute, RGB(5, 80, 174));
        set(t, ColorRole::CodeFunction, RGB(130, 80, 223));

        t.selection = RGB(184, 213, 250);
        t.searchHighlight = RGB(255, 240, 160);
        t.searchCurrent = RGB(255, 186, 61);

        t.barBackground = RGB(243, 243, 243);
        t.barBorder = RGB(205, 205, 205);
        t.fieldBackground = RGB(255, 255, 255);
        t.fieldBorder = RGB(188, 188, 188);
        t.fieldText = RGB(31, 35, 40);
        t.fieldError = RGB(196, 43, 28);
        t.buttonFace = RGB(252, 252, 252);
        t.buttonHot = RGB(238, 238, 238);
        t.buttonPressed = RGB(226, 226, 226);
        t.buttonBorder = RGB(188, 188, 188);
        t.buttonText = RGB(31, 35, 40);
        t.menuBackground = RGB(246, 246, 246);
        t.menuText = RGB(26, 26, 26);
        t.menuHot = RGB(230, 230, 230);
        t.captionBackground = RGB(252, 252, 252);
        t.captionInactive = RGB(246, 246, 246);
        t.captionText = RGB(26, 26, 26);
        t.captionTextInactive = RGB(130, 130, 130);
        t.captionButtonHot = RGB(232, 232, 232);
        t.captionButtonPressed = RGB(216, 216, 216);
        return t;
    }

    t.background = RGB(13, 17, 23);
    set(t, ColorRole::None, RGB(13, 17, 23));
    set(t, ColorRole::Text, RGB(230, 237, 243));
    set(t, ColorRole::Muted, RGB(139, 148, 158));
    set(t, ColorRole::Heading, RGB(240, 246, 252));
    set(t, ColorRole::Link, RGB(88, 166, 255));
    set(t, ColorRole::CodeText, RGB(230, 237, 243));
    set(t, ColorRole::CodeBg, RGB(22, 27, 34));
    set(t, ColorRole::QuoteText, RGB(158, 167, 179));
    set(t, ColorRole::QuoteBar, RGB(61, 68, 77));
    set(t, ColorRole::Rule, RGB(48, 54, 61));
    set(t, ColorRole::TableBorder, RGB(48, 54, 61));
    set(t, ColorRole::TableHeaderBg, RGB(22, 27, 34));
    set(t, ColorRole::MarkBg, RGB(88, 74, 26));
    set(t, ColorRole::MarkText, RGB(240, 246, 252));
    set(t, ColorRole::InlineCodeBg, RGB(38, 44, 54));
    set(t, ColorRole::Checkbox, RGB(139, 148, 158));

    set(t, ColorRole::CodeKeyword, RGB(255, 123, 114));
    set(t, ColorRole::CodeType, RGB(255, 166, 87));
    set(t, ColorRole::CodeString, RGB(165, 214, 255));
    set(t, ColorRole::CodeNumber, RGB(121, 192, 255));
    set(t, ColorRole::CodeComment, RGB(139, 148, 158));
    set(t, ColorRole::CodeDirective, RGB(210, 168, 255));
    set(t, ColorRole::CodeTag, RGB(126, 231, 135));
    set(t, ColorRole::CodeAttribute, RGB(121, 192, 255));
    set(t, ColorRole::CodeFunction, RGB(210, 168, 255));

    t.selection = RGB(31, 75, 127);
    t.searchHighlight = RGB(84, 72, 30);
    t.searchCurrent = RGB(158, 118, 30);

    t.barBackground = RGB(32, 32, 32);
    t.barBorder = RGB(58, 58, 58);
    t.fieldBackground = RGB(45, 45, 45);
    t.fieldBorder = RGB(70, 70, 70);
    t.fieldText = RGB(230, 237, 243);
    t.fieldError = RGB(255, 123, 114);
    t.buttonFace = RGB(51, 51, 51);
    t.buttonHot = RGB(64, 64, 64);
    t.buttonPressed = RGB(74, 74, 74);
    t.buttonBorder = RGB(80, 80, 80);
    t.buttonText = RGB(230, 237, 243);
    t.menuBackground = RGB(32, 32, 32);
    t.menuText = RGB(230, 237, 243);
    t.menuHot = RGB(60, 60, 60);
    t.captionBackground = RGB(26, 26, 26);
    t.captionInactive = RGB(32, 32, 32);
    t.captionText = RGB(240, 246, 252);
    t.captionTextInactive = RGB(140, 148, 158);
    t.captionButtonHot = RGB(58, 58, 58);
    t.captionButtonPressed = RGB(72, 72, 72);
    return t;
}

bool systemUsesDarkMode() {
    // Build-time override used when checking the light palette on a machine that
    // is set to dark mode (and vice versa). Never defined in shipping builds.
#if defined(SMV_FORCE_LIGHT)
    return false;
#elif defined(SMV_FORCE_DARK)
    return true;
#endif
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0,
                      KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }
    DWORD value = 1;
    DWORD size = sizeof(value);
    DWORD type = 0;
    bool dark = false;
    if (RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, &type,
                         reinterpret_cast<BYTE*>(&value), &size) == ERROR_SUCCESS &&
        type == REG_DWORD) {
        dark = (value == 0);
    }
    RegCloseKey(key);
    return dark;
}

void enableDarkModeSupport() {
    HMODULE uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!uxtheme) return;

    auto setPreferredAppMode = reinterpret_cast<SetPreferredAppModeFn>(
        GetProcAddress(uxtheme, MAKEINTRESOURCEA(135)));
    if (setPreferredAppMode) setPreferredAppMode(AppModeAllowDark);

    auto flushMenuThemes =
        reinterpret_cast<FlushMenuThemesFn>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(136)));
    if (flushMenuThemes) flushMenuThemes();
}

void applyWindowTheme(HWND hwnd, bool dark) {
    BOOL value = dark ? TRUE : FALSE;
    // 20 is DWMWA_USE_IMMERSIVE_DARK_MODE on current Windows 10/11 builds; 19 was
    // used by earlier 1809-era builds.
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &value, sizeof(value)))) {
        DwmSetWindowAttribute(hwnd, 19, &value, sizeof(value));
    }
    SetWindowTheme(hwnd, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
}

} // namespace app
