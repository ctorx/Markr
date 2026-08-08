#include "win_viewer.h"

#include <algorithm>
#include <commctrl.h>
#include <commdlg.h>
#include <cwctype>
#include <shellapi.h>
#include <shlwapi.h>
#include <string>
#include <windowsx.h>

namespace app {
namespace {

const wchar_t* const kAppTitle = L"Simple Markdown Viewer";
const wchar_t* const kProjectUrl = L"https://github.com/ctorx/SimpleMarkDownViewer";
const wchar_t* const kViewClass = L"SmvDocumentView";
const wchar_t* const kFrameClass = L"SmvFrameWindow";

enum Command {
    ID_FILE_OPEN = 1001,
    ID_FILE_EXIT,
    ID_EDIT_COPY,
    ID_ABOUT_PROJECT,
    ID_SEARCH_FIELD,
    ID_SEARCH_BUTTON,
    ID_FIND_NEXT,
    ID_FIND_PREVIOUS,
    ID_FOCUS_SEARCH,
    ID_VIEW_SCROLLED,
    ID_VIEW_DOCUMENT_CHANGED,
};

// Undocumented "UAH" menu messages. Windows sends these while drawing the menu
// bar; handling them is the only supported-in-practice way to make the bar
// follow a dark theme.
constexpr UINT kUahDrawMenu = 0x0091;
constexpr UINT kUahDrawMenuItem = 0x0092;

struct UahMenu {
    HMENU menu;
    HDC dc;
    DWORD flags;
};

struct UahMenuItemMetrics {
    DWORD data[8];
};

struct UahMenuPopupMetrics {
    DWORD widths[4];
    DWORD updateMaxWidths : 2;
};

struct UahMenuItem {
    int position;
    UahMenuItemMetrics metrics;
    UahMenuPopupMetrics popupMetrics;
};

struct UahDrawMenuItem {
    DRAWITEMSTRUCT item;
    UahMenu menu;
    UahMenuItem menuItem;
};

bool menuBarRect(HWND hwnd, RECT* out) {
    MENUBARINFO info = {sizeof(info)};
    if (!GetMenuBarInfo(hwnd, OBJID_MENU, 0, &info)) return false;
    RECT window = {};
    if (!GetWindowRect(hwnd, &window)) return false;
    *out = info.rcBar;
    OffsetRect(out, -window.left, -window.top);
    return true;
}

int windowDpi(HWND hwnd) {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static GetDpiForWindowFn getDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (getDpiForWindow && hwnd) {
        UINT dpi = getDpiForWindow(hwnd);
        if (dpi > 0) return static_cast<int>(dpi);
    }
    HDC dc = GetDC(hwnd);
    int dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(hwnd, dc);
    return dpi > 0 ? dpi : 96;
}

void fillRect(HDC dc, const RECT& rect, COLORREF color) {
    SetDCBrushColor(dc, color);
    FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

void frameRect(HDC dc, const RECT& rect, COLORREF color) {
    SetDCBrushColor(dc, color);
    FrameRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

void drawLine(HDC dc, int x1, int y, int x2, COLORREF color, int thickness) {
    RECT r = {x1, y, x2, y + std::max(1, thickness)};
    fillRect(dc, r, color);
}

std::string readFileUtf8(const std::wstring& path, bool* ok) {
    *ok = false;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return std::string();

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart > (64 << 20)) {
        CloseHandle(file);
        return std::string();
    }

    std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    if (!bytes.empty() &&
        !ReadFile(file, &bytes[0], static_cast<DWORD>(bytes.size()), &read, nullptr)) {
        CloseHandle(file);
        return std::string();
    }
    CloseHandle(file);
    bytes.resize(read);
    *ok = true;

    // Honour the common byte order marks; otherwise assume UTF-8.
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF) {
        return bytes.substr(3);
    }
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF &&
        static_cast<unsigned char>(bytes[1]) == 0xFE) {
        std::wstring wide(reinterpret_cast<const wchar_t*>(bytes.data() + 2),
                          (bytes.size() - 2) / 2);
        return view::toUtf8(wide);
    }
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFE &&
        static_cast<unsigned char>(bytes[1]) == 0xFF) {
        std::wstring wide((bytes.size() - 2) / 2, L'\0');
        for (size_t i = 0; i < wide.size(); ++i) {
            wide[i] = static_cast<wchar_t>(
                (static_cast<unsigned char>(bytes[2 + i * 2]) << 8) |
                static_cast<unsigned char>(bytes[3 + i * 2]));
        }
        return view::toUtf8(wide);
    }
    return bytes;
}

const char* const kWelcomeDocument =
    "# Simple Markdown Viewer\n"
    "\n"
    "A fast, read-only viewer for Markdown files.\n"
    "\n"
    "## Getting started\n"
    "\n"
    "- **File -> Open** (`Ctrl+O`) to open a `.md` file\n"
    "- Type in the search box and press **Enter** or the **Search** button\n"
    "- `F3` finds the next match, `Shift+F3` the previous one\n"
    "- Select text with the mouse, then **Edit -> Copy** (`Ctrl+C`)\n"
    "\n"
    "> The window follows the system light or dark theme and rewraps as you resize it.\n";

} // namespace

// ============================================================ DocumentView

bool DocumentView::registerWindowClass(HINSTANCE instance) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = &DocumentView::windowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kViewClass;
    return RegisterClassExW(&wc) != 0;
}

