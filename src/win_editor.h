// Edit mode: a plain-text markdown editor pane with a formatting toolbar, a
// line-number gutter, a find/replace bar and a Ln/Col status footer. The text
// itself lives in a RichEdit control (plain-text mode) for multi-level
// undo/redo and the standard clipboard behaviour.
#pragma once

#include "win_theme.h"

#include <functional>
#include <string>
#include <vector>
#include <windows.h>

struct ITextDocument; // Text Object Model, used for undo-free source styling

namespace app {

class EditorPane {
public:
    EditorPane() = default;
    ~EditorPane();

    EditorPane(const EditorPane&) = delete;
    EditorPane& operator=(const EditorPane&) = delete;

    static bool registerWindowClass(HINSTANCE instance);

    HWND create(HWND parent, HINSTANCE instance);
    HWND hwnd() const { return hwnd_; }
    HWND editControl() const { return edit_; }

    void setTheme(const Theme& theme);
    void setDpi(int dpi);

    // Replaces the buffer, resets undo history and the modified flag.
    void setContent(const std::wstring& text, const std::wstring& path);
    std::wstring content() const; // CRLF line ends
    const std::wstring& filePath() const { return path_; }

    bool modified() const;
    bool save();
    // Save under a new path (Save As); adopts the path on success.
    bool saveTo(const std::wstring& path);
    // Keeps the buffer but drops the modified flag, so the next switch into
    // edit mode reloads from disk.
    void discardChanges();

    // Whether closing the app with unsaved changes should prompt to save.
    // Defaults per document: off for untitled buffers, on for files from disk.
    // Toggled from the frame chrome's disk button.
    bool nagOnExit() const { return nagOnExit_; }
    void toggleNagOnExit() { nagOnExit_ = !nagOnExit_; }

    void focusEditor();

    void showFindBar(bool show, bool focusReplace);
    bool findBarVisible() const { return findVisible_; }
    void findNext(bool forward);

    void undo();
    void redo();
    void cut();
    void copy();
    void paste();
    void selectAll();
    bool canUndo() const;
    bool canRedo() const;
    bool hasSelection() const;
    bool canPaste() const;

    // Formatting shortcuts also reachable from the frame's accelerators.
    void toggleBold();
    void toggleItalic();

    // Off for non-markdown documents: the formatting toolbar disappears, the
    // markdown commands become no-ops and source styling is dropped.
    void setMarkdownMode(bool on);

    // Word wrap, persisted with the window state.
    bool wordWrap() const { return wordWrap_; }
    void setWordWrap(bool on);

    // Line-number gutter, persisted with the window state.
    bool showLineNumbers() const { return showLineNumbers_; }
    void setShowLineNumbers(bool on);

    // Editor zoom, persisted separately from the reading view's zoom.
    void adjustZoom(int deltaPercent);
    void resetZoom();
    void setZoomPercent(int percent);
    int zoomPercent() const { return zoomPercent_; }

    // Fired whenever the modified flag may have flipped.
    void setOnModifiedChanged(std::function<void()> callback) {
        onModifiedChanged_ = std::move(callback);
    }

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK editProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                     UINT_PTR id, DWORD_PTR data);
    static LRESULT CALLBACK fieldProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                      UINT_PTR id, DWORD_PTR data);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void createChildren(HINSTANCE instance);
    void layoutChildren();
    // Pads the text away from the control edges, matching the reading view's
    // content padding. Must be re-applied after every resize.
    void applyEditorInsets();
    // Live markdown source styling (VS Code-like colours) via the Text Object
    // Model, so it never touches the undo stack or the selection.
    void applyHighlighting();
    void scheduleHighlight();
    void rebuildFonts();
    void destroyFonts();
    void applyEditorFormat();
    void updateTooltips();

    void paint(HDC dc, const RECT& client);
    void paintToolbar(HDC dc, const RECT& client);
    void paintFindBar(HDC dc, const RECT& client);
    void paintGutter(HDC dc, const RECT& client);
    void paintStatus(HDC dc, const RECT& client);
    void drawGlyph(HDC dc, const RECT& bounds, int command, COLORREF color) const;

    // The formatting toolbar only exists for markdown documents.
    int toolbarHeight() const { return markdownMode_ ? scale(44) : 0; }
    int findBarHeight() const { return findVisible_ ? scale(44) : 0; }
    int statusHeight() const { return scale(24); }
    int gutterWidth() const;
    RECT contentRect() const; // gutter + editor area
    int hitTestButton(POINT p) const;

    // Selection / text plumbing (RichEdit uses a single CR per line break).
    struct Sel {
        LONG start = 0;
        LONG end = 0;
    };
    Sel selection() const;
    void setSelection(LONG start, LONG end);
    std::wstring textRange(LONG start, LONG end) const;
    std::wstring allTextRaw() const; // CR line ends; indices match the control
    void replaceRange(LONG start, LONG end, const std::wstring& text);

    struct LineBlock {
        LONG start = 0;
        LONG end = 0;
        std::wstring text;
    };
    LineBlock selectedLines() const;

    void runCommand(int command);
    void toggleInline(const std::wstring& prefix, const std::wstring& suffix);
    void setHeading(int level);
    enum class LineStyle { Bullet, Numbered, Task, Quote };
    void toggleLineStyle(LineStyle style);
    void toggleCodeFence();
    void insertLink();
    void insertTable();
    void insertHorizontalRule();
    void insertAtLineBoundary(const std::wstring& block);

    void onEditChanged();
    void refreshChrome(); // gutter + status repaint
    void notifyModified();

    void doReplaceOne();
    void doReplaceAll();
    std::wstring fieldText(HWND field) const;

    int scale(int value) const { return MulDiv(value, dpi_, 96); }

    struct ToolButton {
        int command = 0; // 0 = separator
        const wchar_t* tip = nullptr;
        RECT bounds = {0, 0, 0, 0};
    };

    HWND hwnd_ = nullptr;
    HWND edit_ = nullptr;
    HWND tooltip_ = nullptr;
    HWND findField_ = nullptr;
    HWND replaceField_ = nullptr;
    HWND findNextButton_ = nullptr;
    HWND replaceButton_ = nullptr;
    HWND replaceAllButton_ = nullptr;
    HINSTANCE instance_ = nullptr;

    Theme theme_;
    HBRUSH fieldBrush_ = nullptr;
    ITextDocument* tomDoc_ = nullptr;
    int dpi_ = 96;
    int zoomPercent_ = 100;
    std::wstring path_;
    bool findVisible_ = false;
    bool lastModified_ = false;
    bool suppressChange_ = false;
    bool nagOnExit_ = false;
    bool markdownMode_ = true;
    bool wordWrap_ = false;
    bool showLineNumbers_ = true;

    HFONT gutterFont_ = nullptr;
    HFONT uiFont_ = nullptr;
    HFONT smallBoldFont_ = nullptr;
    HFONT codiconFont_ = nullptr;
    int gutterCharWidth_ = 8;
    int gutterDigits_ = 3;
    // Vertical offset that centres the (smaller) gutter font in a text line.
    int gutterYOffset_ = 0;

    std::vector<ToolButton> buttons_;
    int hotButton_ = -1;
    int pressedButton_ = -1;
    bool trackingMouse_ = false;
    int hotFindButton_ = 0; // control id under the mouse, 0 when none

    std::function<void()> onModifiedChanged_;
};

} // namespace app
