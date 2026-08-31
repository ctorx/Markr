#include "win_viewer.h"

#include <algorithm>
#include <commctrl.h>
#include <commdlg.h>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <shellapi.h>
#include <shlwapi.h>
#include <string>
#include <windowsx.h>

namespace app {
namespace {

const wchar_t* const kAppTitle = L"Markr";
const wchar_t* const kProjectUrl = L"https://github.com/ctorx/SimpleMarkDownViewer";
const wchar_t* const kViewClass = L"MarkrDocumentView";
const wchar_t* const kFrameClass = L"MarkrFrameWindow";

enum Command {
    ID_FILE_NEW = 1000,
    ID_FILE_OPEN,
    ID_FILE_SAVE,
    ID_FILE_EXIT,
    ID_EDIT_COPY,
    ID_EDIT_MODE,
    ID_EDIT_UNDO,
    ID_EDIT_REDO,
    ID_EDIT_CUT,
    ID_EDIT_PASTE,
    ID_EDIT_SELECTALL,
    ID_ABOUT_PROJECT,
    ID_SEARCH_FIELD,
    ID_SEARCH_BUTTON,
    ID_FIND_NEXT,
    ID_FIND_PREVIOUS,
    ID_FOCUS_SEARCH,
    ID_FIND_REPLACE,
    ID_VIEW_SCROLLED,
    ID_VIEW_DOCUMENT_CHANGED,
    ID_RELOAD,
    ID_NAV_BACK,
    ID_NAV_FORWARD,
    ID_ZOOM_IN,
    ID_ZOOM_OUT,
    ID_ZOOM_RESET,
    ID_FORMAT_BOLD,
    ID_FORMAT_ITALIC,
};

// Commands on the document's own right-click menu. They stay local to the view:
// the menu is tracked with TPM_RETURNCMD, so they never reach a message loop.
enum SelectionMenuCommand {
    ID_COPY_MARKDOWN = 1,
    ID_COPY_FORMATTED,
};

// Polls the open document for external edits.
constexpr UINT_PTR kFileWatchTimer = 1;

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

std::wstring toCrlf(const std::wstring& text) {
    std::wstring out;
    out.reserve(text.size() + 16);
    for (wchar_t c : text) {
        if (c == L'\n') out += L"\r\n";
        else out.push_back(c);
    }
    return out;
}

HGLOBAL globalCopy(const void* data, size_t bytes) {
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!handle) return nullptr;
    void* target = GlobalLock(handle);
    if (!target) {
        GlobalFree(handle);
        return nullptr;
    }
    memcpy(target, data, bytes);
    GlobalUnlock(handle);
    return handle;
}

// Wraps an HTML fragment in the CF_HTML descriptor, whose byte offsets point at
// the fragment inside the finished buffer.
std::string makeClipboardHtml(const std::string& fragment) {
    const char* const kOpen = "<html>\r\n<body>\r\n<!--StartFragment-->";
    const char* const kClose = "<!--EndFragment-->\r\n</body>\r\n</html>";
    const char* const kHeaderFormat =
        "Version:0.9\r\nStartHTML:%010u\r\nEndHTML:%010u\r\n"
        "StartFragment:%010u\r\nEndFragment:%010u\r\n";

    char header[256] = {0};
    // The offsets are fixed-width, so the header written with zeros is exactly as
    // long as the final one.
    int headerLength = snprintf(header, sizeof(header), kHeaderFormat, 0u, 0u, 0u, 0u);
    if (headerLength <= 0) return std::string();

    unsigned startHtml = static_cast<unsigned>(headerLength);
    unsigned startFragment = startHtml + static_cast<unsigned>(strlen(kOpen));
    unsigned endFragment = startFragment + static_cast<unsigned>(fragment.size());
    unsigned endHtml = endFragment + static_cast<unsigned>(strlen(kClose));
    snprintf(header, sizeof(header), kHeaderFormat, startHtml, endHtml, startFragment,
             endFragment);

    return std::string(header) + kOpen + fragment + kClose;
}

// Puts the selection on the clipboard as text, optionally with an HTML flavour
// alongside it for applications that accept rich text.
void setClipboard(HWND owner, const std::wstring& text, const std::wstring* html) {
    std::wstring plain = toCrlf(text);
    if (!OpenClipboard(owner)) return;
    EmptyClipboard();

    HGLOBAL unicode = globalCopy(plain.c_str(), (plain.size() + 1) * sizeof(wchar_t));
    if (unicode && !SetClipboardData(CF_UNICODETEXT, unicode)) GlobalFree(unicode);

    if (html && !html->empty()) {
        static const UINT htmlFormat = RegisterClipboardFormatW(L"HTML Format");
        std::string buffer = makeClipboardHtml(view::toUtf8(*html));
        if (htmlFormat && !buffer.empty()) {
            HGLOBAL handle = globalCopy(buffer.c_str(), buffer.size() + 1);
            if (handle && !SetClipboardData(htmlFormat, handle)) GlobalFree(handle);
        }
    }
    CloseClipboard();
}

bool hasUriScheme(const std::wstring& url) {
    size_t colon = url.find(L':');
    if (colon == std::wstring::npos || colon < 2) return false; // "C:\..." is a path
    size_t slash = url.find_first_of(L"/\\");
    if (slash != std::wstring::npos && slash < colon) return false;
    for (size_t i = 0; i < colon; ++i) {
        wchar_t c = url[i];
        if (!iswalnum(c) && c != L'+' && c != L'-' && c != L'.') return false;
    }
    return true;
}

std::wstring percentDecode(const std::wstring& text) {
    std::wstring out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'%' && i + 2 < text.size() && iswxdigit(text[i + 1]) &&
            iswxdigit(text[i + 2])) {
            wchar_t buffer[3] = {text[i + 1], text[i + 2], 0};
            out.push_back(static_cast<wchar_t>(wcstol(buffer, nullptr, 16)));
            i += 2;
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
}

bool isMarkdownPath(const std::wstring& path) {
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;
    std::wstring extension = path.substr(dot + 1);
    for (wchar_t& c : extension) c = static_cast<wchar_t>(towlower(c));
    return extension == L"md" || extension == L"markdown" || extension == L"mdown" ||
           extension == L"mkd" || extension == L"mdtxt" || extension == L"text" ||
           extension == L"txt";
}

std::wstring directoryOf(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? std::wstring() : path.substr(0, slash);
}

} // namespace

