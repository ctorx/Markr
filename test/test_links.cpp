// Link, image, autolink and reference-definition requirements.
#include "test_md.h"

TEST(Links, InlineLink) {
    CHECK_EQ(P("[text](http://a.com)"), "(doc (p (a http://a.com \"text\")))");
}

TEST(Links, InlineLinkWithTitle) {
    CHECK_EQ(P("[text](http://a.com \"Title\")"),
             "(doc (p (a http://a.com title=Title \"text\")))");
}

TEST(Links, LinkWithAngleBracketDestination) {
    CHECK_EQ(P("[t](<http://a.com/x y>)"), "(doc (p (a http://a.com/x%20y \"t\")))");
}

TEST(Links, LinkWithFormattedLabel) {
    CHECK_EQ(P("[**b**](u)"), "(doc (p (a u (strong \"b\"))))");
}

TEST(Links, EmptyDestination) {
    CHECK_EQ(P("[t]()"), "(doc (p (a  \"t\")))");
}

TEST(Links, ReferenceLinkFull) {
    CHECK_EQ(P("[text][id]\n\n[id]: http://a.com"),
             "(doc (p (a http://a.com \"text\")))");
}

TEST(Links, ReferenceLinkCollapsed) {
    CHECK_EQ(P("[id][]\n\n[id]: http://a.com"),
             "(doc (p (a http://a.com \"id\")))");
}

TEST(Links, ReferenceLinkShortcut) {
    CHECK_EQ(P("[id]\n\n[id]: http://a.com"), "(doc (p (a http://a.com \"id\")))");
}

TEST(Links, ReferenceLabelIsCaseInsensitive) {
    CHECK_EQ(P("[ID]\n\n[id]: http://a.com"), "(doc (p (a http://a.com \"ID\")))");
}

TEST(Links, ReferenceDefinitionWithTitle) {
    CHECK_EQ(P("[id]\n\n[id]: http://a.com \"T\""),
             "(doc (p (a http://a.com title=T \"id\")))");
}

TEST(Links, ReferenceDefinitionBeforeUse) {
    CHECK_EQ(P("[id]: http://a.com\n\n[id]"), "(doc (p (a http://a.com \"id\")))");
}

TEST(Links, UnresolvedReferenceStaysLiteral) {
    CHECK_EQ(P("[missing]"), "(doc (p \"[missing]\"))");
}

TEST(Links, AutolinkAngleBrackets) {
    CHECK_EQ(P("<http://a.com>"), "(doc (p (a http://a.com \"http://a.com\")))");
}

TEST(Links, AutolinkEmail) {
    CHECK_EQ(P("<a@b.com>"), "(doc (p (a mailto:a@b.com \"a@b.com\")))");
}

TEST(Links, BareUrlAutolink) {
    CHECK_EQ(P("see http://a.com now"),
             "(doc (p \"see \" (a http://a.com \"http://a.com\") \" now\"))");
}

TEST(Links, BareUrlTrailingPunctuationExcluded) {
    CHECK_EQ(P("see https://a.com."),
             "(doc (p \"see \" (a https://a.com \"https://a.com\") \".\"))");
}

TEST(Links, BareWwwAutolink) {
    CHECK_EQ(P("www.a.com"), "(doc (p (a http://www.a.com \"www.a.com\")))");
}

TEST(Links, Image) {
    CHECK_EQ(P("![alt](pic.png)"), "(doc (p (img pic.png \"alt\")))");
}

TEST(Links, ImageWithTitle) {
    CHECK_EQ(P("![alt](pic.png \"T\")"), "(doc (p (img pic.png title=T \"alt\")))");
}

TEST(Links, ImageReference) {
    CHECK_EQ(P("![alt][id]\n\n[id]: pic.png"), "(doc (p (img pic.png \"alt\")))");
}

TEST(Links, LinkInsideList) {
    CHECK_EQ(P("- [t](u)"), "(doc (ul tight (li (p (a u \"t\")))))");
}

TEST(Links, NestedBracketsInLabel) {
    CHECK_EQ(P("[a [b] c](u)"), "(doc (p (a u \"a [b] c\")))");
}

TEST(Links, LinkDestinationWithParens) {
    CHECK_EQ(P("[t](http://a.com/x_(y))"), "(doc (p (a http://a.com/x_(y) \"t\")))");
}

TEST(Links, CodeSpanInsideLink) {
    CHECK_EQ(P("[`c`](u)"), "(doc (p (a u (c \"c\"))))");
}
