#include "md_export.h"

#include <algorithm>
#include <map>

namespace view {
namespace {

using md::Node;
using md::NodeType;

// --------------------------------------------------------------- selection

// A selected range of the rendered text, answering two questions about any node
// of the document: does it contribute to the selection, and which of its text
// falls inside it.
class Selection {
public:
    Selection(const md::Document& doc, const Layout& layout, size_t from, size_t to)
        : layout_(layout), from_(from), to_(to) {
        for (size_t i = 0; i < doc.footnoteOrder.size(); ++i) {
            footnotes_[doc.footnoteOrder[i]] = static_cast<int>(i) + 1;
        }
    }

    bool covers(const Node& node) const {
        const TextRange* r = range(node);
        if (!r) return false;
        // Rules and images render no text; they belong to the selection only when
        // it runs past them on both sides.
        if (r->start == r->end) return r->start > from_ && r->start < to_;
        return r->start < to_ && r->end > from_;
    }

    std::wstring clip(const Node& node) const {
        const TextRange* r = range(node);
        if (!r) return std::wstring();
        size_t a = std::max(r->start, from_);
        size_t b = std::min(std::min(r->end, to_), layout_.text.size());
        if (b <= a) return std::wstring();
        return layout_.text.substr(a, b - a);
    }

    int footnoteNumber(const std::string& id) const {
        auto it = footnotes_.find(id);
        return it == footnotes_.end() ? 0 : it->second;
    }

    // The same document with nothing clipped away. Used for the parts that have
    // to come along whole to stay meaningful, such as a table's header row.
    Selection everything() const {
        Selection copy(*this);
        copy.from_ = 0;
        copy.to_ = layout_.text.size();
        return copy;
    }

private:
    const TextRange* range(const Node& node) const {
        auto it = layout_.nodeRanges.find(&node);
        return it == layout_.nodeRanges.end() ? nullptr : &it->second;
    }

