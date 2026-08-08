// List requirements: bullets, ordered lists, nesting, tight vs loose, tasks.
#include "test_md.h"

TEST(Lists, BulletList) {
    CHECK_EQ(P("- a\n- b"), "(doc (ul tight (li (p \"a\")) (li (p \"b\"))))");
}

TEST(Lists, BulletMarkersAllThree) {
    CHECK_EQ(P("* a"), "(doc (ul tight (li (p \"a\"))))");
    CHECK_EQ(P("+ a"), "(doc (ul tight (li (p \"a\"))))");
    CHECK_EQ(P("- a"), "(doc (ul tight (li (p \"a\"))))");
}

TEST(Lists, ChangingBulletStartsNewList) {
    CHECK_EQ(P("- a\n* b"),
             "(doc (ul tight (li (p \"a\"))) (ul tight (li (p \"b\"))))");
}

TEST(Lists, OrderedList) {
    CHECK_EQ(P("1. a\n2. b"), "(doc (ol start=1 tight (li (p \"a\")) (li (p \"b\"))))");
}

TEST(Lists, OrderedListCustomStart) {
    CHECK_EQ(P("5. a\n6. b"), "(doc (ol start=5 tight (li (p \"a\")) (li (p \"b\"))))");
}

TEST(Lists, OrderedListParenDelimiter) {
    CHECK_EQ(P("1) a"), "(doc (ol start=1 tight (li (p \"a\"))))");
}

TEST(Lists, LooseListWhenItemsSeparatedByBlankLine) {
    CHECK_EQ(P("- a\n\n- b"), "(doc (ul loose (li (p \"a\")) (li (p \"b\"))))");
}

TEST(Lists, NestedList) {
    CHECK_EQ(P("- a\n  - b"),
             "(doc (ul tight (li (p \"a\") (ul tight (li (p \"b\"))))))");
}

TEST(Lists, DeeplyNestedMixedList) {
    CHECK_EQ(P("1. a\n   - b\n     1. c"),
             "(doc (ol start=1 tight (li (p \"a\") (ul tight (li (p \"b\")"
             " (ol start=1 tight (li (p \"c\"))))))))");
}

TEST(Lists, ItemWithMultipleParagraphs) {
    CHECK_EQ(P("- a\n\n  b"), "(doc (ul loose (li (p \"a\") (p \"b\"))))");
}

TEST(Lists, ItemWithCodeBlock) {
    CHECK_EQ(P("- a\n\n  ```\n  x\n  ```"),
             "(doc (ul loose (li (p \"a\") (code \"x\"))))");
}

TEST(Lists, ItemLazyContinuation) {
    CHECK_EQ(P("- a\nb"), "(doc (ul tight (li (p \"a\" (sb) \"b\"))))");
}

TEST(Lists, TaskListItems) {
    CHECK_EQ(P("- [ ] todo\n- [x] done"),
             "(doc (ul tight (li task=0 (p \"todo\")) (li task=1 (p \"done\"))))");
}

TEST(Lists, TaskListUppercaseX) {
    CHECK_EQ(P("- [X] done"), "(doc (ul tight (li task=1 (p \"done\"))))");
}

TEST(Lists, ListItemWithInlineFormatting) {
    CHECK_EQ(P("- **bold** item"),
             "(doc (ul tight (li (p (strong \"bold\") \" item\"))))");
}

TEST(Lists, ListFollowedByParagraph) {
    CHECK_EQ(P("- a\n\ntext"), "(doc (ul tight (li (p \"a\"))) (p \"text\"))");
}

TEST(Lists, ListItemContainingQuote) {
    CHECK_EQ(P("- > quoted"), "(doc (ul tight (li (quote (p \"quoted\")))))");
}

TEST(Lists, EmptyListItem) {
    CHECK_EQ(P("- a\n-\n- b"),
             "(doc (ul tight (li (p \"a\")) (li) (li (p \"b\"))))");
}

TEST(Lists, ParagraphThenListWithoutBlankLine) {
    CHECK_EQ(P("text\n- a"), "(doc (p \"text\") (ul tight (li (p \"a\"))))");
}
