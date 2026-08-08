// GFM table requirements: header/delimiter rows, alignment, ragged rows.
#include "test_md.h"

TEST(Tables, SimpleTable) {
    CHECK_EQ(P("| a | b |\n| - | - |\n| 1 | 2 |"),
             "(doc (table align=nn (th (td \"a\") (td \"b\")) (tr (td \"1\") (td \"2\"))))");
}

TEST(Tables, TableWithoutOuterPipes) {
    CHECK_EQ(P("a | b\n- | -\n1 | 2"),
             "(doc (table align=nn (th (td \"a\") (td \"b\")) (tr (td \"1\") (td \"2\"))))");
}

TEST(Tables, Alignment) {
    CHECK_EQ(P("| a | b | c |\n| :- | :-: | -: |\n| 1 | 2 | 3 |"),
             "(doc (table align=lcr (th (td \"a\") (td \"b\") (td \"c\"))"
             " (tr (td \"1\") (td \"2\") (td \"3\"))))");
}

TEST(Tables, HeaderOnly) {
    CHECK_EQ(P("| a |\n| - |"), "(doc (table align=n (th (td \"a\"))))");
}

TEST(Tables, ShortRowIsPadded) {
    CHECK_EQ(P("| a | b |\n| - | - |\n| 1 |"),
             "(doc (table align=nn (th (td \"a\") (td \"b\")) (tr (td \"1\") (td))))");
}

TEST(Tables, LongRowIsTruncated) {
    CHECK_EQ(P("| a |\n| - |\n| 1 | 2 |"),
             "(doc (table align=n (th (td \"a\")) (tr (td \"1\"))))");
}

TEST(Tables, InlineFormattingInCells) {
    CHECK_EQ(P("| a |\n| - |\n| *x* |"),
             "(doc (table align=n (th (td \"a\")) (tr (td (em \"x\")))))");
}

TEST(Tables, EscapedPipeInCell) {
    CHECK_EQ(P("| a |\n| - |\n| x \\| y |"),
             "(doc (table align=n (th (td \"a\")) (tr (td \"x | y\"))))");
}

TEST(Tables, MismatchedDelimiterCountIsNotATable) {
    CHECK_EQ(P("| a | b |\n| - |"), "(doc (p \"| a | b |\" (sb) \"| - |\"))");
}

TEST(Tables, TableEndsAtBlankLine) {
    CHECK_EQ(P("| a |\n| - |\n| 1 |\n\ntext"),
             "(doc (table align=n (th (td \"a\")) (tr (td \"1\"))) (p \"text\"))");
}

TEST(Tables, TableInsideBlockQuote) {
    CHECK_EQ(P("> | a |\n> | - |\n> | 1 |"),
             "(doc (quote (table align=n (th (td \"a\")) (tr (td \"1\")))))");
}
