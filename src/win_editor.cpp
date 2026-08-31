#include "win_editor.h"

#include "highlight.h"
#include "layout.h"
#include "search.h"

#include <algorithm>
#include <commctrl.h>
#include <cstdio>
#include <richedit.h>
#include <richole.h>
#include <shellapi.h>
#include <tom.h>
#include <windowsx.h>

namespace app {
namespace {

const wchar_t* const kEditorClass = L"MarkrEditorPane";

// Toolbar commands. Zero marks a separator in the button list.
enum ToolbarCommand {
    CmdBold = 1,
    CmdItalic,
    CmdStrike,
    CmdHighlight,
    CmdCode,
    CmdH1,
    CmdH2,
    CmdH3,
    CmdBullet,
    CmdNumber,
    CmdTask,
    CmdQuote,
    CmdCodeBlock,
    CmdLink,
    CmdTable,
    CmdHr,
};

enum ControlId {
    kEditId = 301,
    kFindFieldId,
    kReplaceFieldId,
    kFindNextId,
    kReplaceOneId,
    kReplaceAllId,
};

// Coalesces re-highlighting while the user types.
constexpr UINT_PTR kHighlightTimer = 2;

// The editing surface uses VS Code's default Dark+ / Light+ colours for
// markdown source, so what the toolbar applies is immediately visible.
struct SourcePalette {
    COLORREF background;
    COLORREF text;
    COLORREF lineNumber;
    COLORREF heading;
    COLORREF boldText;
    COLORREF italic;
    COLORREF codeSpan;
    COLORREF listMark;
    COLORREF quoteMark;
    COLORREF linkText;
    COLORREF markBg;
    COLORREF markText;
    COLORREF token[10]; // indexed by syntax::TokenType
};

SourcePalette sourcePalette(bool dark) {
    SourcePalette p = {};
    if (dark) {
        p.background = RGB(30, 30, 30);
        p.text = RGB(212, 212, 212);
        p.lineNumber = RGB(133, 133, 133);
        p.heading = RGB(86, 156, 214);
        p.boldText = RGB(86, 156, 214);
        p.italic = RGB(197, 134, 192);
        p.codeSpan = RGB(206, 145, 120);
        p.listMark = RGB(103, 150, 230);
        p.quoteMark = RGB(106, 153, 85);
        p.linkText = RGB(206, 145, 120);
        p.markBg = RGB(92, 80, 32);
        p.markText = RGB(240, 246, 252);
        const COLORREF tokens[10] = {
            RGB(212, 212, 212), // Text
            RGB(86, 156, 214),  // Keyword
            RGB(78, 201, 176),  // Type
            RGB(206, 145, 120), // String
            RGB(181, 206, 168), // Number
            RGB(106, 153, 85),  // Comment
            RGB(197, 134, 192), // Preprocessor
            RGB(86, 156, 214),  // Tag
            RGB(156, 220, 254), // Attribute
            RGB(220, 220, 170), // Function
        };
        memcpy(p.token, tokens, sizeof(tokens));
    } else {
        p.background = RGB(255, 255, 255);
        p.text = RGB(0, 0, 0);
        p.lineNumber = RGB(140, 140, 140);
        p.heading = RGB(128, 0, 0);
        p.boldText = RGB(0, 0, 128);
        p.italic = RGB(128, 0, 128);
        p.codeSpan = RGB(128, 0, 0);
        p.listMark = RGB(4, 81, 165);
        p.quoteMark = RGB(4, 81, 165);
        p.linkText = RGB(163, 21, 21);
        p.markBg = RGB(255, 248, 197);
        p.markText = RGB(31, 35, 40);
        const COLORREF tokens[10] = {
            RGB(0, 0, 0),      // Text
            RGB(0, 0, 255),    // Keyword
            RGB(38, 127, 153), // Type
            RGB(163, 21, 21),  // String
            RGB(9, 134, 88),   // Number
            RGB(0, 128, 0),    // Comment
            RGB(175, 0, 219),  // Preprocessor
            RGB(128, 0, 0),    // Tag
            RGB(229, 0, 0),    // Attribute
            RGB(121, 94, 38),  // Function
        };
        memcpy(p.token, tokens, sizeof(tokens));
    }
    return p;
}

// One styling instruction over a source range. Attributes combine when spans
// overlap; colours are applied in span order (later wins).
struct StyleSpan {
    LONG start = 0;
    LONG end = 0;
    COLORREF color = 0;
    bool hasColor = false;
    COLORREF back = 0;
    bool hasBack = false;
    bool bold = false;
    bool italic = false;
    bool strike = false;
    bool underline = false;
};

// Inline styling for one line segment: code spans, emphasis, strikethrough,
// highlight and links. Heuristic on purpose — an editor highlighter, not a
// full CommonMark parser.
void scanInline(const std::wstring& t, size_t from, size_t to, const SourcePalette& pal,
                std::vector<StyleSpan>& out) {
    // Code spans claim their range first; nothing else applies inside them.
    std::vector<std::pair<size_t, size_t>> code;
    for (size_t i = from; i < to;) {
        if (t[i] != L'`') {
            ++i;
            continue;
        }
        size_t run = i;
        while (run < to && t[run] == L'`') ++run;
        size_t need = run - i;
        size_t j = run;
        while (j < to) {
            if (t[j] != L'`') {
                ++j;
                continue;
            }
            size_t k = j;
            while (k < to && t[k] == L'`') ++k;
            if (k - j == need) break;
            j = k;
        }
        if (j < to) {
            StyleSpan s;
            s.start = static_cast<LONG>(i);
            s.end = static_cast<LONG>(j + need);
            s.color = pal.codeSpan;
            s.hasColor = true;
            out.push_back(s);
            code.push_back({i, j + need});
            i = j + need;
        } else {
            i = run;
        }
    }
    auto inCode = [&](size_t pos) {
        for (const auto& c : code) {
            if (pos >= c.first && pos < c.second) return true;
        }
        return false;
    };
    auto findClose = [&](size_t start, const wchar_t* marker, size_t length) -> size_t {
        for (size_t j = start; j + length <= to; ++j) {
            if (inCode(j)) continue;
            if (wcsncmp(&t[j], marker, length) == 0 && j > 0 && !iswspace(t[j - 1])) return j;
        }
        return std::wstring::npos;
    };
    auto pushPair = [&](size_t i, size_t close, size_t markerLength, COLORREF color,
                        bool hasColor, bool bold, bool italic, bool strike, COLORREF back,
                        bool hasBack) {
        StyleSpan s;
        s.start = static_cast<LONG>(i);
        s.end = static_cast<LONG>(close + markerLength);
        s.color = color;
        s.hasColor = hasColor;
        s.bold = bold;
        s.italic = italic;
        s.strike = strike;
        s.back = back;
        s.hasBack = hasBack;
        out.push_back(s);
    };

    for (size_t i = from; i < to;) {
        if (inCode(i)) {
            ++i;
            continue;
        }
        wchar_t c = t[i];

        if (c == L'[') { // [title](url)
            size_t closeBracket = t.find(L']', i + 1);
            if (closeBracket != std::wstring::npos && closeBracket + 1 < to &&
                t[closeBracket + 1] == L'(') {
                size_t closeParen = t.find(L')', closeBracket + 2);
                if (closeParen != std::wstring::npos && closeParen < to) {
                    StyleSpan title;
                    title.start = static_cast<LONG>(i);
                    title.end = static_cast<LONG>(closeBracket + 1);
                    title.color = pal.linkText;
                    title.hasColor = true;
                    out.push_back(title);
                    StyleSpan url;
                    url.start = static_cast<LONG>(closeBracket + 1);
                    url.end = static_cast<LONG>(closeParen + 1);
                    url.underline = true;
                    out.push_back(url);
                    i = closeParen + 1;
                    continue;
                }
            }
            ++i;
            continue;
        }

        auto tryPair = [&](const wchar_t* marker, size_t length, COLORREF color, bool hasColor,
                           bool bold, bool italic, bool strike, COLORREF back,
                           bool hasBack) -> bool {
            if (i + length >= to) return false;
            if (wcsncmp(&t[i], marker, length) != 0) return false;
            if (iswspace(t[i + length])) return false; // opener must touch text
            size_t close = findClose(i + length + 1, marker, length);
            if (close == std::wstring::npos) return false;
            pushPair(i, close, length, color, hasColor, bold, italic, strike, back, hasBack);
            i = close + length;
            return true;
        };

        if (c == L'*' || c == L'_') {
            // Underscores inside identifiers are not emphasis.
            if (c == L'_' && i > from && iswalnum(t[i - 1])) {
                ++i;
                continue;
            }
            const wchar_t two[3] = {c, c, 0};
            const wchar_t one[2] = {c, 0};
            if (tryPair(two, 2, pal.boldText, true, true, false, false, 0, false)) continue;
            if (tryPair(one, 1, pal.italic, true, false, true, false, 0, false)) continue;
            ++i;
            continue;
        }
        if (c == L'~') {
            if (tryPair(L"~~", 2, 0, false, false, false, true, 0, false)) continue;
            ++i;
            continue;
        }
        if (c == L'=') {
            if (tryPair(L"==", 2, pal.markText, true, false, false, false, pal.markBg, true)) {
                continue;
            }
            ++i;
            continue;
        }
        ++i;
    }
}

// Whole-document scan: fenced code (with the viewer's lexers), headings,
// quote and list markers, then inline styles per line.
std::vector<StyleSpan> computeSourceSpans(const std::wstring& text, const SourcePalette& pal) {
    std::vector<StyleSpan> spans;
    bool inFence = false;
    size_t fenceContentStart = 0;
    syntax::Language fenceLang = syntax::Language::None;

    auto flushFence = [&](size_t contentEnd) {
        if (contentEnd <= fenceContentStart) return;
        std::wstring content = text.substr(fenceContentStart, contentEnd - fenceContentStart);
        for (wchar_t& ch : content) {
            if (ch == L'\r') ch = L'\n';
        }
        if (fenceLang == syntax::Language::None) {
            // No lexer for this fence: give the block the raw-code colour so
            // it still reads as code.
            StyleSpan s;
            s.start = static_cast<LONG>(fenceContentStart);
            s.end = static_cast<LONG>(contentEnd);
            s.color = pal.codeSpan;
            s.hasColor = true;
            spans.push_back(s);
            return;
        }
        std::string utf8 = view::toUtf8(content);
        // Token offsets are UTF-8 bytes; map them back to UTF-16 positions.
        std::vector<LONG> wideAtByte(utf8.size() + 1, static_cast<LONG>(content.size()));
        size_t byte = 0;
        for (size_t wi = 0; wi < content.size();) {
            unsigned units = 1;
            unsigned bytes = 0;
            wchar_t ch = content[wi];
            if (ch >= 0xD800 && ch < 0xDC00 && wi + 1 < content.size()) {
                units = 2;
                bytes = 4;
            } else if (ch < 0x80) {
                bytes = 1;
            } else if (ch < 0x800) {
                bytes = 2;
            } else {
                bytes = 3;
            }
            for (unsigned b = 0; b < bytes && byte + b < wideAtByte.size(); ++b) {
                wideAtByte[byte + b] = static_cast<LONG>(wi);
            }
            byte += bytes;
            wi += units;
        }
        for (const syntax::Token& token : syntax::tokenize(fenceLang, utf8)) {
            if (token.type == syntax::TokenType::Text) continue;
            LONG ws = wideAtByte[std::min(token.start, utf8.size())];
            LONG we = wideAtByte[std::min(token.start + token.length, utf8.size())];
            if (we <= ws) continue;
            StyleSpan s;
            s.start = static_cast<LONG>(fenceContentStart) + ws;
            s.end = static_cast<LONG>(fenceContentStart) + we;
            s.color = pal.token[static_cast<int>(token.type)];
            s.hasColor = true;
            spans.push_back(s);
        }
    };

    size_t pos = 0;
    while (pos <= text.size()) {
        size_t eol = text.find(L'\r', pos);
        size_t end = (eol == std::wstring::npos) ? text.size() : eol;

        size_t first = pos;
        while (first < end && (text[first] == L' ' || text[first] == L'\t')) ++first;

        bool fenceLine = false;
        if (first < end && first - pos <= 3 && (text[first] == L'`' || text[first] == L'~')) {
            wchar_t fenceChar = text[first];
            size_t run = first;
            while (run < end && text[run] == fenceChar) ++run;
            if (run - first >= 3) fenceLine = true;
        }

        if (inFence) {
            if (fenceLine) {
                flushFence(pos);
                inFence = false;
            }
        } else if (fenceLine) {
            inFence = true;
            fenceLang =
                syntax::languageFromInfo(view::toUtf8(text.substr(first, end - first)));
            fenceContentStart = (eol == std::wstring::npos) ? text.size() : eol + 1;
        } else {
            size_t hash = first;
            int level = 0;
            while (hash < end && text[hash] == L'#' && level < 7) {
                ++hash;
                ++level;
            }
            if (level >= 1 && level <= 6 &&
                (hash == end || text[hash] == L' ' || text[hash] == L'\t')) {
                StyleSpan s;
                s.start = static_cast<LONG>(pos);
                s.end = static_cast<LONG>(end);
                s.color = pal.heading;
                s.hasColor = true;
                s.bold = true;
                spans.push_back(s);
            } else {
                size_t cursor = first;

                // Quote markers: colour the '>' run.
                size_t probe = cursor;
                size_t lastQuote = std::wstring::npos;
                while (probe < end && (text[probe] == L'>' || text[probe] == L' ')) {
                    if (text[probe] == L'>') lastQuote = probe;
                    ++probe;
                }
                if (lastQuote != std::wstring::npos) {
                    StyleSpan s;
                    s.start = static_cast<LONG>(cursor);
                    s.end = static_cast<LONG>(lastQuote + 1);
                    s.color = pal.quoteMark;
                    s.hasColor = true;
                    spans.push_back(s);
                    cursor = lastQuote + 1;
                    while (cursor < end && text[cursor] == L' ') ++cursor;
                }

                // List markers: bullet (with optional task box) or number.
                if (cursor + 1 < end &&
                    (text[cursor] == L'-' || text[cursor] == L'*' || text[cursor] == L'+') &&
                    text[cursor + 1] == L' ') {
                    size_t markEnd = cursor + 1;
                    if (cursor + 5 < end && text[cursor + 2] == L'[' &&
                        (text[cursor + 3] == L' ' || text[cursor + 3] == L'x' ||
                         text[cursor + 3] == L'X') &&
                        text[cursor + 4] == L']' && text[cursor + 5] == L' ') {
                        markEnd = cursor + 5;
                    }
                    StyleSpan s;
                    s.start = static_cast<LONG>(cursor);
                    s.end = static_cast<LONG>(markEnd);
                    s.color = pal.listMark;
                    s.hasColor = true;
                    spans.push_back(s);
                    cursor = markEnd + 1;
                } else {
                    size_t digit = cursor;
                    while (digit < end && digit - cursor < 9 && iswdigit(text[digit])) ++digit;
                    if (digit > cursor && digit + 1 < end &&
                        (text[digit] == L'.' || text[digit] == L')') &&
                        text[digit + 1] == L' ') {
                        StyleSpan s;
                        s.start = static_cast<LONG>(cursor);
                        s.end = static_cast<LONG>(digit + 1);
                        s.color = pal.listMark;
                        s.hasColor = true;
                        spans.push_back(s);
                        cursor = digit + 2;
                    }
                }

                if (cursor < end) scanInline(text, cursor, end, pal, spans);
            }
        }

        if (eol == std::wstring::npos) break;
        pos = eol + 1;
    }
    if (inFence) flushFence(text.size());
    return spans;
}

void fillRect(HDC dc, const RECT& rect, COLORREF color) {
    SetDCBrushColor(dc, color);
    FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

void frameRect(HDC dc, const RECT& rect, COLORREF color) {
    SetDCBrushColor(dc, color);
    FrameRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

bool startsWith(const std::wstring& text, const wchar_t* prefix) {
    size_t length = wcslen(prefix);
    return text.size() >= length && text.compare(0, length, prefix) == 0;
}

// "- ", "* ", "+ " bullets; returns the prefix length or 0.
size_t bulletPrefixLength(const std::wstring& line) {
    if (line.size() >= 2 && (line[0] == L'-' || line[0] == L'*' || line[0] == L'+') &&
        line[1] == L' ') {
        return 2;
    }
    return 0;
}

// "- [ ] " / "- [x] " task prefix length, or 0.
size_t taskPrefixLength(const std::wstring& line) {
    if (line.size() >= 6 && bulletPrefixLength(line) == 2 && line[2] == L'[' &&
        (line[3] == L' ' || line[3] == L'x' || line[3] == L'X') && line[4] == L']' &&
        line[5] == L' ') {
        return 6;
    }
    return 0;
}

// "12. " / "3) " ordered prefix length, or 0.
size_t numberPrefixLength(const std::wstring& line) {
    size_t i = 0;
    while (i < line.size() && iswdigit(line[i])) ++i;
    if (i == 0 || i > 9 || i + 1 >= line.size()) return 0;
    if ((line[i] == L'.' || line[i] == L')') && line[i + 1] == L' ') return i + 2;
    return 0;
}

// Leading "#..# " heading marker; returns its level (0 when none) and length.
int headingPrefix(const std::wstring& line, size_t* length) {
    size_t i = 0;
    while (i < line.size() && i < 6 && line[i] == L'#') ++i;
    if (i == 0 || i >= line.size() || line[i] != L' ') {
        *length = 0;
        return 0;
    }
    *length = i + 1;
    return static_cast<int>(i);
}

std::vector<std::wstring> splitLines(const std::wstring& text) {
    std::vector<std::wstring> lines;
    size_t start = 0;
    while (true) {
        size_t cr = text.find(L'\r', start);
        if (cr == std::wstring::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, cr - start));
        start = cr + 1;
    }
    return lines;
}

std::wstring joinLines(const std::vector<std::wstring>& lines) {
    std::wstring out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i) out.push_back(L'\r');
        out += lines[i];
    }
    return out;
}

bool equalsIgnoreCase(const std::wstring& a, const std::wstring& b) {
    return a.size() == b.size() &&
           CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(),
                                static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

} // namespace

EditorPane::~EditorPane() {
    destroyFonts();
    if (fieldBrush_) DeleteObject(fieldBrush_);
    if (tomDoc_) tomDoc_->Release();
}

bool EditorPane::registerWindowClass(HINSTANCE instance) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &EditorPane::windowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kEditorClass;
    return RegisterClassExW(&wc) != 0;
}

