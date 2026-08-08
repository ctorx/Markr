// Notepad-style find over the laid-out document text.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace view {

struct Match {
    size_t start = 0;
    size_t length = 0;
};

// All non-overlapping matches, in document order. Empty needle yields none.
std::vector<Match> findAll(const std::wstring& haystack, const std::wstring& needle,
                           bool matchCase);

// Index of the next match at or after `from` (forward) or strictly before `from`
// (backward), wrapping around like Notepad. Returns -1 when there are none.
int nextMatch(const std::vector<Match>& matches, size_t from, bool forward);

} // namespace view
