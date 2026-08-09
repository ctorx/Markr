// Layout requirements: wrapping (never horizontal overflow), padding, block
// spacing, styling, selection and copy.
#include "../src/layout.h"
#include "test_framework.h"

#include <algorithm>

namespace {

// Fixed-metric measurer: every glyph is 10 wide, every line 20 tall.
struct StubMeasurer : view::IMeasurer {
    int width(view::FontId, const wchar_t*, size_t length) const override {
        return static_cast<int>(length) * 10;
    }
    int lineHeight(view::FontId) const override { return 20; }
    int ascent(view::FontId) const override { return 16; }
};

view::Metrics stubMetrics() {
    view::Metrics m;
    m.padding = 20;
    m.blockSpacing = 10;
    m.listIndent = 20;
    m.quoteIndent = 20;
    m.quoteBarWidth = 4;
    m.codePadding = 10;
    m.cellPadding = 10;
    m.headingRuleGap = 8;
    m.headingRuleSpacing = 14;
    m.sectionSpacing = 18;
    return m;
}

// Vertical gap between the bottom of run `a` and the top of run `b`.
int gapBetweenRuns(const view::Layout& l, size_t a, size_t b) {
    return l.runs[b].y - (l.runs[a].y + l.runs[a].height);
}

view::Layout lay(const std::string& markdown, int width) {
    static StubMeasurer measurer;
    md::Document doc = md::parse(markdown);
    return view::buildLayout(doc, measurer, stubMetrics(), width, nullptr);
}

std::wstring runTexts(const view::Layout& l) {
    std::wstring out;
    for (const view::Run& r : l.runs) {
        if (!out.empty()) out += L"|";
        out += r.text;
    }
    return out;
}

int maxRight(const view::Layout& l) {
    int right = 0;
    for (const view::Run& r : l.runs) right = std::max(right, r.x + r.width);
    for (const view::Decoration& d : l.decorations) right = std::max(right, d.x + d.width);
    return right;
}

int distinctLines(const view::Layout& l) {
    std::vector<int> ys;
    for (const view::Run& r : l.runs) {
        if (std::find(ys.begin(), ys.end(), r.y) == ys.end()) ys.push_back(r.y);
    }
    return static_cast<int>(ys.size());
}

} // namespace

TEST(Layout, PaddingAppliedOnAllSides) {
    view::Layout l = lay("hello", 400);
    CHECK_EQ(l.runs.size(), size_t(1));
    CHECK_EQ(l.runs[0].x, 20);
    CHECK_EQ(l.runs[0].y, 20);
    // Bottom padding is part of the reported content height.
    CHECK_EQ(l.contentHeight, 60);
}

TEST(Layout, ParagraphWrapsWithinContentWidth) {
    // 400 wide - 2*20 padding = 360 usable = 36 characters.
    view::Layout l = lay("aaaa bbbb cccc dddd eeee ffff gggg hhhh iiii", 400);
    CHECK_EQ(distinctLines(l), 2);
    CHECK_TRUE(maxRight(l) <= 380);
}

TEST(Layout, NarrowerWidthWrapsMoreNotWider) {
    view::Layout wide = lay("aaaa bbbb cccc dddd eeee ffff gggg hhhh iiii", 400);
    view::Layout narrow = lay("aaaa bbbb cccc dddd eeee ffff gggg hhhh iiii", 240);
    CHECK_TRUE(distinctLines(narrow) > distinctLines(wide));
    CHECK_TRUE(maxRight(narrow) <= 220);
}