HWND EditorPane::create(HWND parent, HINSTANCE instance) {
    instance_ = instance;
    hwnd_ = CreateWindowExW(0, kEditorClass, nullptr, WS_CHILD | WS_CLIPCHILDREN, 0, 0, 100,
                            100, parent, nullptr, instance, this);
    return hwnd_;
}

LRESULT CALLBACK EditorPane::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    EditorPane* self = reinterpret_cast<EditorPane*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<EditorPane*>(cs->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(hwnd, message, wParam, lParam);
    return self->handleMessage(message, wParam, lParam);
}

void EditorPane::createChildren(HINSTANCE instance) {
    static HMODULE richEditLibrary = LoadLibraryW(L"Msftedit.dll");
    (void)richEditLibrary;

    edit_ = CreateWindowExW(0, MSFTEDIT_CLASS, L"",
                            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE |
                                ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_NOHIDESEL |
                                ES_NOOLEDRAGDROP,
                            0, 0, 10, 10, hwnd_, reinterpret_cast<HMENU>(kEditId), instance,
                            nullptr);
    SendMessageW(edit_, EM_SETTEXTMODE, TM_PLAINTEXT | TM_MULTILEVELUNDO | TM_MULTICODEPAGE, 0);
    SendMessageW(edit_, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE);
    // A target line width of 1 is RichEdit's documented "word wrap off" switch;
    // 0 wraps to the window width.
    SendMessageW(edit_, EM_SETTARGETDEVICE, 0, wordWrap_ ? 0 : 1);
    DragAcceptFiles(edit_, TRUE);
    SetWindowSubclass(edit_, &EditorPane::editProc, 1, reinterpret_cast<DWORD_PTR>(this));

    IRichEditOle* richEditOle = nullptr;
    if (SendMessageW(edit_, EM_GETOLEINTERFACE, 0, reinterpret_cast<LPARAM>(&richEditOle)) &&
        richEditOle) {
        richEditOle->QueryInterface(__uuidof(ITextDocument),
                                    reinterpret_cast<void**>(&tomDoc_));
        richEditOle->Release();
    }

    findField_ = CreateWindowExW(0, L"EDIT", L"",
                                 WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL | ES_LEFT, 0, 0, 10, 10,
                                 hwnd_, reinterpret_cast<HMENU>(kFindFieldId), instance,
                                 nullptr);
    SendMessageW(findField_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Find"));
    SetWindowSubclass(findField_, &EditorPane::fieldProc, kFindFieldId,
                      reinterpret_cast<DWORD_PTR>(this));

    replaceField_ = CreateWindowExW(0, L"EDIT", L"",
                                    WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL | ES_LEFT, 0, 0, 10,
                                    10, hwnd_, reinterpret_cast<HMENU>(kReplaceFieldId),
                                    instance, nullptr);
    SendMessageW(replaceField_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Replace"));
    SetWindowSubclass(replaceField_, &EditorPane::fieldProc, kReplaceFieldId,
                      reinterpret_cast<DWORD_PTR>(this));

    auto makeButton = [&](int id, const wchar_t* label) {
        HWND button = CreateWindowExW(0, L"BUTTON", label,
                                      WS_CHILD | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10,
                                      hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                      instance, nullptr);
        return button;
    };
    findNextButton_ = makeButton(kFindNextId, L"Find Next");
    replaceButton_ = makeButton(kReplaceOneId, L"Replace");
    replaceAllButton_ = makeButton(kReplaceAllId, L"Replace All");

    tooltip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                               WS_POPUP | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT,
                               CW_USEDEFAULT, CW_USEDEFAULT, hwnd_, nullptr, instance, nullptr);

    buttons_.clear();
    const ToolButton layout[] = {
        {CmdBold, L"Bold (Ctrl+B)", {}},
        {CmdItalic, L"Italic (Ctrl+I)", {}},
        {CmdStrike, L"Strikethrough", {}},
        {CmdHighlight, L"Highlight", {}},
        {CmdCode, L"Inline code", {}},
        {0, nullptr, {}},
        {CmdH1, L"Heading 1", {}},
        {CmdH2, L"Heading 2", {}},
        {CmdH3, L"Heading 3", {}},
        {0, nullptr, {}},
        {CmdBullet, L"Bulleted list", {}},
        {CmdNumber, L"Numbered list", {}},
        {CmdTask, L"Task list", {}},
        {CmdQuote, L"Quote", {}},
        {0, nullptr, {}},
        {CmdCodeBlock, L"Code block", {}},
        {CmdLink, L"Link", {}},
        {CmdTable, L"Table", {}},
        {CmdHr, L"Horizontal rule", {}},
    };
    for (const ToolButton& button : layout) buttons_.push_back(button);

    rebuildFonts();
    applyEditorFormat();
    layoutChildren();
}

void EditorPane::destroyFonts() {
    HFONT* fonts[] = {&gutterFont_, &uiFont_, &smallBoldFont_, &codiconFont_};
    for (HFONT* font : fonts) {
        if (*font) {
            DeleteObject(*font);
            *font = nullptr;
        }
    }
}

void EditorPane::rebuildFonts() {
    destroyFonts();

    auto makeFont = [&](const wchar_t* face, int pointsTimesTen, int weight, bool italic,
                        bool strike) {
        LOGFONTW lf = {};
        lf.lfHeight = -MulDiv(pointsTimesTen, dpi_, 720);
        lf.lfWeight = weight;
        lf.lfItalic = italic ? TRUE : FALSE;
        lf.lfStrikeOut = strike ? TRUE : FALSE;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfQuality = CLEARTYPE_QUALITY;
        wcsncpy_s(lf.lfFaceName, face, _TRUNCATE);
        return CreateFontIndirectW(&lf);
    };

    // Line numbers are slightly smaller than the text (both scale with the
    // editor zoom); gutterYOffset_ centres them within a text line.
    gutterFont_ = makeFont(L"Consolas", MulDiv(90, zoomPercent_, 100), FW_NORMAL, false, false);
    HFONT contentFont =
        makeFont(L"Consolas", MulDiv(105, zoomPercent_, 100), FW_NORMAL, false, false);
    uiFont_ = makeFont(L"Segoe UI", 90, FW_NORMAL, false, false);
    smallBoldFont_ = makeFont(L"Segoe UI", 85, FW_BOLD, false, false);
    // The Codicon icon font, embedded at build time. 12pt is the 16px grid the
    // set is designed on.
    codiconFont_ = makeFont(L"codicon", 120, FW_NORMAL, false, false);

    HDC dc = GetDC(hwnd_);
    HGDIOBJ previous = SelectObject(dc, gutterFont_);
    TEXTMETRICW tm = {};
    GetTextMetricsW(dc, &tm);
    gutterCharWidth_ = tm.tmAveCharWidth;
    const int gutterTextHeight = tm.tmHeight;
    SelectObject(dc, contentFont);
    GetTextMetricsW(dc, &tm);
    gutterYOffset_ = std::max(0, static_cast<int>(tm.tmHeight - gutterTextHeight) / 2);
    SelectObject(dc, previous);
    ReleaseDC(hwnd_, dc);
    DeleteObject(contentFont);

    HWND fontTargets[] = {findField_, replaceField_, findNextButton_, replaceButton_,
                          replaceAllButton_};
    for (HWND target : fontTargets) {
        if (target) SendMessageW(target, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    }
}

void EditorPane::applyEditorFormat() {
    if (!edit_) return;
    SourcePalette pal = sourcePalette(theme_.dark);
    CHARFORMAT2W cf = {};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_BOLD | CFM_ITALIC;
    // VS Code's Windows defaults: Consolas 14px (10.5pt; RichEdit scales the
    // twips with the monitor DPI).
    cf.yHeight = 210;
    cf.crTextColor = pal.text;
    wcsncpy_s(cf.szFaceName, L"Consolas", _TRUNCATE);
    SendMessageW(edit_, EM_SETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&cf));
    SendMessageW(edit_, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&cf));
    SendMessageW(edit_, EM_SETBKGNDCOLOR, 0, static_cast<LPARAM>(pal.background));
}