HWND DocumentView::create(HWND parent, HINSTANCE instance) {
    hwnd_ = CreateWindowExW(0, kViewClass, nullptr,
                            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP, 0, 0, 100, 100,
                            parent, nullptr, instance, this);
    return hwnd_;
}

LRESULT CALLBACK DocumentView::windowProc(HWND hwnd, UINT message, WPARAM wParam,
                                          LPARAM lParam) {
    DocumentView* self = reinterpret_cast<DocumentView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<DocumentView*>(cs->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(hwnd, message, wParam, lParam);
    return self->handleMessage(message, wParam, lParam);
}

int DocumentView::clientHeight() const {
    RECT r = {};
    GetClientRect(hwnd_, &r);
    return r.bottom - r.top;
}

LRESULT DocumentView::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            dpi_ = windowDpi(hwnd_);
            fonts_.rebuild(dpi_);
            theme_ = makeTheme(systemUsesDarkMode());
            return 0;

        case WM_SIZE:
            relayout();
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
            onPaint();
            return 0;

        case WM_VSCROLL: {
            SCROLLINFO si = {sizeof(si)};
            si.fMask = SIF_ALL;
            GetScrollInfo(hwnd_, SB_VERT, &si);
            int position = si.nPos;
            int line = fonts_.lineHeight(view::FontId::Body);
            switch (LOWORD(wParam)) {
                case SB_LINEUP: position -= line; break;
                case SB_LINEDOWN: position += line; break;
                case SB_PAGEUP: position -= static_cast<int>(si.nPage); break;
                case SB_PAGEDOWN: position += static_cast<int>(si.nPage); break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: position = si.nTrackPos; break;
                case SB_TOP: position = 0; break;
                case SB_BOTTOM: position = si.nMax; break;
                default: break;
            }
            scrollTo(position);
            return 0;
        }

        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            UINT lines = 3;
            SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
            if (lines == 0 || lines > 20) lines = 3;
            scrollBy(-delta * static_cast<int>(lines) *
                     fonts_.lineHeight(view::FontId::Body) / WHEEL_DELTA);
            return 0;
        }

        case WM_KEYDOWN: {
            int line = fonts_.lineHeight(view::FontId::Body);
            switch (wParam) {
                case VK_UP: scrollBy(-line); return 0;
                case VK_DOWN: scrollBy(line); return 0;
                case VK_PRIOR: scrollBy(-clientHeight() + line); return 0;
                case VK_NEXT: scrollBy(clientHeight() - line); return 0;
                case VK_HOME: scrollTo(0); return 0;
                case VK_END: scrollTo(layout_.contentHeight); return 0;
                case 'C':
                    if (GetKeyState(VK_CONTROL) < 0) copySelection();
                    return 0;
                case VK_TAB: {
                    HWND next = GetNextDlgTabItem(GetParent(hwnd_), hwnd_,
                                                  GetKeyState(VK_SHIFT) < 0);
                    if (next) SetFocus(next);
                    return 0;
                }
                case 'A':
                    if (GetKeyState(VK_CONTROL) < 0) {
                        selectionAnchor_ = 0;
                        selectionFocus_ = layout_.text.size();
                        InvalidateRect(hwnd_, nullptr, FALSE);
                    }
                    return 0;
                default:
                    break;
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            SetFocus(hwnd_);
            selectionAnchor_ = selectionFocus_ =
                hitTest(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            selecting_ = true;
            dragMoved_ = false;
            SetCapture(hwnd_);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }

        case WM_LBUTTONDBLCLK:
            selectWordAt(hitTest(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)));
            return 0;

        case WM_MOUSEMOVE: {
            if (!selecting_) return 0;
            int y = GET_Y_LPARAM(lParam);
            if (y < 0) scrollBy(-fonts_.lineHeight(view::FontId::Body));
            else if (y > clientHeight()) scrollBy(fonts_.lineHeight(view::FontId::Body));
            size_t index = hitTest(GET_X_LPARAM(lParam), y);
            if (index != selectionFocus_) {
                selectionFocus_ = index;
                dragMoved_ = true;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (selecting_) {
                selecting_ = false;
                ReleaseCapture();
                if (!dragMoved_) {
                    int link = view::linkAtPoint(layout_, GET_X_LPARAM(lParam),
                                                 GET_Y_LPARAM(lParam) + scrollY_);
                    if (link >= 0 && link < static_cast<int>(layout_.links.size())) {
                        ShellExecuteW(hwnd_, L"open", layout_.links[link].url.c_str(), nullptr,
                                      nullptr, SW_SHOWNORMAL);
                    }
                }
            }
            return 0;
        }

        case WM_SETCURSOR: {
            POINT p = {};
            GetCursorPos(&p);
            ScreenToClient(hwnd_, &p);
            RECT client = {};
            GetClientRect(hwnd_, &client);
            if (PtInRect(&client, p)) {
                if (view::linkAtPoint(layout_, p.x, p.y + scrollY_) >= 0) {
                    SetCursor(LoadCursorW(nullptr, IDC_HAND));
                } else {
                    SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
                }
                return TRUE;
            }
            break;
        }

        default:
            break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

void DocumentView::setTheme(const Theme& theme) {
    theme_ = theme;
    applyWindowTheme(hwnd_, theme.dark);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void DocumentView::setDpi(int dpi) {
    dpi_ = dpi > 0 ? dpi : 96;
    fonts_.rebuild(dpi_);
    relayout();
}

bool DocumentView::loadFile(const std::wstring& path) {
    bool ok = false;
    std::string utf8 = readFileUtf8(path, &ok);
    if (!ok) return false;
    setDocument(std::move(utf8), path);
    return true;
}

void DocumentView::showWelcome() {
    setDocument(kWelcomeDocument, std::wstring());
}

void DocumentView::setDocument(std::string utf8, const std::wstring& path) {
    document_ = md::parse(utf8);
    path_ = path;

    std::wstring directory = path;
    size_t slash = directory.find_last_of(L"\\/");
    directory = (slash == std::wstring::npos) ? std::wstring() : directory.substr(0, slash);
    images_.setBaseDirectory(directory);

    scrollY_ = 0;
    selectionAnchor_ = selectionFocus_ = 0;
    clearSearch();
    relayout();
    InvalidateRect(hwnd_, nullptr, TRUE);
    notifyParent(ID_VIEW_DOCUMENT_CHANGED);
}

void DocumentView::relayout() {
    RECT client = {};
    GetClientRect(hwnd_, &client);
    int width = std::max(scale(200), static_cast<int>(client.right - client.left));

    view::Metrics metrics;
    metrics.padding = scale(20);
    metrics.blockSpacing = scale(12);
    metrics.listIndent = scale(26);
    metrics.quoteIndent = scale(18);
    metrics.quoteBarWidth = std::max(2, scale(4));
    metrics.codePadding = scale(10);
    metrics.cellPadding = scale(8);
    metrics.ruleThickness = std::max(1, scale(1));
    metrics.checkboxSize = scale(13);

    layout_ = view::buildLayout(document_, fonts_, metrics, width, &images_);

    int maxScroll = std::max(0, layout_.contentHeight - clientHeight());
    if (scrollY_ > maxScroll) scrollY_ = maxScroll;
    updateScrollBar();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void DocumentView::updateScrollBar() {
    SCROLLINFO si = {sizeof(si)};
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
    si.nMin = 0;
    si.nMax = std::max(0, layout_.contentHeight - 1);
    si.nPage = static_cast<UINT>(std::max(1, clientHeight()));
    si.nPos = scrollY_;
    SetScrollInfo(hwnd_, SB_VERT, &si, TRUE);
}

void DocumentView::scrollBy(int delta) { scrollTo(scrollY_ + delta); }

void DocumentView::scrollTo(int position) {
    int maxScroll = std::max(0, layout_.contentHeight - clientHeight());
    position = std::max(0, std::min(position, maxScroll));
    if (position == scrollY_) return;
    int delta = scrollY_ - position;
    scrollY_ = position;
    ScrollWindowEx(hwnd_, 0, delta, nullptr, nullptr, nullptr, nullptr, SW_INVALIDATE);
    updateScrollBar();
    notifyParent(ID_VIEW_SCROLLED);
}

void DocumentView::notifyParent(int command) const {
    HWND parent = GetParent(hwnd_);
    if (parent) {
        SendMessageW(parent, WM_COMMAND, MAKEWPARAM(command, 0),
                     reinterpret_cast<LPARAM>(hwnd_));
    }
}

int DocumentView::activeOutlineIndex() const {
    const std::vector<view::OutlineEntry>& entries = layout_.outline;
    int threshold = scrollY_ + scale(24);
    int active = -1;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].y <= threshold) active = static_cast<int>(i);
        else break;
    }
    return active;
}

void DocumentView::scrollToOutlineEntry(size_t index) {
    if (index >= layout_.outline.size()) return;
    scrollTo(layout_.outline[index].y - scale(8));
    SetFocus(hwnd_);
}

size_t DocumentView::hitTest(int x, int y) const {
    return view::indexAtPoint(layout_, x, y + scrollY_);
}

void DocumentView::selectWordAt(size_t index) {
    const std::wstring& text = layout_.text;
    if (text.empty()) return;
    index = std::min(index, text.size() - 1);
    auto isWord = [](wchar_t c) {
        return iswalnum(c) != 0 || c == L'_' || c == L'-';
    };
    if (!isWord(text[index])) {
        selectionAnchor_ = index;
        selectionFocus_ = index + 1;
    } else {
        size_t start = index;
        while (start > 0 && isWord(text[start - 1])) --start;
        size_t end = index;
        while (end < text.size() && isWord(text[end])) ++end;
        selectionAnchor_ = start;
        selectionFocus_ = end;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void DocumentView::copySelection() const {
    std::wstring text = view::extractText(layout_, selectionStart(), selectionEnd());
    if (text.empty()) return;

    std::wstring crlf;
    crlf.reserve(text.size() + 16);
    for (wchar_t c : text) {
        if (c == L'\n') crlf += L"\r\n";
        else crlf.push_back(c);
    }

    if (!OpenClipboard(hwnd_)) return;
    EmptyClipboard();
    size_t bytes = (crlf.size() + 1) * sizeof(wchar_t);
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (handle) {
        void* target = GlobalLock(handle);
        if (target) {
            memcpy(target, crlf.c_str(), bytes);
            GlobalUnlock(handle);
            SetClipboardData(CF_UNICODETEXT, handle);
        } else {
            GlobalFree(handle);
        }
    }
    CloseClipboard();
}

bool DocumentView::findText(const std::wstring& needle, bool forward) {
    if (needle.empty()) {
        clearSearch();
        return false;
    }
    if (needle != searchTerm_) {
        searchTerm_ = needle;
        matches_ = view::findAll(layout_.text, needle, false);
        currentMatch_ = -1;
    }
    if (matches_.empty()) {
        InvalidateRect(hwnd_, nullptr, FALSE);
        return false;
    }

    size_t from;
    if (currentMatch_ >= 0) {
        from = forward ? matches_[currentMatch_].start + 1 : matches_[currentMatch_].start;
    } else {
        from = forward ? selectionEnd() : selectionStart();
    }

    int index = view::nextMatch(matches_, from, forward);
    if (index < 0) return false;

    currentMatch_ = index;
    selectionAnchor_ = matches_[index].start;
    selectionFocus_ = matches_[index].start + matches_[index].length;
    ensureRangeVisible(selectionAnchor_, selectionFocus_);
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void DocumentView::clearSearch() {
    searchTerm_.clear();
    matches_.clear();
    currentMatch_ = -1;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void DocumentView::ensureRangeVisible(size_t from, size_t to) {
    std::vector<view::Rect> rects;
    view::selectionRects(layout_, from, to, rects);
    if (rects.empty()) return;

    int top = rects.front().y;
    int bottom = rects.front().y + rects.front().height;
    for (const view::Rect& r : rects) {
        top = std::min(top, r.y);
        bottom = std::max(bottom, r.y + r.height);
    }
    int height = clientHeight();
    if (top < scrollY_) {
        scrollTo(top - height / 4);
    } else if (bottom > scrollY_ + height) {
        scrollTo(bottom - height + height / 4);
    }
}

void DocumentView::onPaint() {
    PAINTSTRUCT ps = {};
    HDC dc = BeginPaint(hwnd_, &ps);

    RECT client = {};
    GetClientRect(hwnd_, &client);

    HDC memory = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
    HGDIOBJ previousBitmap = SelectObject(memory, bitmap);

    paintContent(memory, client);

    BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left,
           ps.rcPaint.bottom - ps.rcPaint.top, memory, ps.rcPaint.left, ps.rcPaint.top,
           SRCCOPY);

    SelectObject(memory, previousBitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
    EndPaint(hwnd_, &ps);
}

void DocumentView::paintContent(HDC dc, const RECT& client) {
    fillRect(dc, client, theme_.background);

    const int top = scrollY_;
    const int bottom = scrollY_ + (client.bottom - client.top);

    for (const view::Decoration& d : layout_.decorations) {
        if (d.y + d.height < top || d.y > bottom) continue;
        RECT r = {d.x, d.y - scrollY_, d.x + d.width, d.y + d.height - scrollY_};
        switch (d.type) {
            case view::DecorationType::Rule:
            case view::DecorationType::QuoteBar:
            case view::DecorationType::CodeBlockBg:
            case view::DecorationType::TableHeaderBg:
                fillRect(dc, r, theme_.role(d.color));
                break;
            case view::DecorationType::TableBorder:
                frameRect(dc, r, theme_.role(d.color));
                break;
            case view::DecorationType::Checkbox:
            case view::DecorationType::CheckboxChecked: {
                frameRect(dc, r, theme_.role(view::ColorRole::Checkbox));
                if (d.type == view::DecorationType::CheckboxChecked) {
                    RECT inner = r;
                    InflateRect(&inner, -std::max(2, d.width / 4), -std::max(2, d.height / 4));
                    fillRect(dc, inner, theme_.role(view::ColorRole::Link));
                }
                break;
            }
            case view::DecorationType::Image:
                if (d.imageIndex >= 0 && d.imageIndex < static_cast<int>(layout_.images.size())) {
                    images_.draw(dc, layout_.images[d.imageIndex].source, r.left, r.top,
                                 d.width, d.height);
                }
                break;
        }
    }

    // Inline backgrounds (code spans, highlights) sit under the search and
    // selection highlights.
    for (const view::Run& run : layout_.runs) {
        if (run.style.background == view::ColorRole::None) continue;
        if (run.y + run.height < top || run.y > bottom) continue;
        RECT r = {run.x, run.y - scrollY_, run.x + run.width, run.y + run.height - scrollY_};
        fillRect(dc, r, theme_.role(run.style.background));
    }

    std::vector<view::Rect> rects;
    for (size_t i = 0; i < matches_.size(); ++i) {
        view::selectionRects(layout_, matches_[i].start, matches_[i].start + matches_[i].length,
                             rects);
        COLORREF color = (static_cast<int>(i) == currentMatch_) ? theme_.searchCurrent
                                                                : theme_.searchHighlight;
        for (const view::Rect& rect : rects) {
            if (rect.y + rect.height < top || rect.y > bottom) continue;
            RECT r = {rect.x, rect.y - scrollY_, rect.x + rect.width,
                      rect.y + rect.height - scrollY_};
            fillRect(dc, r, color);
        }
    }

    view::selectionRects(layout_, selectionStart(), selectionEnd(), rects);
    for (const view::Rect& rect : rects) {
        if (rect.y + rect.height < top || rect.y > bottom) continue;
        RECT r = {rect.x, rect.y - scrollY_, rect.x + rect.width,
                  rect.y + rect.height - scrollY_};
        fillRect(dc, r, theme_.selection);
    }

    SetBkMode(dc, TRANSPARENT);
    SetTextAlign(dc, TA_LEFT | TA_BASELINE);
    HGDIOBJ previousFont = nullptr;
    view::FontId currentFont = view::FontId::Count;

    for (const view::Run& run : layout_.runs) {
        if (run.y + run.height < top || run.y > bottom) continue;
        if (run.text.empty()) continue;

        if (currentFont != run.style.font) {
            HGDIOBJ previous = SelectObject(dc, fonts_.handle(run.style.font));
            if (!previousFont) previousFont = previous;
            currentFont = run.style.font;
        }
        COLORREF color = theme_.role(run.style.color);
        SetTextColor(dc, color);
        int baseline = run.y + run.baseline - scrollY_;
        TextOutW(dc, run.x, baseline, run.text.c_str(), static_cast<int>(run.text.size()));

        if (run.style.underline) {
            drawLine(dc, run.x, baseline + std::max(1, scale(2)), run.x + run.width, color,
                     scale(1));
        }
        if (run.style.strike) {
            int mid = baseline - fonts_.ascent(run.style.font) / 3;
            drawLine(dc, run.x, mid, run.x + run.width, color, scale(1));
        }
    }
    if (previousFont) SelectObject(dc, previousFont);
}

// ============================================================== AppWindow

bool AppWindow::create(HINSTANCE instance, int showCommand, const std::wstring& initialFile) {
    instance_ = instance;
    enableDarkModeSupport();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &AppWindow::windowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kFrameClass;
    if (!RegisterClassExW(&wc)) return false;
    if (!DocumentView::registerWindowClass(instance)) return false;
    if (!OutlinePanel::registerWindowClass(instance)) return false;

    theme_ = makeTheme(systemUsesDarkMode());
    WindowState saved = loadWindowState();
    startExpanded_ = saved.outlineExpanded;

    HMENU fileMenu = CreatePopupMenu();
    AppendMenuW(fileMenu, MF_STRING, ID_FILE_OPEN, L"&Open...\tCtrl+O");
    AppendMenuW(fileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(fileMenu, MF_STRING, ID_FILE_EXIT, L"E&xit");

    HMENU editMenu = CreatePopupMenu();
    AppendMenuW(editMenu, MF_STRING | MF_GRAYED, ID_EDIT_COPY, L"&Copy\tCtrl+C");

    HMENU aboutMenu = CreatePopupMenu();
    AppendMenuW(aboutMenu, MF_STRING, ID_ABOUT_PROJECT, L"&Simple Markdown Viewer");

    HMENU menuBar = CreateMenu();
    AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(fileMenu), L"&File");
    AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(editMenu), L"&Edit");
    AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(aboutMenu), L"&About");

    int x = CW_USEDEFAULT, y = CW_USEDEFAULT, width = 1040, height = 780;
    if (saved.valid) {
        x = saved.bounds.left;
        y = saved.bounds.top;
        width = saved.bounds.right - saved.bounds.left;
        height = saved.bounds.bottom - saved.bounds.top;
    }

    hwnd_ = CreateWindowExW(0, kFrameClass, kAppTitle,
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, width, height,
                            nullptr, menuBar, instance, this);
    if (!hwnd_) return false;

    ACCEL accelerators[] = {
        {FVIRTKEY | FCONTROL, 'O', ID_FILE_OPEN},
        {FVIRTKEY | FCONTROL, 'C', ID_EDIT_COPY},
        {FVIRTKEY | FCONTROL, 'F', ID_FOCUS_SEARCH},
        {FVIRTKEY, VK_F3, ID_FIND_NEXT},
        {FVIRTKEY | FSHIFT, VK_F3, ID_FIND_PREVIOUS},
    };
    accelerators_ = CreateAcceleratorTableW(accelerators, ARRAYSIZE(accelerators));

    if (!initialFile.empty() && view_.loadFile(initialFile)) {
        updateTitle();
    } else {
        view_.showWelcome();
    }

    ShowWindow(hwnd_, saved.maximized ? SW_SHOWMAXIMIZED : showCommand);
    UpdateWindow(hwnd_);
    SetFocus(view_.hwnd());
    return true;
}

void AppWindow::persistWindowState() {
    WINDOWPLACEMENT placement = {sizeof(placement)};
    if (!GetWindowPlacement(hwnd_, &placement)) return;

    WindowState state;
    state.bounds = placement.rcNormalPosition;
    state.maximized = placement.showCmd == SW_SHOWMAXIMIZED ||
                      (placement.flags & WPF_RESTORETOMAXIMIZED) != 0;
    state.outlineExpanded = outline_.expanded();
    saveWindowState(state);
}

void AppWindow::refreshOutline() {
    outline_.setEntries(view_.outline());
    outline_.setActiveIndex(view_.activeOutlineIndex());
}

int AppWindow::runMessageLoop() {
    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (accelerators_ && TranslateAcceleratorW(hwnd_, accelerators_, &message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

LRESULT CALLBACK AppWindow::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    AppWindow* self = reinterpret_cast<AppWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<AppWindow*>(cs->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(hwnd, message, wParam, lParam);
    return self->handleMessage(message, wParam, lParam);
}

int AppWindow::barHeight() const {
    int fieldHeight = std::max(scale(26), scale(24));
    return scale(10) * 2 + fieldHeight;
}

LRESULT AppWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            dpi_ = windowDpi(hwnd_);

            NONCLIENTMETRICSW ncm = {sizeof(ncm)};
            SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0,
                                       static_cast<UINT>(dpi_));
            uiFont_ = CreateFontIndirectW(&ncm.lfMessageFont);
            menuFont_ = CreateFontIndirectW(&ncm.lfMenuFont);

            searchField_ = CreateWindowExW(0, L"EDIT", L"",
                                           WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                               ES_AUTOHSCROLL | ES_LEFT,
                                           0, 0, 10, 10, hwnd_,
                                           reinterpret_cast<HMENU>(ID_SEARCH_FIELD), instance_,
                                           nullptr);
            SendMessageW(searchField_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
            SendMessageW(searchField_, EM_SETCUEBANNER, TRUE,
                         reinterpret_cast<LPARAM>(L"Search"));
            SetWindowSubclass(searchField_, &AppWindow::searchEditProc, 1,
                              reinterpret_cast<DWORD_PTR>(this));

            searchButton_ = CreateWindowExW(0, L"BUTTON", L"Search",
                                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                            0, 0, 10, 10,
                                            hwnd_, reinterpret_cast<HMENU>(ID_SEARCH_BUTTON),
                                            instance_, nullptr);
            SendMessageW(searchButton_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
            // Same subclass: Enter searches, Escape clears, Tab moves on.
            SetWindowSubclass(searchButton_, &AppWindow::searchEditProc, 2,
                              reinterpret_cast<DWORD_PTR>(this));

            outline_.create(hwnd_, instance_);
            outline_.setDpi(dpi_);
            outline_.setUiFont(uiFont_);
            outline_.setExpanded(startExpanded_);
            outline_.setOnToggle([this]() {
                outline_.setExpanded(!outline_.expanded());
                layoutChildren();
            });
            outline_.setOnSelect([this](size_t index) { view_.scrollToOutlineEntry(index); });

            view_.create(hwnd_, instance_);
            applyTheme();
            layoutChildren();
            return 0;
        }

        case WM_SIZE:
            layoutChildren();
            return 0;

        case WM_GETMINMAXINFO: {
            MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = 640;
            info->ptMinTrackSize.y = 480;
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps = {};
            HDC dc = BeginPaint(hwnd_, &ps);
            paintChrome(dc);
            EndPaint(hwnd_, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, searchFailed_ ? theme_.fieldError : theme_.fieldText);
            SetBkColor(dc, theme_.fieldBackground);
            return reinterpret_cast<LRESULT>(fieldBrush_);
        }

        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (item->CtlID != ID_SEARCH_BUTTON) break;
            bool pressed = (item->itemState & ODS_SELECTED) != 0;
            COLORREF face = pressed ? theme_.buttonPressed
                                    : (buttonHot_ ? theme_.buttonHot : theme_.buttonFace);
            fillRect(item->hDC, item->rcItem, face);
            frameRect(item->hDC, item->rcItem, theme_.buttonBorder);

            SetBkMode(item->hDC, TRANSPARENT);
            SetTextColor(item->hDC, theme_.buttonText);
            HGDIOBJ previous = SelectObject(item->hDC, uiFont_);
            RECT text = item->rcItem;
            DrawTextW(item->hDC, L"Search", -1, &text,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            SelectObject(item->hDC, previous);
            if (item->itemState & ODS_FOCUS) {
                RECT focusRect = item->rcItem;
                InflateRect(&focusRect, -scale(3), -scale(3));
                DrawFocusRect(item->hDC, &focusRect);
            }
            return TRUE;
        }

        case WM_MOUSEMOVE: {
            POINT p = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            RECT button = {};
            GetWindowRect(searchButton_, &button);
            MapWindowPoints(nullptr, hwnd_, reinterpret_cast<POINT*>(&button), 2);
            bool hot = PtInRect(&button, p) != FALSE;
            if (hot != buttonHot_) {
                buttonHot_ = hot;
                InvalidateRect(searchButton_, nullptr, TRUE);
                TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, hwnd_, 0};
                TrackMouseEvent(&track);
            }
            return 0;
        }

        case WM_MOUSELEAVE:
            if (buttonHot_) {
                buttonHot_ = false;
                InvalidateRect(searchButton_, nullptr, TRUE);
            }
            return 0;

        case WM_MOUSEWHEEL:
            return SendMessageW(view_.hwnd(), message, wParam, lParam);

        case WM_INITMENUPOPUP: {
            bool editHasSelection = false;
            if (GetFocus() == searchField_) {
                DWORD start = 0, end = 0;
                SendMessageW(searchField_, EM_GETSEL, reinterpret_cast<WPARAM>(&start),
                             reinterpret_cast<LPARAM>(&end));
                editHasSelection = start != end;
            }
            EnableMenuItem(GetMenu(hwnd_), ID_EDIT_COPY,
                           MF_BYCOMMAND |
                               ((view_.hasSelection() || editHasSelection) ? MF_ENABLED
                                                                          : MF_GRAYED));
            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int notification = HIWORD(wParam);
            switch (id) {
                case ID_FILE_OPEN:
                    openFileDialog();
                    return 0;
                case ID_FILE_EXIT:
                    DestroyWindow(hwnd_);
                    return 0;
                case ID_EDIT_COPY:
                    if (GetFocus() == searchField_) SendMessageW(searchField_, WM_COPY, 0, 0);
                    else view_.copySelection();
                    return 0;
                case ID_ABOUT_PROJECT:
                    ShellExecuteW(hwnd_, L"open", kProjectUrl, nullptr, nullptr, SW_SHOWNORMAL);
                    return 0;
                case ID_SEARCH_BUTTON:
                    if (notification == BN_CLICKED) doSearch(true);
                    return 0;
                case ID_FIND_NEXT:
                    doSearch(true);
                    return 0;
                case ID_FIND_PREVIOUS:
                    doSearch(false);
                    return 0;
                case ID_FOCUS_SEARCH:
                    SetFocus(searchField_);
                    SendMessageW(searchField_, EM_SETSEL, 0, -1);
                    return 0;
                case ID_SEARCH_FIELD:
                    if (notification == EN_CHANGE && searchFailed_) {
                        searchFailed_ = false;
                        InvalidateRect(searchField_, nullptr, TRUE);
                    }
                    return 0;
                case ID_VIEW_SCROLLED:
                    outline_.setActiveIndex(view_.activeOutlineIndex());
                    return 0;
                case ID_VIEW_DOCUMENT_CHANGED:
                    refreshOutline();
                    return 0;
                default:
                    break;
            }
            break;
        }

        case kUahDrawMenu: {
            if (!theme_.dark) break;
            UahMenu* menu = reinterpret_cast<UahMenu*>(lParam);
            RECT bar = {};
            if (!menuBarRect(hwnd_, &bar)) break;
            fillRect(menu->dc, bar, theme_.menuBackground);
            return TRUE;
        }

        case kUahDrawMenuItem: {
            if (!theme_.dark) break;
            UahDrawMenuItem* draw = reinterpret_cast<UahDrawMenuItem*>(lParam);

            wchar_t label[128] = {0};
            MENUITEMINFOW info = {sizeof(info)};
            info.fMask = MIIM_STRING;
            info.dwTypeData = label;
            info.cch = ARRAYSIZE(label) - 1;
            GetMenuItemInfoW(draw->menu.menu, static_cast<UINT>(draw->menuItem.position), TRUE,
                             &info);

            bool highlighted = (draw->item.itemState & (ODS_HOTLIGHT | ODS_SELECTED)) != 0;
            fillRect(draw->menu.dc, draw->item.rcItem,
                     highlighted ? theme_.menuHot : theme_.menuBackground);

            UINT flags = DT_CENTER | DT_SINGLELINE | DT_VCENTER;
            if (draw->item.itemState & ODS_NOACCEL) flags |= DT_HIDEPREFIX;

            SetBkMode(draw->menu.dc, TRANSPARENT);
            SetTextColor(draw->menu.dc, (draw->item.itemState & ODS_GRAYED)
                                            ? theme_.role(view::ColorRole::Muted)
                                            : theme_.menuText);
            HGDIOBJ previous = SelectObject(draw->menu.dc, menuFont_);
            DrawTextW(draw->menu.dc, label, -1, &draw->item.rcItem, flags);
            SelectObject(draw->menu.dc, previous);
            return TRUE;
        }

        case WM_NCACTIVATE:
        case WM_NCPAINT: {
            LRESULT result = DefWindowProcW(hwnd_, message, wParam, lParam);
            if (theme_.dark) {
                // Paint over the light one pixel seam the frame leaves under the
                // menu bar.
                RECT bar = {};
                if (menuBarRect(hwnd_, &bar)) {
                    RECT seam = {bar.left, bar.bottom, bar.right, bar.bottom + 1};
                    HDC dc = GetWindowDC(hwnd_);
                    fillRect(dc, seam, theme_.menuBackground);
                    ReleaseDC(hwnd_, dc);
                }
            }
            return result;
        }

        case WM_SETTINGCHANGE:
            if (lParam && CompareStringOrdinal(reinterpret_cast<const wchar_t*>(lParam), -1,
                                               L"ImmersiveColorSet", -1, TRUE) == CSTR_EQUAL) {
                applyTheme();
            }
            return 0;

        case WM_DPICHANGED: {
            dpi_ = HIWORD(wParam);
            if (uiFont_) DeleteObject(uiFont_);
            if (menuFont_) DeleteObject(menuFont_);
            NONCLIENTMETRICSW ncm = {sizeof(ncm)};
            SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0,
                                       static_cast<UINT>(dpi_));
            uiFont_ = CreateFontIndirectW(&ncm.lfMessageFont);
            menuFont_ = CreateFontIndirectW(&ncm.lfMenuFont);
            SendMessageW(searchField_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
            SendMessageW(searchButton_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
            outline_.setUiFont(uiFont_);
            outline_.setDpi(dpi_);
            view_.setDpi(dpi_);

            RECT* suggested = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            layoutChildren();
            return 0;
        }

        case WM_SETFOCUS:
            SetFocus(view_.hwnd());
            return 0;

        case WM_DESTROY:
            persistWindowState();
            if (fieldBrush_) DeleteObject(fieldBrush_);
            if (uiFont_) DeleteObject(uiFont_);
            if (menuFont_) DeleteObject(menuFont_);
            if (accelerators_) DestroyAcceleratorTable(accelerators_);
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

LRESULT CALLBACK AppWindow::searchEditProc(HWND hwnd, UINT message, WPARAM wParam,
                                           LPARAM lParam, UINT_PTR id, DWORD_PTR data) {
    AppWindow* self = reinterpret_cast<AppWindow*>(data);
    switch (message) {
        case WM_KEYDOWN:
            if (wParam == VK_RETURN) {
                self->doSearch(GetKeyState(VK_SHIFT) >= 0);
                return 0;
            }
            if (wParam == VK_TAB) {
                HWND next = GetNextDlgTabItem(GetParent(hwnd), hwnd, GetKeyState(VK_SHIFT) < 0);
                if (next) SetFocus(next);
                return 0;
            }
            if (wParam == VK_ESCAPE) {
                self->view_.clearSearch();
                self->searchFailed_ = false;
                InvalidateRect(hwnd, nullptr, TRUE);
                SetFocus(self->view_.hwnd());
                return 0;
            }
            break;
        case WM_CHAR:
            // Swallow the beep the edit control makes for unhandled Return/Escape.
            if (wParam == VK_RETURN || wParam == VK_ESCAPE) return 0;
            break;
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, &AppWindow::searchEditProc, id);
            break;
        default:
            break;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

void AppWindow::layoutChildren() {
    RECT client = {};
    GetClientRect(hwnd_, &client);

    const int pad = scale(10);
    const int gap = scale(10);
    const int bar = barHeight();
    const int fieldHeight = bar - pad * 2;

    HDC dc = GetDC(hwnd_);
    HGDIOBJ previous = SelectObject(dc, uiFont_);
    SIZE textSize = {};
    GetTextExtentPoint32W(dc, L"Search", 6, &textSize);
    SelectObject(dc, previous);
    ReleaseDC(hwnd_, dc);

    const int textWidth = static_cast<int>(textSize.cx);
    const int textHeight = static_cast<int>(textSize.cy);

    int buttonWidth = std::max(scale(88), textWidth + scale(32));
    int buttonX = client.right - pad - buttonWidth;
    int fieldWidth = std::max(scale(60), buttonX - gap - pad);

    // The edit control sits inside the painted field frame so its text is
    // vertically centred.
    int inset = scale(5);
    int editHeight = std::min(fieldHeight - inset, textHeight + scale(2));
    MoveWindow(searchField_, pad + inset, pad + (fieldHeight - editHeight) / 2,
               fieldWidth - inset * 2, editHeight, TRUE);
    MoveWindow(searchButton_, buttonX, pad, buttonWidth, fieldHeight, TRUE);

    // Below the bar: the outline panel is flush to the left edge, and the
    // scrolling document sits in a box inset by the gutter on every side.
    const int margin = gutter();
    // Never let the outline take more than a third of a narrow window.
    const int panelWidth =
        std::max(outline_.collapsedWidth(),
                 std::min(outline_.panelWidth(), static_cast<int>(client.right) / 3));
    const int belowBar = std::max(0, static_cast<int>(client.bottom) - bar);
    MoveWindow(outline_.hwnd(), 0, bar, panelWidth, belowBar, TRUE);

    int viewLeft = panelWidth + margin;
    int viewWidth = std::max(scale(120), static_cast<int>(client.right) - viewLeft - margin);
    int viewHeight = std::max(0, belowBar - margin * 2);
    MoveWindow(view_.hwnd(), viewLeft, bar + margin, viewWidth, viewHeight, TRUE);

    InvalidateRect(hwnd_, nullptr, FALSE);
}

void AppWindow::paintChrome(HDC dc) {
    RECT client = {};
    GetClientRect(hwnd_, &client);

    const int pad = scale(10);
    const int bar = barHeight();
    const int border = std::max(1, scale(1));

    // The gutter around the document box shares the document's background so the
    // scrolling area reads as an inset box.
    RECT below = {0, bar, client.right, client.bottom};
    fillRect(dc, below, theme_.background);

    RECT barRect = {0, 0, client.right, bar};
    fillRect(dc, barRect, theme_.barBackground);

    RECT borderRect = {0, bar - border, client.right, bar};
    fillRect(dc, borderRect, theme_.barBorder);

    RECT fieldRect = {};
    GetWindowRect(searchField_, &fieldRect);
    MapWindowPoints(nullptr, hwnd_, reinterpret_cast<POINT*>(&fieldRect), 2);
    int inset = scale(5);
    RECT visual = {pad, pad, fieldRect.right + inset, bar - pad};
    fillRect(dc, visual, theme_.fieldBackground);
    frameRect(dc, visual, theme_.fieldBorder);
}

void AppWindow::applyTheme() {
    theme_ = makeTheme(systemUsesDarkMode());
    applyWindowTheme(hwnd_, theme_.dark);
    if (fieldBrush_) DeleteObject(fieldBrush_);
    fieldBrush_ = CreateSolidBrush(theme_.fieldBackground);
    view_.setTheme(theme_);
    outline_.setTheme(theme_);
    InvalidateRect(hwnd_, nullptr, TRUE);
    if (searchField_) InvalidateRect(searchField_, nullptr, TRUE);
    if (searchButton_) InvalidateRect(searchButton_, nullptr, TRUE);
    DrawMenuBar(hwnd_);
}

void AppWindow::openFileDialog() {
    wchar_t path[MAX_PATH * 2] = {0};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = L"Markdown files\0*.md;*.markdown;*.mdown;*.mkd;*.mdtxt;*.text\0"
                      L"Text files\0*.txt\0All files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = ARRAYSIZE(path);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = L"Open Markdown File";

    if (!GetOpenFileNameW(&ofn)) return;
    if (!view_.loadFile(path)) {
        MessageBoxW(hwnd_, L"That file could not be opened.", kAppTitle, MB_ICONWARNING | MB_OK);
        return;
    }
    searchFailed_ = false;
    updateTitle();
    SetFocus(view_.hwnd());
}

void AppWindow::doSearch(bool forward) {
    wchar_t buffer[512] = {0};
    GetWindowTextW(searchField_, buffer, ARRAYSIZE(buffer));
    std::wstring needle = buffer;
    if (needle.empty()) {
        SetFocus(searchField_);
        return;
    }
    bool found = view_.findText(needle, forward);
    if (found == searchFailed_) {
        searchFailed_ = !found;
        InvalidateRect(searchField_, nullptr, TRUE);
    }
    searchFailed_ = !found;
    if (!found) MessageBeep(MB_OK);
}

void AppWindow::updateTitle() {
    const std::wstring& path = view_.filePath();
    if (path.empty()) {
        SetWindowTextW(hwnd_, kAppTitle);
        return;
    }
    size_t slash = path.find_last_of(L"\\/");
    std::wstring name = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
    SetWindowTextW(hwnd_, (name + L" - " + kAppTitle).c_str());
}

} // namespace app
