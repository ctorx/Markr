// Block-level markdown requirements: headings, paragraphs, rules, code, quotes,
// front matter and raw HTML blocks.
#include "test_md.h"

TEST(Blocks, EmptyDocument) {
    CHECK_EQ(P(""), "(doc)");
    CHECK_EQ(P("\n\n   \n"), "(doc)");
}

TEST(Blocks, Paragraph) {
    CHECK_EQ(P("hello world"), "(doc (p \"hello world\"))");
    CHECK_EQ(P("one\n\ntwo"), "(doc (p \"one\") (p \"two\"))");
}

TEST(Blocks, ParagraphSoftBreakJoinsLines) {
    CHECK_EQ(P("one\ntwo"), "(doc (p \"one\" (sb) \"two\"))");
}

TEST(Blocks, ParagraphLeadingWhitespaceStripped) {
    CHECK_EQ(P("   hello"), "(doc (p \"hello\"))");
    CHECK_EQ(P("hello   "), "(doc (p \"hello\"))");
}

TEST(Blocks, AtxHeadings) {
    CHECK_EQ(P("# One"), "(doc (h1 \"One\"))");
    CHECK_EQ(P("## Two"), "(doc (h2 \"Two\"))");
    CHECK_EQ(P("### Three"), "(doc (h3 \"Three\"))");
    CHECK_EQ(P("#### Four"), "(doc (h4 \"Four\"))");
    CHECK_EQ(P("##### Five"), "(doc (h5 \"Five\"))");
    CHECK_EQ(P("###### Six"), "(doc (h6 \"Six\"))");
}

TEST(Blocks, AtxHeadingSevenHashesIsParagraph) {
    CHECK_EQ(P("####### Seven"), "(doc (p \"####### Seven\"))");
}

TEST(Blocks, AtxHeadingRequiresSpace) {
    CHECK_EQ(P("#hashtag"), "(doc (p \"#hashtag\"))");
}

TEST(Blocks, AtxHeadingClosingSequenceRemoved) {
    CHECK_EQ(P("## Title ##"), "(doc (h2 \"Title\"))");
    CHECK_EQ(P("## Title #not closing"), "(doc (h2 \"Title #not closing\"))");
}

TEST(Blocks, AtxHeadingEmpty) {
    CHECK_EQ(P("#"), "(doc (h1))");
    CHECK_EQ(P("## ##"), "(doc (h2))");
}

TEST(Blocks, AtxHeadingWithInlines) {
    CHECK_EQ(P("# A *b* c"), "(doc (h1 \"A \" (em \"b\") \" c\"))");
}

TEST(Blocks, SetextHeadings) {
    CHECK_EQ(P("Title\n====="), "(doc (h1 \"Title\"))");
    CHECK_EQ(P("Title\n-----"), "(doc (h2 \"Title\"))");
    CHECK_EQ(P("Multi\nline\n==="), "(doc (h1 \"Multi\" (sb) \"line\"))");
}

TEST(Blocks, ThematicBreaks) {
    CHECK_EQ(P("---"), "(doc (hr))");
    CHECK_EQ(P("***"), "(doc (hr))");
    CHECK_EQ(P("___"), "(doc (hr))");
    CHECK_EQ(P("- - -"), "(doc (hr))");
    CHECK_EQ(P("*****"), "(doc (hr))");
    CHECK_EQ(P("   ---"), "(doc (hr))");
}

TEST(Blocks, ThematicBreakNotWithTooFewMarkers) {
    CHECK_EQ(P("--"), "(doc (p \"--\"))");
}

TEST(Blocks, FencedCodeBackticks) {
    CHECK_EQ(P("```\ncode here\n```"), "(doc (code \"code here\"))");
}

TEST(Blocks, FencedCodeWithLanguage) {
    CHECK_EQ(P("```cpp\nint x = 1;\n```"), "(doc (code lang=cpp \"int x = 1;\"))");
}

TEST(Blocks, FencedCodeTildes) {
    CHECK_EQ(P("~~~\na ``` b\n~~~"), "(doc (code \"a ``` b\"))");
}

