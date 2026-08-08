// Structural dump of a parsed document. Used by the test suite as the readable
// contract for every markdown feature.
#include "md_types.h"

namespace md {
namespace {

std::string quoteString(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c); break;
        }
    }
    out.push_back('"');
    return out;
}

char alignChar(Align a) {
    switch (a) {
        case Align::Left: return 'l';
        case Align::Center: return 'c';
        case Align::Right: return 'r';
        default: return 'n';
    }
}

void dumpChildren(const Node& node, std::string& out) {
    for (const NodePtr& child : node.children) {
        out.push_back(' ');
        out += dump(*child);
    }
}

} // namespace

std::string dump(const Node& node) {
    std::string out;

    switch (node.type) {
        case NodeType::Text:
            return quoteString(node.text);
        case NodeType::SoftBreak:
            return "(sb)";
        case NodeType::LineBreak:
            return "(br)";
        case NodeType::ThematicBreak:
            return "(hr)";
        case NodeType::CodeSpan:
            return "(c " + quoteString(node.text) + ")";
        case NodeType::CodeBlock:
            out = "(code";
            if (!node.info.empty()) out += " lang=" + node.info;
            out += " " + quoteString(node.text) + ")";
            return out;
        case NodeType::HtmlBlock:
            return "(html " + quoteString(node.text) + ")";
        case NodeType::FrontMatter:
            return "(frontmatter " + quoteString(node.text) + ")";
        case NodeType::FootnoteRef:
            return "(fnref " + node.id + ")";
        default:
            break;
    }

    switch (node.type) {
        case NodeType::Document: out = "(doc"; break;
        case NodeType::Paragraph: out = "(p"; break;
        case NodeType::Heading: out = "(h" + std::to_string(node.level); break;
        case NodeType::BlockQuote: out = "(quote"; break;
        case NodeType::List:
            out = node.ordered ? "(ol start=" + std::to_string(node.start) : "(ul";
            out += node.tight ? " tight" : " loose";
            break;
        case NodeType::ListItem:
            out = "(li";
            if (node.taskState >= 0) out += " task=" + std::to_string(node.taskState);
            break;
        case NodeType::Table: {
            out = "(table align=";
            for (Align a : node.aligns) out.push_back(alignChar(a));
            break;
        }
        case NodeType::TableRow: out = node.headerRow ? "(th" : "(tr"; break;
        case NodeType::TableCell: out = "(td"; break;
        case NodeType::FootnoteDef: out = "(fndef " + node.id; break;
        case NodeType::Emph: out = "(em"; break;
        case NodeType::Strong: out = "(strong"; break;
        case NodeType::Strike: out = "(del"; break;
        case NodeType::Highlight: out = "(mark"; break;
        case NodeType::Underline: out = "(u"; break;
        case NodeType::Superscript: out = "(sup"; break;
        case NodeType::Subscript: out = "(sub"; break;
        case NodeType::Link:
            out = "(a " + node.url;
            if (!node.title.empty()) out += " title=" + node.title;
            break;
        case NodeType::Image:
            out = "(img " + node.url;
            if (!node.title.empty()) out += " title=" + node.title;
            break;
        default: out = "(?"; break;
    }

    dumpChildren(node, out);
    out.push_back(')');
    return out;
}

std::string dump(const Document& doc) {
    return doc.root ? dump(*doc.root) : "(doc)";
}

} // namespace md
