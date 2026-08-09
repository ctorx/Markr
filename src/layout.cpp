#include "layout.h"

#include "highlight.h"

#include <algorithm>
#include <map>

namespace view {

std::wstring toWide(const std::string& utf8) {
    std::wstring out;
    out.reserve(utf8.size());
    size_t i = 0;
    while (i < utf8.size()) {
        unsigned char c = static_cast<unsigned char>(utf8[i]);
        unsigned int cp = 0;
        size_t extra = 0;
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1F;
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0F;
            extra = 2;
        } else if ((c & 0xF8) == 0xF0) {
            cp = c & 0x07;
            extra = 3;
        } else {
            cp = 0xFFFD;
        }
        ++i;
        for (size_t k = 0; k < extra; ++k) {
            if (i >= utf8.size()) {
                cp = 0xFFFD;
                break;
            }
            unsigned char cc = static_cast<unsigned char>(utf8[i]);
            if ((cc & 0xC0) != 0x80) {
                cp = 0xFFFD;
                break;
            }
            cp = (cp << 6) | (cc & 0x3F);
            ++i;
        }
        if (cp >= 0x10000 && cp <= 0x10FFFF) {
            cp -= 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<wchar_t>(cp > 0xFFFF ? 0xFFFD : cp));
        }
    }
    return out;
}

std::string toUtf8(const std::wstring& wide) {
    std::string out;
    out.reserve(wide.size());
    for (size_t i = 0; i < wide.size(); ++i) {
        unsigned int cp = static_cast<unsigned int>(wide[i]);
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < wide.size()) {
            unsigned int low = static_cast<unsigned int>(wide[i + 1]);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            }
        }
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
    return out;
}

