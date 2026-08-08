// Markdown parser: block phase builds the tree, inline phase fills leaf blocks.
// Covers CommonMark plus the GitHub extensions (tables, task lists, strikethrough,
// bare autolinks, footnotes) and a small inline-HTML subset.
#include "md_types.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace md {
namespace {

// ---------------------------------------------------------------- utilities

bool isBlankLine(const std::string& s) {
    for (char c : s) {
        if (c != ' ' && c != '\t' && c != '\r') return false;
    }
    return true;
}

size_t leadingSpaces(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && s[i] == ' ') ++i;
    return i;
}

std::string stripIndent(const std::string& s, size_t n) {
    size_t i = 0;
    while (i < n && i < s.size() && s[i] == ' ') ++i;
    return s.substr(i);
}

std::string trimLeft(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

std::string trimRight(const std::string& s) {
    size_t e = s.size();
    while (e > 0 && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
    return s.substr(0, e);
}

std::string trim(const std::string& s) { return trimRight(trimLeft(s)); }

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool isAlnum(char c) { return isAlpha(c) || isDigit(c); }

char lowerChar(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }

std::string toLower(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = lowerChar(c);
    return out;
}

bool isAsciiPunct(char c) {
    static const char* p = "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
    return c != 0 && std::strchr(p, c) != nullptr;
}

// Treats any byte >= 0x80 as a word character so UTF-8 text is not mistaken for
// whitespace when deciding emphasis flanking.
bool isUnicodeWhitespace(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

void appendUtf8(std::string& out, unsigned int cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

struct NamedEntity { const char* name; unsigned int cp; };

const NamedEntity kEntities[] = {
    {"amp", 38}, {"lt", 60}, {"gt", 62}, {"quot", 34}, {"apos", 39},
    {"nbsp", 160}, {"copy", 169}, {"reg", 174}, {"trade", 8482},
    {"hellip", 8230}, {"mdash", 8212}, {"ndash", 8211},
    {"laquo", 171}, {"raquo", 187}, {"ldquo", 8220}, {"rdquo", 8221},
    {"lsquo", 8216}, {"rsquo", 8217}, {"times", 215}, {"divide", 247},
    {"deg", 176}, {"plusmn", 177}, {"frac12", 189}, {"frac14", 188},
    {"frac34", 190}, {"larr", 8592}, {"rarr", 8594}, {"uarr", 8593},
    {"darr", 8595}, {"harr", 8596}, {"bull", 8226}, {"middot", 183},
    {"dagger", 8224}, {"Dagger", 8225}, {"euro", 8364}, {"pound", 163},
    {"yen", 165}, {"cent", 162}, {"sect", 167}, {"para", 182},
    {"sup2", 178}, {"sup3", 179}, {"micro", 181}, {"permil", 8240},
    {"infin", 8734}, {"ne", 8800}, {"le", 8804}, {"ge", 8805},
    {"alpha", 945}, {"beta", 946}, {"gamma", 947}, {"delta", 948},
    {"epsilon", 949}, {"lambda", 955}, {"mu", 956}, {"pi", 960},
    {"sigma", 963}, {"tau", 964}, {"phi", 966}, {"omega", 969},
    {"Alpha", 913}, {"Beta", 914}, {"Gamma", 915}, {"Delta", 916},
    {"Lambda", 923}, {"Pi", 928}, {"Sigma", 931}, {"Omega", 937},
    {"check", 10003}, {"cross", 10007}, {"star", 9733}, {"hearts", 9829},
    {"emsp", 8195}, {"ensp", 8194}, {"thinsp", 8201}, {"shy", 173},
};

// Decodes an entity starting at src[i] ('&'). Returns true and advances i.
bool decodeEntity(const std::string& src, size_t& i, std::string& out) {
    size_t semi = src.find(';', i + 1);
    if (semi == std::string::npos || semi - i > 32 || semi == i + 1) return false;
    std::string body = src.substr(i + 1, semi - i - 1);

    if (body[0] == '#') {
        unsigned int cp = 0;
        if (body.size() > 2 && (body[1] == 'x' || body[1] == 'X')) {
            for (size_t k = 2; k < body.size(); ++k) {
                char c = lowerChar(body[k]);
                if (isDigit(c)) cp = cp * 16 + (c - '0');
                else if (c >= 'a' && c <= 'f') cp = cp * 16 + (c - 'a' + 10);
                else return false;
            }
        } else if (body.size() > 1) {
            for (size_t k = 1; k < body.size(); ++k) {
                if (!isDigit(body[k])) return false;
                cp = cp * 10 + (body[k] - '0');
            }
        } else {
            return false;
        }
        if (cp == 0 || cp > 0x10FFFF) cp = 0xFFFD;
        appendUtf8(out, cp);
        i = semi + 1;
        return true;
    }

    for (const NamedEntity& e : kEntities) {
        if (body == e.name) {
            appendUtf8(out, e.cp);
            i = semi + 1;
            return true;
        }
    }
    return false;
}

// ------------------------------------------------------- inline HTML subset

struct HtmlTagInfo { const char* name; NodeType type; };

const HtmlTagInfo kInlineTags[] = {
    {"b", NodeType::Strong},     {"strong", NodeType::Strong},
    {"i", NodeType::Emph},       {"em", NodeType::Emph},
    {"u", NodeType::Underline},  {"ins", NodeType::Underline},
    {"s", NodeType::Strike},     {"del", NodeType::Strike},
    {"strike", NodeType::Strike},{"mark", NodeType::Highlight},
    {"sub", NodeType::Subscript},{"sup", NodeType::Superscript},
};

const char* kBlockTags[] = {
    "address", "article", "aside", "blockquote", "center", "col", "colgroup",
    "dd", "details", "div", "dl", "dt", "fieldset", "figcaption", "figure",
    "footer", "form", "h1", "h2", "h3", "h4", "h5", "h6", "head", "header",
    "hr", "html", "iframe", "legend", "li", "main", "menu", "nav", "noscript",
    "ol", "optgroup", "option", "p", "param", "section", "source", "style",
    "summary", "table", "tbody", "td", "tfoot", "th", "thead", "title", "tr",
    "track", "ul", "script", "pre", "video", "audio", "picture",
};

bool isBlockTag(const std::string& name) {
    std::string lower = toLower(name);
    for (const char* t : kBlockTags) {
        if (lower == t) return true;
    }
    return false;
}

// Parses "<name ...>" or "</name>" at src[i]. Returns tag length or 0.
size_t parseHtmlTag(const std::string& src, size_t i, std::string& name, bool& closing,
                    bool& selfClosing) {
    if (i >= src.size() || src[i] != '<') return 0;
    size_t p = i + 1;
    closing = false;
    selfClosing = false;
    if (p < src.size() && src[p] == '/') {
        closing = true;
        ++p;
    }
    size_t nameStart = p;
    if (p >= src.size() || !isAlpha(src[p])) return 0;
    while (p < src.size() && (isAlnum(src[p]) || src[p] == '-')) ++p;
    name = src.substr(nameStart, p - nameStart);

    // Attributes: scan to '>' honouring quoted values.
    bool inSingle = false, inDouble = false;
    while (p < src.size()) {
        char c = src[p];
        if (inSingle) {
            if (c == '\'') inSingle = false;
        } else if (inDouble) {
            if (c == '"') inDouble = false;
        } else if (c == '\'') {
            inSingle = true;
        } else if (c == '"') {
            inDouble = true;
        } else if (c == '>') {
            if (p > i && src[p - 1] == '/') selfClosing = true;
            return p - i + 1;
        } else if (c == '<') {
            return 0;
        }
        ++p;
    }
    return 0;
}

// ------------------------------------------------------------- link helpers

std::string normalizeLabel(const std::string& raw) {
    std::string out;
    bool pendingSpace = false;
    for (char c : trim(raw)) {
        if (c == ' ' || c == '\t' || c == '\n') {
            pendingSpace = true;
        } else {
            if (pendingSpace && !out.empty()) out.push_back(' ');
            pendingSpace = false;
            out.push_back(lowerChar(c));
        }
    }
    return out;
}

std::string encodeSpaces(const std::string& url) {
    std::string out;
    for (char c : url) {
        if (c == ' ') out += "%20";
        else out.push_back(c);
    }
    return out;
}

// Removes backslash escapes from a URL or title.
std::string unescapeText(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size() && isAsciiPunct(s[i + 1])) {
            out.push_back(s[++i]);
        } else if (s[i] == '&') {
            size_t j = i;
            if (decodeEntity(s, j, out)) {
                i = j - 1;
                continue;
            }
            out.push_back(s[i]);
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

struct LinkRef {
    std::string url;
    std::string title;
};

// --------------------------------------------------------------- the parser

class Parser {
public:
    explicit Parser(const std::string& text) { splitLines(text); }

    Document run() {
        Document doc;
        doc.root = makeNode(NodeType::Document);

        size_t start = 0;
        parseFrontMatter(doc.root.get(), start);
        parseBlocks(lines_, start, lines_.size(), doc.root.get());

        inlinePass(doc.root.get());
        doc.footnoteOrder = footnoteOrder_;
        return doc;
    }

private:
    std::vector<std::string> lines_;
    std::map<std::string, LinkRef> refs_;
    std::vector<std::string> footnoteOrder_;

    void splitLines(const std::string& text) {
        std::string cur;
        size_t col = 0;
        auto push = [&]() {
            lines_.push_back(cur);
            cur.clear();
            col = 0;
        };
        for (size_t i = 0; i < text.size(); ++i) {
            char c = text[i];
            if (c == '\r') {
                if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
                push();
            } else if (c == '\n') {
                push();
            } else if (c == '\t') {
                size_t next = (col / 4 + 1) * 4;
                cur.append(next - col, ' ');
                col = next;
            } else {
                cur.push_back(c);
                ++col;
            }
        }
        if (!cur.empty()) lines_.push_back(cur);
    }

    // ------------------------------------------------------ block detection

    static bool isThematicBreak(const std::string& line) {
        if (leadingSpaces(line) >= 4) return false;
        std::string s = trim(line);
        if (s.size() < 3) return false;
        char c = s[0];
        if (c != '-' && c != '*' && c != '_') return false;
        int count = 0;
        for (char ch : s) {
            if (ch == c) ++count;
            else if (ch != ' ' && ch != '\t') return false;
        }
        return count >= 3;
    }

    static bool isAtxHeading(const std::string& line, int& level, std::string& content) {
        size_t ind = leadingSpaces(line);
        if (ind >= 4) return false;
        size_t i = ind;
        int hashes = 0;
        while (i < line.size() && line[i] == '#') {
            ++hashes;
            ++i;
        }
        if (hashes < 1 || hashes > 6) return false;
        if (i < line.size() && line[i] != ' ' && line[i] != '\t') return false;
        level = hashes;

        std::string rest = trim(line.substr(i));
        // Strip an optional closing run of '#'.
        size_t e = rest.size();
        while (e > 0 && rest[e - 1] == '#') --e;
        if (e == 0) {
            rest.clear();
        } else if (e < rest.size() && (rest[e - 1] == ' ' || rest[e - 1] == '\t')) {
            rest = trimRight(rest.substr(0, e));
        }
        content = rest;
        return true;
    }

    static bool isFence(const std::string& line, char& fenceChar, size_t& fenceLen,
                        std::string& info) {
        size_t ind = leadingSpaces(line);
        if (ind >= 4) return false;
        size_t i = ind;
        if (i >= line.size()) return false;
        char c = line[i];
        if (c != '`' && c != '~') return false;
        size_t n = 0;
        while (i < line.size() && line[i] == c) {
            ++n;
            ++i;
        }
        if (n < 3) return false;
        std::string rest = trim(line.substr(i));
        if (c == '`' && rest.find('`') != std::string::npos) return false;
        fenceChar = c;
        fenceLen = n;
        info = rest;
        return true;
    }

    static bool isClosingFence(const std::string& line, char fenceChar, size_t fenceLen) {
        size_t ind = leadingSpaces(line);
        if (ind >= 4) return false;
        size_t i = ind, n = 0;
        while (i < line.size() && line[i] == fenceChar) {
            ++n;
            ++i;
        }
        if (n < fenceLen) return false;
        return trim(line.substr(i)).empty();
    }

    static bool isBlockQuote(const std::string& line) {
        size_t ind = leadingSpaces(line);
        return ind < 4 && ind < line.size() && line[ind] == '>';
    }

    struct ListMarker {
        bool ordered = false;
        int number = 1;
        char delimiter = 0;
        size_t contentIndent = 0;
    };

    static bool parseListMarker(const std::string& line, ListMarker& m) {
        size_t ind = leadingSpaces(line);
        if (ind >= 4 || ind >= line.size()) return false;
        size_t i = ind;
        size_t markerLen = 0;

        char c = line[i];
        if (c == '-' || c == '+' || c == '*') {
            m.ordered = false;
            m.delimiter = c;
            markerLen = 1;
            ++i;
        } else if (isDigit(c)) {
            size_t d = i;
            while (d < line.size() && isDigit(line[d]) && d - i < 9) ++d;
            if (d >= line.size() || (line[d] != '.' && line[d] != ')')) return false;
            m.ordered = true;
            m.number = std::atoi(line.substr(i, d - i).c_str());
            m.delimiter = line[d];
            markerLen = d - i + 1;
            i = d + 1;
        } else {
            return false;
        }

        if (i >= line.size() || trim(line.substr(i)).empty()) {
            m.contentIndent = ind + markerLen + 1;
            return true;
        }
        if (line[i] != ' ') return false;
        size_t spaces = 0;
        while (i + spaces < line.size() && line[i + spaces] == ' ') ++spaces;
        m.contentIndent = (spaces >= 5) ? ind + markerLen + 1 : ind + markerLen + spaces;
        return true;
    }

    static bool isHtmlBlockStart(const std::string& line) {
        size_t ind = leadingSpaces(line);
        if (ind >= 4 || ind >= line.size() || line[ind] != '<') return false;
        if (line.compare(ind, 4, "<!--") == 0) return true;
        std::string name;
        bool closing = false, selfClosing = false;
        if (parseHtmlTag(line, ind, name, closing, selfClosing) == 0) return false;
        return isBlockTag(name);
    }

    static bool isSetextUnderline(const std::string& line, int& level) {
        size_t ind = leadingSpaces(line);
        if (ind >= 4) return false;
        std::string s = trim(line);
        if (s.empty()) return false;
        char c = s[0];
        if (c != '=' && c != '-') return false;
        for (char ch : s) {
            if (ch != c) return false;
        }
        level = (c == '=') ? 1 : 2;
        return true;
    }

    static bool isFootnoteDef(const std::string& line, std::string& id, std::string& rest) {
        size_t ind = leadingSpaces(line);
        if (ind >= 4 || ind + 1 >= line.size()) return false;
        if (line[ind] != '[' || line[ind + 1] != '^') return false;
        size_t close = line.find(']', ind + 2);
        if (close == std::string::npos || close + 1 >= line.size() || line[close + 1] != ':')
            return false;
        id = line.substr(ind + 2, close - ind - 2);
        if (id.empty()) return false;
        rest = trimLeft(line.substr(close + 2));
        return true;
    }

    // Cheap shape test used to stop lazy continuation before a definition line.
    static bool looksLikeLinkRefDefinition(const std::string& line) {
        size_t ind = leadingSpaces(line);
        if (ind >= 4 || ind >= line.size() || line[ind] != '[') return false;
        if (ind + 1 < line.size() && line[ind + 1] == '^') return false;
        size_t close = line.find("]:", ind + 1);
        return close != std::string::npos;
    }

    // Splits a table row on unescaped pipes.
    static std::vector<std::string> splitTableRow(const std::string& raw) {
        std::string s = trim(raw);
        if (!s.empty() && s.front() == '|') s.erase(s.begin());
        if (!s.empty() && s.back() == '|') {
            // Only strip a trailing pipe that is not escaped.
            if (s.size() < 2 || s[s.size() - 2] != '\\') s.pop_back();
        }
        std::vector<std::string> cells;
        std::string cur;
        bool inCode = false;
        for (size_t i = 0; i < s.size(); ++i) {
            char c = s[i];
            if (c == '\\' && i + 1 < s.size()) {
                cur.push_back(c);
                cur.push_back(s[++i]);
                continue;
            }
            if (c == '`') inCode = !inCode;
            if (c == '|' && !inCode) {
                cells.push_back(trim(cur));
                cur.clear();
            } else {
                cur.push_back(c);
            }
        }
        cells.push_back(trim(cur));
        return cells;
    }

    static bool isTableDelimiterRow(const std::string& line, std::vector<Align>& aligns) {
        if (line.find('|') == std::string::npos && line.find('-') == std::string::npos)
            return false;
        std::vector<std::string> cells = splitTableRow(line);
        if (cells.empty()) return false;
        aligns.clear();
        for (const std::string& cell : cells) {
            std::string c = trim(cell);
            if (c.size() < 1) return false;
            bool left = c.front() == ':';
            bool right = c.back() == ':';
            size_t b = left ? 1 : 0;
            size_t e = c.size() - (right ? 1 : 0);
            if (e <= b) return false;
            for (size_t i = b; i < e; ++i) {
                if (c[i] != '-') return false;
            }
            if (left && right) aligns.push_back(Align::Center);
            else if (left) aligns.push_back(Align::Left);
            else if (right) aligns.push_back(Align::Right);
            else aligns.push_back(Align::None);
        }
        return true;
    }

    bool isTableStart(const std::vector<std::string>& L, size_t i, size_t end) const {
        if (i + 1 >= end) return false;
        if (L[i].find('|') == std::string::npos) return false;
        if (leadingSpaces(L[i]) >= 4) return false;
        std::vector<Align> aligns;
        if (!isTableDelimiterRow(L[i + 1], aligns)) return false;
        return splitTableRow(L[i]).size() == aligns.size();
    }

    bool startsNewBlock(const std::vector<std::string>& L, size_t i, size_t end) const {
        const std::string& line = L[i];
        if (isBlankLine(line)) return true;
        if (isThematicBreak(line)) return true;
        int lvl;
        std::string content;
        if (isAtxHeading(line, lvl, content)) return true;
        char fc;
        size_t fl;
        std::string info;
        if (isFence(line, fc, fl, info)) return true;
        if (isBlockQuote(line)) return true;
        ListMarker m;
        if (parseListMarker(line, m)) return true;
        if (isHtmlBlockStart(line)) return true;
        if (isTableStart(L, i, end)) return true;
        return false;
    }

    // ---------------------------------------------------------- block phase

    void parseFrontMatter(Node* parent, size_t& start) {
        if (lines_.empty()) return;
        if (trimRight(lines_[0]) != "---") return;
        for (size_t i = 1; i < lines_.size(); ++i) {
            std::string t = trimRight(lines_[i]);
            if (t == "---" || t == "...") {
                Node* fm = parent->add(makeNode(NodeType::FrontMatter));
                std::string content;
                for (size_t k = 1; k < i; ++k) {
                    if (k > 1) content.push_back('\n');
                    content += trimRight(lines_[k]);
                }
                fm->text = content;
                start = i + 1;
                return;
            }
        }
    }

    void parseBlocks(const std::vector<std::string>& L, size_t i, size_t end, Node* parent) {
        while (i < end) {
            const std::string& line = L[i];

            if (isBlankLine(line)) {
                ++i;
                continue;
            }

            size_t ind = leadingSpaces(line);

            if (ind >= 4) {
                parseIndentedCode(L, i, end, parent);
                continue;
            }
            if (isThematicBreak(line)) {
                parent->add(makeNode(NodeType::ThematicBreak));
                ++i;
                continue;
            }
            int level = 0;
            std::string content;
            if (isAtxHeading(line, level, content)) {
                Node* h = parent->add(makeNode(NodeType::Heading));
                h->level = level;
                h->text = content;
                ++i;
                continue;
            }
            char fenceChar = 0;
            size_t fenceLen = 0;
            std::string info;
            if (isFence(line, fenceChar, fenceLen, info)) {
                parseFencedCode(L, i, end, parent, fenceChar, fenceLen, info, ind);
                continue;
            }
            if (isBlockQuote(line)) {
                parseBlockQuote(L, i, end, parent);
                continue;
            }
            ListMarker marker;
            if (parseListMarker(line, marker)) {
                parseList(L, i, end, parent, marker);
                continue;
            }
            std::string fnId, fnRest;
            if (isFootnoteDef(line, fnId, fnRest)) {
                parseFootnoteDef(L, i, end, parent, fnId, fnRest);
                continue;
            }
            if (parseLinkRefDefinition(L, i, end)) continue;
            if (isHtmlBlockStart(line)) {
                parseHtmlBlock(L, i, end, parent);
                continue;
            }
            if (isTableStart(L, i, end)) {
                parseTable(L, i, end, parent);
                continue;
            }
            parseParagraph(L, i, end, parent);
        }
    }

    void parseIndentedCode(const std::vector<std::string>& L, size_t& i, size_t end,
                           Node* parent) {
        std::vector<std::string> content;
        size_t lastNonBlank = i;
        size_t j = i;
        while (j < end) {
            if (isBlankLine(L[j])) {
                content.push_back("");
                ++j;
                continue;
            }
            if (leadingSpaces(L[j]) < 4) break;
            content.push_back(stripIndent(L[j], 4));
            lastNonBlank = j;
            ++j;
        }
        content.resize(lastNonBlank - i + 1);

        Node* code = parent->add(makeNode(NodeType::CodeBlock));
        std::string text;
        for (size_t k = 0; k < content.size(); ++k) {
            if (k) text.push_back('\n');
            text += trimRight(content[k]);
        }
        code->text = text;
        i = lastNonBlank + 1;
    }

    void parseFencedCode(const std::vector<std::string>& L, size_t& i, size_t end,
                         Node* parent, char fenceChar, size_t fenceLen,
                         const std::string& info, size_t indent) {
        Node* code = parent->add(makeNode(NodeType::CodeBlock));
        code->info = unescapeText(info);

        std::string text;
        bool first = true;
        size_t j = i + 1;
        for (; j < end; ++j) {
            if (isClosingFence(L[j], fenceChar, fenceLen)) {
                ++j;
                break;
            }
            if (!first) text.push_back('\n');
            first = false;
            text += trimRight(stripIndent(L[j], indent));
        }
        code->text = text;
        i = j;
    }

    void parseBlockQuote(const std::vector<std::string>& L, size_t& i, size_t end,
                         Node* parent) {
        std::vector<std::string> inner;
        size_t j = i;
        while (j < end) {
            if (isBlockQuote(L[j])) {
                size_t ind = leadingSpaces(L[j]);
                std::string rest = L[j].substr(ind + 1);
                if (!rest.empty() && rest[0] == ' ') rest.erase(rest.begin());
                inner.push_back(rest);
                ++j;
                continue;
            }
            if (isBlankLine(L[j])) break;
            // Lazy continuation of a paragraph inside the quote.
            if (startsNewBlock(L, j, end)) break;
            if (inner.empty() || isBlankLine(inner.back())) break;
            inner.push_back(trimLeft(L[j]));
            ++j;
        }

        Node* quote = parent->add(makeNode(NodeType::BlockQuote));
        parseBlocks(inner, 0, inner.size(), quote);
        i = j;
    }

    void parseList(const std::vector<std::string>& L, size_t& i, size_t end, Node* parent,
                   const ListMarker& first) {
        Node* list = parent->add(makeNode(NodeType::List));
        list->ordered = first.ordered;
        list->start = first.number;
        list->delimiter = first.delimiter;
        bool loose = false;

        size_t j = i;
        while (j < end) {
            ListMarker m;
            if (isThematicBreak(L[j])) break;
            if (!parseListMarker(L[j], m)) break;
            if (m.ordered != first.ordered || m.delimiter != first.delimiter) break;

            std::vector<std::string> itemLines;
            // The first line still carries the marker, so cut by column, not by
            // leading whitespace.
            itemLines.push_back(m.contentIndent < L[j].size() ? L[j].substr(m.contentIndent)
                                                              : std::string());
            size_t k = j + 1;
            size_t pendingBlanks = 0;
            bool internalBlank = false;
            while (k < end) {
                if (isBlankLine(L[k])) {
                    ++pendingBlanks;
                    ++k;
                    continue;
                }
                size_t ind = leadingSpaces(L[k]);
                if (ind >= m.contentIndent) {
                    if (pendingBlanks) {
                        internalBlank = true;
                        for (size_t b = 0; b < pendingBlanks; ++b) itemLines.push_back("");
                        pendingBlanks = 0;
                    }
                    itemLines.push_back(stripIndent(L[k], m.contentIndent));
                    ++k;
                    continue;
                }
                if (pendingBlanks == 0 && !startsNewBlock(L, k, end)) {
                    itemLines.push_back(trimLeft(L[k]));
                    ++k;
                    continue;
                }
                break;
            }
            if (internalBlank) loose = true;
            if (pendingBlanks > 0 && k < end) {
                ListMarker next;
                if (parseListMarker(L[k], next) && next.ordered == first.ordered &&
                    next.delimiter == first.delimiter && !isThematicBreak(L[k])) {
                    loose = true;
                }
            }

            Node* item = list->add(makeNode(NodeType::ListItem));
            extractTaskMarker(itemLines, item);
            parseBlocks(itemLines, 0, itemLines.size(), item);
            j = k;
        }

        list->tight = !loose;
        i = j;
    }

    static void extractTaskMarker(std::vector<std::string>& itemLines, Node* item) {
        if (itemLines.empty()) return;
        std::string& first = itemLines[0];
        std::string t = trimLeft(first);
        if (t.size() < 3 || t[0] != '[' || t[2] != ']') return;
        char c = t[1];
        if (c == ' ') item->taskState = 0;
        else if (c == 'x' || c == 'X') item->taskState = 1;
        else return;
        std::string rest = t.substr(3);
        if (!rest.empty() && rest[0] == ' ') rest.erase(rest.begin());
        first = rest;
    }

    void parseFootnoteDef(const std::vector<std::string>& L, size_t& i, size_t end,
                          Node* parent, const std::string& id, const std::string& rest) {
        std::vector<std::string> content;
        content.push_back(rest);
        size_t j = i + 1;
        size_t pendingBlanks = 0;
        while (j < end) {
            if (isBlankLine(L[j])) {
                ++pendingBlanks;
                ++j;
                continue;
            }
            size_t ind = leadingSpaces(L[j]);
            if (ind >= 4) {
                for (size_t b = 0; b < pendingBlanks; ++b) content.push_back("");
                pendingBlanks = 0;
                content.push_back(stripIndent(L[j], 4));
                ++j;
                continue;
            }
            // A following definition of any kind ends this one.
            std::string otherId, otherRest;
            bool nextIsDefinition = isFootnoteDef(L[j], otherId, otherRest) ||
                                    looksLikeLinkRefDefinition(L[j]);
            if (pendingBlanks == 0 && !nextIsDefinition && !startsNewBlock(L, j, end)) {
                content.push_back(trimLeft(L[j]));
                ++j;
                continue;
            }
            break;
        }

        Node* def = parent->add(makeNode(NodeType::FootnoteDef));
        def->id = id;
        parseBlocks(content, 0, content.size(), def);
        i = j;
    }

    bool parseLinkRefDefinition(const std::vector<std::string>& L, size_t& i, size_t end) {
        const std::string& line = L[i];
        size_t ind = leadingSpaces(line);
        if (ind >= 4 || ind >= line.size() || line[ind] != '[') return false;
        if (ind + 1 < line.size() && line[ind + 1] == '^') return false;

        size_t p = ind + 1;
        std::string label;
        bool closed = false;
        while (p < line.size()) {
            if (line[p] == '\\' && p + 1 < line.size()) {
                label.push_back(line[p]);
                label.push_back(line[p + 1]);
                p += 2;
                continue;
            }
            if (line[p] == ']') {
                closed = true;
                ++p;
                break;
            }
            label.push_back(line[p]);
            ++p;
        }
        if (!closed || p >= line.size() || line[p] != ':' || label.empty()) return false;
        ++p;

        std::string rest = trim(line.substr(p));
        if (rest.empty()) return false;

        std::string url, title;
        size_t q = 0;
        if (rest[0] == '<') {
            size_t close = rest.find('>');
            if (close == std::string::npos) return false;
            url = rest.substr(1, close - 1);
            q = close + 1;
        } else {
            while (q < rest.size() && rest[q] != ' ' && rest[q] != '\t') ++q;
            url = rest.substr(0, q);
        }

        std::string after = trim(rest.substr(q));
        size_t consumed = 1;
        if (after.empty() && i + 1 < end) {
            std::string next = trim(L[i + 1]);
            if (next.size() >= 2 &&
                ((next.front() == '"' && next.back() == '"') ||
                 (next.front() == '\'' && next.back() == '\'') ||
                 (next.front() == '(' && next.back() == ')'))) {
                title = next.substr(1, next.size() - 2);
                consumed = 2;
            }
        } else if (!after.empty()) {
            if (after.size() >= 2 &&
                ((after.front() == '"' && after.back() == '"') ||
                 (after.front() == '\'' && after.back() == '\'') ||
                 (after.front() == '(' && after.back() == ')'))) {
                title = after.substr(1, after.size() - 2);
            } else {
                return false; // Trailing junk: not a definition.
            }
        }

        std::string key = normalizeLabel(unescapeText(label));
        if (refs_.find(key) == refs_.end()) {
            LinkRef ref;
            ref.url = encodeSpaces(unescapeText(url));
            ref.title = unescapeText(title);
            refs_[key] = ref;
        }
        i += consumed;
        return true;
    }

    void parseHtmlBlock(const std::vector<std::string>& L, size_t& i, size_t end,
                        Node* parent) {
        std::string raw;
        size_t j = i;
        while (j < end && !isBlankLine(L[j])) {
            if (j > i) raw.push_back('\n');
            raw += L[j];
            ++j;
        }
        Node* html = parent->add(makeNode(NodeType::HtmlBlock));
        html->text = stripHtmlTags(raw);
        i = j;
    }

    static std::string stripHtmlTags(const std::string& raw) {
        std::string plain;
        for (size_t i = 0; i < raw.size();) {
            if (raw.compare(i, 4, "<!--") == 0) {
                size_t close = raw.find("-->", i + 4);
                i = (close == std::string::npos) ? raw.size() : close + 3;
                continue;
            }
            if (raw[i] == '<') {
                std::string name;
                bool closing = false, selfClosing = false;
                size_t len = parseHtmlTag(raw, i, name, closing, selfClosing);
                if (len) {
                    std::string lower = toLower(name);
                    if (lower == "br" || lower == "p" || lower == "div" || lower == "tr" ||
                        lower == "li") {
                        plain.push_back('\n');
                    }
                    i += len;
                    continue;
                }
            }
            if (raw[i] == '&') {
                size_t j = i;
                if (decodeEntity(raw, j, plain)) {
                    i = j;
                    continue;
                }
            }
            plain.push_back(raw[i]);
            ++i;
        }

        // Drop blank lines and surrounding whitespace introduced by the tags.
        std::vector<std::string> out;
        std::string cur;
        for (char c : plain) {
            if (c == '\n') {
                if (!trim(cur).empty()) out.push_back(trim(cur));
                cur.clear();
            } else {
                cur.push_back(c);
            }
        }
        if (!trim(cur).empty()) out.push_back(trim(cur));

        std::string result;
        for (size_t i = 0; i < out.size(); ++i) {
            if (i) result.push_back('\n');
            result += out[i];
        }
        return result;
    }

    void parseTable(const std::vector<std::string>& L, size_t& i, size_t end, Node* parent) {
        std::vector<Align> aligns;
        isTableDelimiterRow(L[i + 1], aligns);
        size_t columns = aligns.size();

        Node* table = parent->add(makeNode(NodeType::Table));
        table->aligns = aligns;

        auto addRow = [&](const std::string& raw, bool header) {
            Node* row = table->add(makeNode(NodeType::TableRow));
            row->headerRow = header;
            std::vector<std::string> cells = splitTableRow(raw);
            for (size_t c = 0; c < columns; ++c) {
                Node* cell = row->add(makeNode(NodeType::TableCell));
                if (c < cells.size()) cell->text = cells[c];
            }
        };

        addRow(L[i], true);
        size_t j = i + 2;
        while (j < end) {
            if (isBlankLine(L[j])) break;
            if (L[j].find('|') == std::string::npos && startsNewBlock(L, j, end)) break;
            if (isThematicBreak(L[j])) break;
            addRow(L[j], false);
            ++j;
        }
        i = j;
    }

    void parseParagraph(const std::vector<std::string>& L, size_t& i, size_t end,
                        Node* parent) {
        // Trailing spaces are kept: two of them before a newline is a hard break.
        std::vector<std::string> collected;
        collected.push_back(trimLeft(L[i]));
        size_t j = i + 1;
        int setextLevel = 0;

        while (j < end) {
            if (isBlankLine(L[j])) break;
            int level = 0;
            if (isSetextUnderline(L[j], level)) {
                setextLevel = level;
                ++j;
                break;
            }
            if (startsNewBlock(L, j, end)) break;
            collected.push_back(trimLeft(L[j]));
            ++j;
        }

        std::string text;
        for (size_t k = 0; k < collected.size(); ++k) {
            if (k) text.push_back('\n');
            text += collected[k];
        }
        text = trimRight(text);

        if (setextLevel) {
            Node* h = parent->add(makeNode(NodeType::Heading));
            h->level = setextLevel;
            h->text = text;
        } else {
            Node* p = parent->add(makeNode(NodeType::Paragraph));
            p->text = text;
        }
        i = j;
    }

    // --------------------------------------------------------- inline phase

    void inlinePass(Node* node) {
        if (node->type == NodeType::Paragraph || node->type == NodeType::Heading ||
            node->type == NodeType::TableCell) {
            std::string raw = node->text;
            node->text.clear();
            parseInlines(raw, node);
            return;
        }
        for (NodePtr& child : node->children) inlinePass(child.get());
    }

    struct Delimiter {
        size_t index = 0;   // node slot holding the literal delimiter run
        char ch = 0;
        int count = 0;
        bool canOpen = false;
        bool canClose = false;
        bool active = true;
    };

    struct BracketOpener {
        size_t index = 0;   // node slot holding the literal '[' / '!['
        size_t delimBottom = 0;
        bool image = false;
        bool active = true;
    };

    struct Frame {
        std::vector<NodePtr> nodes;
        std::vector<Delimiter> delims;
        std::vector<BracketOpener> brackets;
        std::string tag;
        NodeType type = NodeType::Text;
        bool isTagFrame = false;
    };

    std::vector<Frame> frames_;
    std::string pending_;

    Frame& top() { return frames_.back(); }

    void flushPending() {
        if (pending_.empty()) return;
        NodePtr t = makeNode(NodeType::Text);
        t->text = pending_;
        pending_.clear();
        top().nodes.push_back(std::move(t));
    }

    size_t pushNode(NodePtr n) {
        flushPending();
        top().nodes.push_back(std::move(n));
        return top().nodes.size() - 1;
    }

    void parseInlines(const std::string& src, Node* parent) {
        frames_.clear();
        pending_.clear();
        frames_.emplace_back();

        scanInlines(src);

        while (frames_.size() > 1) closeFrame(true);
        flushPending();
        processEmphasis(top(), 0);

        std::vector<NodePtr> result;
        finishNodes(top().nodes, result);
        for (NodePtr& n : result) parent->children.push_back(std::move(n));
        frames_.clear();
    }

    // Compacts holes and merges adjacent text nodes.
    static void finishNodes(std::vector<NodePtr>& nodes, std::vector<NodePtr>& out) {
        for (NodePtr& n : nodes) {
            if (!n) continue;
            if (n->type == NodeType::Text && !out.empty() &&
                out.back()->type == NodeType::Text) {
                out.back()->text += n->text;
                continue;
            }
            if (n->type == NodeType::Text && n->text.empty()) continue;
            out.push_back(std::move(n));
        }
    }

    void closeFrame(bool flatten) {
        flushPending();
        Frame frame = std::move(frames_.back());
        frames_.pop_back();
        processEmphasis(frame, 0);

        std::vector<NodePtr> children;
        finishNodes(frame.nodes, children);

        if (flatten) {
            for (NodePtr& c : children) {
                if (c->type == NodeType::Text) {
                    pending_ += c->text;
                } else {
                    flushPending();
                    top().nodes.push_back(std::move(c));
                }
            }
            return;
        }

        NodePtr node = makeNode(frame.type);
        for (NodePtr& c : children) node->children.push_back(std::move(c));
        pushNode(std::move(node));
    }

    void scanInlines(const std::string& src) {
        size_t i = 0;
        while (i < src.size()) {
            char c = src[i];

            if (c == '\\') {
                if (i + 1 < src.size() && src[i + 1] == '\n') {
                    pushNode(makeNode(NodeType::LineBreak));
                    i += 2;
                    continue;
                }
                if (i + 1 < src.size() && isAsciiPunct(src[i + 1])) {
                    pending_.push_back(src[i + 1]);
                    i += 2;
                    continue;
                }
                pending_.push_back(c);
                ++i;
                continue;
            }

            if (c == '\n') {
                size_t trailing = 0;
                while (trailing < pending_.size() &&
                       pending_[pending_.size() - 1 - trailing] == ' ')
                    ++trailing;
                if (trailing >= 2) {
                    pending_.erase(pending_.size() - trailing);
                    pushNode(makeNode(NodeType::LineBreak));
                } else {
                    if (trailing) pending_.erase(pending_.size() - trailing);
                    pushNode(makeNode(NodeType::SoftBreak));
                }
                ++i;
                while (i < src.size() && src[i] == ' ') ++i;
                continue;
            }

            if (c == '&') {
                size_t j = i;
                std::string decoded;
                if (decodeEntity(src, j, decoded)) {
                    pending_ += decoded;
                    i = j;
                    continue;
                }
                pending_.push_back(c);
                ++i;
                continue;
            }

            if (c == '`') {
                if (scanCodeSpan(src, i)) continue;
                pending_.push_back(c);
                ++i;
                continue;
            }

            if (c == '<') {
                if (scanAutolink(src, i)) continue;
                if (scanInlineHtml(src, i)) continue;
                pending_.push_back(c);
                ++i;
                continue;
            }

            if (c == '!' && i + 1 < src.size() && src[i + 1] == '[') {
                NodePtr t = makeNode(NodeType::Text);
                t->text = "![";
                size_t idx = pushNode(std::move(t));
                BracketOpener b;
                b.index = idx;
                b.image = true;
                b.delimBottom = top().delims.size();
                top().brackets.push_back(b);
                i += 2;
                continue;
            }

            if (c == '[') {
                if (scanFootnoteRef(src, i)) continue;
                NodePtr t = makeNode(NodeType::Text);
                t->text = "[";
                size_t idx = pushNode(std::move(t));
                BracketOpener b;
                b.index = idx;
                b.image = false;
                b.delimBottom = top().delims.size();
                top().brackets.push_back(b);
                ++i;
                continue;
            }

            if (c == ']') {
                if (closeBracket(src, i)) continue;
                pending_.push_back(c);
                ++i;
                continue;
            }

            if (c == '*' || c == '_' || c == '~' || c == '=') {
                if (scanDelimiterRun(src, i)) continue;
                pending_.push_back(c);
                ++i;
                continue;
            }

            if ((c == 'h' || c == 'w' || c == 'm' || c == 'f') && scanBareAutolink(src, i)) {
                continue;
            }

            pending_.push_back(c);
            ++i;
        }
    }

    bool scanCodeSpan(const std::string& src, size_t& i) {
        size_t start = i;
        size_t n = 0;
        while (i + n < src.size() && src[i + n] == '`') ++n;

        size_t p = i + n;
        while (p < src.size()) {
            if (src[p] == '`') {
                size_t m = 0;
                while (p + m < src.size() && src[p + m] == '`') ++m;
                if (m == n) {
                    std::string content = src.substr(i + n, p - (i + n));
                    for (char& ch : content) {
                        if (ch == '\n') ch = ' ';
                    }
                    if (content.size() >= 2 && content.front() == ' ' &&
                        content.back() == ' ' &&
                        content.find_first_not_of(' ') != std::string::npos) {
                        content = content.substr(1, content.size() - 2);
                    }
                    NodePtr code = makeNode(NodeType::CodeSpan);
                    code->text = content;
                    pushNode(std::move(code));
                    i = p + m;
                    return true;
                }
                p += m;
                continue;
            }
            ++p;
        }
        i = start;
        return false;
    }

    bool scanAutolink(const std::string& src, size_t& i) {
        size_t close = src.find('>', i + 1);
        if (close == std::string::npos) return false;
        std::string body = src.substr(i + 1, close - i - 1);
        if (body.empty() || body.find(' ') != std::string::npos) return false;

        bool isUrl = false;
        size_t colon = body.find(':');
        if (colon != std::string::npos && colon > 1) {
            isUrl = true;
            for (size_t k = 0; k < colon; ++k) {
                if (!isAlnum(body[k]) && body[k] != '+' && body[k] != '-' && body[k] != '.')
                    isUrl = false;
            }
        }
        bool isEmail = false;
        size_t at = body.find('@');
        if (!isUrl && at != std::string::npos && at > 0 && at + 1 < body.size() &&
            body.find('.', at) != std::string::npos) {
            isEmail = true;
        }
        if (!isUrl && !isEmail) return false;

        NodePtr link = makeNode(NodeType::Link);
        link->url = isEmail ? "mailto:" + body : body;
        NodePtr text = makeNode(NodeType::Text);
        text->text = body;
        link->children.push_back(std::move(text));
        pushNode(std::move(link));
        i = close + 1;
        return true;
    }

    bool scanBareAutolink(const std::string& src, size_t& i) {
        // Only start at a word boundary.
        if (i > 0) {
            char prev = src[i - 1];
            if (isAlnum(prev) || prev == '/' || prev == '@' || prev == '.') return false;
        }
        static const char* prefixes[] = {"https://", "http://", "www.", "mailto:", "ftp://"};
        const char* matched = nullptr;
        for (const char* p : prefixes) {
            size_t len = std::strlen(p);
            if (src.compare(i, len, p) == 0) {
                matched = p;
                break;
            }
        }
        if (!matched) return false;

        size_t p = i;
        while (p < src.size() && !isUnicodeWhitespace(static_cast<unsigned char>(src[p])) &&
               src[p] != '<' && src[p] != '>') {
            ++p;
        }
        // Trim trailing punctuation that is more likely sentence punctuation.
        while (p > i) {
            char last = src[p - 1];
            if (last == '.' || last == ',' || last == ';' || last == ':' || last == '!' ||
                last == '?' || last == '\'' || last == '"' || last == '*' || last == '_') {
                --p;
                continue;
            }
            if (last == ')') {
                size_t opens = 0, closes = 0;
                for (size_t k = i; k < p; ++k) {
                    if (src[k] == '(') ++opens;
                    else if (src[k] == ')') ++closes;
                }
                if (closes > opens) {
                    --p;
                    continue;
                }
            }
            break;
        }
        std::string text = src.substr(i, p - i);
        if (text.size() <= std::strlen(matched)) return false;
        if (std::strcmp(matched, "www.") == 0 && text.find('.', 4) == std::string::npos)
            return false;

        NodePtr link = makeNode(NodeType::Link);
        if (std::strcmp(matched, "www.") == 0) link->url = "http://" + text;
        else link->url = text;
        NodePtr t = makeNode(NodeType::Text);
        t->text = text;
        link->children.push_back(std::move(t));
        pushNode(std::move(link));
        i = p;
        return true;
    }

    bool scanFootnoteRef(const std::string& src, size_t& i) {
        if (i + 2 >= src.size() || src[i + 1] != '^') return false;
        size_t close = src.find(']', i + 2);
        if (close == std::string::npos || close == i + 2) return false;
        std::string id = src.substr(i + 2, close - i - 2);
        if (id.find(' ') != std::string::npos) return false;

        NodePtr ref = makeNode(NodeType::FootnoteRef);
        ref->id = id;
        if (std::find(footnoteOrder_.begin(), footnoteOrder_.end(), id) ==
            footnoteOrder_.end()) {
            footnoteOrder_.push_back(id);
        }
        pushNode(std::move(ref));
        i = close + 1;
        return true;
    }

    bool scanInlineHtml(const std::string& src, size_t& i) {
        if (src.compare(i, 4, "<!--") == 0) {
            size_t close = src.find("-->", i + 4);
            i = (close == std::string::npos) ? src.size() : close + 3;
            return true;
        }
        std::string name;
        bool closing = false, selfClosing = false;
        size_t len = parseHtmlTag(src, i, name, closing, selfClosing);
        if (len == 0) return false;

        std::string lower = toLower(name);
        if (lower == "br") {
            pushNode(makeNode(NodeType::LineBreak));
            i += len;
            return true;
        }
        if (lower == "code" && !closing) {
            size_t close = src.find("</code>", i + len);
            if (close != std::string::npos) {
                NodePtr code = makeNode(NodeType::CodeSpan);
                code->text = src.substr(i + len, close - (i + len));
                pushNode(std::move(code));
                i = close + 7;
                return true;
            }
        }

        for (const HtmlTagInfo& info : kInlineTags) {
            if (lower != info.name) continue;
            if (closing) {
                // Close the innermost matching frame; ignore stray closers.
                for (size_t f = frames_.size(); f-- > 1;) {
                    if (frames_[f].isTagFrame && frames_[f].tag == lower) {
                        while (frames_.size() - 1 > f) closeFrame(true);
                        closeFrame(false);
                        i += len;
                        return true;
                    }
                }
                i += len;
                return true;
            }
            if (!selfClosing) {
                flushPending();
                Frame frame;
                frame.tag = lower;
                frame.type = info.type;
                frame.isTagFrame = true;
                frames_.push_back(std::move(frame));
            }
            i += len;
            return true;
        }

        // Unknown tag: drop it, keeping the surrounding text intact.
        i += len;
        return true;
    }

    bool scanDelimiterRun(const std::string& src, size_t& i) {
        char c = src[i];
        size_t n = 0;
        while (i + n < src.size() && src[i + n] == c) ++n;

        if ((c == '~' || c == '=') && n != 2) {
            pending_.append(n, c);
            i += n;
            return true;
        }

        unsigned char before = (i == 0) ? ' ' : static_cast<unsigned char>(src[i - 1]);
        unsigned char after =
            (i + n >= src.size()) ? ' ' : static_cast<unsigned char>(src[i + n]);

        bool beforeWs = isUnicodeWhitespace(before);
        bool afterWs = isUnicodeWhitespace(after);
        bool beforePunct = isAsciiPunct(static_cast<char>(before));
        bool afterPunct = isAsciiPunct(static_cast<char>(after));

        bool leftFlanking = !afterWs && (!afterPunct || beforeWs || beforePunct);
        bool rightFlanking = !beforeWs && (!beforePunct || afterWs || afterPunct);

        Delimiter d;
        d.ch = c;
        d.count = static_cast<int>(n);
        if (c == '_') {
            d.canOpen = leftFlanking && (!rightFlanking || beforePunct);
            d.canClose = rightFlanking && (!leftFlanking || afterPunct);
        } else {
            d.canOpen = leftFlanking;
            d.canClose = rightFlanking;
        }

        NodePtr text = makeNode(NodeType::Text);
        text->text = src.substr(i, n);
        d.index = pushNode(std::move(text));
        top().delims.push_back(d);
        i += n;
        return true;
    }

    // Handles ']' - resolves inline links, reference links and images.
    bool closeBracket(const std::string& src, size_t& i) {
        Frame& f = top();
        if (f.brackets.empty()) return false;

        size_t bi = f.brackets.size();
        while (bi-- > 0) {
            if (f.brackets[bi].active) break;
            if (bi == 0) return false;
        }
        BracketOpener opener = f.brackets[bi];
        if (!opener.active) return false;
        f.brackets.resize(bi);

        flushPending();
        std::string url, title;
        size_t after = i + 1;
        bool resolved = false;

        // Raw label text (used for reference lookups and shortcut links).
        std::string labelText = rawTextBetween(f.nodes, opener.index + 1, f.nodes.size());

        if (after < src.size() && src[after] == '(') {
            size_t p = after + 1;
            if (parseInlineDestination(src, p, url, title)) {
                after = p;
                resolved = true;
            }
        }
        if (!resolved && after < src.size() && src[after] == '[') {
            size_t close = src.find(']', after + 1);
            if (close != std::string::npos) {
                std::string label = src.substr(after + 1, close - after - 1);
                std::string key = normalizeLabel(label.empty() ? labelText : label);
                auto it = refs_.find(key);
                if (it != refs_.end()) {
                    url = it->second.url;
                    title = it->second.title;
                    after = close + 1;
                    resolved = true;
                }
            }
        }
        if (!resolved) {
            auto it = refs_.find(normalizeLabel(labelText));
            if (it != refs_.end()) {
                url = it->second.url;
                title = it->second.title;
                resolved = true;
            }
        }
        if (!resolved) {
            // Leave the '[' literal in place.
            return false;
        }

        processEmphasis(f, opener.delimBottom);

        NodePtr link = makeNode(opener.image ? NodeType::Image : NodeType::Link);
        link->url = url;
        link->title = title;

        std::vector<NodePtr> inner;
        for (size_t k = opener.index + 1; k < f.nodes.size(); ++k) {
            if (f.nodes[k]) inner.push_back(std::move(f.nodes[k]));
        }
        f.nodes.resize(opener.index + 1);
        std::vector<NodePtr> merged;
        finishNodes(inner, merged);
        for (NodePtr& n : merged) link->children.push_back(std::move(n));

        f.nodes[opener.index] = std::move(link);
        f.delims.resize(opener.delimBottom);

        if (!opener.image) {
            for (BracketOpener& b : f.brackets) b.active = false;
        }
        i = after;
        return true;
    }

    static std::string rawTextBetween(const std::vector<NodePtr>& nodes, size_t from,
                                      size_t to) {
        std::string out;
        for (size_t k = from; k < to && k < nodes.size(); ++k) {
            if (!nodes[k]) continue;
            collectText(*nodes[k], out);
        }
        return out;
    }

    static void collectText(const Node& n, std::string& out) {
        if (n.type == NodeType::Text || n.type == NodeType::CodeSpan) out += n.text;
        for (const NodePtr& c : n.children) collectText(*c, out);
    }

    static bool parseInlineDestination(const std::string& src, size_t& p, std::string& url,
                                       std::string& title) {
        size_t i = p;
        while (i < src.size() && (src[i] == ' ' || src[i] == '\n')) ++i;

        std::string dest;
        if (i < src.size() && src[i] == '<') {
            size_t close = src.find('>', i + 1);
            if (close == std::string::npos) return false;
            dest = src.substr(i + 1, close - i - 1);
            i = close + 1;
        } else {
            int depth = 0;
            while (i < src.size()) {
                char c = src[i];
                if (c == '\\' && i + 1 < src.size()) {
                    dest.push_back(c);
                    dest.push_back(src[i + 1]);
                    i += 2;
                    continue;
                }
                if (c == '(') ++depth;
                if (c == ')') {
                    if (depth == 0) break;
                    --depth;
                }
                if (c == ' ' || c == '\n') break;
                dest.push_back(c);
                ++i;
            }
        }

        while (i < src.size() && (src[i] == ' ' || src[i] == '\n')) ++i;
        if (i < src.size() && (src[i] == '"' || src[i] == '\'' || src[i] == '(')) {
            char open = src[i];
            char close = (open == '(') ? ')' : open;
            size_t end = i + 1;
            std::string t;
            bool closed = false;
            while (end < src.size()) {
                if (src[end] == '\\' && end + 1 < src.size()) {
                    t.push_back(src[end]);
                    t.push_back(src[end + 1]);
                    end += 2;
                    continue;
                }
                if (src[end] == close) {
                    closed = true;
                    break;
                }
                t.push_back(src[end]);
                ++end;
            }
            if (!closed) return false;
            title = unescapeText(t);
            i = end + 1;
        }

        while (i < src.size() && (src[i] == ' ' || src[i] == '\n')) ++i;
        if (i >= src.size() || src[i] != ')') return false;

        url = encodeSpaces(unescapeText(dest));
        p = i + 1;
        return true;
    }

    // Inserts a node, keeping delimiter and bracket slot indices valid.
    static void insertNodeAt(Frame& f, size_t pos, NodePtr n) {
        f.nodes.insert(f.nodes.begin() + static_cast<long>(pos), std::move(n));
        for (Delimiter& d : f.delims) {
            if (d.index >= pos) ++d.index;
        }
        for (BracketOpener& b : f.brackets) {
            if (b.index >= pos) ++b.index;
        }
    }

    // CommonMark's emphasis resolution over the frame's delimiter list.
    void processEmphasis(Frame& f, size_t bottom) {
        if (f.delims.size() <= bottom) {
            f.delims.resize(bottom);
            return;
        }

        size_t closerIdx = bottom;
        while (closerIdx < f.delims.size()) {
            Delimiter& closer = f.delims[closerIdx];
            if (!closer.active || !closer.canClose || closer.count == 0) {
                ++closerIdx;
                continue;
            }

            bool matched = false;
            size_t openerIdx = closerIdx;
            while (openerIdx-- > bottom) {
                Delimiter& opener = f.delims[openerIdx];
                if (!opener.active || !opener.canOpen || opener.ch != closer.ch ||
                    opener.count == 0)
                    continue;

                if (closer.ch == '~' || closer.ch == '=') {
                    if (opener.count != 2 || closer.count != 2) continue;
                } else {
                    // "Rule of three" from the CommonMark spec.
                    bool oddMatch = (closer.canOpen || opener.canClose) &&
                                    (closer.count + opener.count) % 3 == 0 &&
                                    !(closer.count % 3 == 0 && opener.count % 3 == 0);
                    if (oddMatch) continue;
                }

                int use = 1;
                NodeType type = NodeType::Emph;
                if (closer.ch == '~') {
                    use = 2;
                    type = NodeType::Strike;
                } else if (closer.ch == '=') {
                    use = 2;
                    type = NodeType::Highlight;
                } else if (opener.count >= 2 && closer.count >= 2) {
                    use = 2;
                    type = NodeType::Strong;
                }

                NodePtr node = makeNode(type);
                std::vector<NodePtr> inner;
                size_t innerStart = opener.index + 1;
                for (size_t s = innerStart; s < closer.index; ++s) {
                    if (f.nodes[s]) inner.push_back(std::move(f.nodes[s]));
                }
                std::vector<NodePtr> merged;
                finishNodes(inner, merged);
                for (NodePtr& n : merged) node->children.push_back(std::move(n));

                opener.count -= use;
                closer.count -= use;
                f.nodes[opener.index]->text.erase(f.nodes[opener.index]->text.size() -
                                                  static_cast<size_t>(use));
                f.nodes[closer.index]->text.erase(0, static_cast<size_t>(use));

                // Delimiters between the pair can no longer match anything.
                for (size_t k = openerIdx + 1; k < closerIdx; ++k) f.delims[k].active = false;

                if (innerStart < closer.index) {
                    f.nodes[innerStart] = std::move(node);
                } else {
                    insertNodeAt(f, closer.index, std::move(node));
                }

                if (opener.count == 0) {
                    f.nodes[opener.index].reset();
                    opener.active = false;
                }
                if (closer.count == 0) {
                    f.nodes[closer.index].reset();
                    closer.active = false;
                }
                matched = true;
                break;
            }

            if (!matched || !f.delims[closerIdx].active || f.delims[closerIdx].count == 0) {
                ++closerIdx;
            }
        }

        f.delims.resize(bottom);
    }
};

} // namespace

Document parse(const std::string& utf8) {
    Parser parser(utf8);
    return parser.run();
}

} // namespace md
