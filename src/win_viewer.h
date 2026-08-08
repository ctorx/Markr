// The two windows that make up the viewer: the scrolling document view and the
// frame that hosts the menu, the search bar and the view.
#pragma once

#include "layout.h"
#include "md_types.h"
#include "search.h"
#include "win_outline.h"
#include "win_settings.h"
#include "win_text.h"
#include "win_theme.h"

#include <string>
#include <vector>
#include <windows.h>

namespace app {

class DocumentView {
public:
    static bool registerWindowClass(HINSTANCE instance);

    HWND create(HWND parent, HINSTANCE instance);
    HWND hwnd() const { return hwnd_; }

    bool loadFile(const std::wstring& path);
    void showWelcome();
    const std::wstring& filePath() const { return path_; }

    void setTheme(const Theme& theme);
    void setDpi(int dpi);

    bool hasSelection() const { return selectionStart() != selectionEnd(); }
    void copySelection() const;

    // Document outline, for the navigation panel.
    const std::vector<view::OutlineEntry>& outline() const { return layout_.outline; }
    int activeOutlineIndex() const;
    void scrollToOutlineEntry(size_t index);

    // Notepad-style find: returns false when the term is not present.
    bool findText(const std::wstring& needle, bool forward);
    void clearSearch();

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void setDocument(std::string utf8, const std::wstring& path);
    void notifyParent(int command) const;
    void relayout();
    void onPaint();
    void paintContent(HDC dc, const RECT& client);
    void updateScrollBar();
    void scrollBy(int delta);
    void scrollTo(int position);
    void ensureRangeVisible(size_t from, size_t to);
    size_t hitTest(int x, int y) const;
    void selectWordAt(size_t index);
    size_t selectionStart() const { return selectionAnchor_ < selectionFocus_ ? selectionAnchor_ : selectionFocus_; }
    size_t selectionEnd() const { return selectionAnchor_ < selectionFocus_ ? selectionFocus_ : selectionAnchor_; }
    int clientHeight() const;
    int scale(int value) const { return MulDiv(value, dpi_, 96); }

    HWND hwnd_ = nullptr;
    md::Document document_;
    view::Layout layout_;
    FontCache fonts_;
    ImageStore images_;
    Theme theme_;
    std::wstring path_;

    int dpi_ = 96;
    int scrollY_ = 0;

    size_t selectionAnchor_ = 0;
    size_t selectionFocus_ = 0;
    bool selecting_ = false;
    bool dragMoved_ = false;

    std::wstring searchTerm_;
    std::vector<view::Match> matches_;
    int currentMatch_ = -1;
};

class AppWindow {
public:
    bool create(HINSTANCE instance, int showCommand, const std::wstring& initialFile);
    int runMessageLoop();

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK searchEditProc(HWND hwnd, UINT message, WPARAM wParam,
                                           LPARAM lParam, UINT_PTR id, DWORD_PTR data);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void layoutChildren();
    void paintChrome(HDC dc);
    void applyTheme();
    void openFileDialog();
    void doSearch(bool forward);
    void updateTitle();
    void refreshOutline();
    void persistWindowState();
    int barHeight() const;
    int gutter() const { return scale(10); }
    int scale(int value) const { return MulDiv(value, dpi_, 96); }

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND searchField_ = nullptr;
    HWND searchButton_ = nullptr;
    HACCEL accelerators_ = nullptr;
    HFONT uiFont_ = nullptr;
    HFONT menuFont_ = nullptr;
    HBRUSH fieldBrush_ = nullptr;

    DocumentView view_;
    OutlinePanel outline_;
    Theme theme_;
    int dpi_ = 96;
    bool startExpanded_ = false;
    bool searchFailed_ = false;
    bool buttonHot_ = false;
    bool buttonPressed_ = false;
};

} // namespace app
