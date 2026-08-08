// Markdown document model. Deliberately free of any Windows dependency so the
// parser and layout engine can be exercised by the console test runner.
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace md {

enum class NodeType {
    Document,
    FrontMatter,
    Paragraph,
    Heading,
    ThematicBreak,
    CodeBlock,
    HtmlBlock,
    BlockQuote,
    List,
    ListItem,
    Table,
    TableRow,
    TableCell,
    FootnoteDef,

    Text,
    SoftBreak,
    LineBreak,
    CodeSpan,
    Emph,
    Strong,
    Strike,
    Highlight,
    Underline,
    Superscript,
    Subscript,
    Link,
    Image,
    FootnoteRef,
};

enum class Align { None, Left, Center, Right };

struct Node;
using NodePtr = std::unique_ptr<Node>;

struct Node {
    explicit Node(NodeType t) : type(t) {}

    NodeType type;

    std::string text;   // Text / CodeSpan / CodeBlock / HtmlBlock payload
    std::string url;    // Link / Image destination
    std::string title;  // Link / Image title
    std::string info;   // Fenced code info string (language)
    std::string id;     // Footnote identifier

    int level = 0;          // Heading level 1..6
    bool ordered = false;   // List
    int start = 1;          // Ordered list start number
    bool tight = true;      // List spacing
    char delimiter = 0;     // List marker character
    int taskState = -1;     // -1 = not a task item, 0 = unchecked, 1 = checked
    bool headerRow = false; // TableRow
    std::vector<Align> aligns; // Table column alignment

    std::vector<NodePtr> children;

    Node* add(NodePtr child) {
        children.push_back(std::move(child));
        return children.back().get();
    }
};

inline NodePtr makeNode(NodeType t) { return std::make_unique<Node>(t); }

struct Document {
    NodePtr root;
    // Footnote identifiers in first-reference order; drives the rendered numbering.
    std::vector<std::string> footnoteOrder;
};

// Parses UTF-8 markdown text into a document tree.
Document parse(const std::string& utf8);

// Structural dump used by the tests. Stable, single line, S-expression shaped.
std::string dump(const Node& node);
std::string dump(const Document& doc);

} // namespace md
