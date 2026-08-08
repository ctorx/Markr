// Find/next/previous behaviour, matching a basic Notepad-style search.
#include "../src/search.h"
#include "test_framework.h"

namespace {
const std::wstring kText = L"the cat sat on the mat, the CAT slept";
}

TEST(Search, FindsAllOccurrencesCaseInsensitively) {
    std::vector<view::Match> m = view::findAll(kText, L"cat", false);
    CHECK_EQ(m.size(), size_t(2));
    CHECK_EQ(m[0].start, size_t(4));
    CHECK_EQ(m[1].start, size_t(28));
    CHECK_EQ(m[0].length, size_t(3));
}

TEST(Search, MatchCaseRestrictsResults) {
    std::vector<view::Match> m = view::findAll(kText, L"CAT", true);
    CHECK_EQ(m.size(), size_t(1));
    CHECK_EQ(m[0].start, size_t(28));
}

TEST(Search, EmptyNeedleFindsNothing) {
    CHECK_EQ(view::findAll(kText, L"", false).size(), size_t(0));
}

TEST(Search, MissingNeedleFindsNothing) {
    CHECK_EQ(view::findAll(kText, L"dog", false).size(), size_t(0));
}

TEST(Search, MatchesDoNotOverlap) {
    std::vector<view::Match> m = view::findAll(L"aaaa", L"aa", false);
    CHECK_EQ(m.size(), size_t(2));
    CHECK_EQ(m[0].start, size_t(0));
    CHECK_EQ(m[1].start, size_t(2));
}

TEST(Search, NextMovesForwardFromCaret) {
    std::vector<view::Match> m = view::findAll(kText, L"the", false);
    CHECK_EQ(m.size(), size_t(3));
    CHECK_EQ(view::nextMatch(m, 0, true), 0);
    CHECK_EQ(view::nextMatch(m, 1, true), 1);
    CHECK_EQ(view::nextMatch(m, 16, true), 2);
}

TEST(Search, NextWrapsToStart) {
    std::vector<view::Match> m = view::findAll(kText, L"the", false);
    CHECK_EQ(view::nextMatch(m, 100, true), 0);
}

TEST(Search, PreviousMovesBackwards) {
    std::vector<view::Match> m = view::findAll(kText, L"the", false);
    CHECK_EQ(view::nextMatch(m, 100, false), 2);
    CHECK_EQ(view::nextMatch(m, 15, false), 0);
}

TEST(Search, PreviousWrapsToEnd) {
    std::vector<view::Match> m = view::findAll(kText, L"the", false);
    CHECK_EQ(view::nextMatch(m, 0, false), 2);
}

TEST(Search, NoMatchesReturnsMinusOne) {
    std::vector<view::Match> empty;
    CHECK_EQ(view::nextMatch(empty, 0, true), -1);
    CHECK_EQ(view::nextMatch(empty, 0, false), -1);
}

TEST(Search, SearchSpansWrappedLinesBecauseTextIsLogical) {
    // The layout stores logical text, so a phrase broken across visual lines
    // is still a single contiguous match.
    std::wstring text = L"alpha beta gamma\n";
    std::vector<view::Match> m = view::findAll(text, L"beta gamma", false);
    CHECK_EQ(m.size(), size_t(1));
}