std::string readDocumentFile(const std::wstring& path, bool* ok) {
    return readFileUtf8(path, ok);
}

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
            fonts_.rebuild(contentDpi());
            theme_ = makeTheme(systemUsesDarkMode());
            DragAcceptFiles(hwnd_, TRUE);
            SetTimer(hwnd_, kFileWatchTimer, 1000, nullptr);
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd_, kFileWatchTimer);
            return 0;

        case WM_TIMER:
            if (wParam == kFileWatchTimer && !path_.empty() && fileChangedOnDisk()) reload();
            return 0;

        case WM_DROPFILES: {
            HDROP drop = reinterpret_cast<HDROP>(wParam);
            wchar_t path[MAX_PATH * 2] = {0};
            if (DragQueryFileW(drop, 0, path, ARRAYSIZE(path)) > 0) {
                back_.clear();
                forward_.clear();
                loadFile(path);
            }
            DragFinish(drop);
            SetFocus(hwnd_);
            return 0;
        }

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
            if (GetKeyState(VK_CONTROL) < 0) {
                adjustZoom(delta > 0 ? 10 : -10);
                return 0;
            }
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
                case VK_BACK:
                    goBack();
                    return 0;
                case VK_OEM_PLUS:
                case VK_ADD:
                    if (GetKeyState(VK_CONTROL) < 0) adjustZoom(10);
                    return 0;
                case VK_OEM_MINUS:
                case VK_SUBTRACT:
                    if (GetKeyState(VK_CONTROL) < 0) adjustZoom(-10);
                    return 0;
                case '0':
                case VK_NUMPAD0:
                    if (GetKeyState(VK_CONTROL) < 0) setZoomPercent(100);
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
                        activateLink(layout_.links[link].url);
                    }
                }
            }
            return 0;
        }

        case WM_RBUTTONDOWN:
            // Keep the selection: the menu acts on it. Focus follows the click so
            // Ctrl+C still lands here afterwards.
            SetFocus(hwnd_);
            return 0;

        case WM_CONTEXTMENU: {
            if (reinterpret_cast<HWND>(wParam) != hwnd_) break;
            // Nothing selected, nothing to copy: no menu at all.
            if (!hasSelection()) return 0;
            POINT where = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            // -1 means the keyboard asked for it (Shift+F10 or the menu key).
            if (lParam == static_cast<LPARAM>(-1)) where = selectionMenuPoint();
            showSelectionMenu(where.x, where.y);
            return 0;
        }

        case WM_XBUTTONUP:
            if (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) goBack();
            else if (GET_XBUTTON_WPARAM(wParam) == XBUTTON2) goForward();
            return TRUE;

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
    fonts_.rebuild(contentDpi());
    relayout();
}

void DocumentView::setZoomPercent(int percent) {
    percent = std::max(50, std::min(300, percent));
    if (percent == zoomPercent_) return;
    zoomPercent_ = percent;
    fonts_.rebuild(contentDpi());
    relayoutKeepingPosition();
}

void DocumentView::adjustZoom(int delta) { setZoomPercent(zoomPercent_ + delta); }