TEST(Layout, VeryLongWordIsBrokenRatherThanOverflowing) {
    view::Layout l = lay("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 240);
    CHECK_TRUE(maxRight(l) <= 220);
    CHECK_TRUE(distinctLines(l) >= 3);
}

TEST(Layout, DocumentTextIsLogicalNotWrapped) {
    view::Layout l = lay("one\n\ntwo", 400);
    CHECK_EQ(l.text, std::wstring(L"one\ntwo\n"));
}

TEST(Layout, WrappedParagraphKeepsSingleLogicalLine) {
    view::Layout l = lay("aaaa bbbb cccc dddd eeee ffff gggg hhhh iiii", 400);
    CHECK_EQ(l.text, std::wstring(L"aaaa bbbb cccc dddd eeee ffff gggg hhhh iiii\n"));
}

TEST(Layout, HeadingUsesHeadingFont) {
    view::Layout l = lay("# Title", 400);
    CHECK_EQ(l.runs.size(), size_t(1));
    CHECK_TRUE(l.runs[0].style.font == view::FontId::H1);
    CHECK_TRUE(l.runs[0].style.color == view::ColorRole::Heading);
}

TEST(Layout, EmphasisSelectsFontVariants) {
    view::Layout l = lay("*a* **b** ***c*** `d`", 400);
    CHECK_TRUE(l.runs[0].style.font == view::FontId::BodyItalic);
    CHECK_TRUE(l.runs[2].style.font == view::FontId::BodyBold);
    CHECK_TRUE(l.runs[4].style.font == view::FontId::BodyBoldItalic);
    CHECK_TRUE(l.runs[6].style.font == view::FontId::Code);
    CHECK_TRUE(l.runs[6].style.background == view::ColorRole::InlineCodeBg);
}

TEST(Layout, StrikethroughAndLinkStyling) {
    view::Layout l = lay("~~x~~ [t](http://a.com)", 400);
    CHECK_TRUE(l.runs[0].style.strike);
    CHECK_EQ(l.links.size(), size_t(1));
    CHECK_EQ(l.links[0].url, std::wstring(L"http://a.com"));
    const view::Run& linkRun = l.runs[l.runs.size() - 1];
    CHECK_EQ(linkRun.style.linkIndex, 0);
    CHECK_TRUE(linkRun.style.color == view::ColorRole::Link);
    CHECK_TRUE(linkRun.style.underline);
}

TEST(Layout, CodeBlockGetsBackgroundAndCodeFont) {
    view::Layout l = lay("```\nint x;\n```", 400);
    CHECK_EQ(l.runs.size(), size_t(1));
    CHECK_TRUE(l.runs[0].style.font == view::FontId::Code);
    bool hasBg = false;
    for (const view::Decoration& d : l.decorations) {
        if (d.type == view::DecorationType::CodeBlockBg) hasBg = true;
    }
    CHECK_TRUE(hasBg);
}

TEST(Highlighting, CodeBlockWithLanguageIsColoured) {
    view::Layout l = lay("```cs\nint x = 1;\n```", 400);
    bool type = false, number = false;
    for (const view::Run& r : l.runs) {
        if (r.text == std::wstring(L"int") && r.style.color == view::ColorRole::CodeType)
            type = true;
        if (r.text == std::wstring(L"1") && r.style.color == view::ColorRole::CodeNumber)
            number = true;
        CHECK_TRUE(r.style.font == view::FontId::Code);
    }
    CHECK_TRUE(type);
    CHECK_TRUE(number);
}

TEST(Highlighting, CodeBlockWithoutLanguageStaysPlain) {
    view::Layout l = lay("```\nint x = 1;\n```", 400);
    CHECK_EQ(l.runs.size(), size_t(1));
    CHECK_TRUE(l.runs[0].style.color == view::ColorRole::CodeText);
}

TEST(Highlighting, UnknownLanguageStaysPlain) {
    view::Layout l = lay("```brainfuck\nint x = 1;\n```", 400);
    CHECK_EQ(l.runs.size(), size_t(1));
}

TEST(Highlighting, DoesNotChangeTheDocumentText) {
    view::Layout plain = lay("```\nint x = 1;\n```", 400);
    view::Layout coloured = lay("```cs\nint x = 1;\n```", 400);
    CHECK_EQ(coloured.text, plain.text);
    CHECK_EQ(coloured.text, std::wstring(L"int x = 1;\n"));
}

TEST(Highlighting, MultiLineStringsKeepTheirColourOnEveryLine) {
    view::Layout l = lay("```pgsql\nAS $$\nBEGIN\nEND;\n$$ LANGUAGE plpgsql\n```", 400);
    for (const view::Run& r : l.runs) {
        if (r.text == std::wstring(L"BEGIN")) {
            CHECK_TRUE(r.style.color == view::ColorRole::CodeString);
        }
        if (r.text == std::wstring(L"AS")) {
            CHECK_TRUE(r.style.color == view::ColorRole::CodeKeyword);
        }
    }
}

TEST(Highlighting, MultiLineCommentsSpanLines) {
    view::Layout l = lay("```cs\n/* a\nb */\nint x;\n```", 400);
    int commentRuns = 0;
    for (const view::Run& r : l.runs) {
        if (r.style.color == view::ColorRole::CodeComment) ++commentRuns;
    }
    CHECK_EQ(commentRuns, 2);
}

TEST(Layout, CodeBlockLongLineWrapsInsteadOfOverflowing) {
    view::Layout l = lay("```\naaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n```", 300);
    CHECK_TRUE(maxRight(l) <= 280);
    CHECK_TRUE(distinctLines(l) >= 2);
}

TEST(Spacing, HeadingRuleSitsBelowTheTextWithBreathingRoom) {
    view::Layout l = lay("# Title", 400);
    CHECK_EQ(l.decorations.size(), size_t(1));
    CHECK_EQ(l.decorations[0].y - (l.runs[0].y + l.runs[0].height), 8);
}

TEST(Spacing, ContentClearsTheHeadingRule) {
    view::Layout afterH1 = lay("# A\n\ntext", 400);
    view::Layout afterH2 = lay("## A\n\ntext", 400);

    int h1Gap = afterH1.runs[1].y - (afterH1.decorations[0].y + afterH1.decorations[0].height);
    int h2Gap = afterH2.runs[1].y - (afterH2.decorations[0].y + afterH2.decorations[0].height);
    // Both clear the rule, and a top-level section gets the wider gap.
    CHECK_TRUE(h2Gap > 10);
    CHECK_TRUE(h1Gap > h2Gap);
}

TEST(Spacing, SectionHeadingsGetExtraSpaceAbove) {
    int paragraphGap = gapBetweenRuns(lay("a\n\nb", 400), 0, 1);
    int sectionGap = gapBetweenRuns(lay("a\n\n## b", 400), 0, 1);
    CHECK_EQ(paragraphGap, 10);
    CHECK_EQ(sectionGap - paragraphGap, 18);
}

TEST(Spacing, SpaceAboveHeadingsShrinksWithDepth) {
    int h1 = gapBetweenRuns(lay("a\n\n# b", 400), 0, 1);
    int h2 = gapBetweenRuns(lay("a\n\n## b", 400), 0, 1);
    int h3 = gapBetweenRuns(lay("a\n\n### b", 400), 0, 1);
    int h4 = gapBetweenRuns(lay("a\n\n#### b", 400), 0, 1);
    CHECK_TRUE(h1 > h2);
    CHECK_TRUE(h2 > h3);
    CHECK_TRUE(h3 >= h4);
}

TEST(Spacing, NoExtraSpaceAboveAHeadingThatOpensTheDocument) {
    view::Layout l = lay("# Title", 400);
    CHECK_EQ(l.runs[0].y, 20);
}

TEST(Layout, ThematicBreakEmitsRule) {
    view::Layout l = lay("---", 400);
    CHECK_EQ(l.runs.size(), size_t(0));
    CHECK_EQ(l.decorations.size(), size_t(1));
    CHECK_TRUE(l.decorations[0].type == view::DecorationType::Rule);
    CHECK_EQ(l.decorations[0].x, 20);
    CHECK_EQ(l.decorations[0].width, 360);
}

TEST(Layout, BulletListMarkerAndIndent) {
    view::Layout l = lay("- item", 400);
    CHECK_EQ(runTexts(l), std::wstring(L"\x2022 |item"));
    CHECK_EQ(l.runs[0].x, 20);
    CHECK_EQ(l.runs[1].x, 40);
    CHECK_EQ(l.text, std::wstring(L"\x2022 item\n"));
}

TEST(Layout, OrderedListMarkersCount) {
    view::Layout l = lay("3. a\n4. b", 400);
    CHECK_EQ(l.runs[0].text, std::wstring(L"3. "));
    CHECK_EQ(l.runs[2].text, std::wstring(L"4. "));
}

TEST(Layout, NestedListIndentsFurther) {
    view::Layout l = lay("- a\n  - b", 400);
    CHECK_EQ(l.runs[0].x, 20);
    CHECK_EQ(l.runs[2].x, 40);
    CHECK_EQ(l.runs[3].x, 60);
}

TEST(Layout, TaskListEmitsCheckboxes) {
    view::Layout l = lay("- [ ] a\n- [x] b", 400);
    int unchecked = 0, checked = 0;
    for (const view::Decoration& d : l.decorations) {
        if (d.type == view::DecorationType::Checkbox) ++unchecked;
        if (d.type == view::DecorationType::CheckboxChecked) ++checked;
    }
    CHECK_EQ(unchecked, 1);
    CHECK_EQ(checked, 1);
}

TEST(Layout, BlockQuoteIndentsAndDrawsBar) {
    view::Layout l = lay("> quoted", 400);
    CHECK_EQ(l.runs[0].x, 40);
    CHECK_TRUE(l.runs[0].style.color == view::ColorRole::QuoteText);
    bool bar = false;
    for (const view::Decoration& d : l.decorations) {
        if (d.type == view::DecorationType::QuoteBar) bar = true;
    }
    CHECK_TRUE(bar);
}

TEST(Layout, TableStaysWithinWidthAndDrawsHeader) {
    view::Layout l = lay("| aaa | bbb |\n| - | - |\n| 1 | 2 |", 400);
    CHECK_TRUE(maxRight(l) <= 380);
    bool headerBg = false;
    for (const view::Decoration& d : l.decorations) {
        if (d.type == view::DecorationType::TableHeaderBg) headerBg = true;
    }
    CHECK_TRUE(headerBg);
    CHECK_TRUE(l.runs[0].style.font == view::FontId::BodyBold);
}

TEST(Layout, NarrowedTableColumnStillFitsItsLongestWord) {
    // The second column has to give up width; the first must stay wide enough
    // for "Headings" so words are not broken mid-way.
    view::Layout l = lay("| Headings | Notes |\n| - | - |\n"
                         "| Headings | aaaa bbbb cccc dddd eeee ffff gggg |",
                         400);
    bool intact = false;
    for (const view::Run& r : l.runs) {
        if (r.text == std::wstring(L"Headings")) intact = true;
    }
    CHECK_TRUE(intact);
    CHECK_TRUE(maxRight(l) <= 380);
}

TEST(Layout, WideTableWrapsCellsInsteadOfOverflowing) {
    view::Layout l = lay("| aaaaaaaaaaaaaaa | bbbbbbbbbbbbbbb |\n| - | - |\n"
                         "| ccccccccccccccc | ddddddddddddddd |",
                         300);
    CHECK_TRUE(maxRight(l) <= 280);
}

TEST(Layout, ImageWithoutSourceFallsBackToAltText) {
    view::Layout l = lay("![alt text](missing.png)", 400);
    CHECK_EQ(l.images.size(), size_t(1));
    CHECK_EQ(l.images[0].source, std::wstring(L"missing.png"));
    CHECK_EQ(runTexts(l), std::wstring(L"alt text"));
    CHECK_TRUE(l.runs[0].style.color == view::ColorRole::Muted);
}

TEST(Layout, FootnoteReferenceIsSuperscriptNumber) {
    view::Layout l = lay("a[^x]\n\n[^x]: note", 400);
    bool found = false;
    for (const view::Run& r : l.runs) {
        if (r.text == std::wstring(L"1") && r.style.verticalShift < 0) found = true;
    }
    CHECK_TRUE(found);
}

TEST(Layout, HardBreakStartsNewLineWithoutBlockGap) {
    view::Layout l = lay("a  \nb", 400);
    CHECK_EQ(distinctLines(l), 2);
    CHECK_EQ(l.text, std::wstring(L"a\nb\n"));
}

TEST(Layout, BlocksAreOrderedTopToBottom) {
    view::Layout l = lay("# h\n\npara\n\n- item", 400);
    for (size_t i = 1; i < l.runs.size(); ++i) {
        CHECK_TRUE(l.runs[i].y >= l.runs[i - 1].y);
    }
}

TEST(Outline, CollectsHeadingsInDocumentOrder) {
    view::Layout l = lay("# One\n\ntext\n\n## Two\n\n### Three", 400);
    CHECK_EQ(l.outline.size(), size_t(3));
    CHECK_EQ(l.outline[0].text, std::wstring(L"One"));
    CHECK_EQ(l.outline[0].level, 1);
    CHECK_EQ(l.outline[1].text, std::wstring(L"Two"));
    CHECK_EQ(l.outline[1].level, 2);
    CHECK_EQ(l.outline[2].level, 3);
    CHECK_TRUE(l.outline[1].y > l.outline[0].y);
    CHECK_TRUE(l.outline[2].y > l.outline[1].y);
}

TEST(Outline, EntryPointsAtTheHeadingBlock) {
    view::Layout l = lay("para\n\n## Section", 400);
    CHECK_EQ(l.outline.size(), size_t(1));
    // The recorded offset is the top of the heading block, above its first run.
    CHECK_TRUE(l.outline[0].y <= l.runs[1].y);
}

TEST(Outline, GeneratesGitHubStyleAnchors) {
    view::Layout l = lay("# Hello World\n\n## API: Reference!\n\n### Hello World", 400);
    CHECK_EQ(l.outline.size(), size_t(3));
    CHECK_EQ(l.outline[0].anchor, std::wstring(L"hello-world"));
    CHECK_EQ(l.outline[1].anchor, std::wstring(L"api-reference"));
    // Repeated headings get the numeric suffix GitHub uses.
    CHECK_EQ(l.outline[2].anchor, std::wstring(L"hello-world-1"));
}

TEST(Outline, AnchorLookupIsCaseInsensitive) {
    view::Layout l = lay("# Getting Started", 400);
    CHECK_EQ(view::outlineIndexForAnchor(l, L"getting-started"), 0);
    CHECK_EQ(view::outlineIndexForAnchor(l, L"Getting-Started"), 0);
    CHECK_EQ(view::outlineIndexForAnchor(l, L"missing"), -1);
}

TEST(Outline, IgnoresEverythingThatIsNotAHeading) {
    view::Layout l = lay("text\n\n- item\n\n```\ncode\n```", 400);
    CHECK_EQ(l.outline.size(), size_t(0));
}

TEST(Outline, UsesPlainTextOfFormattedHeading) {
    view::Layout l = lay("# A *b* `c` [d](u)", 400);
    CHECK_EQ(l.outline.size(), size_t(1));
    CHECK_EQ(l.outline[0].text, std::wstring(L"A b c d"));
}

TEST(Outline, IncludesHeadingsInsideContainers) {
    view::Layout l = lay("> # Quoted\n\n- # In a list", 400);
    CHECK_EQ(l.outline.size(), size_t(2));
    CHECK_EQ(l.outline[0].text, std::wstring(L"Quoted"));
    CHECK_EQ(l.outline[1].text, std::wstring(L"In a list"));
}

TEST(Outline, SetextHeadingsAreIncluded) {
    view::Layout l = lay("Title\n=====\n\nSub\n---", 400);
    CHECK_EQ(l.outline.size(), size_t(2));
    CHECK_EQ(l.outline[0].level, 1);
    CHECK_EQ(l.outline[1].level, 2);
}

TEST(Layout, ExtractTextReturnsSelectedRange) {
    view::Layout l = lay("hello world", 400);
    CHECK_EQ(view::extractText(l, 0, 5), std::wstring(L"hello"));
    CHECK_EQ(view::extractText(l, 6, 11), std::wstring(L"world"));
    CHECK_EQ(view::extractText(l, 11, 6), std::wstring(L"world"));
}

TEST(Layout, IndexAtPointMapsToCharacters) {
    view::Layout l = lay("hello", 400);
    CHECK_EQ(view::indexAtPoint(l, 20, 25), size_t(0));
    CHECK_EQ(view::indexAtPoint(l, 44, 25), size_t(2));
    CHECK_EQ(view::indexAtPoint(l, 46, 25), size_t(3));
    CHECK_EQ(view::indexAtPoint(l, 1000, 25), size_t(5));
}

TEST(Layout, SelectionRectsCoverSelectedRuns) {
    view::Layout l = lay("hello", 400);
    std::vector<view::Rect> rects;
    view::selectionRects(l, 1, 3, rects);
    CHECK_EQ(rects.size(), size_t(1));
    CHECK_EQ(rects[0].x, 30);
    CHECK_EQ(rects[0].width, 20);
}

TEST(Layout, LinkAtPointFindsTheLink) {
    view::Layout l = lay("[click](http://a.com)", 400);
    CHECK_EQ(view::linkAtPoint(l, 25, 25), 0);
    CHECK_EQ(view::linkAtPoint(l, 300, 25), -1);
}

TEST(Layout, EmptyDocumentHasOnlyPadding) {
    view::Layout l = lay("", 400);
    CHECK_EQ(l.runs.size(), size_t(0));
    CHECK_EQ(l.contentHeight, 40);
}

TEST(Layout, UnicodeRoundTrip) {
    CHECK_EQ(view::toUtf8(view::toWide("caf\xC3\xA9")), std::string("caf\xC3\xA9"));
    CHECK_EQ(view::toWide("\xE2\x80\x94").size(), size_t(1));
}
