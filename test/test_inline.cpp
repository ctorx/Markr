// Inline markdown requirements: emphasis, code spans, escapes, entities,
// breaks and the inline HTML subset.
#include "test_md.h"

TEST(Inline, Emphasis) {
    CHECK_EQ(P("*a*"), "(doc (p (em \"a\")))");
    CHECK_EQ(P("_a_"), "(doc (p (em \"a\")))");
}

TEST(Inline, Strong) {
    CHECK_EQ(P("**a**"), "(doc (p (strong \"a\")))");
    CHECK_EQ(P("__a__"), "(doc (p (strong \"a\")))");
}

TEST(Inline, StrongInsideEmphasis) {
    CHECK_EQ(P("***a***"), "(doc (p (em (strong \"a\"))))");
}

TEST(Inline, NestedEmphasis) {
    CHECK_EQ(P("*a **b** c*"), "(doc (p (em \"a \" (strong \"b\") \" c\")))");
}

TEST(Inline, EmphasisNotInsideWordWithUnderscore) {
    CHECK_EQ(P("snake_case_name"), "(doc (p \"snake_case_name\"))");
}

TEST(Inline, EmphasisInsideWordWithAsterisk) {
    CHECK_EQ(P("a*b*c"), "(doc (p \"a\" (em \"b\") \"c\"))");
}

TEST(Inline, UnmatchedDelimiterIsLiteral) {
    CHECK_EQ(P("a * b"), "(doc (p \"a * b\"))");
    CHECK_EQ(P("**unclosed"), "(doc (p \"**unclosed\"))");
}

TEST(Inline, Strikethrough) {
    CHECK_EQ(P("~~gone~~"), "(doc (p (del \"gone\")))");
}

TEST(Inline, Highlight) {
    CHECK_EQ(P("==note=="), "(doc (p (mark \"note\")))");
}

TEST(Inline, CodeSpan) {
    CHECK_EQ(P("`code`"), "(doc (p (c \"code\")))");
}

TEST(Inline, CodeSpanKeepsMarkupLiteral) {
    CHECK_EQ(P("`*not em*`"), "(doc (p (c \"*not em*\")))");
}

TEST(Inline, CodeSpanDoubleBacktick) {
    CHECK_EQ(P("``a ` b``"), "(doc (p (c \"a ` b\")))");
}

TEST(Inline, CodeSpanStripsOneSpaceEachSide) {
    CHECK_EQ(P("`` `x` ``"), "(doc (p (c \"`x`\")))");
}

TEST(Inline, CodeSpanUnclosedIsLiteral) {
    CHECK_EQ(P("`abc"), "(doc (p \"`abc\"))");
}

TEST(Inline, BackslashEscapes) {
    CHECK_EQ(P("\\*not em\\*"), "(doc (p \"*not em*\"))");
    CHECK_EQ(P("\\# not heading"), "(doc (p \"# not heading\"))");
    CHECK_EQ(P("\\\\"), "(doc (p \"\\\\\"))");
}

TEST(Inline, BackslashBeforeNonPunctuationIsLiteral) {
    CHECK_EQ(P("\\a"), "(doc (p \"\\\\a\"))");
}

TEST(Inline, Entities) {
    CHECK_EQ(P("a &amp; b"), "(doc (p \"a & b\"))");
    CHECK_EQ(P("&lt;tag&gt;"), "(doc (p \"<tag>\"))");
    CHECK_EQ(P("&#65;"), "(doc (p \"A\"))");
    CHECK_EQ(P("&#x41;"), "(doc (p \"A\"))");
}

TEST(Inline, UnknownEntityIsLiteral) {
    CHECK_EQ(P("&notarealentity;"), "(doc (p \"&notarealentity;\"))");
}

TEST(Inline, HardBreakWithTwoSpaces) {
    CHECK_EQ(P("a  \nb"), "(doc (p \"a\" (br) \"b\"))");
}

TEST(Inline, HardBreakWithBackslash) {
    CHECK_EQ(P("a\\\nb"), "(doc (p \"a\" (br) \"b\"))");
}

TEST(Inline, InlineHtmlBold) {
    CHECK_EQ(P("<b>bold</b>"), "(doc (p (strong \"bold\")))");
    CHECK_EQ(P("<i>it</i>"), "(doc (p (em \"it\")))");
    CHECK_EQ(P("<u>under</u>"), "(doc (p (u \"under\")))");
    CHECK_EQ(P("<mark>hl</mark>"), "(doc (p (mark \"hl\")))");
}

TEST(Inline, InlineHtmlSubSup) {
    CHECK_EQ(P("H<sub>2</sub>O"), "(doc (p \"H\" (sub \"2\") \"O\"))");
    CHECK_EQ(P("x<sup>2</sup>"), "(doc (p \"x\" (sup \"2\")))");
}

TEST(Inline, InlineHtmlBreak) {
    CHECK_EQ(P("a<br>b"), "(doc (p \"a\" (br) \"b\"))");
    CHECK_EQ(P("a<br/>b"), "(doc (p \"a\" (br) \"b\"))");
}

TEST(Inline, UnknownInlineHtmlIsDropped) {
    CHECK_EQ(P("a<span class=\"x\">b</span>c"), "(doc (p \"abc\"))");
}

TEST(Inline, EmphasisAcrossCodeSpanBoundaryStaysLiteral) {
    CHECK_EQ(P("*a`b*c`"), "(doc (p \"*a\" (c \"b*c\")))");
}

TEST(Inline, MixedFormatting) {
    CHECK_EQ(P("**bold** and *em* and `code` and ~~del~~"),
             "(doc (p (strong \"bold\") \" and \" (em \"em\") \" and \" (c \"code\")"
             " \" and \" (del \"del\")))");
}
