// Copying a selection back out as markdown or as HTML: structure is preserved,
// and a partial selection is clipped without losing the markup around it.
#include "../src/md_export.h"
#include "test_framework.h"

namespace {

struct StubMeasurer : view::IMeasurer {
    int width(view::FontId, const wchar_t*, size_t length) const override {
        return static_cast<int>(length) * 10;
    }
    int lineHeight(view::FontId) const override { return 20; }
    int ascent(view::FontId) const override { return 16; }
};

const view::IMeasurer& measurer() {
    static StubMeasurer stub;
    return stub;
}

view::Metrics stubMetrics() {
    view::Metrics m;
    m.padding = 20;
    m.blockSpacing = 10;
    m.listIndent = 20;
    m.quoteIndent = 20;
    m.codePadding = 10;
    m.cellPadding = 10;
    return m;
}

// A parsed document plus its layout. Held by value in each test; the layout's
// node ranges point into the document, so the two travel together.
struct Fixture {
    md::Document document;
    view::Layout layout;

    explicit Fixture(const std::string& markdown, int width = 800) {
        document = md::parse(markdown);
        layout = view::buildLayout(document, measurer(), stubMetrics(), width, nullptr);
    }

    // Offset of `needle` in the rendered text; the tests select by content so
    // they do not have to count characters.
    size_t at(const wchar_t* needle) const {
        size_t index = layout.text.find(needle);
        return index == std::wstring::npos ? 0 : index;
    }

    std::wstring markdownAll() const {
        return view::selectionMarkdown(document, layout, 0, layout.text.size());
    }
    std::wstring markdownFrom(const wchar_t* first, const wchar_t* last) const {
        return view::selectionMarkdown(document, layout, at(first),
                                       at(last) + std::wstring(last).size());
    }
    std::wstring htmlAll() const {
        return view::selectionHtml(document, layout, 0, layout.text.size());
    }
};

bool contains(const std::wstring& haystack, const wchar_t* needle) {
    return haystack.find(needle) != std::wstring::npos;
}

} // namespace

TEST(Export, EmphasisSurvivesRoundTrip) {
    Fixture f("Some **bold** and *italic* text\n");
    CHECK_EQ(f.markdownAll(), std::wstring(L"Some **bold** and *italic* text"));
}

TEST(Export, PartialSelectionKeepsTheMarkupItOverlaps) {
    Fixture f("Some **bold** and *italic* text\n");
    // "bold and" - starts inside the strong span, ends outside it.
    CHECK_EQ(f.markdownFrom(L"bold", L"and"), std::wstring(L"**bold** and"));
}

TEST(Export, SelectionInsideOneWordOfEmphasisClipsTheWord) {
    Fixture f("Some **bolder** text\n");
    CHECK_EQ(f.markdownFrom(L"bold", L"bold"), std::wstring(L"**bold**"));
}

TEST(Export, HeadingKeepsItsLevel) {
    Fixture f("### Third level\n\nBody text\n");
    CHECK_EQ(f.markdownFrom(L"Third", L"level"), std::wstring(L"### Third level"));
}

TEST(Export, BlocksAreSeparatedByBlankLines) {
    Fixture f("# Title\n\nFirst paragraph\n\nSecond paragraph\n");
    CHECK_EQ(f.markdownAll(),
             std::wstring(L"# Title\n\nFirst paragraph\n\nSecond paragraph"));
}

TEST(Export, ListKeepsItsMarkers) {
    Fixture f("- one\n- two\n");
    CHECK_EQ(f.markdownAll(), std::wstring(L"- one\n- two"));
}

TEST(Export, OrderedListKeepsItsNumbering) {
    Fixture f("3. three\n4. four\n");
    CHECK_EQ(f.markdownAll(), std::wstring(L"3. three\n4. four"));
}

TEST(Export, TaskListKeepsItsCheckboxes) {
    Fixture f("- [x] done\n- [ ] todo\n");
    CHECK_EQ(f.markdownAll(), std::wstring(L"- [x] done\n- [ ] todo"));
}

TEST(Export, NestedListIsIndented) {
    Fixture f("- outer\n    - inner\n");
    CHECK_EQ(f.markdownAll(), std::wstring(L"- outer\n  - inner"));
}

TEST(Export, BlockQuoteKeepsItsPrefix) {
    Fixture f("> quoted line\n");
    CHECK_EQ(f.markdownAll(), std::wstring(L"> quoted line"));
}

TEST(Export, CodeBlockStaysFenced) {
    Fixture f("```cpp\nint main() {}\n```\n");
    CHECK_EQ(f.markdownAll(), std::wstring(L"```cpp\nint main() {}\n```"));
}

TEST(Export, InlineCodeKeepsItsBackticks) {
    Fixture f("Call `run()` now\n");
    CHECK_EQ(f.markdownAll(), std::wstring(L"Call `run()` now"));
}

TEST(Export, LinkKeepsItsDestination) {
    Fixture f("See [the docs](https://example.com/a) today\n");
    CHECK_EQ(f.markdownFrom(L"the", L"docs"),
             std::wstring(L"[the docs](https://example.com/a)"));
}

TEST(Export, TableKeepsItsHeaderEvenWhenOnlyABodyCellIsSelected) {
    Fixture f("| a | b |\n| --- | ---: |\n| 1 | 2 |\n");
    std::wstring markdown = f.markdownFrom(L"1", L"1");
    CHECK_EQ(markdown, std::wstring(L"| a | b |\n| --- | ---: |\n| 1 |  |"));
}

TEST(Export, LiteralMarkupIsEscaped) {
    Fixture f("A star \\* and an underscore \\_ stay literal\n");
    CHECK_TRUE(contains(f.markdownAll(), L"\\*"));
    CHECK_TRUE(contains(f.markdownAll(), L"\\_"));
}

TEST(Export, EmptySelectionProducesNothing) {
    Fixture f("Some text\n");
    CHECK_EQ(view::selectionMarkdown(f.document, f.layout, 3, 3), std::wstring());
    CHECK_EQ(view::selectionHtml(f.document, f.layout, 3, 3), std::wstring());
}

TEST(Export, HtmlUsesSemanticTags) {
    Fixture f("# Title\n\nSome **bold** and *italic* text\n");
    std::wstring html = f.htmlAll();
    CHECK_TRUE(contains(html, L"<h1>Title</h1>"));
    CHECK_TRUE(contains(html, L"<strong>bold</strong>"));
    CHECK_TRUE(contains(html, L"<em>italic</em>"));
    CHECK_TRUE(contains(html, L"<p>"));
}

TEST(Export, HtmlEscapesTextButNotItsOwnTags) {
    Fixture f("5 < 6 & 7 > 4\n");
    std::wstring html = f.htmlAll();
    CHECK_TRUE(contains(html, L"5 &lt; 6 &amp; 7 &gt; 4"));
}

TEST(Export, HtmlListsAndLinksAreStructured) {
    Fixture f("- one\n- [two](https://example.com)\n");
    std::wstring html = f.htmlAll();
    CHECK_TRUE(contains(html, L"<ul>"));
    CHECK_TRUE(contains(html, L"<li>one</li>"));
    CHECK_TRUE(contains(html, L"<a href=\"https://example.com\">two</a>"));
}

TEST(Export, HtmlPartialSelectionClipsInsideTags) {
    Fixture f("Some **bolder** text\n");
    size_t start = f.at(L"bold");
    std::wstring html = view::selectionHtml(f.document, f.layout, start, start + 4);
    CHECK_TRUE(contains(html, L"<strong>bold</strong>"));
    CHECK_TRUE(!contains(html, L"bolder"));
}