void EditorPane::setTheme(const Theme& theme) {
    theme_ = theme;
    if (fieldBrush_) DeleteObject(fieldBrush_);
    fieldBrush_ = CreateSolidBrush(theme_.fieldBackground);
    applyEditorFormat();
    applyHighlighting();
    if (edit_) applyWindowTheme(edit_, theme_.dark);
    if (hwnd_) InvalidateRect(hwnd_, nullptr, TRUE);
    if (findField_) InvalidateRect(findField_, nullptr, TRUE);
    if (replaceField_) InvalidateRect(replaceField_, nullptr, TRUE);
}

void EditorPane::setDpi(int dpi) {
    dpi_ = dpi > 0 ? dpi : 96;
    rebuildFonts();
    if (hwnd_) {
        layoutChildren();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
}

int EditorPane::gutterWidth() const {
    if (!showLineNumbers_) return 0;
    return scale(8) * 2 + gutterDigits_ * gutterCharWidth_;
}

RECT EditorPane::contentRect() const {
    RECT client = {};
    GetClientRect(hwnd_, &client);
    RECT content = client;
    content.top = toolbarHeight() + findBarHeight();
    content.bottom = std::max(content.top, client.bottom - statusHeight());
    return content;
}

void EditorPane::layoutChildren() {
    if (!hwnd_) return;
    RECT client = {};
    GetClientRect(hwnd_, &client);

    // Toolbar button rects.
    const int buttonSize = scale(32);
    const int gap = scale(3);
    const int separator = scale(13);
    int x = scale(8);
    int y = (toolbarHeight() - buttonSize) / 2;
    for (ToolButton& button : buttons_) {
        if (!markdownMode_) {
            // Markdown formatting is unavailable; an empty rect hides the
            // button from painting, hit testing and tooltips alike.
            button.bounds = RECT{0, 0, 0, 0};
        } else if (button.command == 0) {
            button.bounds = RECT{x, y, x + separator, y + buttonSize};
            x += separator;
        } else {
            button.bounds = RECT{x, y, x + buttonSize, y + buttonSize};
            x += buttonSize + gap;
        }
    }
    updateTooltips();

    // Find bar controls.
    if (findVisible_) {
        HDC dc = GetDC(hwnd_);
        HGDIOBJ previous = SelectObject(dc, uiFont_);
        SIZE textSize = {};
        GetTextExtentPoint32W(dc, L"Replace All", 11, &textSize);
        TEXTMETRICW tm = {};
        GetTextMetricsW(dc, &tm);
        SelectObject(dc, previous);
        ReleaseDC(hwnd_, dc);

        const int barTop = toolbarHeight();
        const int barHeight = findBarHeight();
        const int pad = scale(10);
        const int fieldGap = scale(8);
        const int fieldHeight = std::max(scale(26), static_cast<int>(tm.tmHeight) + scale(8));
        const int fieldTop = barTop + (barHeight - fieldHeight) / 2;
        const int buttonWidth = textSize.cx + scale(24);

        int right = client.right - pad;
        auto placeButton = [&](HWND button) {
            MoveWindow(button, right - buttonWidth, fieldTop, buttonWidth, fieldHeight, TRUE);
            right -= buttonWidth + fieldGap;
        };
        placeButton(replaceAllButton_);
        placeButton(replaceButton_);
        placeButton(findNextButton_);

        int available = std::max(scale(120), right - pad - fieldGap);
        int fieldWidth = available / 2;
        const int inset = scale(5);
        int editHeight =
            std::min(fieldHeight - inset, static_cast<int>(tm.tmHeight) + scale(2));
        int editTop = fieldTop + (fieldHeight - editHeight) / 2;
        MoveWindow(findField_, pad + inset, editTop, fieldWidth - inset * 2, editHeight, TRUE);
        MoveWindow(replaceField_, pad + fieldWidth + fieldGap + inset, editTop,
                   fieldWidth - inset * 2, editHeight, TRUE);
    }

    RECT content = contentRect();
    int gutter = gutterWidth();
    int editWidth = static_cast<int>(content.right - content.left) - gutter;
    int editHeight = static_cast<int>(content.bottom - content.top);
    MoveWindow(edit_, content.left + gutter, content.top, std::max(scale(60), editWidth),
               std::max(0, editHeight), TRUE);
    applyEditorInsets();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void EditorPane::applyEditorInsets() {
    if (!edit_) return;
    RECT rect = {};
    GetClientRect(edit_, &rect);
    // The reading view pads its content by 20 logical pixels; mirror that.
    const int pad = scale(20);
    rect.left += pad;
    rect.top += pad;
    rect.right = std::max(static_cast<int>(rect.left) + scale(40),
                          static_cast<int>(rect.right) - pad);
    SendMessageW(edit_, EM_SETRECT, 0, reinterpret_cast<LPARAM>(&rect));
}

void EditorPane::updateTooltips() {
    if (!tooltip_) return;
    for (size_t i = 0; i < buttons_.size(); ++i) {
        if (!buttons_[i].tip) continue;
        TOOLINFOW info = {};
        info.cbSize = sizeof(info);
        info.uFlags = TTF_SUBCLASS;
        info.hwnd = hwnd_;
        info.uId = i + 1;
        info.rect = buttons_[i].bounds;
        info.lpszText = const_cast<wchar_t*>(buttons_[i].tip);
        SendMessageW(tooltip_, TTM_DELTOOLW, 0, reinterpret_cast<LPARAM>(&info));
        SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
    }
}

int EditorPane::hitTestButton(POINT p) const {
    for (size_t i = 0; i < buttons_.size(); ++i) {
        if (buttons_[i].command == 0) continue;
        if (PtInRect(&buttons_[i].bounds, p)) return static_cast<int>(i);
    }
    return -1;
}

// ------------------------------------------------------------ text plumbing

EditorPane::Sel EditorPane::selection() const {
    CHARRANGE range = {};
    SendMessageW(edit_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&range));
    return Sel{range.cpMin, range.cpMax};
}

void EditorPane::setSelection(LONG start, LONG end) {
    CHARRANGE range = {start, end};
    SendMessageW(edit_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
}

std::wstring EditorPane::textRange(LONG start, LONG end) const {
    if (end <= start) return std::wstring();
    std::vector<wchar_t> buffer(static_cast<size_t>(end - start) + 1, L'\0');
    TEXTRANGEW range = {};
    range.chrg.cpMin = start;
    range.chrg.cpMax = end;
    range.lpstrText = buffer.data();
    LRESULT copied = SendMessageW(edit_, EM_GETTEXTRANGE, 0, reinterpret_cast<LPARAM>(&range));
    return std::wstring(buffer.data(), static_cast<size_t>(std::max<LRESULT>(0, copied)));
}

std::wstring EditorPane::allTextRaw() const {
    GETTEXTLENGTHEX lengthSpec = {GTL_DEFAULT | GTL_NUMCHARS, 1200};
    LRESULT length =
        SendMessageW(edit_, EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&lengthSpec), 0);
    if (length <= 0) return std::wstring();
    std::vector<wchar_t> buffer(static_cast<size_t>(length) + 1, L'\0');
    GETTEXTEX spec = {};
    spec.cb = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
    spec.flags = GT_DEFAULT;
    spec.codepage = 1200;
    LRESULT copied = SendMessageW(edit_, EM_GETTEXTEX, reinterpret_cast<WPARAM>(&spec),
                                  reinterpret_cast<LPARAM>(buffer.data()));
    return std::wstring(buffer.data(), static_cast<size_t>(std::max<LRESULT>(0, copied)));
}

std::wstring EditorPane::content() const {
    GETTEXTLENGTHEX lengthSpec = {GTL_USECRLF | GTL_NUMCHARS, 1200};
    LRESULT length =
        SendMessageW(edit_, EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&lengthSpec), 0);
    if (length <= 0) return std::wstring();
    std::vector<wchar_t> buffer(static_cast<size_t>(length) + 2, L'\0');
    GETTEXTEX spec = {};
    spec.cb = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
    spec.flags = GT_USECRLF;
    spec.codepage = 1200;
    LRESULT copied = SendMessageW(edit_, EM_GETTEXTEX, reinterpret_cast<WPARAM>(&spec),
                                  reinterpret_cast<LPARAM>(buffer.data()));
    return std::wstring(buffer.data(), static_cast<size_t>(std::max<LRESULT>(0, copied)));
}