namespace {

bool isSpaceW(wchar_t c) { return c == L' ' || c == L'\t'; }

int measure(const IMeasurer& m, FontId font, const std::wstring& s) {
    return s.empty() ? 0 : m.width(font, s.c_str(), s.size());
}

// Style state while walking the inline tree.
struct InlineState {
    Style style;
    bool bold = false;
    bool italic = false;
    bool code = false;
    FontId base = FontId::Body;
};

ColorRole colorForToken(syntax::TokenType type) {
    switch (type) {
        case syntax::TokenType::Keyword: return ColorRole::CodeKeyword;
        case syntax::TokenType::Type: return ColorRole::CodeType;
        case syntax::TokenType::String: return ColorRole::CodeString;
        case syntax::TokenType::Number: return ColorRole::CodeNumber;
        case syntax::TokenType::Comment: return ColorRole::CodeComment;
        case syntax::TokenType::Preprocessor: return ColorRole::CodeDirective;
        case syntax::TokenType::Tag: return ColorRole::CodeTag;
        case syntax::TokenType::Attribute: return ColorRole::CodeAttribute;
        case syntax::TokenType::Function: return ColorRole::CodeFunction;
        default: return ColorRole::CodeText;
    }
}

// GitHub-compatible heading slug: lower-cased, punctuation dropped, spaces
// hyphenated, duplicates suffixed.
std::wstring slugify(const std::wstring& text) {
    std::wstring slug;
    for (wchar_t c : text) {
        if (c >= L'A' && c <= L'Z') {
            slug.push_back(static_cast<wchar_t>(c - L'A' + L'a'));
        } else if ((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == L'-' ||
                   c == L'_' || c >= 128) {
            slug.push_back(c);
        } else if (c == L' ' || c == L'\t') {
            if (!slug.empty() && slug.back() != L'-') slug.push_back(L'-');
        }
    }
    while (!slug.empty() && slug.back() == L'-') slug.pop_back();
    return slug;
}

FontId resolveFont(const InlineState& s) {
    if (s.code) return s.bold ? FontId::CodeBold : FontId::Code;
    switch (s.base) {
        case FontId::Body:
            if (s.bold && s.italic) return FontId::BodyBoldItalic;
            if (s.bold) return FontId::BodyBold;
            if (s.italic) return FontId::BodyItalic;
            return FontId::Body;
        case FontId::Small:
            return s.bold ? FontId::SmallBold : FontId::Small;
        default:
            return s.base; // headings and code keep their own face
    }
}

class Builder {
public:
    Builder(const IMeasurer& measurer, const Metrics& metrics, int viewportWidth,
            IImageSource* images, Layout& out)
        : m_(measurer), met_(metrics), images_(images), out_(out) {
        out_.width = viewportWidth;
        out_.measurer = &measurer;
        left_ = met_.padding;
        right_ = std::max(left_ + 40, viewportWidth - met_.padding);
    }

    void build(const md::Document& doc) {
        for (size_t i = 0; i < doc.footnoteOrder.size(); ++i) {
            footnotes_[doc.footnoteOrder[i]] = static_cast<int>(i) + 1;
        }
        y_ = met_.padding;
        if (doc.root) layoutChildren(*doc.root, left_, right_);
        out_.contentHeight = y_ + met_.padding;
    }

private:
    const IMeasurer& m_;
    const Metrics& met_;
    IImageSource* images_;
    Layout& out_;
    std::map<std::string, int> footnotes_;
    std::map<std::wstring, int> slugCounts_;

    int left_ = 0;
    int right_ = 0;
    int y_ = 0;

    // Flow state for the block currently being filled.
    int flowLeft_ = 0;
    int flowRight_ = 0;
    md::Align flowAlign_ = md::Align::None;
    int x_ = 0;
    int lineTop_ = 0;
    int lineHeight_ = 0;
    int lineAscent_ = 0;
    size_t lineStartRun_ = 0;
    bool justWrapped_ = false;
    bool haveLineContent_ = false;

    std::wstring pendingText_;
    Style pendingStyle_;
    int pendingX_ = 0;
    int pendingWidth_ = 0;
    bool pendingActive_ = false;

    // -------------------------------------------------------- source mapping

    // Position in the document text, counting the run still being assembled.
    size_t textPos() const {
        return out_.text.size() + (pendingActive_ ? pendingText_.size() : 0);
    }

    void recordRange(const md::Node& node, size_t start) {
        TextRange range;
        range.start = start;
        range.end = textPos();
        out_.nodeRanges[&node] = range;
    }

    // ------------------------------------------------------------- flowing

    void beginFlow(int left, int right, md::Align align, FontId defaultFont) {
        flowLeft_ = left;
        flowRight_ = std::max(left + 1, right);
        flowAlign_ = align;
        x_ = left;
        lineTop_ = y_;
        lineHeight_ = m_.lineHeight(defaultFont);
        lineAscent_ = m_.ascent(defaultFont);
        lineStartRun_ = out_.runs.size();
        justWrapped_ = false;
        haveLineContent_ = false;
        pendingActive_ = false;
        pendingText_.clear();
    }

    void flushPending() {
        if (!pendingActive_ || pendingText_.empty()) {
            pendingActive_ = false;
            pendingText_.clear();
            return;
        }
        Run run;
        run.text = pendingText_;
        run.textStart = out_.text.size();
        out_.text += pendingText_;
        run.x = pendingX_;
        run.width = pendingWidth_;
        run.style = pendingStyle_;
        run.y = 0;      // assigned when the line is closed
        run.height = 0;
        run.baseline = 0;
        out_.runs.push_back(run);
        pendingActive_ = false;
        pendingText_.clear();
        pendingWidth_ = 0;
    }

    bool sameStyle(const Style& a, const Style& b) const {
        return a.font == b.font && a.color == b.color && a.background == b.background &&
               a.underline == b.underline && a.strike == b.strike &&
               a.linkIndex == b.linkIndex && a.verticalShift == b.verticalShift;
    }

    void appendToPending(const std::wstring& text, const Style& style, int width) {
        if (text.empty()) return;
        if (pendingActive_ && sameStyle(pendingStyle_, style)) {
            pendingText_ += text;
            pendingWidth_ += width;
        } else {
            flushPending();
            pendingActive_ = true;
            pendingStyle_ = style;
            pendingText_ = text;
            pendingX_ = x_;
            pendingWidth_ = width;
        }
        lineHeight_ = std::max(lineHeight_, m_.lineHeight(style.font) +
                                                std::abs(style.verticalShift));
        lineAscent_ = std::max(lineAscent_, m_.ascent(style.font));
        haveLineContent_ = true;
    }

    void endLine() {
        flushPending();
        int lineRight = flowLeft_;
        for (size_t i = lineStartRun_; i < out_.runs.size(); ++i) {
            Run& r = out_.runs[i];
            r.y = lineTop_;
            r.height = lineHeight_;
            r.baseline = lineAscent_ + r.style.verticalShift;
            lineRight = std::max(lineRight, r.x + r.width);
        }
        if (flowAlign_ == md::Align::Center || flowAlign_ == md::Align::Right) {
            int slack = (flowRight_ - flowLeft_) - (lineRight - flowLeft_);
            if (slack > 0) {
                int shift = (flowAlign_ == md::Align::Center) ? slack / 2 : slack;
                for (size_t i = lineStartRun_; i < out_.runs.size(); ++i) {
                    out_.runs[i].x += shift;
                }
            }
        }

        y_ = lineTop_ + lineHeight_;
        lineTop_ = y_;
        lineStartRun_ = out_.runs.size();
        x_ = flowLeft_;
        haveLineContent_ = false;
    }

    void wrapLine() {
        endLine();
        justWrapped_ = true;
    }

    void endFlow(bool appendNewline) {
        endLine();
        if (appendNewline) out_.text.push_back(L'\n');
    }

    void hardBreak() {
        endLine();
        out_.text.push_back(L'\n');
        justWrapped_ = false;
    }

    // Number of characters of `text` that fit in `available` pixels (at least one).
    size_t fitChars(FontId font, const std::wstring& text, int available) const {
        size_t lo = 1, hi = text.size();
        while (lo < hi) {
            size_t mid = (lo + hi + 1) / 2;
            if (m_.width(font, text.c_str(), mid) <= available) lo = mid;
            else hi = mid - 1;
        }
        return lo;
    }

    void addToken(const std::wstring& word, const std::wstring& spaces, const Style& style) {
        FontId font = style.font;
        if (!word.empty()) {
            int wordWidth = measure(m_, font, word);
            if (x_ + wordWidth > flowRight_ && haveLineContent_) wrapLine();

            if (wordWidth > flowRight_ - flowLeft_) {
                // A single word longer than the line: break it across lines so the
                // view never needs a horizontal scrollbar.
                std::wstring rest = word;
                while (!rest.empty()) {
                    int available = flowRight_ - x_;
                    if (available <= 0) {
                        wrapLine();
                        available = flowRight_ - x_;
                    }
                    size_t count = fitChars(font, rest, available);
                    if (count == 0) count = 1;
                    std::wstring piece = rest.substr(0, count);
                    int pieceWidth = measure(m_, font, piece);
                    if (pieceWidth > available && haveLineContent_) {
                        wrapLine();
                        continue;
                    }
                    appendToPending(piece, style, pieceWidth);
                    x_ += pieceWidth;
                    rest = rest.substr(count);
                    if (!rest.empty()) wrapLine();
                }
            } else {
                appendToPending(word, style, wordWidth);
                x_ += wordWidth;
            }
            justWrapped_ = false;
        }

        if (!spaces.empty()) {
            if (justWrapped_ && !haveLineContent_) return; // swallow spaces after a wrap
            int spaceWidth = measure(m_, font, spaces);
            appendToPending(spaces, style, spaceWidth);
            x_ += spaceWidth;
        }
    }

    void addText(const std::wstring& text, const Style& style) {
        size_t i = 0;
        while (i < text.size()) {
            std::wstring word, spaces;
            while (i < text.size() && !isSpaceW(text[i])) word.push_back(text[i++]);
            while (i < text.size() && isSpaceW(text[i])) spaces.push_back(text[i++]);
            if (word.empty() && spaces.empty()) break;
            if (word.empty() && justWrapped_ && !haveLineContent_) continue;
            addToken(word, spaces, style);
        }
    }

    void addImage(int imageIndex, int width, int height) {
        if (x_ + width > flowRight_ && haveLineContent_) wrapLine();
        flushPending();
        Decoration d;
        d.type = DecorationType::Image;
        d.x = x_;
        d.y = lineTop_;
        d.width = width;
        d.height = height;
        d.imageIndex = imageIndex;
        out_.decorations.push_back(d);
        x_ += width;
        lineHeight_ = std::max(lineHeight_, height);
        haveLineContent_ = true;
        justWrapped_ = false;
    }

    // ---------------------------------------------------------- inline tree

    void emitInlines(const md::Node& node, InlineState state) {
        for (const md::NodePtr& childPtr : node.children) {
            const md::Node& child = *childPtr;
            InlineState next = state;
            size_t childStart = textPos();
            switch (child.type) {
                case md::NodeType::Text:
                    state.style.font = resolveFont(state);
                    addText(toWide(child.text), state.style);
                    break;
                case md::NodeType::SoftBreak:
                    state.style.font = resolveFont(state);
                    addText(L" ", state.style);
                    break;
                case md::NodeType::LineBreak:
                    hardBreak();
                    break;
                case md::NodeType::CodeSpan: {
                    InlineState codeState = state;
                    codeState.code = true;
                    codeState.style.font = resolveFont(codeState);
                    codeState.style.color = ColorRole::CodeText;
                    codeState.style.background = ColorRole::InlineCodeBg;
                    addText(toWide(child.text), codeState.style);
                    break;
                }
                case md::NodeType::Emph:
                    next.italic = true;
                    emitInlines(child, next);
                    break;
                case md::NodeType::Strong:
                    next.bold = true;
                    emitInlines(child, next);
                    break;
                case md::NodeType::Strike:
                    next.style.strike = true;
                    emitInlines(child, next);
                    break;
                case md::NodeType::Underline:
                    next.style.underline = true;
                    emitInlines(child, next);
                    break;
                case md::NodeType::Highlight:
                    next.style.background = ColorRole::MarkBg;
                    next.style.color = ColorRole::MarkText;
                    emitInlines(child, next);
                    break;
                case md::NodeType::Superscript:
                    next.base = FontId::Small;
                    next.style.verticalShift = -m_.ascent(FontId::Body) / 3;
                    emitInlines(child, next);
                    break;
                case md::NodeType::Subscript:
                    next.base = FontId::Small;
                    next.style.verticalShift = m_.ascent(FontId::Body) / 4;
                    emitInlines(child, next);
                    break;
                case md::NodeType::Link: {
                    LinkTarget target;
                    target.url = toWide(child.url);
                    target.title = toWide(child.title);
                    out_.links.push_back(target);
                    next.style.linkIndex = static_cast<int>(out_.links.size()) - 1;
                    next.style.color = ColorRole::Link;
                    next.style.underline = true;
                    emitInlines(child, next);
                    break;
                }
                case md::NodeType::Image:
                    emitImage(child, state);
                    break;
                case md::NodeType::FootnoteRef: {
                    auto it = footnotes_.find(child.id);
                    int number = (it == footnotes_.end()) ? 0 : it->second;
                    InlineState refState = state;
                    refState.base = FontId::Small;
                    refState.style.font = resolveFont(refState);
                    refState.style.color = ColorRole::Link;
                    refState.style.verticalShift = -m_.ascent(FontId::Body) / 3;
                    addText(number ? std::to_wstring(number) : toWide(child.id),
                            refState.style);
                    break;
                }
                default:
                    emitInlines(child, next);
                    break;
            }
            recordRange(child, childStart);
        }
    }

    void emitImage(const md::Node& node, InlineState state) {
        ImageRef ref;
        ref.source = toWide(node.url);
        std::string altText;
        collectPlainText(node, altText);
        ref.alt = toWide(altText);
        out_.images.push_back(ref);
        int index = static_cast<int>(out_.images.size()) - 1;

        int w = 0, h = 0;
        if (images_ && images_->imageSize(ref.source, &w, &h) && w > 0 && h > 0) {
            int maxWidth = flowRight_ - flowLeft_;
            if (w > maxWidth) {
                h = static_cast<int>(static_cast<long long>(h) * maxWidth / w);
                w = maxWidth;
            }
            addImage(index, w, h);
            return;
        }

        InlineState altState = state;
        altState.italic = true;
        altState.style.font = resolveFont(altState);
        altState.style.color = ColorRole::Muted;
        addText(ref.alt.empty() ? ref.source : ref.alt, altState.style);
    }

    static void collectPlainText(const md::Node& node, std::string& out) {
        if (node.type == md::NodeType::Text || node.type == md::NodeType::CodeSpan)
            out += node.text;
        for (const md::NodePtr& c : node.children) collectPlainText(*c, out);
    }

    // ----------------------------------------------------------- block tree

    // Extra breathing room above a heading, so sections read as separate. The
    // deeper the heading, the tighter it binds to what follows.
    int spaceAboveHeading(int level) const {
        switch (level) {
            case 1: return met_.sectionSpacing * 3 / 2;
            case 2: return met_.sectionSpacing;
            case 3: return met_.sectionSpacing / 2;
            default: return met_.sectionSpacing / 4;
        }
    }

    void layoutChildren(const md::Node& node, int left, int right) {
        bool first = true;
        for (const md::NodePtr& child : node.children) {
            if (!first) {
                y_ += met_.blockSpacing;
                if (child->type == md::NodeType::Heading) {
                    y_ += spaceAboveHeading(std::min(6, std::max(1, child->level)));
                }
            }
            layoutBlock(*child, left, right);
            first = false;
        }
    }

    void layoutBlock(const md::Node& node, int left, int right) {
        size_t blockStart = textPos();
        layoutBlockBody(node, left, right);
        recordRange(node, blockStart);
    }

    void layoutBlockBody(const md::Node& node, int left, int right) {
        switch (node.type) {
            case md::NodeType::Paragraph: {
                InlineState state;
                beginFlow(left, right, md::Align::None, FontId::Body);
                emitInlines(node, state);
                endFlow(true);
                break;
            }
            case md::NodeType::Heading:
                layoutHeading(node, left, right);
                break;
            case md::NodeType::ThematicBreak: {
                Decoration d;
                d.type = DecorationType::Rule;
                d.x = left;
                d.y = y_;
                d.width = right - left;
                d.height = std::max(1, met_.ruleThickness);
                d.color = ColorRole::Rule;
                out_.decorations.push_back(d);
                y_ += d.height;
                break;
            }
            case md::NodeType::CodeBlock:
                layoutCodeBlock(node, left, right, ColorRole::CodeText);
                break;
            case md::NodeType::FrontMatter:
                layoutCodeBlock(node, left, right, ColorRole::Muted);
                break;
            case md::NodeType::HtmlBlock:
                layoutPlainLines(node.text, left, right);
                break;
            case md::NodeType::BlockQuote:
                layoutBlockQuote(node, left, right);
                break;
            case md::NodeType::List:
                layoutList(node, left, right);
                break;
            case md::NodeType::Table:
                layoutTable(node, left, right);
                break;
            case md::NodeType::FootnoteDef:
                layoutFootnote(node, left, right);
                break;
            default:
                layoutChildren(node, left, right);
                break;
        }
    }

    void layoutHeading(const md::Node& node, int left, int right) {
        static const FontId kHeadingFonts[7] = {FontId::H1, FontId::H1, FontId::H2,
                                                FontId::H3, FontId::H4, FontId::H5,
                                                FontId::H6};
        int level = std::min(6, std::max(1, node.level));

        OutlineEntry entry;
        entry.level = level;
        entry.y = y_;
        std::string plain;
        collectPlainText(node, plain);
        entry.text = toWide(plain);

        std::wstring slug = slugify(entry.text);
        int& seen = slugCounts_[slug];
        entry.anchor = (seen == 0) ? slug : slug + L"-" + std::to_wstring(seen);
        ++seen;
        out_.outline.push_back(entry);

        InlineState state;
        state.base = kHeadingFonts[level];
        state.style.color = ColorRole::Heading;
        state.style.font = state.base;

        beginFlow(left, right, md::Align::None, state.base);
        emitInlines(node, state);
        endFlow(true);

        if (level <= 2) {
            y_ += met_.headingRuleGap;
            Decoration d;
            d.type = DecorationType::Rule;
            d.x = left;
            d.y = y_;
            d.width = right - left;
            d.height = std::max(1, met_.ruleThickness);
            d.color = ColorRole::Rule;
            out_.decorations.push_back(d);
            y_ += d.height;
            // Keep the next block clear of the rule; a top-level section gets
            // the wider gap.
            y_ += (level == 1) ? met_.headingRuleSpacing : met_.headingRuleSpacing / 2;
        }
    }

    void layoutCodeBlock(const md::Node& node, int left, int right, ColorRole textColor) {
        size_t bgIndex = out_.decorations.size();
        Decoration bg;
        bg.type = DecorationType::CodeBlockBg;
        bg.x = left;
        bg.y = y_;
        bg.width = right - left;
        bg.color = ColorRole::CodeBg;
        out_.decorations.push_back(bg);

        y_ += met_.codePadding;
        Style style;
        style.font = FontId::Code;
        style.color = textColor;

        // Tokens carry byte offsets into node.text, so lines are walked in UTF-8
        // and each segment converted as it is emitted.
        std::vector<syntax::Token> tokens;
        if (textColor == ColorRole::CodeText) {
            syntax::Language language = syntax::languageFromInfo(node.info);
            if (language != syntax::Language::None) {
                tokens = syntax::tokenize(language, node.text);
            }
        }

        size_t tokenIndex = 0;
        size_t lineStart = 0;
        while (true) {
            size_t nl = node.text.find('\n', lineStart);
            size_t lineEnd = (nl == std::string::npos) ? node.text.size() : nl;

            beginFlow(left + met_.codePadding, right - met_.codePadding, md::Align::None,
                      FontId::Code);
            if (tokens.empty()) {
                addText(toWide(node.text.substr(lineStart, lineEnd - lineStart)), style);
            } else {
                while (tokenIndex < tokens.size() &&
                       tokens[tokenIndex].start + tokens[tokenIndex].length <= lineStart) {
                    ++tokenIndex;
                }
                for (size_t t = tokenIndex; t < tokens.size(); ++t) {
                    const syntax::Token& token = tokens[t];
                    if (token.start >= lineEnd) break;
                    size_t from = std::max(token.start, lineStart);
                    size_t to = std::min(token.start + token.length, lineEnd);
                    if (to <= from) continue;
                    Style tokenStyle = style;
                    tokenStyle.color = colorForToken(token.type);
                    addText(toWide(node.text.substr(from, to - from)), tokenStyle);
                }
            }
            endFlow(true);

            if (nl == std::string::npos) break;
            lineStart = nl + 1;
        }

        y_ += met_.codePadding;
        out_.decorations[bgIndex].height = y_ - out_.decorations[bgIndex].y;
    }

    void layoutPlainLines(const std::string& utf8, int left, int right) {
        Style style;
        style.font = FontId::Body;
        style.color = ColorRole::Text;
        std::wstring text = toWide(utf8);
        size_t start = 0;
        while (true) {
            size_t nl = text.find(L'\n', start);
            std::wstring line =
                text.substr(start, nl == std::wstring::npos ? std::wstring::npos : nl - start);
            beginFlow(left, right, md::Align::None, FontId::Body);
            addText(line, style);
            endFlow(true);
            if (nl == std::wstring::npos) break;
            start = nl + 1;
        }
    }

    void layoutBlockQuote(const md::Node& node, int left, int right) {
        size_t barIndex = out_.decorations.size();
        Decoration bar;
        bar.type = DecorationType::QuoteBar;
        bar.x = left;
        bar.y = y_;
        bar.width = met_.quoteBarWidth;
        bar.color = ColorRole::QuoteBar;
        out_.decorations.push_back(bar);

        size_t firstRun = out_.runs.size();
        layoutChildren(node, left + met_.quoteIndent, right);
        out_.decorations[barIndex].height = y_ - out_.decorations[barIndex].y;

        for (size_t i = firstRun; i < out_.runs.size(); ++i) {
            Run& r = out_.runs[i];
            if (r.style.color == ColorRole::Text) r.style.color = ColorRole::QuoteText;
        }
    }

    void layoutList(const md::Node& node, int left, int right) {
        int contentLeft = left + met_.listIndent;
        int number = node.start;
        bool first = true;

        for (const md::NodePtr& itemPtr : node.children) {
            const md::Node& item = *itemPtr;
            if (!first) y_ += node.tight ? met_.blockSpacing / 3 : met_.blockSpacing;
            first = false;
            size_t itemStart = textPos();

            if (item.taskState >= 0) {
                Decoration box;
                box.type = item.taskState == 1 ? DecorationType::CheckboxChecked
                                               : DecorationType::Checkbox;
                box.x = left;
                box.width = met_.checkboxSize;
                box.height = met_.checkboxSize;
                box.y = y_ + (m_.lineHeight(FontId::Body) - met_.checkboxSize) / 2;
                box.color = ColorRole::Checkbox;
                out_.decorations.push_back(box);
            } else {
                std::wstring marker =
                    node.ordered ? std::to_wstring(number) + (node.delimiter == ')' ? L") " : L". ")
                                 : std::wstring(L"\x2022 ");
                emitStandaloneRun(marker, left, FontId::Body, ColorRole::Muted);
            }
            if (node.ordered) ++number;

            if (item.children.empty()) {
                y_ += m_.lineHeight(FontId::Body);
            } else {
                layoutChildren(item, contentLeft, right);
            }
            recordRange(item, itemStart);
        }
    }

    void layoutFootnote(const md::Node& node, int left, int right) {
        auto it = footnotes_.find(node.id);
        std::wstring marker = (it == footnotes_.end())
                                  ? toWide(node.id) + L". "
                                  : std::to_wstring(it->second) + L". ";
        emitStandaloneRun(marker, left, FontId::Small, ColorRole::Muted);

        size_t firstRun = out_.runs.size();
        layoutChildren(node, left + met_.listIndent, right);
        for (size_t i = firstRun; i < out_.runs.size(); ++i) {
            Run& r = out_.runs[i];
            if (r.style.color == ColorRole::Text) r.style.color = ColorRole::Muted;
        }
    }

    void emitStandaloneRun(const std::wstring& text, int x, FontId font, ColorRole color) {
        Run run;
        run.text = text;
        run.textStart = out_.text.size();
        out_.text += text;
        run.x = x;
        run.y = y_;
        run.width = measure(m_, font, text);
        run.height = m_.lineHeight(font);
        run.baseline = m_.ascent(font);
        run.style.font = font;
        run.style.color = color;
        out_.runs.push_back(run);
    }

    void layoutTable(const md::Node& node, int left, int right) {
        size_t columns = node.aligns.size();
        if (columns == 0) return;

        // Natural width per column plus the width below which the column would
        // have to break words; shrink towards the latter to fit.
        int floorWidth = met_.cellPadding * 2 + m_.lineHeight(FontId::Body);
        std::vector<int> widths(columns, 0);
        std::vector<int> minWidths(columns, floorWidth);
        for (const md::NodePtr& rowPtr : node.children) {
            const md::Node& row = *rowPtr;
            FontId font = row.headerRow ? FontId::BodyBold : FontId::Body;
            for (size_t c = 0; c < columns && c < row.children.size(); ++c) {
                std::string plain;
                collectPlainText(*row.children[c], plain);
                std::wstring wide = toWide(plain);
                widths[c] = std::max(widths[c], measure(m_, font, wide) + met_.cellPadding * 2);

                size_t start = 0;
                while (start <= wide.size()) {
                    size_t end = start;
                    while (end < wide.size() && !isSpaceW(wide[end])) ++end;
                    if (end > start) {
                        int wordWidth =
                            measure(m_, font, wide.substr(start, end - start)) +
                            met_.cellPadding * 2;
                        minWidths[c] = std::max(minWidths[c], wordWidth);
                    }
                    if (end >= wide.size()) break;
                    start = end + 1;
                }
            }
        }
        for (size_t c = 0; c < columns; ++c) {
            minWidths[c] = std::min(minWidths[c], widths[c]);
        }

        int available = right - left;
        int total = 0;
        for (int w : widths) total += w;
        if (total > available) {
            int flexible = 0;
            for (size_t c = 0; c < columns; ++c) flexible += std::max(0, widths[c] - minWidths[c]);
            int excess = total - available;

            if (excess <= flexible && flexible > 0) {
                for (size_t c = 0; c < columns; ++c) {
                    int slack = widths[c] - minWidths[c];
                    if (slack <= 0) continue;
                    int share =
                        static_cast<int>(static_cast<long long>(slack) * excess / flexible);
                    widths[c] = std::max(minWidths[c], widths[c] - share);
                }
            } else {
                // Even the longest words do not fit; scale everything and accept
                // that long words will be broken.
                for (size_t c = 0; c < columns; ++c) {
                    widths[c] = std::max(
                        floorWidth,
                        static_cast<int>(static_cast<long long>(widths[c]) * available / total));
                }
            }

            total = 0;
            for (int w : widths) total += w;
            // Trim any rounding leftovers from the widest column.
            while (total > available) {
                size_t widest = 0;
                for (size_t c = 1; c < columns; ++c) {
                    if (widths[c] > widths[widest]) widest = c;
                }
                if (widths[widest] <= floorWidth) break;
                --widths[widest];
                --total;
            }
        } else if (total < available) {
            // Fill the row so borders line up with the content column.
            int extra = (available - total) / static_cast<int>(columns);
            for (int& w : widths) w += extra;
        }

        for (const md::NodePtr& rowPtr : node.children) {
            const md::Node& row = *rowPtr;
            int rowTop = y_;
            int rowBottom = y_;
            int cellX = left;
            size_t rowStart = textPos();

            std::vector<int> cellLefts;
            for (size_t c = 0; c < columns; ++c) {
                cellLefts.push_back(cellX);
                const md::Node* cell =
                    c < row.children.size() ? row.children[c].get() : nullptr;

                y_ = rowTop + met_.cellPadding;
                size_t cellStart = textPos();
                InlineState state;
                state.bold = row.headerRow;
                beginFlow(cellX + met_.cellPadding, cellX + widths[c] - met_.cellPadding,
                          c < node.aligns.size() ? node.aligns[c] : md::Align::None,
                          FontId::Body);
                if (cell) emitInlines(*cell, state);
                endFlow(false);
                if (cell) recordRange(*cell, cellStart);
                out_.text += (c + 1 == columns) ? L'\n' : L'\t';
                rowBottom = std::max(rowBottom, y_);
                cellX += widths[c];
            }
            recordRange(row, rowStart);

            int rowHeight = rowBottom + met_.cellPadding - rowTop;
            if (row.headerRow) {
                Decoration bg;
                bg.type = DecorationType::TableHeaderBg;
                bg.x = left;
                bg.y = rowTop;
                bg.width = cellX - left;
                bg.height = rowHeight;
                bg.color = ColorRole::TableHeaderBg;
                out_.decorations.push_back(bg);
            }
            for (size_t c = 0; c < columns; ++c) {
                Decoration border;
                border.type = DecorationType::TableBorder;
                border.x = cellLefts[c];
                border.y = rowTop;
                border.width = widths[c];
                border.height = rowHeight;
                border.color = ColorRole::TableBorder;
                out_.decorations.push_back(border);
            }
            y_ = rowTop + rowHeight;
        }
    }
};

// Runs on the same visual line share a y coordinate; collect that line's range.
void lineRange(const Layout& layout, size_t anyRun, size_t* first, size_t* last) {
    int y = layout.runs[anyRun].y;
    size_t f = anyRun, l = anyRun;
    while (f > 0 && layout.runs[f - 1].y == y) --f;
    while (l + 1 < layout.runs.size() && layout.runs[l + 1].y == y) ++l;
    *first = f;
    *last = l;
}

int prefixWidth(const Layout& layout, const Run& run, size_t chars) {
    if (chars == 0) return 0;
    if (chars >= run.text.size()) return run.width;
    if (!layout.measurer) {
        return static_cast<int>(static_cast<long long>(run.width) * chars / run.text.size());
    }
    return layout.measurer->width(run.style.font, run.text.c_str(), chars);
}

} // namespace

Layout buildLayout(const md::Document& doc, const IMeasurer& measurer, const Metrics& metrics,
                   int viewportWidth, IImageSource* images) {
    Layout layout;
    Builder builder(measurer, metrics, viewportWidth, images, layout);
    builder.build(doc);
    return layout;
}

size_t indexAtPoint(const Layout& layout, int x, int y) {
    if (layout.runs.empty()) return 0;

    size_t best = 0;
    int bestDistance = -1;
    for (size_t i = 0; i < layout.runs.size(); ++i) {
        const Run& r = layout.runs[i];
        int distance = 0;
        if (y < r.y) distance = r.y - y;
        else if (y >= r.y + r.height) distance = y - (r.y + r.height) + 1;
        if (bestDistance < 0 || distance < bestDistance) {
            bestDistance = distance;
            best = i;
        }
        if (distance == 0 && x >= r.x && x < r.x + r.width) {
            best = i;
            bestDistance = 0;
            break;
        }
    }

    size_t first = 0, last = 0;
    lineRange(layout, best, &first, &last);

    if (x < layout.runs[first].x) return layout.runs[first].textStart;
    if (x >= layout.runs[last].x + layout.runs[last].width)
        return layout.runs[last].textStart + layout.runs[last].text.size();

    for (size_t i = first; i <= last; ++i) {
        const Run& r = layout.runs[i];
        if (x < r.x || x >= r.x + r.width) continue;
        size_t bestChar = 0;
        int bestDelta = -1;
        for (size_t c = 0; c <= r.text.size(); ++c) {
            int px = r.x + prefixWidth(layout, r, c);
            int delta = px > x ? px - x : x - px;
            if (bestDelta < 0 || delta < bestDelta) {
                bestDelta = delta;
                bestChar = c;
            }
        }
        return r.textStart + bestChar;
    }
    return layout.runs[last].textStart + layout.runs[last].text.size();
}

int outlineIndexForAnchor(const Layout& layout, const std::wstring& anchor) {
    auto fold = [](const std::wstring& value) {
        std::wstring out;
        out.reserve(value.size());
        for (wchar_t c : value) {
            out.push_back((c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c - L'A' + L'a') : c);
        }
        return out;
    };

    std::wstring wanted = fold(anchor);
    for (size_t i = 0; i < layout.outline.size(); ++i) {
        if (fold(layout.outline[i].anchor) == wanted) return static_cast<int>(i);
    }
    return -1;
}

int linkAtPoint(const Layout& layout, int x, int y) {
    for (const Run& r : layout.runs) {
        if (r.style.linkIndex < 0) continue;
        if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height)
            return r.style.linkIndex;
    }
    return -1;
}

void selectionRects(const Layout& layout, size_t from, size_t to, std::vector<Rect>& out) {
    out.clear();
    if (from == to) return;
    if (from > to) std::swap(from, to);

    for (const Run& run : layout.runs) {
        size_t runStart = run.textStart;
        size_t runEnd = runStart + run.text.size();
        if (runEnd <= from || runStart >= to) continue;

        size_t a = std::max(from, runStart) - runStart;
        size_t b = std::min(to, runEnd) - runStart;
        if (b <= a) continue;

        Rect rect;
        rect.x = run.x + prefixWidth(layout, run, a);
        rect.width = run.x + prefixWidth(layout, run, b) - rect.x;
        rect.y = run.y;
        rect.height = run.height;
        out.push_back(rect);
    }
}

std::wstring extractText(const Layout& layout, size_t from, size_t to) {
    if (from > to) std::swap(from, to);
    if (from >= layout.text.size()) return std::wstring();
    to = std::min(to, layout.text.size());
    return layout.text.substr(from, to - from);
}

} // namespace view
