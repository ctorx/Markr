// Layout engine: turns a parsed markdown document into positioned text runs and
// decorations for a fixed viewport width. Knows nothing about Windows, so the
// test suite can drive it with a synthetic text measurer.
#pragma once

#include "md_types.h"

#include <string>
#include <vector>

namespace view {

enum class FontId {
    Body,
    BodyBold,
    BodyItalic,
    BodyBoldItalic,
    Code,
    CodeBold,
    H1,
    H2,
    H3,
    H4,
    H5,
    H6,
    Small,
    SmallBold,
    Count
};

enum class ColorRole {
    None,
    Text,
    Muted,
    Heading,
    Link,
    CodeText,
    CodeBg,
    QuoteText,
    QuoteBar,
    Rule,
    TableBorder,
    TableHeaderBg,
    MarkBg,
    MarkText,
    InlineCodeBg,
    Checkbox,

    // Syntax highlighting inside fenced code blocks.
    CodeKeyword,
    CodeType,
    CodeString,
    CodeNumber,
    CodeComment,
    CodeDirective,
    CodeTag,
    CodeAttribute,
    CodeFunction,
};

struct Style {
    FontId font = FontId::Body;
    ColorRole color = ColorRole::Text;
    ColorRole background = ColorRole::None;
    bool underline = false;
    bool strike = false;
    int linkIndex = -1;   // index into Layout::links, -1 when not a link
    int verticalShift = 0; // negative raises (superscript), positive lowers
};

struct Run {
    std::wstring text;
    size_t textStart = 0; // offset of this run's text inside Layout::text
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int baseline = 0;
    Style style;
};

enum class DecorationType {
    Rule,
    QuoteBar,
    CodeBlockBg,
    TableHeaderBg,
    TableBorder,
    Checkbox,
    CheckboxChecked,
    Image,
};

struct Decoration {
    DecorationType type = DecorationType::Rule;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    ColorRole color = ColorRole::Rule;
    int imageIndex = -1;
};

struct LinkTarget {
    std::wstring url;
    std::wstring title;
};

struct ImageRef {
    std::wstring source;
    std::wstring alt;
};

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// One heading, for the document outline / navigation panel.
struct OutlineEntry {
    std::wstring text;
    std::wstring anchor; // GitHub-style slug, for in-document #links
    int level = 1;
    int y = 0; // top of the heading block in document coordinates
};

struct IMeasurer;

struct Layout {
    std::wstring text; // full document text: selection, copy and search operate on this
    std::vector<Run> runs;
    std::vector<Decoration> decorations;
    std::vector<LinkTarget> links;
    std::vector<ImageRef> images;
    std::vector<OutlineEntry> outline;
    int width = 0;
    int contentHeight = 0;
    const IMeasurer* measurer = nullptr; // kept for hit testing
};

// Text measurement supplied by the host (GDI in the app, a stub in the tests).
struct IMeasurer {
    virtual ~IMeasurer() = default;
    virtual int width(FontId font, const wchar_t* text, size_t length) const = 0;
    virtual int lineHeight(FontId font) const = 0;
    virtual int ascent(FontId font) const = 0;
};

// Optional image size lookup. Returning false renders the alt text instead.
struct IImageSource {
    virtual ~IImageSource() = default;
    virtual bool imageSize(const std::wstring& source, int* width, int* height) = 0;
};

struct Metrics {
    int padding = 20;        // required content padding on all four sides
    int blockSpacing = 12;   // vertical gap between blocks
    int headingRuleGap = 8;      // heading text to the rule underneath it
    int headingRuleSpacing = 14; // rule to the next block, on top of blockSpacing
    int sectionSpacing = 18;     // extra space above a section heading
    int listIndent = 28;
    int quoteIndent = 18;
    int quoteBarWidth = 4;
    int codePadding = 10;
    int cellPadding = 8;
    int ruleThickness = 1;
    int checkboxSize = 13;
};

Layout buildLayout(const md::Document& doc, const IMeasurer& measurer, const Metrics& metrics,
                   int viewportWidth, IImageSource* images = nullptr);

// Index of the heading with this anchor, or -1. Comparison is case-insensitive.
int outlineIndexForAnchor(const Layout& layout, const std::wstring& anchor);

// Hit testing and selection helpers.
size_t indexAtPoint(const Layout& layout, int x, int y);
int linkAtPoint(const Layout& layout, int x, int y);
void selectionRects(const Layout& layout, size_t from, size_t to, std::vector<Rect>& out);
std::wstring extractText(const Layout& layout, size_t from, size_t to);

// UTF-8 <-> UTF-16 helpers shared by the parser boundary and the UI.
std::wstring toWide(const std::string& utf8);
std::string toUtf8(const std::wstring& wide);

} // namespace view
