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

    // The document palette matches VS Code's markdown preview under the
    // default Light+ / Dark+ themes (editor colours plus markdown.css).
    if (!dark) {
        t.background = RGB(255, 255, 255);
        set(t, ColorRole::None, RGB(255, 255, 255));
        set(t, ColorRole::Text, RGB(0, 0, 0));
        set(t, ColorRole::Muted, RGB(110, 110, 110));
        set(t, ColorRole::Heading, RGB(0, 0, 0));
        set(t, ColorRole::Link, RGB(0, 106, 177));
        set(t, ColorRole::CodeText, RGB(0, 0, 0));
        set(t, ColorRole::CodeBg, RGB(241, 241, 241));
        set(t, ColorRole::QuoteText, RGB(0, 0, 0));
        set(t, ColorRole::QuoteBar, RGB(128, 189, 230));
        set(t, ColorRole::Rule, RGB(209, 209, 209));
        set(t, ColorRole::TableBorder, RGB(209, 209, 209));
        set(t, ColorRole::TableHeaderBg, RGB(255, 255, 255));
        set(t, ColorRole::MarkBg, RGB(255, 255, 0));
        set(t, ColorRole::MarkText, RGB(0, 0, 0));
        set(t, ColorRole::InlineCodeBg, RGB(229, 229, 229));
        set(t, ColorRole::Checkbox, RGB(110, 110, 110));

        set(t, ColorRole::CodeKeyword, RGB(0, 0, 255));
        set(t, ColorRole::CodeType, RGB(38, 127, 153));
        set(t, ColorRole::CodeString, RGB(163, 21, 21));
        set(t, ColorRole::CodeNumber, RGB(9, 134, 88));
        set(t, ColorRole::CodeComment, RGB(0, 128, 0));
        set(t, ColorRole::CodeDirective, RGB(175, 0, 219));
        set(t, ColorRole::CodeTag, RGB(128, 0, 0));
        set(t, ColorRole::CodeAttribute, RGB(229, 0, 0));
        set(t, ColorRole::CodeFunction, RGB(121, 94, 38));

        t.selection = RGB(173, 214, 255);
        t.searchHighlight = RGB(248, 201, 171);
        t.searchCurrent = RGB(168, 172, 148);

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

    t.background = RGB(30, 30, 30);
    set(t, ColorRole::None, RGB(30, 30, 30));
    set(t, ColorRole::Text, RGB(212, 212, 212));
    set(t, ColorRole::Muted, RGB(157, 157, 157));
    set(t, ColorRole::Heading, RGB(212, 212, 212));
    set(t, ColorRole::Link, RGB(55, 148, 255));
    set(t, ColorRole::CodeText, RGB(212, 212, 212));
    set(t, ColorRole::CodeBg, RGB(22, 22, 22));
    set(t, ColorRole::QuoteText, RGB(212, 212, 212));
    set(t, ColorRole::QuoteBar, RGB(15, 76, 117));
    set(t, ColorRole::Rule, RGB(70, 70, 70));
    set(t, ColorRole::TableBorder, RGB(70, 70, 70));
    set(t, ColorRole::TableHeaderBg, RGB(30, 30, 30));
    set(t, ColorRole::MarkBg, RGB(255, 255, 0));
    set(t, ColorRole::MarkText, RGB(0, 0, 0));
    set(t, ColorRole::InlineCodeBg, RGB(52, 52, 52));
    set(t, ColorRole::Checkbox, RGB(157, 157, 157));

    set(t, ColorRole::CodeKeyword, RGB(86, 156, 214));
    set(t, ColorRole::CodeType, RGB(78, 201, 176));
    set(t, ColorRole::CodeString, RGB(206, 145, 120));
    set(t, ColorRole::CodeNumber, RGB(181, 206, 168));
    set(t, ColorRole::CodeComment, RGB(106, 153, 85));
    set(t, ColorRole::CodeDirective, RGB(197, 134, 192));
    set(t, ColorRole::CodeTag, RGB(86, 156, 214));
    set(t, ColorRole::CodeAttribute, RGB(156, 220, 254));
    set(t, ColorRole::CodeFunction, RGB(220, 220, 170));

    t.selection = RGB(38, 79, 120);
    t.searchHighlight = RGB(97, 50, 20);
    t.searchCurrent = RGB(81, 92, 106);

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