void EditorPane::replaceRange(LONG start, LONG end, const std::wstring& text) {
    setSelection(start, end);
    SendMessageW(edit_, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(text.c_str()));
}

EditorPane::LineBlock EditorPane::selectedLines() const {
    Sel sel = selection();
    LONG firstLine = static_cast<LONG>(SendMessageW(edit_, EM_EXLINEFROMCHAR, 0, sel.start));
    LONG lastLine = static_cast<LONG>(SendMessageW(edit_, EM_EXLINEFROMCHAR, 0, sel.end));
    LONG lastLineStart = static_cast<LONG>(SendMessageW(edit_, EM_LINEINDEX, lastLine, 0));
    // A selection ending at the start of a line was a full-line sweep; do not
    // pull the following line into the block.
    if (lastLine > firstLine && sel.end == lastLineStart && sel.end > sel.start) {
        --lastLine;
        lastLineStart = static_cast<LONG>(SendMessageW(edit_, EM_LINEINDEX, lastLine, 0));
    }
    LineBlock block;
    block.start = static_cast<LONG>(SendMessageW(edit_, EM_LINEINDEX, firstLine, 0));
    LONG lastLength = static_cast<LONG>(SendMessageW(edit_, EM_LINELENGTH, lastLineStart, 0));
    block.end = lastLineStart + lastLength;
    block.text = textRange(block.start, block.end);
    return block;
}