TEST(Blocks, FencedCodeKeepsMarkupLiteral) {
    CHECK_EQ(P("```\n# not a heading *not em*\n```"),
             "(doc (code \"# not a heading *not em*\"))");
}

TEST(Blocks, FencedCodeUnclosedRunsToEnd) {
    CHECK_EQ(P("```\nabc"), "(doc (code \"abc\"))");
}

TEST(Blocks, FencedCodeEmpty) {
    CHECK_EQ(P("```\n```"), "(doc (code \"\"))");
}

TEST(Blocks, FencedCodePreservesBlankLines) {
    CHECK_EQ(P("```\na\n\nb\n```"), "(doc (code \"a\\n\\nb\"))");
}

TEST(Blocks, IndentedCode) {
    CHECK_EQ(P("    indented\n    lines"), "(doc (code \"indented\\nlines\"))");
}

TEST(Blocks, IndentedCodeAfterParagraphIsLazyText) {
    // An indented line directly after a paragraph continues the paragraph.
    CHECK_EQ(P("text\n    more"), "(doc (p \"text\" (sb) \"more\"))");
}

TEST(Blocks, TabExpandsToFourColumnsForCode) {
    CHECK_EQ(P("\tindented"), "(doc (code \"indented\"))");
}

TEST(Blocks, BlockQuote) {
    CHECK_EQ(P("> quoted"), "(doc (quote (p \"quoted\")))");
    CHECK_EQ(P(">quoted"), "(doc (quote (p \"quoted\")))");
}

TEST(Blocks, BlockQuoteMultipleParagraphs) {
    CHECK_EQ(P("> one\n>\n> two"), "(doc (quote (p \"one\") (p \"two\")))");
}

TEST(Blocks, BlockQuoteLazyContinuation) {
    CHECK_EQ(P("> one\ntwo"), "(doc (quote (p \"one\" (sb) \"two\")))");
}

TEST(Blocks, BlockQuoteNested) {
    CHECK_EQ(P("> > deep"), "(doc (quote (quote (p \"deep\"))))");
}

TEST(Blocks, BlockQuoteContainsOtherBlocks) {
    CHECK_EQ(P("> # head\n> - item"),
             "(doc (quote (h1 \"head\") (ul tight (li (p \"item\")))))");
}

TEST(Blocks, HtmlBlockStripsTags) {
    CHECK_EQ(P("<div>\nhello\n</div>"), "(doc (html \"hello\"))");
}

TEST(Blocks, HtmlBlockComment) {
    CHECK_EQ(P("<!-- hidden -->"), "(doc (html \"\"))");
}

TEST(Blocks, FrontMatter) {
    CHECK_EQ(P("---\ntitle: Doc\n---\n\nbody"),
             "(doc (frontmatter \"title: Doc\") (p \"body\"))");
}

TEST(Blocks, FrontMatterOnlyAtStart) {
    CHECK_EQ(P("body\n\n---\ntitle: x\n---"),
             "(doc (p \"body\") (hr) (h2 \"title: x\"))");
}

TEST(Blocks, CrLfLineEndings) {
    CHECK_EQ(P("# Title\r\n\r\nbody\r\n"), "(doc (h1 \"Title\") (p \"body\"))");
}

TEST(Blocks, FootnoteDefinition) {
    CHECK_EQ(P("Text[^1]\n\n[^1]: The note"),
             "(doc (p \"Text\" (fnref 1)) (fndef 1 (p \"The note\")))");
}

TEST(Blocks, ConsecutiveFootnoteDefinitions) {
    CHECK_EQ(P("[^a]: one\n[^b]: two"),
             "(doc (fndef a (p \"one\")) (fndef b (p \"two\")))");
}

TEST(Blocks, FootnoteDefinitionFollowedByLinkDefinition) {
    CHECK_EQ(P("[^a]: one\n[id]: http://x.com\n\n[id]"),
             "(doc (fndef a (p \"one\")) (p (a http://x.com \"id\")))");
}

TEST(Blocks, FootnoteDefinitionMultiLine) {
    CHECK_EQ(P("[^a]: line one\n    line two"),
             "(doc (fndef a (p \"line one\" (sb) \"line two\")))");
}