void DocumentView::relayoutKeepingPosition() {
    // Keep the same part of the document in view across a zoom or reflow.
    double ratio = layout_.contentHeight > 0
                       ? static_cast<double>(scrollY_) / layout_.contentHeight
                       : 0.0;
    relayout();
    int target = static_cast<int>(ratio * layout_.contentHeight);
    scrollY_ = 0;
    scrollTo(target);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool DocumentView::loadFile(const std::wstring& path) {
    bool ok = false;
    std::string utf8 = readFileUtf8(path, &ok);
    if (!ok) return false;
    setDocument(std::move(utf8), path);
    return true;
}

void DocumentView::rememberFileStamp() {
    lastWriteTime_ = FILETIME{0, 0};
    lastFileSize_ = 0;
    if (path_.empty()) return;

    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (!GetFileAttributesExW(path_.c_str(), GetFileExInfoStandard, &data)) return;
    lastWriteTime_ = data.ftLastWriteTime;
    lastFileSize_ = (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) |
                    data.nFileSizeLow;
}

bool DocumentView::fileChangedOnDisk() const {
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (!GetFileAttributesExW(path_.c_str(), GetFileExInfoStandard, &data)) return false;
    unsigned long long size =
        (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    return size != lastFileSize_ ||
           CompareFileTime(&data.ftLastWriteTime, &lastWriteTime_) != 0;
}

void DocumentView::reload() {
    if (path_.empty()) return;

    bool ok = false;
    std::string utf8 = readFileUtf8(path_, &ok);
    if (!ok) {
        rememberFileStamp(); // stop retrying until it changes again
        return;
    }

    int scroll = scrollY_;
    std::wstring term = searchTerm_;
    std::wstring path = path_;

    setDocument(std::move(utf8), path);

    scrollTo(scroll);
    if (!term.empty()) {
        searchTerm_ = term;
        matches_ = view::findAll(layout_.text, term, false);
        currentMatch_ = -1;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void DocumentView::showWelcome() {
    // An empty untitled document; the frame opens it in edit mode.
    setDocument(std::string(), std::wstring());
}

void DocumentView::showText(std::string utf8, const std::wstring& path) {
    setDocument(std::move(utf8), path);
}

void DocumentView::selectAll() {
    selectionAnchor_ = 0;
    selectionFocus_ = layout_.text.size();
    InvalidateRect(hwnd_, nullptr, FALSE);
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
    rememberFileStamp();
    relayout();
    InvalidateRect(hwnd_, nullptr, TRUE);
    notifyParent(ID_VIEW_DOCUMENT_CHANGED);
}

void DocumentView::activateLink(const std::wstring& url) {
    if (url.empty()) return;

    if (url[0] == L'#') {
        jumpToAnchor(percentDecode(url.substr(1)), true);
        return;
    }
    if (hasUriScheme(url)) {
        ShellExecuteW(hwnd_, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }

    // A document-relative path, optionally with a #fragment.
    std::wstring target = url;
    std::wstring fragment;
    size_t hash = target.find(L'#');
    if (hash != std::wstring::npos) {
        fragment = percentDecode(target.substr(hash + 1));
        target = target.substr(0, hash);
    }
    if (target.empty()) {
        jumpToAnchor(fragment, true);
        return;
    }

    target = percentDecode(target);
    for (wchar_t& c : target) {
        if (c == L'/') c = L'\\';
    }

    std::wstring resolved = target;
    std::wstring base = directoryOf(path_);
    if (PathIsRelativeW(target.c_str()) && !base.empty()) {
        wchar_t combined[MAX_PATH * 2] = {0};
        PathCombineW(combined, base.c_str(), target.c_str());
        resolved = combined;
    }

    if (isMarkdownPath(resolved) &&
        GetFileAttributesW(resolved.c_str()) != INVALID_FILE_ATTRIBUTES) {
        navigateToFile(resolved, fragment);
        return;
    }
    ShellExecuteW(hwnd_, L"open", resolved.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

bool DocumentView::jumpToAnchor(const std::wstring& anchor, bool record) {
    int index = view::outlineIndexForAnchor(layout_, anchor);
    if (index < 0) {
        MessageBeep(MB_OK);
        return false;
    }
    if (record) recordHistory();
    scrollTo(layout_.outline[index].y - scale(8));
    return true;
}

void DocumentView::navigateToFile(const std::wstring& path, const std::wstring& anchor) {
    recordHistory();
    if (!loadFile(path)) {
        if (!back_.empty()) back_.pop_back();
        return;
    }
    if (!anchor.empty()) jumpToAnchor(anchor, false);
}

void DocumentView::recordHistory() {
    HistoryEntry entry;
    entry.path = path_;
    entry.scrollY = scrollY_;
    back_.push_back(entry);
    forward_.clear();
}

void DocumentView::restoreHistory(const std::wstring& path, int scroll) {
    if (path != path_) {
        if (path.empty()) {
            showWelcome();
        } else if (!loadFile(path)) {
            return;
        }
    }
    scrollTo(scroll);
}

void DocumentView::goBack() {
    if (back_.empty()) return;
    HistoryEntry current;
    current.path = path_;
    current.scrollY = scrollY_;

    HistoryEntry target = back_.back();
    back_.pop_back();
    restoreHistory(target.path, target.scrollY);
    forward_.push_back(current);
}

void DocumentView::goForward() {
    if (forward_.empty()) return;
    HistoryEntry current;
    current.path = path_;
    current.scrollY = scrollY_;

    HistoryEntry target = forward_.back();
    forward_.pop_back();
    restoreHistory(target.path, target.scrollY);
    back_.push_back(current);
}

void DocumentView::relayout() {
    RECT client = {};
    GetClientRect(hwnd_, &client);
    int width = std::max(scale(200), static_cast<int>(client.right - client.left));

    view::Metrics metrics;
    metrics.padding = scale(20);
    metrics.blockSpacing = scale(12);
    metrics.headingRuleGap = scale(9);
    metrics.headingRuleSpacing = scale(16);
    metrics.sectionSpacing = scale(20);
    metrics.listIndent = scale(26);
    metrics.quoteIndent = scale(18);
    metrics.quoteBarWidth = std::max(2, scale(5));
    metrics.codePadding = scale(16);
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
    setClipboard(hwnd_, text, nullptr);
}

void DocumentView::copySelectionMarkdown() const {
    std::wstring markdown =
        view::selectionMarkdown(document_, layout_, selectionStart(), selectionEnd());
    if (markdown.empty()) return;
    setClipboard(hwnd_, markdown, nullptr);
}

void DocumentView::copySelectionFormatted() const {
    std::wstring html =
        view::selectionHtml(document_, layout_, selectionStart(), selectionEnd());
    if (html.empty()) return;
    // The plain flavour is what applications that ignore HTML will paste.
    std::wstring text = view::extractText(layout_, selectionStart(), selectionEnd());
    setClipboard(hwnd_, text, &html);
}

POINT DocumentView::selectionMenuPoint() const {
    RECT client = {};
    GetClientRect(hwnd_, &client);
    POINT point = {client.left + (client.right - client.left) / 4,
                   client.top + (client.bottom - client.top) / 4};

    std::vector<view::Rect> rects;
    view::selectionRects(layout_, selectionStart(), selectionEnd(), rects);
    if (!rects.empty()) {
        const view::Rect& first = rects.front();
        int y = first.y + first.height - scrollY_;
        // Only follow the selection while it is on screen.
        if (y >= client.top && y <= client.bottom) {
            point.x = first.x;
            point.y = y;
        }
    }
    ClientToScreen(hwnd_, &point);
    return point;
}

void DocumentView::showSelectionMenu(int screenX, int screenY) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, ID_COPY_MARKDOWN, L"Copy &Markdown");
    AppendMenuW(menu, MF_STRING, ID_COPY_FORMATTED, L"Copy &Formatted");

    SetFocus(hwnd_);
    int choice = TrackPopupMenuEx(menu,
                                  TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN |
                                      TPM_RIGHTBUTTON,
                                  screenX, screenY, hwnd_, nullptr);
    DestroyMenu(menu);

    if (choice == ID_COPY_MARKDOWN) copySelectionMarkdown();
    else if (choice == ID_COPY_FORMATTED) copySelectionFormatted();
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
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kFrameClass;
    if (!RegisterClassExW(&wc)) return false;
    if (!DocumentView::registerWindowClass(instance)) return false;
    if (!OutlinePanel::registerWindowClass(instance)) return false;
    if (!EditorPane::registerWindowClass(instance)) return false;

    theme_ = makeTheme(systemUsesDarkMode());
    WindowState saved = loadWindowState();
    startExpanded_ = saved.outlineExpanded;
    startZoom_ = saved.zoomPercent;
    startEditorZoom_ = saved.editorZoomPercent;

    // The menus stay unattached: the window draws its own strip, because owning
    // the caption means owning everything the system would put beside it.
    fileMenu_ = CreatePopupMenu();
    AppendMenuW(fileMenu_, MF_STRING, ID_FILE_NEW, L"&New\tCtrl+N");
    AppendMenuW(fileMenu_, MF_STRING, ID_FILE_OPEN, L"&Open...\tCtrl+O");
    AppendMenuW(fileMenu_, MF_STRING | MF_GRAYED, ID_FILE_SAVE, L"&Save\tCtrl+S");
    AppendMenuW(fileMenu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(fileMenu_, MF_STRING, ID_FILE_EXIT, L"E&xit");

    editMenu_ = CreatePopupMenu();
    AppendMenuW(editMenu_, MF_STRING, ID_EDIT_MODE, L"Edit &Mode\tCtrl+E");
    AppendMenuW(editMenu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(editMenu_, MF_STRING | MF_GRAYED, ID_EDIT_UNDO, L"&Undo\tCtrl+Z");
    AppendMenuW(editMenu_, MF_STRING | MF_GRAYED, ID_EDIT_REDO, L"&Redo\tCtrl+Y");
    AppendMenuW(editMenu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(editMenu_, MF_STRING | MF_GRAYED, ID_EDIT_CUT, L"Cu&t\tCtrl+X");
    AppendMenuW(editMenu_, MF_STRING | MF_GRAYED, ID_EDIT_COPY, L"&Copy\tCtrl+C");
    AppendMenuW(editMenu_, MF_STRING | MF_GRAYED, ID_EDIT_PASTE, L"&Paste\tCtrl+V");
    AppendMenuW(editMenu_, MF_STRING, ID_EDIT_SELECTALL, L"Select &All\tCtrl+A");
    AppendMenuW(editMenu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(editMenu_, MF_STRING, ID_FOCUS_SEARCH, L"&Find\tCtrl+F");
    AppendMenuW(editMenu_, MF_STRING | MF_GRAYED, ID_FIND_REPLACE, L"R&eplace\tCtrl+H");

    aboutMenu_ = CreatePopupMenu();
    AppendMenuW(aboutMenu_, MF_STRING, ID_ABOUT_PROJECT, L"&About Markr");

    int x = CW_USEDEFAULT, y = CW_USEDEFAULT, width = 1040, height = 780;
    if (saved.valid) {
        x = saved.bounds.left;
        y = saved.bounds.top;
        width = saved.bounds.right - saved.bounds.left;
        height = saved.bounds.bottom - saved.bounds.top;
    }

    hwnd_ = CreateWindowExW(0, kFrameClass, kAppTitle,
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, width, height,
                            nullptr, nullptr, instance, this);
    if (!hwnd_) return false;

    ACCEL accelerators[] = {
        {FVIRTKEY | FCONTROL, 'N', ID_FILE_NEW},
        {FVIRTKEY | FCONTROL, 'O', ID_FILE_OPEN},
        {FVIRTKEY | FCONTROL, 'S', ID_FILE_SAVE},
        {FVIRTKEY | FCONTROL, 'E', ID_EDIT_MODE},
        {FVIRTKEY | FCONTROL, 'C', ID_EDIT_COPY},
        {FVIRTKEY | FCONTROL, 'H', ID_FIND_REPLACE},
        {FVIRTKEY | FCONTROL, 'B', ID_FORMAT_BOLD},
        {FVIRTKEY | FCONTROL, 'I', ID_FORMAT_ITALIC},
        {FVIRTKEY | FCONTROL, 'F', ID_FOCUS_SEARCH},
        {FVIRTKEY, VK_F3, ID_FIND_NEXT},
        {FVIRTKEY | FSHIFT, VK_F3, ID_FIND_PREVIOUS},
        {FVIRTKEY, VK_F5, ID_RELOAD},
        {FVIRTKEY | FALT, VK_LEFT, ID_NAV_BACK},
        {FVIRTKEY | FALT, VK_RIGHT, ID_NAV_FORWARD},
        {FVIRTKEY | FCONTROL, VK_OEM_PLUS, ID_ZOOM_IN},
        {FVIRTKEY | FCONTROL, VK_ADD, ID_ZOOM_IN},
        {FVIRTKEY | FCONTROL, VK_OEM_MINUS, ID_ZOOM_OUT},
        {FVIRTKEY | FCONTROL, VK_SUBTRACT, ID_ZOOM_OUT},
        {FVIRTKEY | FCONTROL, '0', ID_ZOOM_RESET},
    };
    accelerators_ = CreateAcceleratorTableW(accelerators, ARRAYSIZE(accelerators));

    if (!initialFile.empty() && view_.loadFile(initialFile)) {
        updateTitle();
    } else {
        // No file: start on a fresh untitled document, ready to type into.
        newDocument();
    }

    ShowWindow(hwnd_, saved.maximized ? SW_SHOWMAXIMIZED : showCommand);
    UpdateWindow(hwnd_);
    if (editMode_) editor_.focusEditor();
    else SetFocus(view_.hwnd());
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
    state.zoomPercent = view_.zoomPercent();
    state.editorZoomPercent = editor_.zoomPercent();
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
        // WM_NCCALCSIZE can arrive before WM_CREATE and needs the frame metrics.
        self->dpi_ = windowDpi(hwnd);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(hwnd, message, wParam, lParam);
    return self->handleMessage(message, wParam, lParam);
}

int AppWindow::barHeight() const {
    if (!searchVisible_) return 0;
    int fieldHeight = std::max(scale(26), scale(24));
    return scale(10) * 2 + fieldHeight;
}

void AppWindow::showSearch(bool show) {
    if (searchVisible_ == show) {
        if (show) {
            SetFocus(searchField_);
            SendMessageW(searchField_, EM_SETSEL, 0, -1);
        }
        return;
    }

    searchVisible_ = show;
    chrome_.setSearchActive(show);
    ShowWindow(searchField_, show ? SW_SHOW : SW_HIDE);
    ShowWindow(searchButton_, show ? SW_SHOW : SW_HIDE);
    layoutChildren();
    InvalidateRect(hwnd_, nullptr, TRUE);

    if (show) {
        SetFocus(searchField_);
        SendMessageW(searchField_, EM_SETSEL, 0, -1);
    } else {
        searchFailed_ = false;
        SetFocus(view_.hwnd());
    }
}

LRESULT AppWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_NCCALCSIZE: {
            if (!wParam) break;
            NCCALCSIZE_PARAMS* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
            RECT& rect = params->rgrc[0];

            int borderX = GetSystemMetricsForDpi(SM_CXFRAME, static_cast<UINT>(dpi_)) +
                          GetSystemMetricsForDpi(SM_CXPADDEDBORDER, static_cast<UINT>(dpi_));
            int borderY = GetSystemMetricsForDpi(SM_CYFRAME, static_cast<UINT>(dpi_)) +
                          GetSystemMetricsForDpi(SM_CXPADDEDBORDER, static_cast<UINT>(dpi_));

            rect.left += borderX;
            rect.right -= borderX;
            rect.bottom -= borderY;
            // The top border is kept as client area so the caption can be drawn
            // there; when maximised the frame hangs off-screen, so inset it.
            WINDOWPLACEMENT placement = {sizeof(placement)};
            if (GetWindowPlacement(hwnd_, &placement) && placement.showCmd == SW_SHOWMAXIMIZED) {
                rect.top += borderY;
            }
            return 0;
        }

        case WM_NCHITTEST: {
            LRESULT hit = DefWindowProcW(hwnd_, message, wParam, lParam);
            if (hit != HTCLIENT) return hit;

            POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd_, &point);
            LRESULT chromeHit = chrome_.hitTest(point);
            return chromeHit == HTNOWHERE ? HTCLIENT : chromeHit;
        }

        case WM_NCMOUSEMOVE: {
            chrome_.setHot(static_cast<LRESULT>(wParam));
            TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE | TME_NONCLIENT, hwnd_, 0};
            TrackMouseEvent(&track);
            break;
        }

        case WM_NCMOUSELEAVE:
            chrome_.clearHot();
            break;

        case WM_NCLBUTTONDOWN:
            if (wParam == HTMINBUTTON || wParam == HTMAXBUTTON || wParam == HTCLOSE) {
                chrome_.setPressed(static_cast<LRESULT>(wParam));
                return 0;
            }
            break;

        case WM_NCLBUTTONUP: {
            LRESULT pressed = chrome_.pressed();
            chrome_.setPressed(HTNOWHERE);
            if (pressed != HTNOWHERE && pressed == static_cast<LRESULT>(wParam)) {
                if (pressed == HTMINBUTTON) {
                    PostMessageW(hwnd_, WM_SYSCOMMAND, SC_MINIMIZE, 0);
                } else if (pressed == HTMAXBUTTON) {
                    WINDOWPLACEMENT placement = {sizeof(placement)};
                    bool maximized = GetWindowPlacement(hwnd_, &placement) &&
                                     placement.showCmd == SW_SHOWMAXIMIZED;
                    PostMessageW(hwnd_, WM_SYSCOMMAND, maximized ? SC_RESTORE : SC_MAXIMIZE, 0);
                } else {
                    PostMessageW(hwnd_, WM_CLOSE, 0, 0);
                }
                return 0;
            }
            break;
        }

        case WM_NCACTIVATE:
            InvalidateRect(hwnd_, nullptr, FALSE);
            break;

        case WM_CREATE: {
            dpi_ = windowDpi(hwnd_);
            chrome_.setDpi(dpi_);
            chrome_.initialize(hwnd_, fileMenu_, editMenu_, aboutMenu_);
            chrome_.setTitle(kAppTitle);
            // Follow the system convention: menu mnemonics stay hidden until the
            // user reaches for the keyboard.
            SendMessageW(hwnd_, WM_CHANGEUISTATE,
                         MAKEWPARAM(UIS_INITIALIZE, UISF_HIDEACCEL | UISF_HIDEFOCUS), 0);

            NONCLIENTMETRICSW ncm = {sizeof(ncm)};
            SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0,
                                       static_cast<UINT>(dpi_));
            uiFont_ = CreateFontIndirectW(&ncm.lfMessageFont);
            menuFont_ = CreateFontIndirectW(&ncm.lfMenuFont);

            searchField_ = CreateWindowExW(0, L"EDIT", L"",
                                           WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL | ES_LEFT,
                                           0, 0, 10, 10, hwnd_,
                                           reinterpret_cast<HMENU>(ID_SEARCH_FIELD), instance_,
                                           nullptr);
            SendMessageW(searchField_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
            SendMessageW(searchField_, EM_SETCUEBANNER, TRUE,
                         reinterpret_cast<LPARAM>(L"Search"));
            SetWindowSubclass(searchField_, &AppWindow::searchEditProc, 1,
                              reinterpret_cast<DWORD_PTR>(this));

            searchButton_ = CreateWindowExW(0, L"BUTTON", L"Search",
                                            WS_CHILD | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10,
                                            hwnd_, reinterpret_cast<HMENU>(ID_SEARCH_BUTTON),
                                            instance_, nullptr);
            SendMessageW(searchButton_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
            // Same subclass: Enter searches, Escape clears, Tab moves on.
            SetWindowSubclass(searchButton_, &AppWindow::searchEditProc, 2,
                              reinterpret_cast<DWORD_PTR>(this));

            outline_.create(hwnd_, instance_);
            outline_.setDpi(dpi_);
            outline_.setExpanded(startExpanded_);
            outline_.setOnToggle([this]() {
                outline_.setExpanded(!outline_.expanded());
                layoutChildren();
            });
            outline_.setOnSelect([this](size_t index) { view_.scrollToOutlineEntry(index); });

            view_.create(hwnd_, instance_);
            view_.setZoomPercent(startZoom_);

            editor_.create(hwnd_, instance_);
            editor_.setDpi(dpi_);
            editor_.setZoomPercent(startEditorZoom_);
            editor_.setOnModifiedChanged([this]() { updateTitle(); });

            applyTheme();
            layoutChildren();
            DragAcceptFiles(hwnd_, TRUE);
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
            if (chrome_.updateMenuHover(p)) InvalidateRect(hwnd_, nullptr, FALSE);

            RECT button = {};
            GetWindowRect(searchButton_, &button);
            MapWindowPoints(nullptr, hwnd_, reinterpret_cast<POINT*>(&button), 2);
            bool hot = PtInRect(&button, p) != FALSE;
            if (hot != buttonHot_) {
                buttonHot_ = hot;
                InvalidateRect(searchButton_, nullptr, TRUE);
            }
            TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, hwnd_, 0};
            TrackMouseEvent(&track);
            return 0;
        }

        case WM_MOUSELEAVE: {
            POINT outside = {-1, -1};
            if (chrome_.updateMenuHover(outside)) InvalidateRect(hwnd_, nullptr, FALSE);
            if (buttonHot_) {
                buttonHot_ = false;
                InvalidateRect(searchButton_, nullptr, TRUE);
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            POINT p = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (chrome_.hitSearchButton(p)) {
                if (editMode_) editor_.showFindBar(!editor_.findBarVisible(), false);
                else showSearch(!searchVisible_);
                return 0;
            }
            if (chrome_.hitEditButton(p)) {
                setEditMode(!editMode_);
                return 0;
            }
            if (chrome_.openMenuAt(p)) return 0;
            break;
        }

        case WM_SYSCHAR:
            if (chrome_.openMenuForMnemonic(static_cast<wchar_t>(wParam))) return 0;
            break;

        case WM_SYSKEYDOWN:
            if (wParam == VK_F10 && GetKeyState(VK_SHIFT) >= 0) {
                chrome_.openFirstMenu();
                return 0;
            }
            break;

        case WM_MOUSEWHEEL:
            return SendMessageW(editMode_ ? editor_.editControl() : view_.hwnd(), message,
                                wParam, lParam);

        case WM_DROPFILES: {
            // A drop on the frame chrome (or the editor) runs through the same
            // open flow as File -> Open, including the unsaved-changes prompt.
            HDROP drop = reinterpret_cast<HDROP>(wParam);
            wchar_t path[MAX_PATH * 2] = {0};
            bool have = DragQueryFileW(drop, 0, path, ARRAYSIZE(path)) > 0;
            DragFinish(drop);
            if (have) openPath(path);
            return 0;
        }

        case WM_INITMENUPOPUP: {
            bool fieldHasSelection = false;
            if (GetFocus() == searchField_) {
                DWORD start = 0, end = 0;
                SendMessageW(searchField_, EM_GETSEL, reinterpret_cast<WPARAM>(&start),
                             reinterpret_cast<LPARAM>(&end));
                fieldHasSelection = start != end;
            }
            bool copyEnabled = fieldHasSelection ||
                               (editMode_ ? editor_.hasSelection() : view_.hasSelection());
            auto enable = [](HMENU menu, int id, bool on) {
                EnableMenuItem(menu, static_cast<UINT>(id),
                               MF_BYCOMMAND | (on ? MF_ENABLED : MF_GRAYED));
            };
            // An untitled document can always be saved (it asks for a path).
            enable(fileMenu_, ID_FILE_SAVE,
                   editor_.modified() || editor_.filePath().empty());
            CheckMenuItem(editMenu_, ID_EDIT_MODE,
                          MF_BYCOMMAND | (editMode_ ? MF_CHECKED : MF_UNCHECKED));
            enable(editMenu_, ID_EDIT_UNDO, editMode_ && editor_.canUndo());
            enable(editMenu_, ID_EDIT_REDO, editMode_ && editor_.canRedo());
            enable(editMenu_, ID_EDIT_CUT, editMode_ && editor_.hasSelection());
            enable(editMenu_, ID_EDIT_COPY, copyEnabled);
            enable(editMenu_, ID_EDIT_PASTE, editMode_ && editor_.canPaste());
            enable(editMenu_, ID_FIND_REPLACE, editMode_);
            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int notification = HIWORD(wParam);
            switch (id) {
                case ID_FILE_NEW:
                    if (confirmSaveDiscard()) newDocument();
                    return 0;
                case ID_FILE_OPEN:
                    openFileDialog();
                    return 0;
                case ID_FILE_SAVE:
                    saveEditor();
                    return 0;
                case ID_FILE_EXIT:
                    PostMessageW(hwnd_, WM_CLOSE, 0, 0);
                    return 0;
                case ID_EDIT_MODE:
                    setEditMode(!editMode_);
                    return 0;
                case ID_EDIT_COPY: {
                    HWND focus = GetFocus();
                    wchar_t className[16] = {0};
                    if (focus && GetClassNameW(focus, className, ARRAYSIZE(className)) &&
                        _wcsicmp(className, L"EDIT") == 0) {
                        SendMessageW(focus, WM_COPY, 0, 0);
                    } else if (editMode_) {
                        editor_.copy();
                    } else {
                        view_.copySelection();
                    }
                    return 0;
                }
                case ID_EDIT_UNDO:
                    if (editMode_) editor_.undo();
                    return 0;
                case ID_EDIT_REDO:
                    if (editMode_) editor_.redo();
                    return 0;
                case ID_EDIT_CUT:
                    if (editMode_) editor_.cut();
                    return 0;
                case ID_EDIT_PASTE:
                    if (editMode_) editor_.paste();
                    return 0;
                case ID_EDIT_SELECTALL:
                    if (editMode_) editor_.selectAll();
                    else view_.selectAll();
                    return 0;
                case ID_FORMAT_BOLD:
                    if (editMode_) editor_.toggleBold();
                    return 0;
                case ID_FORMAT_ITALIC:
                    if (editMode_) editor_.toggleItalic();
                    return 0;
                case ID_FIND_REPLACE:
                    if (editMode_) editor_.showFindBar(true, true);
                    else showSearch(true);
                    return 0;
                case ID_ABOUT_PROJECT:
                    ShellExecuteW(hwnd_, L"open", kProjectUrl, nullptr, nullptr, SW_SHOWNORMAL);
                    return 0;
                case ID_SEARCH_BUTTON:
                    if (notification == BN_CLICKED) doSearch(true);
                    return 0;
                case ID_FIND_NEXT:
                    if (editMode_) editor_.findNext(true);
                    else doSearch(true);
                    return 0;
                case ID_FIND_PREVIOUS:
                    if (editMode_) editor_.findNext(false);
                    else doSearch(false);
                    return 0;
                case ID_FOCUS_SEARCH:
                    if (editMode_) editor_.showFindBar(true, false);
                    else showSearch(true);
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
                    updateTitle();
                    return 0;
                case ID_RELOAD:
                    if (!editMode_) view_.reload();
                    return 0;
                case ID_NAV_BACK:
                    if (!editMode_) view_.goBack();
                    return 0;
                case ID_NAV_FORWARD:
                    if (!editMode_) view_.goForward();
                    return 0;
                case ID_ZOOM_IN:
                    if (editMode_) editor_.adjustZoom(10);
                    else view_.adjustZoom(10);
                    return 0;
                case ID_ZOOM_OUT:
                    if (editMode_) editor_.adjustZoom(-10);
                    else view_.adjustZoom(-10);
                    return 0;
                case ID_ZOOM_RESET:
                    if (editMode_) editor_.resetZoom();
                    else view_.setZoomPercent(100);
                    return 0;
                default:
                    break;
            }
            break;
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
            outline_.setDpi(dpi_);
            chrome_.setDpi(dpi_);
            view_.setDpi(dpi_);
            editor_.setDpi(dpi_);

            RECT* suggested = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            layoutChildren();
            return 0;
        }

        case WM_SETFOCUS:
            if (editMode_) editor_.focusEditor();
            else SetFocus(view_.hwnd());
            return 0;

        case WM_CLOSE:
            if (!confirmSaveDiscard()) return 0;
            DestroyWindow(hwnd_);
            return 0;

        case WM_DESTROY:
            persistWindowState();
            if (fieldBrush_) DeleteObject(fieldBrush_);
            if (uiFont_) DeleteObject(uiFont_);
            if (menuFont_) DeleteObject(menuFont_);
            if (fileMenu_) DestroyMenu(fileMenu_);
            if (editMenu_) DestroyMenu(editMenu_);
            if (aboutMenu_) DestroyMenu(aboutMenu_);
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
                self->showSearch(false);
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
    const int chromeTop = chrome_.height();
    const int bar = barHeight();

    if (editMode_) {
        MoveWindow(editor_.hwnd(), 0, chromeTop, client.right,
                   std::max(0, static_cast<int>(client.bottom) - chromeTop), TRUE);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (searchVisible_) {
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
        MoveWindow(searchField_, pad + inset, chromeTop + pad + (fieldHeight - editHeight) / 2,
                   fieldWidth - inset * 2, editHeight, TRUE);
        MoveWindow(searchButton_, buttonX, chromeTop + pad, buttonWidth, fieldHeight, TRUE);
    }

    // Below the bar: the outline panel is flush to the left edge, and the
    // scrolling document sits in a box inset by the gutter on every side.
    const int margin = gutter();
    // Never let the outline take more than a third of a narrow window.
    const int panelWidth =
        std::max(outline_.collapsedWidth(),
                 std::min(outline_.panelWidth(), static_cast<int>(client.right) / 3));
    const int contentTop = chromeTop + bar;
    const int belowBar = std::max(0, static_cast<int>(client.bottom) - contentTop);
    MoveWindow(outline_.hwnd(), 0, contentTop, panelWidth, belowBar, TRUE);

    int viewLeft = panelWidth + margin;
    int viewWidth = std::max(scale(120), static_cast<int>(client.right) - viewLeft - margin);
    int viewHeight = std::max(0, belowBar - margin * 2);
    MoveWindow(view_.hwnd(), viewLeft, contentTop + margin, viewWidth, viewHeight, TRUE);

    InvalidateRect(hwnd_, nullptr, FALSE);
}

void AppWindow::paintChrome(HDC dc) {
    RECT client = {};
    GetClientRect(hwnd_, &client);

    const int pad = scale(10);
    const int chromeTop = chrome_.height();
    const int bar = barHeight();
    const int border = std::max(1, scale(1));

    chrome_.paint(dc, client);

    // The gutter around the document box shares the document's background so the
    // scrolling area reads as an inset box.
    RECT below = {0, chromeTop + bar, client.right, client.bottom};
    fillRect(dc, below, theme_.background);

    if (searchVisible_) {
        RECT barRect = {0, chromeTop, client.right, chromeTop + bar};
        fillRect(dc, barRect, theme_.barBackground);

        RECT borderRect = {0, chromeTop + bar - border, client.right, chromeTop + bar};
        fillRect(dc, borderRect, theme_.barBorder);

        RECT fieldRect = {};
        GetWindowRect(searchField_, &fieldRect);
        MapWindowPoints(nullptr, hwnd_, reinterpret_cast<POINT*>(&fieldRect), 2);
        int inset = scale(5);
        RECT visual = {pad, chromeTop + pad, fieldRect.right + inset, chromeTop + bar - pad};
        fillRect(dc, visual, theme_.fieldBackground);
        frameRect(dc, visual, theme_.fieldBorder);
    }

    chrome_.paintResizeGrip(dc, client);
}

void AppWindow::applyTheme() {
    theme_ = makeTheme(systemUsesDarkMode());
    applyWindowTheme(hwnd_, theme_.dark);
    if (fieldBrush_) DeleteObject(fieldBrush_);
    fieldBrush_ = CreateSolidBrush(theme_.fieldBackground);
    view_.setTheme(theme_);
    editor_.setTheme(theme_);
    outline_.setTheme(theme_);
    chrome_.setTheme(theme_);
    InvalidateRect(hwnd_, nullptr, TRUE);
    if (searchField_) InvalidateRect(searchField_, nullptr, TRUE);
    if (searchButton_) InvalidateRect(searchButton_, nullptr, TRUE);
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
    openPath(path);
}

void AppWindow::openPath(const std::wstring& path) {
    if (!confirmSaveDiscard()) return;

    bool ok = false;
    std::string utf8 = readDocumentFile(path, &ok);
    if (!ok) {
        MessageBoxW(hwnd_, L"That file could not be opened.", kAppTitle, MB_ICONWARNING | MB_OK);
        return;
    }
    if (editMode_) {
        editor_.setContent(view::toWide(utf8), path);
        editor_.focusEditor();
    }
    view_.showText(std::move(utf8), path);
    searchFailed_ = false;
    updateTitle();
    if (!editMode_) SetFocus(view_.hwnd());
}

void AppWindow::setEditMode(bool on) {
    if (on == editMode_) return;

    if (on) {
        const std::wstring& path = view_.filePath();
        // Keep an unsaved buffer for this file, and always keep the buffer of
        // an untitled document (there is no disk copy to reload). Anything
        // else starts fresh from disk, after settling changes that belong to a
        // different file.
        bool keepBuffer = path.empty() || (editor_.filePath() == path && editor_.modified());
        if (!keepBuffer) {
            if (!confirmSaveDiscard()) return;
            bool ok = false;
            std::string utf8 = readDocumentFile(path, &ok);
            if (!ok) {
                MessageBoxW(hwnd_, L"That file could not be opened for editing.", kAppTitle,
                            MB_ICONWARNING | MB_OK);
                return;
            }
            editor_.setContent(view::toWide(utf8), path);
        }
        showSearch(false);
        editMode_ = true;
        ShowWindow(outline_.hwnd(), SW_HIDE);
        ShowWindow(view_.hwnd(), SW_HIDE);
        ShowWindow(editor_.hwnd(), SW_SHOW);
        editor_.focusEditor();
    } else {
        editMode_ = false;
        // Render the buffer as it stands, saved or not.
        if (editor_.modified()) {
            view_.showText(view::toUtf8(editor_.content()), editor_.filePath());
        }
        ShowWindow(editor_.hwnd(), SW_HIDE);
        ShowWindow(view_.hwnd(), SW_SHOW);
        ShowWindow(outline_.hwnd(), SW_SHOW);
        refreshOutline();
        SetFocus(view_.hwnd());
    }

    chrome_.setEditActive(editMode_);
    layoutChildren();
    updateTitle();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void AppWindow::newDocument() {
    editor_.setContent(std::wstring(), std::wstring());
    view_.showText(std::string(), std::wstring());
    // A blank document has nothing to view; open it ready to type into.
    if (!editMode_) setEditMode(true);
    else editor_.focusEditor();
    searchFailed_ = false;
    updateTitle();
}

void AppWindow::saveEditor() {
    // A titled, unchanged buffer has nothing to write. An untitled document
    // always goes through the dialog so a deliberate Ctrl+S can create it.
    if (!editor_.modified() && !editor_.filePath().empty()) return;
    saveEditorInternal();
}

bool AppWindow::saveEditorInternal() {
    std::wstring target = editor_.filePath();
    if (target.empty()) {
        wchar_t path[MAX_PATH * 2] = {0};
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd_;
        ofn.lpstrFilter = L"Markdown files\0*.md;*.markdown;*.mdown;*.mkd;*.mdtxt;*.text\0"
                          L"Text files\0*.txt\0All files\0*.*\0";
        ofn.lpstrFile = path;
        ofn.nMaxFile = ARRAYSIZE(path);
        ofn.lpstrDefExt = L"md";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
        ofn.lpstrTitle = L"Save Markdown File";
        if (!GetSaveFileNameW(&ofn)) return false;
        target = path;
    }

    if (!editor_.saveTo(target)) {
        MessageBoxW(hwnd_, L"The file could not be saved.", kAppTitle, MB_ICONWARNING | MB_OK);
        return false;
    }

    if (view_.filePath() != target) {
        // The document just gained its name (Save As): point the view at it.
        view_.showText(view::toUtf8(editor_.content()), target);
    } else if (!editMode_) {
        // In view mode the on-screen document may predate the buffer.
        view_.reload();
    }
    updateTitle();
    return true;
}

bool AppWindow::confirmSaveDiscard() {
    if (!editor_.modified()) return true;

    const std::wstring& path = editor_.filePath();
    size_t slash = path.find_last_of(L"\\/");
    std::wstring name = path.empty()
                            ? std::wstring(L"Untitled")
                            : (slash == std::wstring::npos ? path : path.substr(slash + 1));
    std::wstring message = L"Save changes to " + name + L"?";
    int choice =
        MessageBoxW(hwnd_, message.c_str(), kAppTitle, MB_YESNOCANCEL | MB_ICONWARNING);
    if (choice == IDCANCEL) return false;
    if (choice == IDYES) return saveEditorInternal();
    editor_.discardChanges();
    return true;
}

void AppWindow::doSearch(bool forward) {
    wchar_t buffer[512] = {0};
    GetWindowTextW(searchField_, buffer, ARRAYSIZE(buffer));
    std::wstring needle = buffer;
    if (needle.empty()) {
        // Nothing to look for yet: reveal the bar and let the user type.
        showSearch(true);
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
    const std::wstring& path = editMode_ ? editor_.filePath() : view_.filePath();
    size_t slash = path.find_last_of(L"\\/");
    std::wstring name = path.empty()
                            ? std::wstring(L"Untitled")
                            : (slash == std::wstring::npos ? path : path.substr(slash + 1));
    if (editor_.modified() && editor_.filePath() == path) name += L"*";
    std::wstring title = name + L" - " + kAppTitle;
    // The window text still drives the taskbar and Alt+Tab; the caption strip is
    // drawn from the same string.
    SetWindowTextW(hwnd_, title.c_str());
    chrome_.setTitle(title);
}

} // namespace app