// ------------------------------------------------------------ public editing

void EditorPane::setContent(const std::wstring& text, const std::wstring& path) {
    path_ = path;
    nagOnExit_ = !path.empty();
    SetWindowTextW(edit_, text.c_str());
    SendMessageW(edit_, EM_SETMODIFY, FALSE, 0);
    SendMessageW(edit_, EM_EMPTYUNDOBUFFER, 0, 0);
    setSelection(0, 0);
    SendMessageW(edit_, EM_SETZOOM, zoomPercent_, 100);
    applyHighlighting();
    onEditChanged();
}

bool EditorPane::modified() const {
    return edit_ && SendMessageW(edit_, EM_GETMODIFY, 0, 0) != 0;
}

bool EditorPane::save() { return saveTo(path_); }

bool EditorPane::saveTo(const std::wstring& path) {
    if (path.empty()) return false;
    std::string utf8 = view::toUtf8(content());

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = utf8.empty() ? TRUE
                           : WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()),
                                       &written, nullptr);
    CloseHandle(file);
    if (!ok || written != utf8.size()) return false;

    path_ = path;
    SendMessageW(edit_, EM_SETMODIFY, FALSE, 0);
    notifyModified();
    return true;
}

void EditorPane::discardChanges() {
    if (edit_) SendMessageW(edit_, EM_SETMODIFY, FALSE, 0);
    notifyModified();
}

void EditorPane::focusEditor() {
    if (edit_) SetFocus(edit_);
}

void EditorPane::undo() { SendMessageW(edit_, EM_UNDO, 0, 0); }
void EditorPane::redo() { SendMessageW(edit_, EM_REDO, 0, 0); }
void EditorPane::cut() { SendMessageW(edit_, WM_CUT, 0, 0); }
void EditorPane::copy() { SendMessageW(edit_, WM_COPY, 0, 0); }
void EditorPane::paste() { SendMessageW(edit_, WM_PASTE, 0, 0); }

void EditorPane::selectAll() {
    setSelection(0, -1);
}

bool EditorPane::canUndo() const { return SendMessageW(edit_, EM_CANUNDO, 0, 0) != 0; }
bool EditorPane::canRedo() const { return SendMessageW(edit_, EM_CANREDO, 0, 0) != 0; }

bool EditorPane::hasSelection() const {
    Sel sel = selection();
    return sel.end > sel.start;
}

bool EditorPane::canPaste() const {
    return SendMessageW(edit_, EM_CANPASTE, 0, 0) != 0;
}

void EditorPane::toggleBold() {
    if (markdownMode_) toggleInline(L"**", L"**");
}
void EditorPane::toggleItalic() {
    if (markdownMode_) toggleInline(L"*", L"*");
}