    const Layout& layout_;
    size_t from_;
    size_t to_;
    std::map<std::string, int> footnotes_;
};

// ----------------------------------------------------------------- helpers

bool isSpace(wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n'; }

std::wstring trimEnd(const std::wstring& s) {
    size_t end = s.size();
    while (end > 0 && isSpace(s[end - 1])) --end;
    return s.substr(0, end);
}

std::wstring trimBoth(const std::wstring& s) {
    size_t start = 0;
    while (start < s.size() && isSpace(s[start])) ++start;
    size_t end = s.size();
    while (end > start && isSpace(s[end - 1])) --end;
    return s.substr(start, end - start);
}

// Applies `first` to the first line and `rest` to every line after it, dropping
// trailing spaces so blank lines stay blank.
std::wstring prefixLines(const std::wstring& text, const std::wstring& first,
                         const std::wstring& rest) {
    std::wstring out;
    size_t start = 0;
    bool isFirst = true;
    while (start <= text.size()) {
        size_t nl = text.find(L'\n', start);
        size_t end = (nl == std::wstring::npos) ? text.size() : nl;
        std::wstring line = trimEnd(text.substr(start, end - start));
        std::wstring prefix = isFirst ? first : rest;
        out += line.empty() ? trimEnd(prefix) : prefix + line;
        if (nl == std::wstring::npos) break;
        out.push_back(L'\n');
        start = nl + 1;
        isFirst = false;
    }
    return out;
}

bool isWordChar(wchar_t c) {
    return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
           c >= 128;
}

// Backslash-escapes the characters that would otherwise be read back as markup.
// `atLineStart` reports whether the text lands at the start of an output line,
// where a marker character would start a whole new block.
void appendEscaped(std::wstring& out, const std::wstring& text, bool atLineStart) {
    bool lineStart = atLineStart;
    for (size_t i = 0; i < text.size(); ++i) {
        wchar_t c = text[i];
        if (lineStart) {
            if (c == L'#' || c == L'>' || c == L'-' || c == L'+' || c == L'=') {
                out.push_back(L'\\');
            } else if (c >= L'0' && c <= L'9') {
                size_t digits = i;
                while (digits < text.size() && text[digits] >= L'0' && text[digits] <= L'9') {
                    ++digits;
                }
                if (digits < text.size() && (text[digits] == L'.' || text[digits] == L')')) {
                    out.append(text, i, digits - i);
                    out.push_back(L'\\');
                    out.push_back(text[digits]);
                    i = digits;
                    lineStart = false;
                    continue;
                }
            }
        }
        switch (c) {
            case L'\\':
            case L'`':
            case L'*':
            case L'[':
            case L']':
            case L'<':
                out.push_back(L'\\');
                break;
            case L'_': {
                // Intra-word underscores never start emphasis, so leave snake_case
                // alone.
                bool before = i > 0 && isWordChar(text[i - 1]);
                bool after = i + 1 < text.size() && isWordChar(text[i + 1]);
                if (!before || !after) out.push_back(L'\\');
                break;
            }
            default:
                break;
        }
        out.push_back(c);
        lineStart = (c == L'\n');
    }
}

std::wstring escapeHtml(const std::wstring& text) {
    std::wstring out;
    out.reserve(text.size());
    for (wchar_t c : text) {
        switch (c) {
            case L'&': out += L"&amp;"; break;
            case L'<': out += L"&lt;"; break;
            case L'>': out += L"&gt;"; break;
            case L'"': out += L"&quot;"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

// Longest run of backticks in `text`, so a code span can be fenced clear of it.
size_t longestBacktickRun(const std::wstring& text) {
    size_t longest = 0, current = 0;
    for (wchar_t c : text) {
        current = (c == L'`') ? current + 1 : 0;
        longest = std::max(longest, current);
    }
    return longest;
}

std::wstring codeSpanMarkdown(const std::wstring& text) {
    if (text.empty()) return std::wstring();
    std::wstring fence(longestBacktickRun(text) + 1, L'`');
    std::wstring padding =
        (text.front() == L'`' || text.back() == L'`') ? std::wstring(L" ") : std::wstring();
    return fence + padding + text + padding + fence;
}

std::wstring linkDestination(const std::string& url) {
    std::wstring wide = toWide(url);
    bool needsBrackets = false;
    for (wchar_t c : wide) {
        if (c == L' ' || c == L'(' || c == L')' || c == L'<' || c == L'>') needsBrackets = true;
    }
    return needsBrackets ? L"<" + wide + L">" : wide;
}

std::wstring plainText(const Node& node) {
    std::wstring out;
    if (node.type == NodeType::Text || node.type == NodeType::CodeSpan) out += toWide(node.text);
    for (const md::NodePtr& child : node.children) out += plainText(*child);
    return out;
}

// ---------------------------------------------------------------- markdown

std::wstring inlineMarkdown(const Node& parent, const Selection& sel);
std::wstring blockMarkdown(const Node& node, const Selection& sel);
std::wstring blocksMarkdown(const Node& parent, const Selection& sel, bool inListItem);

std::wstring wrapMarkdown(const Node& node, const Selection& sel, const wchar_t* open,
                          const wchar_t* close) {
    std::wstring inner = inlineMarkdown(node, sel);
    if (inner.empty()) return inner;
    return open + inner + close;
}

std::wstring inlineMarkdown(const Node& parent, const Selection& sel) {
    std::wstring out;
    for (const md::NodePtr& childPtr : parent.children) {
        const Node& child = *childPtr;
        if (!sel.covers(child)) continue;
        bool atLineStart = out.empty() || out.back() == L'\n';

        switch (child.type) {
            case NodeType::Text:
                appendEscaped(out, sel.clip(child), atLineStart);
                break;
            case NodeType::SoftBreak:
                out.push_back(L'\n');
                break;
            case NodeType::LineBreak:
                out += L"\\\n";
                break;
            case NodeType::CodeSpan:
                out += codeSpanMarkdown(sel.clip(child));
                break;
            case NodeType::Emph:
                out += wrapMarkdown(child, sel, L"*", L"*");
                break;
            case NodeType::Strong:
                out += wrapMarkdown(child, sel, L"**", L"**");
                break;
            case NodeType::Strike:
                out += wrapMarkdown(child, sel, L"~~", L"~~");
                break;
            case NodeType::Highlight:
                out += wrapMarkdown(child, sel, L"==", L"==");
                break;
            case NodeType::Underline:
                out += wrapMarkdown(child, sel, L"<u>", L"</u>");
                break;
            case NodeType::Superscript:
                out += wrapMarkdown(child, sel, L"<sup>", L"</sup>");
                break;
            case NodeType::Subscript:
                out += wrapMarkdown(child, sel, L"<sub>", L"</sub>");
                break;
            case NodeType::Link: {
                std::wstring inner = inlineMarkdown(child, sel);
                if (inner.empty()) break;
                out += L"[" + inner + L"](" + linkDestination(child.url);
                if (!child.title.empty()) out += L" \"" + toWide(child.title) + L"\"";
                out += L")";
                break;
            }
            case NodeType::Image:
                out += L"![" + plainText(child) + L"](" + linkDestination(child.url) + L")";
                break;
            case NodeType::FootnoteRef:
                out += L"[^" + toWide(child.id) + L"]";
                break;
            default:
                out += inlineMarkdown(child, sel);
                break;
        }
    }
    return out;
}

std::wstring codeBlockMarkdown(const Node& node, const Selection& sel) {
    std::wstring code = trimEnd(sel.clip(node));
    if (code.empty()) return code;

    // Fence long enough to survive backticks in the code itself.
    size_t backticks = std::max<size_t>(3, longestBacktickRun(code) + 1);
    std::wstring fence(backticks, L'`');
    return fence + toWide(node.info) + L"\n" + code + L"\n" + fence;
}

std::wstring cellMarkdown(const Node& cell, const Selection& sel) {
    std::wstring text = inlineMarkdown(cell, sel);
    std::wstring out;
    for (wchar_t c : text) {
        if (c == L'\n') {
            if (!out.empty() && out.back() != L' ') out.push_back(L' ');
        } else if (c == L'|') {
            out += L"\\|";
        } else {
            out.push_back(c);
        }
    }
    return trimBoth(out);
}

std::wstring alignmentRow(const std::vector<md::Align>& aligns) {
    std::wstring row = L"|";
    for (md::Align align : aligns) {
        switch (align) {
            case md::Align::Left: row += L" :--- |"; break;
            case md::Align::Center: row += L" :---: |"; break;
            case md::Align::Right: row += L" ---: |"; break;
            default: row += L" --- |"; break;
        }
    }
    return row;
}

std::wstring tableMarkdown(const Node& node, const Selection& sel) {
    size_t columns = node.aligns.size();
    if (columns == 0) return std::wstring();

    const Selection whole = sel.everything();
    std::wstring out;
    bool wroteAlignment = false;
    for (const md::NodePtr& rowPtr : node.children) {
        const Node& row = *rowPtr;
        // The header row always comes along, in full: without it there is no pipe
        // table.
        bool selected = sel.covers(row);
        if (!row.headerRow && !selected) continue;
        const Selection& rowSelection = selected ? sel : whole;

        std::wstring line = L"|";
        for (size_t c = 0; c < columns; ++c) {
            const Node* cell = c < row.children.size() ? row.children[c].get() : nullptr;
            line += L" " + (cell ? cellMarkdown(*cell, rowSelection) : std::wstring()) + L" |";
        }
        if (!out.empty()) out.push_back(L'\n');
        out += line;
        if (!wroteAlignment) {
            out += L"\n" + alignmentRow(node.aligns);
            wroteAlignment = true;
        }
    }
    return out;
}

std::wstring listMarkdown(const Node& node, const Selection& sel) {
    std::wstring out;
    int number = node.start;
    for (const md::NodePtr& itemPtr : node.children) {
        const Node& item = *itemPtr;
        std::wstring marker =
            node.ordered ? std::to_wstring(number) + (node.delimiter == ')' ? L") " : L". ")
                         : std::wstring(L"- ");
        if (node.ordered) ++number;
        if (!sel.covers(item)) continue;

        if (item.taskState >= 0) marker += (item.taskState == 1) ? L"[x] " : L"[ ] ";
        std::wstring body = blocksMarkdown(item, sel, true);
        std::wstring indent(marker.size(), L' ');

        if (!out.empty()) out += node.tight ? L"\n" : L"\n\n";
        out += body.empty() ? trimEnd(marker) : prefixLines(body, marker, indent);
    }
    return out;
}

std::wstring blockMarkdown(const Node& node, const Selection& sel) {
    switch (node.type) {
        case NodeType::Paragraph:
            return inlineMarkdown(node, sel);
        case NodeType::Heading: {
            std::wstring inner = inlineMarkdown(node, sel);
            if (inner.empty()) return inner;
            int level = std::min(6, std::max(1, node.level));
            return std::wstring(static_cast<size_t>(level), L'#') + L" " + inner;
        }
        case NodeType::ThematicBreak:
            return L"---";
        case NodeType::CodeBlock:
            return codeBlockMarkdown(node, sel);
        case NodeType::FrontMatter: {
            std::wstring body = trimEnd(sel.clip(node));
            if (body.empty()) return body;
            return L"---\n" + body + L"\n---";
        }
        case NodeType::HtmlBlock:
            return trimEnd(sel.clip(node));
        case NodeType::BlockQuote:
            return prefixLines(blocksMarkdown(node, sel, false), L"> ", L"> ");
        case NodeType::List:
            return listMarkdown(node, sel);
        case NodeType::Table:
            return tableMarkdown(node, sel);
        case NodeType::FootnoteDef: {
            std::wstring body = blocksMarkdown(node, sel, true);
            if (body.empty()) return body;
            return L"[^" + toWide(node.id) + L"]: " + prefixLines(body, L"", L"    ");
        }
        default:
            return blocksMarkdown(node, sel, false);
    }
}

std::wstring blocksMarkdown(const Node& parent, const Selection& sel, bool inListItem) {
    std::wstring out;
    for (const md::NodePtr& childPtr : parent.children) {
        const Node& child = *childPtr;
        if (!sel.covers(child)) continue;
        std::wstring piece = blockMarkdown(child, sel);
        if (piece.empty()) continue;
        if (!out.empty()) {
            // A nested list binds to the item above it; everything else gets a
            // blank line so it stays a block of its own.
            out += (inListItem && child.type == NodeType::List) ? L"\n" : L"\n\n";
        }
        out += piece;
    }
    return out;
}

// -------------------------------------------------------------------- html

// Inline styles rather than a stylesheet: word processors and mail clients keep
// these when the fragment is pasted, and drop a <style> block.
const wchar_t* const kCodeStyle =
    L" style=\"font-family:Consolas,'Courier New',monospace;background:#f6f8fa;"
    L"padding:0.1em 0.3em;border-radius:3px\"";
const wchar_t* const kPreStyle =
    L" style=\"font-family:Consolas,'Courier New',monospace;background:#f6f8fa;"
    L"padding:10px;border-radius:6px;white-space:pre-wrap\"";
const wchar_t* const kQuoteStyle =
    L" style=\"margin:0 0 0 8px;padding-left:12px;border-left:4px solid #d0d7de;"
    L"color:#57606a\"";
const wchar_t* const kTableStyle = L" style=\"border-collapse:collapse\"";

std::wstring inlineHtml(const Node& parent, const Selection& sel);
std::wstring blockHtml(const Node& node, const Selection& sel);
std::wstring blocksHtml(const Node& parent, const Selection& sel);

std::wstring wrapHtml(const Node& node, const Selection& sel, const wchar_t* tag) {
    std::wstring inner = inlineHtml(node, sel);
    if (inner.empty()) return inner;
    return L"<" + std::wstring(tag) + L">" + inner + L"</" + tag + L">";
}

std::wstring inlineHtml(const Node& parent, const Selection& sel) {
    std::wstring out;
    for (const md::NodePtr& childPtr : parent.children) {
        const Node& child = *childPtr;
        if (!sel.covers(child)) continue;

        switch (child.type) {
            case NodeType::Text:
                out += escapeHtml(sel.clip(child));
                break;
            case NodeType::SoftBreak:
                out.push_back(L' ');
                break;
            case NodeType::LineBreak:
                out += L"<br>";
                break;
            case NodeType::CodeSpan: {
                std::wstring code = sel.clip(child);
                if (!code.empty()) {
                    out += L"<code" + std::wstring(kCodeStyle) + L">" + escapeHtml(code) +
                           L"</code>";
                }
                break;
            }
            case NodeType::Emph: out += wrapHtml(child, sel, L"em"); break;
            case NodeType::Strong: out += wrapHtml(child, sel, L"strong"); break;
            case NodeType::Strike: out += wrapHtml(child, sel, L"del"); break;
            case NodeType::Highlight: out += wrapHtml(child, sel, L"mark"); break;
            case NodeType::Underline: out += wrapHtml(child, sel, L"u"); break;
            case NodeType::Superscript: out += wrapHtml(child, sel, L"sup"); break;
            case NodeType::Subscript: out += wrapHtml(child, sel, L"sub"); break;
            case NodeType::Link: {
                std::wstring inner = inlineHtml(child, sel);
                if (inner.empty()) break;
                out += L"<a href=\"" + escapeHtml(toWide(child.url)) + L"\"";
                if (!child.title.empty()) {
                    out += L" title=\"" + escapeHtml(toWide(child.title)) + L"\"";
                }
                out += L">" + inner + L"</a>";
                break;
            }
            case NodeType::Image:
                out += L"<img src=\"" + escapeHtml(toWide(child.url)) + L"\" alt=\"" +
                       escapeHtml(plainText(child)) + L"\">";
                break;
            case NodeType::FootnoteRef: {
                int number = sel.footnoteNumber(child.id);
                out += L"<sup>" +
                       (number ? std::to_wstring(number) : escapeHtml(toWide(child.id))) +
                       L"</sup>";
                break;
            }
            default:
                out += inlineHtml(child, sel);
                break;
        }
    }
    return out;
}

std::wstring cellHtml(const Node* cell, const Selection& sel, md::Align align, bool header) {
    std::wstring style = L" style=\"border:1px solid #d0d7de;padding:6px 12px";
    switch (align) {
        case md::Align::Center: style += L";text-align:center"; break;
        case md::Align::Right: style += L";text-align:right"; break;
        default: break;
    }
    style += L"\"";

    std::wstring tag = header ? L"th" : L"td";
    std::wstring inner = cell ? inlineHtml(*cell, sel) : std::wstring();
    return L"<" + tag + style + L">" + inner + L"</" + tag + L">";
}

std::wstring tableHtml(const Node& node, const Selection& sel) {
    size_t columns = node.aligns.size();
    if (columns == 0) return std::wstring();

    const Selection whole = sel.everything();
    std::wstring rows;
    for (const md::NodePtr& rowPtr : node.children) {
        const Node& row = *rowPtr;
        bool selected = sel.covers(row);
        if (!row.headerRow && !selected) continue;
        const Selection& rowSelection = selected ? sel : whole;

        rows += L"<tr>";
        for (size_t c = 0; c < columns; ++c) {
            const Node* cell = c < row.children.size() ? row.children[c].get() : nullptr;
            rows += cellHtml(cell, rowSelection, node.aligns[c], row.headerRow);
        }
        rows += L"</tr>";
    }
    if (rows.empty()) return rows;
    return L"<table" + std::wstring(kTableStyle) + L">" + rows + L"</table>";
}

std::wstring listHtml(const Node& node, const Selection& sel) {
    std::wstring items;
    for (const md::NodePtr& itemPtr : node.children) {
        const Node& item = *itemPtr;
        if (!sel.covers(item)) continue;

        std::wstring body;
        for (const md::NodePtr& childPtr : item.children) {
            const Node& child = *childPtr;
            if (!sel.covers(child)) continue;
            // A tight list reads better without a paragraph box around each item.
            std::wstring piece = (node.tight && child.type == NodeType::Paragraph)
                                     ? inlineHtml(child, sel)
                                     : blockHtml(child, sel);
            body += piece;
        }
        if (item.taskState >= 0) {
            body = L"<input type=\"checkbox\" disabled" +
                   std::wstring(item.taskState == 1 ? L" checked" : L"") + L"> " + body;
        }
        items += L"<li>" + body + L"</li>";
    }
    if (items.empty()) return items;

    if (!node.ordered) return L"<ul>" + items + L"</ul>";
    std::wstring start = node.start != 1 ? L" start=\"" + std::to_wstring(node.start) + L"\""
                                         : std::wstring();
    return L"<ol" + start + L">" + items + L"</ol>";
}

std::wstring preformattedHtml(const std::wstring& text) {
    std::wstring body = trimEnd(text);
    if (body.empty()) return body;
    return L"<pre" + std::wstring(kPreStyle) + L"><code>" + escapeHtml(body) + L"</code></pre>";
}

std::wstring blockHtml(const Node& node, const Selection& sel) {
    switch (node.type) {
        case NodeType::Paragraph: {
            std::wstring inner = inlineHtml(node, sel);
            return inner.empty() ? inner : L"<p>" + inner + L"</p>";
        }
        case NodeType::Heading: {
            std::wstring inner = inlineHtml(node, sel);
            if (inner.empty()) return inner;
            std::wstring tag = L"h" + std::to_wstring(std::min(6, std::max(1, node.level)));
            return L"<" + tag + L">" + inner + L"</" + tag + L">";
        }
        case NodeType::ThematicBreak:
            return L"<hr>";
        case NodeType::CodeBlock:
        case NodeType::FrontMatter:
            return preformattedHtml(sel.clip(node));
        case NodeType::HtmlBlock: {
            // The viewer shows raw HTML blocks as their own text, so the copy does
            // the same rather than smuggling live markup into the clipboard.
            std::wstring body = trimEnd(sel.clip(node));
            return body.empty() ? body : L"<p>" + escapeHtml(body) + L"</p>";
        }
        case NodeType::BlockQuote: {
            std::wstring inner = blocksHtml(node, sel);
            if (inner.empty()) return inner;
            return L"<blockquote" + std::wstring(kQuoteStyle) + L">" + inner + L"</blockquote>";
        }
        case NodeType::List:
            return listHtml(node, sel);
        case NodeType::Table:
            return tableHtml(node, sel);
        case NodeType::FootnoteDef: {
            std::wstring inner = blocksHtml(node, sel);
            if (inner.empty()) return inner;
            int number = sel.footnoteNumber(node.id);
            std::wstring label =
                number ? std::to_wstring(number) : escapeHtml(toWide(node.id));
            return L"<div><sup>" + label + L"</sup> " + inner + L"</div>";
        }
        default:
            return blocksHtml(node, sel);
    }
}

std::wstring blocksHtml(const Node& parent, const Selection& sel) {
    std::wstring out;
    for (const md::NodePtr& childPtr : parent.children) {
        const Node& child = *childPtr;
        if (!sel.covers(child)) continue;
        std::wstring piece = blockHtml(child, sel);
        if (piece.empty()) continue;
        if (!out.empty()) out.push_back(L'\n');
        out += piece;
    }
    return out;
}

} // namespace

std::wstring selectionMarkdown(const md::Document& doc, const Layout& layout, size_t from,
                               size_t to) {
    if (from > to) std::swap(from, to);
    if (!doc.root || from >= to) return std::wstring();
    Selection selection(doc, layout, from, to);
    return trimBoth(blocksMarkdown(*doc.root, selection, false));
}

std::wstring selectionHtml(const md::Document& doc, const Layout& layout, size_t from,
                           size_t to) {
    if (from > to) std::swap(from, to);
    if (!doc.root || from >= to) return std::wstring();
    Selection selection(doc, layout, from, to);
    return blocksHtml(*doc.root, selection);
}

} // namespace view
