// Collapsible document outline: a thin toggle strip that expands into a list of
// the document's headings.
#pragma once

#include "layout.h"
#include "win_theme.h"

#include <functional>
#include <string>
#include <vector>
#include <windows.h>

namespace app {

class OutlinePanel {
public:
    OutlinePanel() = default;
    ~OutlinePanel();

    OutlinePanel(const OutlinePanel&) = delete;
    OutlinePanel& operator=(const OutlinePanel&) = delete;

    static bool registerWindowClass(HINSTANCE instance);

    HWND create(HWND parent, HINSTANCE instance);
    HWND hwnd() const { return hwnd_; }

    void setTheme(const Theme& theme);
    void setDpi(int dpi);

    void setEntries(const std::vector<view::OutlineEntry>& entries);
    void setActiveIndex(int index);

    bool expanded() const { return expanded_; }
    void setExpanded(bool expanded);

    // Width the panel wants for its current state, and its floor when collapsed.
    int panelWidth() const;
    int collapsedWidth() const;

    void setOnToggle(std::function<void()> callback) { onToggle_ = std::move(callback); }
    void setOnSelect(std::function<void(size_t)> callback) { onSelect_ = std::move(callback); }

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void paint(HDC dc, const RECT& client);
    void drawChevron(HDC dc, const RECT& button, bool pointingRight) const;
    RECT buttonRect() const;
    int listTop() const;
    int hitTestItem(int x, int y) const;
    void clampScroll();
    int contentHeight() const;
    int scale(int value) const { return MulDiv(value, dpi_, 96); }

    // Headings are bucketed by depth: h1, h2, then everything deeper.
    void rebuildFonts();
    void destroyFonts();
    static int levelBucket(int level);
    int rowHeight(int level) const;
    int rowTop(size_t index) const;

    HWND hwnd_ = nullptr;
    Theme theme_;
    int dpi_ = 96;

    static const int kBuckets = 3;
    HFONT levelFonts_[kBuckets] = {nullptr};
    int levelHeights_[kBuckets] = {0};
    HFONT titleFont_ = nullptr;
    bool expanded_ = false;

    std::vector<view::OutlineEntry> entries_;
    int activeIndex_ = -1;
    int hotItem_ = -1;
    bool hotButton_ = false;
    int scrollY_ = 0;
    bool tracking_ = false;

    std::function<void()> onToggle_;
    std::function<void(size_t)> onSelect_;
};

} // namespace app
