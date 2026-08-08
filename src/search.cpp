#include "search.h"

#include <cwctype>

namespace view {
namespace {

wchar_t fold(wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); }

bool matchAt(const std::wstring& hay, size_t pos, const std::wstring& needle, bool matchCase) {
    if (pos + needle.size() > hay.size()) return false;
    for (size_t i = 0; i < needle.size(); ++i) {
        wchar_t a = hay[pos + i];
        wchar_t b = needle[i];
        if (!matchCase) {
            a = fold(a);
            b = fold(b);
        }
        if (a != b) return false;
    }
    return true;
}

} // namespace

std::vector<Match> findAll(const std::wstring& haystack, const std::wstring& needle,
                           bool matchCase) {
    std::vector<Match> matches;
    if (needle.empty() || needle.size() > haystack.size()) return matches;

    for (size_t i = 0; i + needle.size() <= haystack.size();) {
        if (matchAt(haystack, i, needle, matchCase)) {
            Match m;
            m.start = i;
            m.length = needle.size();
            matches.push_back(m);
            i += needle.size();
        } else {
            ++i;
        }
    }
    return matches;
}

int nextMatch(const std::vector<Match>& matches, size_t from, bool forward) {
    if (matches.empty()) return -1;

    if (forward) {
        for (size_t i = 0; i < matches.size(); ++i) {
            if (matches[i].start >= from) return static_cast<int>(i);
        }
        return 0; // wrap to the first match
    }

    for (size_t i = matches.size(); i-- > 0;) {
        if (matches[i].start < from) return static_cast<int>(i);
    }
    return static_cast<int>(matches.size() - 1); // wrap to the last match
}

} // namespace view
