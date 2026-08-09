// Syntax highlighting for fenced code blocks. Windows-independent so the test
// suite can drive it directly; the layout engine turns tokens into coloured runs.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace syntax {

enum class TokenType {
    Text,         // anything not classified
    Keyword,      // control flow, declarations, at-rules
    Type,         // built-in types, known builtins
    String,       // string and character literals
    Number,       // numeric literals, hex colours, entities
    Comment,      // line and block comments
    Preprocessor, // directives, annotations, decorators, doctypes, variables
    Tag,          // markup tag names and brackets, CSS selectors
    Attribute,    // markup attribute names, CSS property names
    Function,     // identifier in call position
};

struct Token {
    size_t start = 0;
    size_t length = 0;
    TokenType type = TokenType::Text;
};

enum class Language {
    None,
    Markup,      // html, xml, svg, xaml and friends
    Razor,       // cshtml / Razor Pages: markup with C# transitions
    Css,
    JavaScript,  // also TypeScript and the jsx/tsx variants
    CSharp,
    Java,        // also Android sources
    Swift,       // also iOS sources
    Python,
    Sql,         // dialect-neutral
    TransactSql,
    PostgresSql,
};

// Maps a fence info string ("```cs", "```html title=x") to a language.
Language languageFromInfo(const std::string& info);

// Tokens covering the whole input, in order, with no gaps or overlaps.
std::vector<Token> tokenize(Language language, const std::string& code);

// Test helper: the classified (non-Text) tokens as `type:"text"`, space separated.
std::string dumpTokens(Language language, const std::string& code);

} // namespace syntax
