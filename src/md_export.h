// Rebuilds markdown or HTML for a selected range of the rendered document.
// The selection is a range of Layout::text; Layout::nodeRanges maps it back onto
// the document tree, so what comes out keeps the structure the reader selected
// (headings stay headings, emphasis stays emphasis) clipped to their selection.
// Windows-free, so the test suite can exercise it.
#pragma once

#include "layout.h"
#include "md_types.h"

#include <string>

namespace view {

// Markdown source for [from, to). Regenerated from the parsed tree, so it is
// canonical markdown rather than a byte-for-byte slice of the original file.
std::wstring selectionMarkdown(const md::Document& doc, const Layout& layout, size_t from,
                               size_t to);

// HTML fragment for [from, to), for pasting into editors that accept rich text.
std::wstring selectionHtml(const md::Document& doc, const Layout& layout, size_t from,
                           size_t to);

} // namespace view