void EditorPane::setMarkdownMode(bool on) {
    if (markdownMode_ == on) return;
    markdownMode_ = on;
    applyEditorFormat(); // SCF_ALL: clears any leftover source styling
    if (markdownMode_) applyHighlighting();
    if (hwnd_) {
        layoutChildren();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
}

void EditorPane::setShowLineNumbers(bool on) {
    if (showLineNumbers_ == on) return;
    showLineNumbers_ = on;
    if (hwnd_) {
        layoutChildren();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
}

void EditorPane::setWordWrap(bool on) {
    if (wordWrap_ == on) return;
    wordWrap_ = on;
    if (edit_) {
        SendMessageW(edit_, EM_SETTARGETDEVICE, 0, wordWrap_ ? 0 : 1);
        InvalidateRect(edit_, nullptr, TRUE);
    }
    refreshChrome();
}

void EditorPane::setZoomPercent(int percent) {
    zoomPercent_ = std::max(50, std::min(300, percent));
    if (edit_) SendMessageW(edit_, EM_SETZOOM, zoomPercent_, 100);
    // The gutter font tracks the zoomed text size, and its width follows.
    rebuildFonts();
    if (hwnd_) layoutChildren();
    refreshChrome();
}

void EditorPane::adjustZoom(int deltaPercent) { setZoomPercent(zoomPercent_ + deltaPercent); }

void EditorPane::resetZoom() { setZoomPercent(100); }

// ------------------------------------------------------------ formatting ops

void EditorPane::runCommand(int command) {
    if (!markdownMode_) return;
    switch (command) {
        case CmdBold: toggleInline(L"**", L"**"); break;
        case CmdItalic: toggleInline(L"*", L"*"); break;
        case CmdStrike: toggleInline(L"~~", L"~~"); break;
        case CmdHighlight: toggleInline(L"==", L"=="); break;
        case CmdCode: toggleInline(L"`", L"`"); break;
        case CmdH1: setHeading(1); break;
        case CmdH2: setHeading(2); break;
        case CmdH3: setHeading(3); break;
        case CmdBullet: toggleLineStyle(LineStyle::Bullet); break;
        case CmdNumber: toggleLineStyle(LineStyle::Numbered); break;
        case CmdTask: toggleLineStyle(LineStyle::Task); break;
        case CmdQuote: toggleLineStyle(LineStyle::Quote); break;
        case CmdCodeBlock: toggleCodeFence(); break;
        case CmdLink: insertLink(); break;
        case CmdTable: insertTable(); break;
        case CmdHr: insertHorizontalRule(); break;
        default: break;
    }
    SetFocus(edit_);
}

void EditorPane::toggleInline(const std::wstring& prefix, const std::wstring& suffix) {
    Sel sel = selection();
    std::wstring text = textRange(sel.start, sel.end);
    const LONG pre = static_cast<LONG>(prefix.size());
    const LONG suf = static_cast<LONG>(suffix.size());

    // Shrink the selection past any edge whitespace (a double-click selection
    // includes the trailing space): markers around whitespace, like
    // "**word **", are not valid Markdown and would render literally.
    size_t lead = 0;
    while (lead < text.size() && iswspace(text[lead])) ++lead;
    size_t trail = text.size();
    while (trail > lead && iswspace(text[trail - 1])) --trail;
    sel.end = sel.start + static_cast<LONG>(trail);
    sel.start += static_cast<LONG>(lead);
    text = text.substr(lead, trail - lead);

    if (sel.start == sel.end) {
        replaceRange(sel.start, sel.end, prefix + suffix);
        setSelection(sel.start + pre, sel.start + pre);
        return;
    }
    // The selection itself carries the markers: strip them.
    if (text.size() >= prefix.size() + suffix.size() &&
        text.compare(0, prefix.size(), prefix) == 0 &&
        text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0) {
        std::wstring inner =
            text.substr(prefix.size(), text.size() - prefix.size() - suffix.size());
        replaceRange(sel.start, sel.end, inner);
        setSelection(sel.start, sel.start + static_cast<LONG>(inner.size()));
        return;
    }
    // The markers sit just outside the selection: remove them.
    if (sel.start >= pre && textRange(sel.start - pre, sel.start) == prefix &&
        textRange(sel.end, sel.end + suf) == suffix) {
        replaceRange(sel.start - pre, sel.end + suf, text);
        setSelection(sel.start - pre, sel.start - pre + static_cast<LONG>(text.size()));
        return;
    }
    replaceRange(sel.start, sel.end, prefix + text + suffix);
    setSelection(sel.start + pre, sel.start + pre + static_cast<LONG>(text.size()));
}

void EditorPane::setHeading(int level) {
    LineBlock block = selectedLines();
    std::vector<std::wstring> lines = splitLines(block.text);

    // Toggle off when every non-empty line already has this level.
    bool allAtLevel = true;
    bool anyContent = false;
    for (const std::wstring& line : lines) {
        if (line.empty()) continue;
        anyContent = true;
        size_t markerLength = 0;
        if (headingPrefix(line, &markerLength) != level) {
            allAtLevel = false;
            break;
        }
    }
    if (!anyContent) return;

    std::wstring marker(static_cast<size_t>(level), L'#');
    marker += L' ';
    for (std::wstring& line : lines) {
        if (line.empty()) continue;
        size_t markerLength = 0;
        headingPrefix(line, &markerLength);
        line.erase(0, markerLength);
        if (!allAtLevel) line.insert(0, marker);
    }

    std::wstring replacement = joinLines(lines);
    replaceRange(block.start, block.end, replacement);
    setSelection(block.start, block.start + static_cast<LONG>(replacement.size()));
}

void EditorPane::toggleLineStyle(LineStyle style) {
    LineBlock block = selectedLines();
    std::vector<std::wstring> lines = splitLines(block.text);

    auto prefixLength = [style](const std::wstring& line) -> size_t {
        switch (style) {
            case LineStyle::Bullet: return bulletPrefixLength(line);
            case LineStyle::Numbered: return numberPrefixLength(line);
            case LineStyle::Task: return taskPrefixLength(line);
            case LineStyle::Quote: return startsWith(line, L"> ") ? 2 : 0;
        }
        return 0;
    };

    bool allPrefixed = true;
    bool anyContent = false;
    for (const std::wstring& line : lines) {
        if (line.empty()) continue;
        anyContent = true;
        if (prefixLength(line) == 0) {
            allPrefixed = false;
            break;
        }
    }
    if (!anyContent) {
        // An empty block still gets a starter prefix to type after.
        std::wstring starter;
        switch (style) {
            case LineStyle::Bullet: starter = L"- "; break;
            case LineStyle::Numbered: starter = L"1. "; break;
            case LineStyle::Task: starter = L"- [ ] "; break;
            case LineStyle::Quote: starter = L"> "; break;
        }
        replaceRange(block.start, block.end, starter);
        setSelection(block.start + static_cast<LONG>(starter.size()),
                     block.start + static_cast<LONG>(starter.size()));
        return;
    }

    int number = 1;
    for (std::wstring& line : lines) {
        if (line.empty()) continue;
        if (allPrefixed) {
            line.erase(0, prefixLength(line));
            continue;
        }
        // Adding: normalise away any existing list marker first.
        size_t existing = taskPrefixLength(line);
        if (!existing) existing = bulletPrefixLength(line);
        if (!existing) existing = numberPrefixLength(line);
        line.erase(0, existing);
        switch (style) {
            case LineStyle::Bullet: line.insert(0, L"- "); break;
            case LineStyle::Numbered: {
                wchar_t buffer[16] = {0};
                swprintf(buffer, ARRAYSIZE(buffer), L"%d. ", number++);
                line.insert(0, buffer);
                break;
            }
            case LineStyle::Task: line.insert(0, L"- [ ] "); break;
            case LineStyle::Quote: line.insert(0, L"> "); break;
        }
    }

    std::wstring replacement = joinLines(lines);
    replaceRange(block.start, block.end, replacement);
    setSelection(block.start, block.start + static_cast<LONG>(replacement.size()));
}

void EditorPane::toggleCodeFence() {
    LineBlock block = selectedLines();
    std::vector<std::wstring> lines = splitLines(block.text);

    std::wstring replacement;
    if (lines.size() >= 2 && startsWith(lines.front(), L"```") &&
        startsWith(lines.back(), L"```")) {
        lines.erase(lines.begin());
        lines.pop_back();
        replacement = joinLines(lines);
    } else {
        replacement = L"```\r" + block.text + L"\r```";
    }
    replaceRange(block.start, block.end, replacement);
    setSelection(block.start, block.start + static_cast<LONG>(replacement.size()));
}

void EditorPane::insertLink() {
    Sel sel = selection();
    std::wstring text = textRange(sel.start, sel.end);

    size_t lead = 0;
    while (lead < text.size() && iswspace(text[lead])) ++lead;
    size_t trail = text.size();
    while (trail > lead && iswspace(text[trail - 1])) --trail;
    sel.end = sel.start + static_cast<LONG>(trail);
    sel.start += static_cast<LONG>(lead);
    text = text.substr(lead, trail - lead);

    if (text.empty()) {
        replaceRange(sel.start, sel.end, L"[text](url)");
        setSelection(sel.start + 1, sel.start + 5); // select "text"
        return;
    }
    if (startsWith(text, L"http://") || startsWith(text, L"https://")) {
        std::wstring replacement = L"[text](" + text + L")";
        replaceRange(sel.start, sel.end, replacement);
        setSelection(sel.start + 1, sel.start + 5);
        return;
    }
    std::wstring replacement = L"[" + text + L"](url)";
    replaceRange(sel.start, sel.end, replacement);
    LONG urlStart = sel.start + static_cast<LONG>(text.size()) + 3;
    setSelection(urlStart, urlStart + 3); // select "url"
}

void EditorPane::insertAtLineBoundary(const std::wstring& blockText) {
    Sel sel = selection();
    LONG line = static_cast<LONG>(SendMessageW(edit_, EM_EXLINEFROMCHAR, 0, sel.end));
    LONG lineStart = static_cast<LONG>(SendMessageW(edit_, EM_LINEINDEX, line, 0));
    LONG lineLength = static_cast<LONG>(SendMessageW(edit_, EM_LINELENGTH, lineStart, 0));
    LONG at = lineStart + lineLength;

    std::wstring insertion = (lineLength > 0 ? L"\r\r" : L"") + blockText + L"\r";
    replaceRange(at, at, insertion);
    setSelection(at + static_cast<LONG>(insertion.size()),
                 at + static_cast<LONG>(insertion.size()));
}

void EditorPane::insertTable() {
    insertAtLineBoundary(
        L"| Column 1 | Column 2 | Column 3 |\r"
        L"| --- | --- | --- |\r"
        L"|  |  |  |");
}

void EditorPane::insertHorizontalRule() { insertAtLineBoundary(L"---"); }

// ------------------------------------------------------------ find / replace

std::wstring EditorPane::fieldText(HWND field) const {
    wchar_t buffer[512] = {0};
    GetWindowTextW(field, buffer, ARRAYSIZE(buffer));
    return buffer;
}

void EditorPane::showFindBar(bool show, bool focusReplace) {
    if (findVisible_ != show) {
        findVisible_ = show;
        HWND controls[] = {findField_, replaceField_, findNextButton_, replaceButton_,
                           replaceAllButton_};
        for (HWND control : controls) ShowWindow(control, show ? SW_SHOW : SW_HIDE);
        layoutChildren();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
    if (show) {
        // Seed the field with a short, single-line selection.
        Sel sel = selection();
        if (sel.end > sel.start && sel.end - sel.start < 200) {
            std::wstring selected = textRange(sel.start, sel.end);
            if (selected.find(L'\r') == std::wstring::npos && !selected.empty()) {
                SetWindowTextW(findField_, selected.c_str());
            }
        }
        HWND target = focusReplace ? replaceField_ : findField_;
        SetFocus(target);
        SendMessageW(target, EM_SETSEL, 0, -1);
    } else {
        SetFocus(edit_);
    }
}

void EditorPane::findNext(bool forward) {
    std::wstring needle = fieldText(findField_);
    if (needle.empty()) {
        showFindBar(true, false);
        return;
    }
    std::wstring haystack = allTextRaw();
    std::vector<view::Match> matches = view::findAll(haystack, needle, false);
    if (matches.empty()) {
        MessageBeep(MB_OK);
        return;
    }
    Sel sel = selection();
    size_t from = forward ? static_cast<size_t>(sel.end) : static_cast<size_t>(sel.start);
    int index = view::nextMatch(matches, from, forward);
    if (index < 0) {
        MessageBeep(MB_OK);
        return;
    }
    setSelection(static_cast<LONG>(matches[index].start),
                 static_cast<LONG>(matches[index].start + matches[index].length));
    SendMessageW(edit_, EM_SCROLLCARET, 0, 0);
    refreshChrome();
}

void EditorPane::doReplaceOne() {
    std::wstring needle = fieldText(findField_);
    if (needle.empty()) return;
    Sel sel = selection();
    std::wstring current = textRange(sel.start, sel.end);
    if (equalsIgnoreCase(current, needle)) {
        std::wstring replacement = fieldText(replaceField_);
        replaceRange(sel.start, sel.end, replacement);
    }
    findNext(true);
}

void EditorPane::doReplaceAll() {
    std::wstring needle = fieldText(findField_);
    if (needle.empty()) return;
    std::wstring replacement = fieldText(replaceField_);
    std::wstring haystack = allTextRaw();
    std::vector<view::Match> matches = view::findAll(haystack, needle, false);
    if (matches.empty()) {
        MessageBeep(MB_OK);
        return;
    }
    SendMessageW(edit_, WM_SETREDRAW, FALSE, 0);
    for (size_t i = matches.size(); i > 0; --i) {
        const view::Match& match = matches[i - 1];
        replaceRange(static_cast<LONG>(match.start),
                     static_cast<LONG>(match.start + match.length), replacement);
    }
    SendMessageW(edit_, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(edit_, nullptr, TRUE);
    onEditChanged();
}

// ------------------------------------------------------------ highlighting

void EditorPane::scheduleHighlight() {
    if (hwnd_) SetTimer(hwnd_, kHighlightTimer, 150, nullptr);
}

void EditorPane::applyHighlighting() {
    // Plain-text documents get no source styling at all.
    if (!markdownMode_) return;
    if (!tomDoc_ || !edit_) return;
    std::wstring text = allTextRaw();
    SourcePalette pal = sourcePalette(theme_.dark);
    std::vector<StyleSpan> spans = computeSourceSpans(text, pal);

    // Formatting must not dirty the document, land on the undo stack, spam
    // EN_CHANGE, or repaint per span.
    suppressChange_ = true;
    LRESULT oldMask = SendMessageW(edit_, EM_SETEVENTMASK, 0, 0);
    BOOL wasModified = static_cast<BOOL>(SendMessageW(edit_, EM_GETMODIFY, 0, 0));
    SendMessageW(edit_, WM_SETREDRAW, FALSE, 0);
    tomDoc_->Undo(tomSuspend, nullptr);

    auto applyRange = [&](LONG start, LONG end, auto&& styler) {
        ITextRange* range = nullptr;
        if (FAILED(tomDoc_->Range(start, end, &range)) || !range) return;
        ITextFont* font = nullptr;
        if (SUCCEEDED(range->GetFont(&font)) && font) {
            styler(font);
            font->Release();
        }
        range->Release();
    };

    applyRange(0, static_cast<LONG>(text.size()) + 1, [&](ITextFont* font) {
        font->SetForeColor(pal.text);
        font->SetBold(tomFalse);
        font->SetItalic(tomFalse);
        font->SetStrikeThrough(tomFalse);
        font->SetUnderline(tomNone);
        font->SetBackColor(tomAutoColor);
    });
    for (const StyleSpan& span : spans) {
        applyRange(span.start, span.end, [&](ITextFont* font) {
            if (span.hasColor) font->SetForeColor(span.color);
            if (span.hasBack) font->SetBackColor(span.back);
            if (span.bold) font->SetBold(tomTrue);
            if (span.italic) font->SetItalic(tomTrue);
            if (span.strike) font->SetStrikeThrough(tomTrue);
            if (span.underline) font->SetUnderline(tomSingle);
        });
    }

    tomDoc_->Undo(tomResume, nullptr);
    SendMessageW(edit_, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(edit_, nullptr, FALSE);
    SendMessageW(edit_, EM_SETMODIFY, wasModified, 0);
    SendMessageW(edit_, EM_SETEVENTMASK, 0, static_cast<LPARAM>(oldMask));
    suppressChange_ = false;
}

// ------------------------------------------------------------ notifications

void EditorPane::onEditChanged() {
    LONG lineCount = static_cast<LONG>(SendMessageW(edit_, EM_GETLINECOUNT, 0, 0));
    int digits = 2;
    for (LONG remaining = lineCount; remaining >= 100; remaining /= 10) ++digits;
    if (digits != gutterDigits_) {
        gutterDigits_ = digits;
        layoutChildren();
    }
    refreshChrome();
    notifyModified();
}

void EditorPane::notifyModified() {
    bool now = modified();
    if (now != lastModified_) {
        lastModified_ = now;
        if (onModifiedChanged_) onModifiedChanged_();
    }
    refreshChrome();
}

void EditorPane::refreshChrome() {
    if (!hwnd_) return;
    RECT client = {};
    GetClientRect(hwnd_, &client);
    RECT content = contentRect();
    RECT gutter = {0, content.top, gutterWidth(), content.bottom};
    RECT status = {0, content.bottom, client.right, client.bottom};
    InvalidateRect(hwnd_, &gutter, FALSE);
    InvalidateRect(hwnd_, &status, FALSE);
}

// ------------------------------------------------------------ painting

void EditorPane::paint(HDC dc, const RECT& client) {
    fillRect(dc, client, theme_.barBackground);
    if (toolbarHeight() > 0) paintToolbar(dc, client);
    if (findVisible_) paintFindBar(dc, client);
    paintGutter(dc, client);
    paintStatus(dc, client);
}

void EditorPane::paintToolbar(HDC dc, const RECT& client) {
    RECT bar = {0, 0, client.right, toolbarHeight()};
    fillRect(dc, bar, theme_.barBackground);
    RECT border = {0, bar.bottom - std::max(1, scale(1)), client.right, bar.bottom};
    fillRect(dc, border, theme_.barBorder);

    for (size_t i = 0; i < buttons_.size(); ++i) {
        const ToolButton& button = buttons_[i];
        if (button.bounds.right <= button.bounds.left) continue;
        if (button.command == 0) {
            int x = (button.bounds.left + button.bounds.right) / 2;
            RECT line = {x, button.bounds.top + scale(4), x + std::max(1, scale(1)),
                         button.bounds.bottom - scale(4)};
            fillRect(dc, line, theme_.barBorder);
            continue;
        }
        bool hot = static_cast<int>(i) == hotButton_;
        bool pressed = static_cast<int>(i) == pressedButton_;
        if (hot || pressed) {
            fillRect(dc, button.bounds,
                     pressed ? theme_.buttonPressed : theme_.buttonHot);
        }
        drawGlyph(dc, button.bounds, button.command, theme_.buttonText);
    }
}

void EditorPane::paintFindBar(HDC dc, const RECT& client) {
    RECT bar = {0, toolbarHeight(), client.right, toolbarHeight() + findBarHeight()};
    fillRect(dc, bar, theme_.barBackground);
    RECT border = {0, bar.bottom - std::max(1, scale(1)), client.right, bar.bottom};
    fillRect(dc, border, theme_.barBorder);

    auto paintFieldBox = [&](HWND field) {
        if (!field || !IsWindowVisible(field)) return;
        RECT box = {};
        GetWindowRect(field, &box);
        MapWindowPoints(nullptr, hwnd_, reinterpret_cast<POINT*>(&box), 2);
        InflateRect(&box, scale(5), scale(5));
        fillRect(dc, box, theme_.fieldBackground);
        frameRect(dc, box, theme_.fieldBorder);
    };
    paintFieldBox(findField_);
    paintFieldBox(replaceField_);
}

void EditorPane::paintGutter(HDC dc, const RECT& client) {
    (void)client;
    if (!showLineNumbers_) return;
    SourcePalette pal = sourcePalette(theme_.dark);
    RECT content = contentRect();
    RECT gutter = {0, content.top, gutterWidth(), content.bottom};
    // VS Code style: the gutter shares the editor background, no divider.
    fillRect(dc, gutter, pal.background);

    if (!edit_) return;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, pal.lineNumber);
    HGDIOBJ previousFont = SelectObject(dc, gutterFont_);

    LONG first = static_cast<LONG>(SendMessageW(edit_, EM_GETFIRSTVISIBLELINE, 0, 0));
    LONG count = static_cast<LONG>(SendMessageW(edit_, EM_GETLINECOUNT, 0, 0));
    const int rightEdge = gutter.right - scale(8);
    int previousY = INT_MIN;
    int lineStep = 0;
    for (LONG line = first; line < count; ++line) {
        LONG charIndex = static_cast<LONG>(SendMessageW(edit_, EM_LINEINDEX, line, 0));
        if (charIndex < 0) break;
        POINTL position = {};
        SendMessageW(edit_, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&position), charIndex);
        int y = content.top + position.y;
        // EM_POSFROMCHAR for the trailing empty line reports the previous
        // line's position; place it one line further down instead.
        if (previousY != INT_MIN && y <= previousY) {
            if (lineStep <= 0) continue;
            y = previousY + lineStep;
        } else if (previousY != INT_MIN) {
            lineStep = y - previousY;
        }
        previousY = y;
        if (y >= content.bottom) break;
        if (y + scale(20) < content.top) continue;

        wchar_t buffer[16] = {0};
        int length = swprintf(buffer, ARRAYSIZE(buffer), L"%ld", line + 1);
        SIZE size = {};
        GetTextExtentPoint32W(dc, buffer, length, &size);
        TextOutW(dc, rightEdge - size.cx, y + gutterYOffset_, buffer, length);
    }
    SelectObject(dc, previousFont);
}

void EditorPane::paintStatus(HDC dc, const RECT& client) {
    RECT content = contentRect();
    RECT bar = {0, content.bottom, client.right, client.bottom};
    fillRect(dc, bar, theme_.barBackground);
    RECT border = {0, bar.top, client.right, bar.top + std::max(1, scale(1))};
    fillRect(dc, border, theme_.barBorder);

    if (!edit_) return;
    Sel sel = selection();
    LONG line = static_cast<LONG>(SendMessageW(edit_, EM_EXLINEFROMCHAR, 0, sel.end));
    LONG lineStart = static_cast<LONG>(SendMessageW(edit_, EM_LINEINDEX, line, 0));
    LONG column = sel.end - lineStart;

    wchar_t buffer[64] = {0};
    swprintf(buffer, ARRAYSIZE(buffer), L"Ln %ld, Col %ld", line + 1, column + 1);

    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ previousFont = SelectObject(dc, uiFont_);
    SetTextColor(dc, theme_.role(view::ColorRole::Muted));

    RECT textRect = bar;
    textRect.right -= scale(12);
    textRect.left += scale(12);
    DrawTextW(dc, buffer, -1, &textRect,
              DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    if (modified()) {
        DrawTextW(dc, L"Modified", -1, &textRect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    SelectObject(dc, previousFont);
}

void EditorPane::drawGlyph(HDC dc, const RECT& bounds, int command, COLORREF color) const {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);

    auto drawText = [&](const wchar_t* text, HFONT font) {
        HGDIOBJ previous = SelectObject(dc, font);
        RECT r = bounds;
        DrawTextW(dc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, previous);
    };
    // Codicon glyphs (the VS Code icon font, embedded as a resource); the
    // comments name each icon in microsoft/vscode-codicons.
    auto drawIcon = [&](wchar_t glyph) {
        wchar_t text[2] = {glyph, 0};
        drawText(text, codiconFont_);
    };

    switch (command) {
        case CmdBold: drawIcon(0xEAA3); break;      // bold
        case CmdItalic: drawIcon(0xEB0D); break;    // italic
        case CmdStrike: drawIcon(0xEC64); break;    // strikethrough
        case CmdHighlight: {
            // No highlight codicon: a marked "a", echoing the rendered output.
            const int cx = (bounds.left + bounds.right) / 2;
            const int cy = (bounds.top + bounds.bottom) / 2;
            RECT mark = {cx - scale(8), cy - scale(7), cx + scale(8), cy + scale(8)};
            fillRect(dc, mark, theme_.role(view::ColorRole::MarkBg));
            SetTextColor(dc, theme_.role(view::ColorRole::MarkText));
            drawText(L"a", smallBoldFont_);
            SetTextColor(dc, color);
            break;
        }
        case CmdCode: drawIcon(0xEAC4); break;      // code
        case CmdH1: drawText(L"H1", smallBoldFont_); break;
        case CmdH2: drawText(L"H2", smallBoldFont_); break;
        case CmdH3: drawText(L"H3", smallBoldFont_); break;
        case CmdBullet: drawIcon(0xEB17); break;    // list-unordered
        case CmdNumber: drawIcon(0xEB16); break;    // list-ordered
        case CmdTask: drawIcon(0xEB67); break;      // tasklist
        case CmdQuote: drawIcon(0xEB33); break;     // quote
        case CmdCodeBlock: drawIcon(0xEB0F); break; // json (curly braces)
        case CmdLink: drawIcon(0xEB15); break;      // link
        case CmdTable: drawIcon(0xEBB7); break;     // table
        case CmdHr: drawIcon(0xEB07); break;        // horizontal-rule
        default:
            break;
    }
}

// ------------------------------------------------------------ message loop

LRESULT EditorPane::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            createChildren(instance_);
            return 0;

        case WM_SIZE:
            layoutChildren();
            return 0;

        case WM_TIMER:
            if (wParam == kHighlightTimer) {
                KillTimer(hwnd_, kHighlightTimer);
                applyHighlighting();
            }
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps = {};
            HDC dc = BeginPaint(hwnd_, &ps);
            RECT client = {};
            GetClientRect(hwnd_, &client);

            HDC memory = CreateCompatibleDC(dc);
            HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
            HGDIOBJ previous = SelectObject(memory, bitmap);
            paint(memory, client);
            BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left,
                   ps.rcPaint.bottom - ps.rcPaint.top, memory, ps.rcPaint.left, ps.rcPaint.top,
                   SRCCOPY);
            SelectObject(memory, previous);
            DeleteObject(bitmap);
            DeleteDC(memory);
            EndPaint(hwnd_, &ps);
            return 0;
        }

        case WM_MOUSEMOVE: {
            POINT p = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            int hit = hitTestButton(p);
            if (hit != hotButton_) {
                hotButton_ = hit;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            if (!trackingMouse_) {
                TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, hwnd_, 0};
                TrackMouseEvent(&track);
                trackingMouse_ = true;
            }
            return 0;
        }

        case WM_MOUSELEAVE:
            trackingMouse_ = false;
            if (hotButton_ >= 0) {
                hotButton_ = -1;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;

        case WM_LBUTTONDOWN: {
            POINT p = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            int hit = hitTestButton(p);
            if (hit >= 0) {
                pressedButton_ = hit;
                SetCapture(hwnd_);
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (pressedButton_ >= 0) {
                ReleaseCapture();
                POINT p = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                int hit = hitTestButton(p);
                int pressed = pressedButton_;
                pressedButton_ = -1;
                InvalidateRect(hwnd_, nullptr, FALSE);
                if (hit == pressed) runCommand(buttons_[static_cast<size_t>(pressed)].command);
            }
            return 0;
        }

        case WM_SETCURSOR: {
            POINT p = {};
            GetCursorPos(&p);
            ScreenToClient(hwnd_, &p);
            if (hitTestButton(p) >= 0) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            break;
        }

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, theme_.fieldText);
            SetBkColor(dc, theme_.fieldBackground);
            return reinterpret_cast<LRESULT>(fieldBrush_);
        }

        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (item->CtlID != kFindNextId && item->CtlID != kReplaceOneId &&
                item->CtlID != kReplaceAllId) {
                break;
            }
            bool pressed = (item->itemState & ODS_SELECTED) != 0;
            fillRect(item->hDC, item->rcItem, pressed ? theme_.buttonPressed
                                                      : theme_.buttonFace);
            frameRect(item->hDC, item->rcItem, theme_.buttonBorder);

            wchar_t label[32] = {0};
            GetWindowTextW(item->hwndItem, label, ARRAYSIZE(label));
            SetBkMode(item->hDC, TRANSPARENT);
            SetTextColor(item->hDC, theme_.buttonText);
            HGDIOBJ previous = SelectObject(item->hDC, uiFont_);
            RECT text = item->rcItem;
            DrawTextW(item->hDC, label, -1, &text,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            SelectObject(item->hDC, previous);
            if (item->itemState & ODS_FOCUS) {
                RECT focus = item->rcItem;
                InflateRect(&focus, -scale(3), -scale(3));
                DrawFocusRect(item->hDC, &focus);
            }
            return TRUE;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int notification = HIWORD(wParam);
            if (id == kEditId && notification == EN_CHANGE) {
                if (!suppressChange_) {
                    onEditChanged();
                    scheduleHighlight();
                }
                return 0;
            }
            if (notification == BN_CLICKED) {
                if (id == kFindNextId) {
                    findNext(true);
                    return 0;
                }
                if (id == kReplaceOneId) {
                    doReplaceOne();
                    return 0;
                }
                if (id == kReplaceAllId) {
                    doReplaceAll();
                    return 0;
                }
            }
            break;
        }

        case WM_NOTIFY: {
            NMHDR* header = reinterpret_cast<NMHDR*>(lParam);
            if (header->hwndFrom == edit_ && header->code == EN_SELCHANGE) {
                refreshChrome();
                return 0;
            }
            break;
        }

        case WM_SETFOCUS:
            SetFocus(edit_);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

LRESULT CALLBACK EditorPane::editProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                      UINT_PTR id, DWORD_PTR data) {
    EditorPane* self = reinterpret_cast<EditorPane*>(data);
    switch (message) {
        case WM_KEYDOWN:
            if (wParam == 'Y' && GetKeyState(VK_CONTROL) < 0) {
                SendMessageW(hwnd, EM_REDO, 0, 0);
                return 0;
            }
            if (wParam == 'A' && GetKeyState(VK_CONTROL) < 0) {
                self->selectAll();
                return 0;
            }
            if (wParam == 'Z' && GetKeyState(VK_CONTROL) < 0 && GetKeyState(VK_SHIFT) < 0) {
                SendMessageW(hwnd, EM_REDO, 0, 0);
                return 0;
            }
            if (wParam == VK_ESCAPE && self->findVisible_) {
                self->showFindBar(false, false);
                return 0;
            }
            break;

        case WM_MOUSEWHEEL:
            if (GetKeyState(VK_CONTROL) < 0) {
                self->adjustZoom(GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 10 : -10);
                return 0;
            }
            break;

        case WM_DROPFILES:
            // Hand file drops to the frame so they run through the usual
            // open-with-unsaved-changes flow.
            return SendMessageW(GetAncestor(hwnd, GA_ROOT), WM_DROPFILES, wParam, lParam);

        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, &EditorPane::editProc, id);
            break;

        default:
            break;
    }
    LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
    switch (message) {
        case WM_SIZE:
            // Resizing resets the formatting rectangle; restore the padding.
            self->applyEditorInsets();
            self->refreshChrome();
            break;
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_CHAR:
        case WM_VSCROLL:
        case WM_HSCROLL:
        case WM_MOUSEWHEEL:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_MOUSEMOVE:
            self->refreshChrome();
            break;
        default:
            break;
    }
    return result;
}

LRESULT CALLBACK EditorPane::fieldProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                       UINT_PTR id, DWORD_PTR data) {
    EditorPane* self = reinterpret_cast<EditorPane*>(data);
    switch (message) {
        case WM_KEYDOWN:
            if (wParam == VK_RETURN) {
                if (id == kReplaceFieldId) self->doReplaceOne();
                else self->findNext(GetKeyState(VK_SHIFT) >= 0);
                return 0;
            }
            if (wParam == VK_ESCAPE) {
                self->showFindBar(false, false);
                return 0;
            }
            if (wParam == VK_TAB) {
                HWND next = GetNextDlgTabItem(GetParent(hwnd), hwnd, GetKeyState(VK_SHIFT) < 0);
                if (next) SetFocus(next);
                return 0;
            }
            break;
        case WM_CHAR:
            if (wParam == VK_RETURN || wParam == VK_ESCAPE || wParam == VK_TAB) return 0;
            break;
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, &EditorPane::fieldProc, id);
            break;
        default:
            break;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

} // namespace app
