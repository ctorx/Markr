// Hand-written lexers. Each one emits only the tokens it can classify, in
// order and without overlaps; tokenize() fills the gaps with Text.
#include "highlight.h"

#include <algorithm>
#include <cstring>

namespace syntax {
namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isHexDigit(char c) {
    return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
bool isAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool isIdentStart(char c) {
    return isAlpha(c) || c == '_' || c == '$' || static_cast<unsigned char>(c) >= 0x80;
}
bool isIdentChar(char c) { return isIdentStart(c) || isDigit(c); }
bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }

void emit(std::vector<Token>& out, size_t start, size_t length, TokenType type) {
    if (length == 0) return;
    Token token;
    token.start = start;
    token.length = length;
    token.type = type;
    out.push_back(token);
}

bool wordEquals(const std::string& src, size_t start, size_t length, const char* word,
                bool caseInsensitive) {
    size_t wordLength = std::strlen(word);
    if (wordLength != length) return false;
    for (size_t i = 0; i < length; ++i) {
        char a = src[start + i];
        char b = word[i];
        if (caseInsensitive) {
            a = lower(a);
            b = lower(b);
        }
        if (a != b) return false;
    }
    return true;
}

bool inList(const std::string& src, size_t start, size_t length, const char* const* list,
            size_t count, bool caseInsensitive) {
    for (size_t i = 0; i < count; ++i) {
        if (wordEquals(src, start, length, list[i], caseInsensitive)) return true;
    }
    return false;
}

template <size_t N>
bool inList(const std::string& src, size_t start, size_t length, const char* const (&list)[N],
            bool caseInsensitive = false) {
    return inList(src, start, length, list, N, caseInsensitive);
}

size_t skipSpaces(const std::string& src, size_t i, size_t end) {
    while (i < end && isSpace(src[i])) ++i;
    return i;
}

// ---------------------------------------------------------------- keywords

const char* const kCSharpKeywords[] = {
    "abstract", "as",       "async",    "await",     "base",     "break",     "case",
    "catch",    "checked",  "class",    "const",     "continue", "default",   "delegate",
    "do",       "else",     "enum",     "event",     "explicit", "extern",    "false",
    "finally",  "fixed",    "for",      "foreach",   "get",      "global",    "goto",
    "if",       "implicit", "in",       "init",      "interface","internal",  "is",
    "lock",     "nameof",   "namespace","new",       "null",     "operator",  "out",
    "override", "params",   "partial",  "private",   "protected","public",    "readonly",
    "record",   "ref",      "required", "return",    "sealed",   "set",       "sizeof",
    "stackalloc","static",  "struct",   "switch",    "this",     "throw",     "true",
    "try",      "typeof",   "unchecked","unsafe",    "using",    "value",     "var",
    "virtual",  "volatile", "when",     "where",     "while",    "with",      "yield",
};

const char* const kCSharpTypes[] = {
    "bool",  "byte",   "char",  "decimal", "double", "dynamic", "float", "int",
    "long",  "nint",   "nuint", "object",  "sbyte",  "short",   "string","uint",
    "ulong", "ushort", "void",  "Task",    "String", "Int32",   "Boolean",
};

const char* const kJavaScriptKeywords[] = {
    "as",         "async",    "await",   "break",     "case",      "catch",  "class",
    "const",      "continue", "debugger","declare",   "default",   "delete", "do",
    "else",       "enum",     "export",  "extends",   "false",     "finally","for",
    "from",       "function", "get",     "if",        "implements","import", "in",
    "instanceof", "interface","keyof",   "let",       "namespace", "new",    "null",
    "of",         "private",  "protected","public",   "readonly",  "return", "satisfies",
    "set",        "static",   "super",   "switch",    "this",      "throw",  "true",
    "try",        "type",     "typeof",  "undefined", "var",       "void",   "while",
    "with",       "yield",
};

const char* const kJavaScriptTypes[] = {
    "any",    "bigint", "boolean", "never",  "number", "object", "string", "symbol",
    "unknown","Array",  "Promise", "Map",    "Set",    "Object", "String", "Number",
    "Boolean","Date",   "RegExp",  "JSON",   "Math",
};

const char* const kJavaKeywords[] = {
    "abstract", "assert",  "break",     "case",       "catch",   "class",     "const",
    "continue", "default", "do",        "else",       "enum",    "extends",   "false",
    "final",    "finally", "for",       "goto",       "if",      "implements","import",
    "instanceof","interface","native",  "new",        "null",    "package",   "permits",
    "private",  "protected","public",   "record",     "return",  "sealed",    "static",
    "strictfp", "super",   "switch",    "synchronized","this",   "throw",     "throws",
    "transient","true",    "try",       "var",        "volatile","while",     "yield",
};

const char* const kJavaTypes[] = {
    "boolean", "byte",   "char",   "double",  "float",   "int",     "long",
    "short",   "void",   "String", "Integer", "Boolean", "Double",  "Long",
    "Object",  "List",   "Map",    "ArrayList","HashMap","Override",
};

const char* const kSwiftKeywords[] = {
    "actor",   "any",       "as",         "associatedtype", "async",   "await",
    "break",   "case",      "catch",      "class",          "continue","convenience",
    "default", "defer",     "deinit",     "didSet",         "do",      "dynamic",
    "else",    "enum",      "extension",  "fallthrough",    "false",   "fileprivate",
    "final",   "for",       "func",       "get",            "guard",   "if",
    "import",  "in",        "indirect",   "init",           "inout",   "internal",
    "is",      "lazy",      "let",        "mutating",       "nil",     "nonmutating",
    "open",    "operator",  "override",   "private",        "protocol","public",
    "repeat",  "required",  "rethrows",   "return",         "self",    "set",
    "some",    "static",    "struct",     "subscript",      "super",   "switch",
    "throw",   "throws",    "true",       "try",            "typealias","unowned",
    "var",     "weak",      "where",      "while",          "willSet",
};

const char* const kSwiftTypes[] = {
    "Any",     "AnyObject", "Array",   "Bool",   "CGFloat", "CGPoint", "CGRect",
    "CGSize",  "Character", "Data",    "Date",   "Dictionary","Double", "Error",
    "Float",   "IndexPath", "Int",     "NSObject","Optional","Result",  "Self",
    "Set",     "String",    "UIButton","UIColor","UIImage", "UILabel", "UITableView",
    "UIView",  "UIViewController",     "URL",    "Void",
};

const char* const kPythonKeywords[] = {
    "False",  "None",     "True",   "and",    "as",     "assert", "async",  "await",
    "break",  "case",     "class",  "continue","def",   "del",    "elif",   "else",
    "except", "finally",  "for",    "from",   "global", "if",     "import", "in",
    "is",     "lambda",   "match",  "nonlocal","not",   "or",     "pass",   "raise",
    "return", "try",      "while",  "with",   "yield",
};

const char* const kPythonBuiltins[] = {
    "abs",   "all",    "any",   "bool",  "bytes",  "classmethod", "dict",  "enumerate",
    "filter","float",  "frozenset","int","isinstance","len",      "list",  "map",
    "max",   "min",    "object","open",  "print",  "property",    "range", "repr",
    "reversed","round","set",   "sorted","staticmethod","str",     "sum",   "super",
    "tuple", "type",   "zip",   "Exception","ValueError","TypeError","KeyError",
};

const char* const kSqlKeywords[] = {
    "ADD",    "ALL",     "ALTER",   "AND",     "ANY",      "AS",        "ASC",
    "BEGIN",  "BETWEEN", "BY",      "CASCADE", "CASE",     "CHECK",     "COLUMN",
    "COMMIT", "CONSTRAINT","CREATE","CROSS",   "DECLARE",  "DEFAULT",   "DELETE",
    "DESC",   "DISTINCT","DROP",    "ELSE",    "END",      "EXEC",      "EXECUTE",
    "EXISTS", "FETCH",   "FOR",     "FOREIGN", "FROM",     "FULL",      "FUNCTION",
    "GRANT",  "GROUP",   "HAVING",  "IF",      "IN",       "INDEX",     "INNER",
    "INSERT", "INTO",    "IS",      "JOIN",    "KEY",      "LEFT",      "LIKE",
    "LIMIT",  "MERGE",   "NOT",     "NULL",    "OFFSET",   "ON",        "OR",
    "ORDER",  "OUTER",   "OVER",    "PARTITION","PRIMARY", "PROCEDURE", "REFERENCES",
    "RETURN", "RETURNS", "REVOKE",  "RIGHT",   "ROLLBACK", "ROW",       "SCHEMA",
    "SELECT", "SET",     "TABLE",   "THEN",    "TOP",      "TRANSACTION","TRIGGER",
    "TRUNCATE","UNION",  "UNIQUE",  "UPDATE",  "USING",    "VALUES",    "VIEW",
    "WHEN",   "WHERE",   "WHILE",   "WITH",    "DATABASE", "IDENTITY",  "ASC",
};

const char* const kTransactSqlKeywords[] = {
    "CATCH",  "CURSOR",  "DEALLOCATE", "GO",     "NOCOUNT", "NOLOCK", "OUTPUT",
    "PRINT",  "RAISERROR","READONLY",  "THROW",  "TRY",     "APPLY",  "PIVOT",
};

const char* const kPostgresKeywords[] = {
    "ALWAYS",  "ARRAY",   "CONFLICT", "DO",      "GENERATED", "ILIKE",  "IMMUTABLE",
    "LANGUAGE","LATERAL", "NOTHING",  "PLPGSQL", "RETURNING", "STABLE", "UNNEST",
    "VOLATILE","LOOP",    "PERFORM",  "RAISE",   "NOTICE",
};

const char* const kSqlTypes[] = {
    "BIGINT",  "BIGSERIAL","BINARY",  "BIT",     "BOOL",    "BOOLEAN", "BYTEA",
    "CHAR",    "DATE",     "DATETIME","DATETIME2","DECIMAL","DOUBLE",  "FLOAT",
    "IMAGE",   "INT",      "INTEGER", "INTERVAL","JSON",    "JSONB",   "MONEY",
    "NCHAR",   "NTEXT",    "NUMERIC", "NVARCHAR","REAL",    "SERIAL",  "SMALLINT",
    "TEXT",    "TIME",     "TIMESTAMP","TIMESTAMPTZ","TINYINT","UNIQUEIDENTIFIER",
    "UUID",    "VARBINARY","VARCHAR", "XML",
};

// -------------------------------------------------------------- C family

struct CSpec {
    const char* const* keywords;
    size_t keywordCount;
    const char* const* types;
    size_t typeCount;
    bool hashDirectives = false;  // C#: #if, #region on their own line
    bool atAnnotations = false;   // Java/Swift: @Override, @objc
    bool backtickStrings = false; // JavaScript template literals
    bool verbatimStrings = false; // C#: @"..." and $"..."
    bool tripleQuotes = false;    // raw/text blocks: """..."""
};

CSpec cSharpSpec() {
    CSpec spec;
    spec.keywords = kCSharpKeywords;
    spec.keywordCount = sizeof(kCSharpKeywords) / sizeof(kCSharpKeywords[0]);
    spec.types = kCSharpTypes;
    spec.typeCount = sizeof(kCSharpTypes) / sizeof(kCSharpTypes[0]);
    spec.hashDirectives = true;
    spec.verbatimStrings = true;
    spec.tripleQuotes = true;
    return spec;
}

CSpec javaScriptSpec() {
    CSpec spec;
    spec.keywords = kJavaScriptKeywords;
    spec.keywordCount = sizeof(kJavaScriptKeywords) / sizeof(kJavaScriptKeywords[0]);
    spec.types = kJavaScriptTypes;
    spec.typeCount = sizeof(kJavaScriptTypes) / sizeof(kJavaScriptTypes[0]);
    spec.backtickStrings = true;
    return spec;
}

CSpec javaSpec() {
    CSpec spec;
    spec.keywords = kJavaKeywords;
    spec.keywordCount = sizeof(kJavaKeywords) / sizeof(kJavaKeywords[0]);
    spec.types = kJavaTypes;
    spec.typeCount = sizeof(kJavaTypes) / sizeof(kJavaTypes[0]);
    spec.atAnnotations = true;
    spec.tripleQuotes = true;
    return spec;
}

CSpec swiftSpec() {
    CSpec spec;
    spec.keywords = kSwiftKeywords;
    spec.keywordCount = sizeof(kSwiftKeywords) / sizeof(kSwiftKeywords[0]);
    spec.types = kSwiftTypes;
    spec.typeCount = sizeof(kSwiftTypes) / sizeof(kSwiftTypes[0]);
    spec.atAnnotations = true;
    spec.tripleQuotes = true;
    return spec;
}

size_t scanLineComment(const std::string& src, size_t i, size_t end) {
    while (i < end && src[i] != '\n') ++i;
    return i;
}

size_t scanBlockComment(const std::string& src, size_t i, size_t end, const char* closing) {
    size_t closingLength = std::strlen(closing);
    size_t j = i;
    while (j + closingLength <= end) {
        if (src.compare(j, closingLength, closing) == 0) return j + closingLength;
        ++j;
    }
    return end;
}

// Scans a quoted run starting at `i` (which points at the quote).
size_t scanQuoted(const std::string& src, size_t i, size_t end, char quote, bool allowEscapes,
                  bool doubledQuoteEscapes) {
    size_t j = i + 1;
    while (j < end) {
        char c = src[j];
        if (allowEscapes && c == '\\' && j + 1 < end) {
            j += 2;
            continue;
        }
        if (c == quote) {
            if (doubledQuoteEscapes && j + 1 < end && src[j + 1] == quote) {
                j += 2;
                continue;
            }
            return j + 1;
        }
        ++j;
    }
    return end;
}

size_t scanTripleQuoted(const std::string& src, size_t i, size_t end, char quote) {
    char triple[4] = {quote, quote, quote, 0};
    size_t j = i + 3;
    while (j + 3 <= end) {
        if (src.compare(j, 3, triple) == 0) return j + 3;
        ++j;
    }
    return end;
}

size_t scanNumber(const std::string& src, size_t i, size_t end) {
    size_t j = i;
    if (j + 1 < end && src[j] == '0' && (lower(src[j + 1]) == 'x' || lower(src[j + 1]) == 'b' ||
                                         lower(src[j + 1]) == 'o')) {
        j += 2;
        while (j < end && (isHexDigit(src[j]) || src[j] == '_')) ++j;
    } else {
        while (j < end && (isDigit(src[j]) || src[j] == '_')) ++j;
        if (j < end && src[j] == '.' && j + 1 < end && isDigit(src[j + 1])) {
            ++j;
            while (j < end && (isDigit(src[j]) || src[j] == '_')) ++j;
        }
        if (j < end && (src[j] == 'e' || src[j] == 'E')) {
            size_t k = j + 1;
            if (k < end && (src[k] == '+' || src[k] == '-')) ++k;
            if (k < end && isDigit(src[k])) {
                j = k;
                while (j < end && isDigit(src[j])) ++j;
            }
        }
    }
    // Numeric suffixes (1f, 2L, 3m, 4ul).
    while (j < end && (lower(src[j]) == 'f' || lower(src[j]) == 'd' || lower(src[j]) == 'l' ||
                       lower(src[j]) == 'u' || lower(src[j]) == 'm')) {
        ++j;
    }
    return j;
}

bool isAtLineStart(const std::string& src, size_t begin, size_t i) {
    while (i > begin) {
        char c = src[i - 1];
        if (c == '\n') return true;
        if (!isSpace(c)) return false;
        --i;
    }
    return true;
}

void lexCLike(const std::string& src, size_t begin, size_t end, const CSpec& spec,
              std::vector<Token>& out) {
    size_t i = begin;
    while (i < end) {
        char c = src[i];

        if (c == '/' && i + 1 < end && src[i + 1] == '/') {
            size_t stop = scanLineComment(src, i, end);
            emit(out, i, stop - i, TokenType::Comment);
            i = stop;
            continue;
        }
        if (c == '/' && i + 1 < end && src[i + 1] == '*') {
            size_t stop = scanBlockComment(src, i + 2, end, "*/");
            emit(out, i, stop - i, TokenType::Comment);
            i = stop;
            continue;
        }
        if (spec.hashDirectives && c == '#' && isAtLineStart(src, begin, i)) {
            size_t stop = scanLineComment(src, i, end);
            emit(out, i, stop - i, TokenType::Preprocessor);
            i = stop;
            continue;
        }
        if (spec.verbatimStrings && (c == '@' || c == '$') && i + 1 < end) {
            size_t quoteAt = i + 1;
            if (src[quoteAt] == '@' || src[quoteAt] == '$') ++quoteAt;
            if (quoteAt < end && src[quoteAt] == '"') {
                bool verbatim = src[i] == '@' || (quoteAt > i + 1);
                size_t stop = scanQuoted(src, quoteAt, end, '"', !verbatim, verbatim);
                emit(out, i, stop - i, TokenType::String);
                i = stop;
                continue;
            }
        }
        if (spec.atAnnotations && c == '@' && i + 1 < end && isIdentStart(src[i + 1])) {
            size_t j = i + 1;
            while (j < end && (isIdentChar(src[j]) || src[j] == '.')) ++j;
            emit(out, i, j - i, TokenType::Preprocessor);
            i = j;
            continue;
        }
        if (c == '"') {
            size_t stop;
            if (spec.tripleQuotes && i + 2 < end && src[i + 1] == '"' && src[i + 2] == '"') {
                stop = scanTripleQuoted(src, i, end, '"');
            } else {
                stop = scanQuoted(src, i, end, '"', true, false);
            }
            emit(out, i, stop - i, TokenType::String);
            i = stop;
            continue;
        }
        if (c == '\'') {
            size_t stop = scanQuoted(src, i, end, '\'', true, false);
            emit(out, i, stop - i, TokenType::String);
            i = stop;
            continue;
        }
        if (spec.backtickStrings && c == '`') {
            size_t stop = scanQuoted(src, i, end, '`', true, false);
            emit(out, i, stop - i, TokenType::String);
            i = stop;
            continue;
        }
        if (isDigit(c)) {
            size_t stop = scanNumber(src, i, end);
            emit(out, i, stop - i, TokenType::Number);
            i = stop;
            continue;
        }
        if (isIdentStart(c)) {
            size_t j = i;
            while (j < end && isIdentChar(src[j])) ++j;
            size_t length = j - i;
            if (inList(src, i, length, spec.keywords, spec.keywordCount, false)) {
                emit(out, i, length, TokenType::Keyword);
            } else if (inList(src, i, length, spec.types, spec.typeCount, false)) {
                emit(out, i, length, TokenType::Type);
            } else {
                size_t after = skipSpaces(src, j, end);
                if (after < end && src[after] == '(') emit(out, i, length, TokenType::Function);
            }
            i = j;
            continue;
        }
        ++i;
    }
}

// --------------------------------------------------------------- Python

bool isStringPrefix(const std::string& src, size_t start, size_t length) {
    if (length == 0 || length > 2) return false;
    for (size_t i = 0; i < length; ++i) {
        char c = lower(src[start + i]);
        if (c != 'r' && c != 'b' && c != 'f' && c != 'u') return false;
    }
    return true;
}

void lexPython(const std::string& src, size_t begin, size_t end, std::vector<Token>& out) {
    size_t i = begin;
    while (i < end) {
        char c = src[i];

        if (c == '#') {
            size_t stop = scanLineComment(src, i, end);
            emit(out, i, stop - i, TokenType::Comment);
            i = stop;
            continue;
        }
        if (c == '@' && i + 1 < end && isIdentStart(src[i + 1]) && isAtLineStart(src, begin, i)) {
            size_t j = i + 1;
            while (j < end && (isIdentChar(src[j]) || src[j] == '.')) ++j;
            emit(out, i, j - i, TokenType::Preprocessor);
            i = j;
            continue;
        }
        if (c == '"' || c == '\'') {
            size_t stop;
            if (i + 2 < end && src[i + 1] == c && src[i + 2] == c) {
                stop = scanTripleQuoted(src, i, end, c);
            } else {
                stop = scanQuoted(src, i, end, c, true, false);
            }
            emit(out, i, stop - i, TokenType::String);
            i = stop;
            continue;
        }
        if (isDigit(c)) {
            size_t stop = scanNumber(src, i, end);
            emit(out, i, stop - i, TokenType::Number);
            i = stop;
            continue;
        }
        if (isIdentStart(c)) {
            size_t j = i;
            while (j < end && isIdentChar(src[j])) ++j;
            size_t length = j - i;

            // A string prefix glued to a quote is part of the literal.
            if (j < end && (src[j] == '"' || src[j] == '\'') && isStringPrefix(src, i, length)) {
                char quote = src[j];
                size_t stop;
                if (j + 2 < end && src[j + 1] == quote && src[j + 2] == quote) {
                    stop = scanTripleQuoted(src, j, end, quote);
                } else {
                    stop = scanQuoted(src, j, end, quote, true, false);
                }
                emit(out, i, stop - i, TokenType::String);
                i = stop;
                continue;
            }

            if (inList(src, i, length, kPythonKeywords)) {
                emit(out, i, length, TokenType::Keyword);
            } else if (inList(src, i, length, kPythonBuiltins)) {
                emit(out, i, length, TokenType::Type);
            } else {
                size_t after = skipSpaces(src, j, end);
                if (after < end && src[after] == '(') emit(out, i, length, TokenType::Function);
            }
            i = j;
            continue;
        }
        ++i;
    }
}

// ------------------------------------------------------------------ SQL

enum class SqlDialect { Generic, Transact, Postgres };

void lexSql(const std::string& src, size_t begin, size_t end, SqlDialect dialect,
            std::vector<Token>& out) {
    size_t i = begin;
    while (i < end) {
        char c = src[i];

        if (c == '-' && i + 1 < end && src[i + 1] == '-') {
            size_t stop = scanLineComment(src, i, end);
            emit(out, i, stop - i, TokenType::Comment);
            i = stop;
            continue;
        }
        if (c == '/' && i + 1 < end && src[i + 1] == '*') {
            size_t stop = scanBlockComment(src, i + 2, end, "*/");
            emit(out, i, stop - i, TokenType::Comment);
            i = stop;
            continue;
        }
        if (c == '\'') {
            size_t stop = scanQuoted(src, i, end, '\'', false, true);
            emit(out, i, stop - i, TokenType::String);
            i = stop;
            continue;
        }
        if (dialect != SqlDialect::Transact && c == '$') {
            // Dollar quoting: $$ ... $$ or $tag$ ... $tag$ (the tag is a plain
            // identifier, so '$' itself must not be consumed by the scan).
            size_t tagEnd = i + 1;
            while (tagEnd < end && (isAlpha(src[tagEnd]) || isDigit(src[tagEnd]) ||
                                    src[tagEnd] == '_')) {
                ++tagEnd;
            }
            if (tagEnd < end && src[tagEnd] == '$') {
                std::string tag = src.substr(i, tagEnd - i + 1);
                size_t search = src.find(tag, tagEnd + 1);
                size_t stop = (search == std::string::npos || search >= end)
                                  ? end
                                  : search + tag.size();
                emit(out, i, stop - i, TokenType::String);
                i = stop;
                continue;
            }
        }
        if (dialect != SqlDialect::Postgres && c == '@') {
            size_t j = i + 1;
            if (j < end && src[j] == '@') ++j;
            while (j < end && isIdentChar(src[j])) ++j;
            if (j > i + 1) {
                emit(out, i, j - i, TokenType::Preprocessor);
                i = j;
                continue;
            }
        }
        if (dialect != SqlDialect::Postgres && c == '[') {
            // Bracketed identifier: consumed without classification.
            size_t j = i + 1;
            while (j < end && src[j] != ']') ++j;
            i = (j < end) ? j + 1 : end;
            continue;
        }
        if (c == '"') {
            // Quoted identifier: consumed without classification.
            size_t stop = scanQuoted(src, i, end, '"', false, true);
            i = stop;
            continue;
        }
        if (isDigit(c)) {
            size_t stop = scanNumber(src, i, end);
            emit(out, i, stop - i, TokenType::Number);
            i = stop;
            continue;
        }
        if (isIdentStart(c)) {
            size_t j = i;
            while (j < end && isIdentChar(src[j])) ++j;
            size_t length = j - i;
            bool keyword = inList(src, i, length, kSqlKeywords, true);
            if (!keyword && dialect != SqlDialect::Postgres) {
                keyword = inList(src, i, length, kTransactSqlKeywords, true);
            }
            if (!keyword && dialect != SqlDialect::Transact) {
                keyword = inList(src, i, length, kPostgresKeywords, true);
            }
            if (keyword) {
                emit(out, i, length, TokenType::Keyword);
            } else if (inList(src, i, length, kSqlTypes, true)) {
                emit(out, i, length, TokenType::Type);
            }
            i = j;
            continue;
        }
        ++i;
    }
}

// ------------------------------------------------------------------ CSS

void lexCss(const std::string& src, size_t begin, size_t end, std::vector<Token>& out) {
    size_t i = begin;
    bool inBlock = false;
    bool inValue = false;

    while (i < end) {
        char c = src[i];

        if (c == '/' && i + 1 < end && src[i + 1] == '*') {
            size_t stop = scanBlockComment(src, i + 2, end, "*/");
            emit(out, i, stop - i, TokenType::Comment);
            i = stop;
            continue;
        }
        if (c == '"' || c == '\'') {
            size_t stop = scanQuoted(src, i, end, c, true, false);
            emit(out, i, stop - i, TokenType::String);
            i = stop;
            continue;
        }
        if (c == '@' && i + 1 < end && isIdentStart(src[i + 1])) {
            size_t j = i + 1;
            while (j < end && (isIdentChar(src[j]) || src[j] == '-')) ++j;
            emit(out, i, j - i, TokenType::Keyword);
            i = j;
            continue;
        }
        if (c == '!' && i + 1 < end) {
            size_t j = i + 1;
            while (j < end && isIdentChar(src[j])) ++j;
            if (j > i + 1) {
                emit(out, i, j - i, TokenType::Keyword);
                i = j;
                continue;
            }
        }
        if (c == '{') {
            inBlock = true;
            inValue = false;
            ++i;
            continue;
        }
        if (c == '}') {
            inBlock = false;
            inValue = false;
            ++i;
            continue;
        }
        if (c == ':' && inBlock) {
            inValue = true;
            ++i;
            continue;
        }
        if (c == ';') {
            inValue = false;
            ++i;
            continue;
        }
        if (c == '#') {
            size_t j = i + 1;
            while (j < end && isIdentChar(src[j])) ++j;
            if (j > i + 1) {
                emit(out, i, j - i, inValue ? TokenType::Number : TokenType::Tag);
                i = j;
                continue;
            }
        }
        if (isDigit(c) || (c == '.' && i + 1 < end && isDigit(src[i + 1]))) {
            size_t j = i;
            while (j < end && (isDigit(src[j]) || src[j] == '.')) ++j;
            emit(out, i, j - i, TokenType::Number);
            i = j;
            continue;
        }
        if (c == '.' && i + 1 < end && isIdentStart(src[i + 1])) {
            size_t j = i + 1;
            while (j < end && (isIdentChar(src[j]) || src[j] == '-')) ++j;
            emit(out, i, j - i, TokenType::Tag);
            i = j;
            continue;
        }
        if (isIdentStart(c) || c == '-') {
            size_t j = i;
            while (j < end && (isIdentChar(src[j]) || src[j] == '-')) ++j;
            if (j == i) {
                ++i;
                continue;
            }
            if (!inBlock) {
                emit(out, i, j - i, TokenType::Tag);
            } else if (!inValue) {
                emit(out, i, j - i, TokenType::Attribute);
            }
            i = j;
            continue;
        }
        ++i;
    }
}

// --------------------------------------------------------------- markup

void lexMarkup(const std::string& src, size_t begin, size_t end, bool razor,
               std::vector<Token>& out);

size_t skipBalanced(const std::string& src, size_t i, size_t end, char open, char close) {
    // `i` points at the opening character. Skips strings and comments so braces
    // inside them do not confuse the count.
    int depth = 0;
    while (i < end) {
        char c = src[i];
        if (c == '"' || c == '\'') {
            i = scanQuoted(src, i, end, c, true, false);
            continue;
        }
        if (c == '/' && i + 1 < end && src[i + 1] == '/') {
            i = scanLineComment(src, i, end);
            continue;
        }
        if (c == '/' && i + 1 < end && src[i + 1] == '*') {
            i = scanBlockComment(src, i + 2, end, "*/");
            continue;
        }
        if (c == open) ++depth;
        if (c == close) {
            --depth;
            if (depth == 0) return i + 1;
        }
        ++i;
    }
    return end;
}

const char* const kRazorDirectives[] = {
    "page",   "model",     "using",      "inject",   "inherits", "namespace",
    "implements","layout", "addTagHelper","removeTagHelper","attribute","typeparam",
    "preservewhitespace", "rendermode", "section",  "await",
};

const char* const kRazorControlKeywords[] = {
    "if", "else", "for", "foreach", "while", "switch", "do", "try", "catch", "finally",
    "lock", "using",
};

// Handles a Razor `@` transition. `i` points at the '@'.
size_t lexRazorTransition(const std::string& src, size_t i, size_t end,
                          std::vector<Token>& out) {
    if (i + 1 < end && src[i + 1] == '*') {
        size_t stop = scanBlockComment(src, i + 2, end, "*@");
        emit(out, i, stop - i, TokenType::Comment);
        return stop;
    }
    if (i + 1 < end && src[i + 1] == '@') {
        emit(out, i, 2, TokenType::Preprocessor);
        return i + 2;
    }
    if (i + 1 < end && src[i + 1] == '{') {
        emit(out, i, 1, TokenType::Preprocessor);
        size_t stop = skipBalanced(src, i + 1, end, '{', '}');
        lexCLike(src, i + 1, stop, cSharpSpec(), out);
        return stop;
    }
    if (i + 1 < end && src[i + 1] == '(') {
        emit(out, i, 1, TokenType::Preprocessor);
        size_t stop = skipBalanced(src, i + 1, end, '(', ')');
        lexCLike(src, i + 1, stop, cSharpSpec(), out);
        return stop;
    }
    if (i + 1 >= end || !isIdentStart(src[i + 1])) {
        return i + 1;
    }

    size_t nameStart = i + 1;
    size_t nameEnd = nameStart;
    while (nameEnd < end && isIdentChar(src[nameEnd])) ++nameEnd;
    size_t nameLength = nameEnd - nameStart;

    bool isCodeSection = wordEquals(src, nameStart, nameLength, "code", false) ||
                         wordEquals(src, nameStart, nameLength, "functions", false);
    bool isControl = inList(src, nameStart, nameLength, kRazorControlKeywords);
    bool isDirective = inList(src, nameStart, nameLength, kRazorDirectives);

    // `@using (...)` is a statement; `@using X.Y` is a directive.
    if (isControl && isDirective) {
        size_t after = skipSpaces(src, nameEnd, end);
        isDirective = !(after < end && src[after] == '(');
    }

    if (isCodeSection) {
        emit(out, i, 1, TokenType::Preprocessor);
        emit(out, nameStart, nameLength, TokenType::Keyword);
        size_t braceAt = skipSpaces(src, nameEnd, end);
        if (braceAt < end && src[braceAt] == '{') {
            size_t stop = skipBalanced(src, braceAt, end, '{', '}');
            lexCLike(src, braceAt, stop, cSharpSpec(), out);
            return stop;
        }
        return nameEnd;
    }

    if (isDirective) {
        emit(out, i, nameLength + 1, TokenType::Preprocessor);
        size_t lineEnd = scanLineComment(src, nameEnd, end);
        lexCLike(src, nameEnd, lineEnd, cSharpSpec(), out);
        return lineEnd;
    }

    if (isControl) {
        emit(out, i, 1, TokenType::Preprocessor);
        emit(out, nameStart, nameLength, TokenType::Keyword);
        size_t after = skipSpaces(src, nameEnd, end);
        if (after < end && src[after] == '(') {
            size_t stop = skipBalanced(src, after, end, '(', ')');
            lexCLike(src, after, stop, cSharpSpec(), out);
            return stop;
        }
        return nameEnd;
    }

    // Implicit expression: @Model.Items[0].Name()
    emit(out, i, 1, TokenType::Preprocessor);
    size_t j = nameEnd;
    while (j < end) {
        if (src[j] == '.' && j + 1 < end && isIdentStart(src[j + 1])) {
            j += 2;
            while (j < end && isIdentChar(src[j])) ++j;
            continue;
        }
        if (src[j] == '(') {
            j = skipBalanced(src, j, end, '(', ')');
            continue;
        }
        if (src[j] == '[') {
            j = skipBalanced(src, j, end, '[', ']');
            continue;
        }
        break;
    }
    lexCLike(src, nameStart, j, cSharpSpec(), out);
    return j;
}

bool nameEquals(const std::string& src, size_t start, size_t length, const char* name) {
    return wordEquals(src, start, length, name, true);
}

void lexMarkup(const std::string& src, size_t begin, size_t end, bool razor,
               std::vector<Token>& out) {
    size_t i = begin;
    while (i < end) {
        char c = src[i];

        if (c == '<' && src.compare(i, 4, "<!--") == 0) {
            size_t stop = scanBlockComment(src, i + 4, end, "-->");
            emit(out, i, stop - i, TokenType::Comment);
            i = stop;
            continue;
        }
        if (c == '<' && src.compare(i, 9, "<![CDATA[") == 0) {
            size_t stop = scanBlockComment(src, i + 9, end, "]]>");
            emit(out, i, stop - i, TokenType::String);
            i = stop;
            continue;
        }
        if (c == '<' && i + 1 < end && (src[i + 1] == '!' || src[i + 1] == '?')) {
            size_t j = i;
            while (j < end && src[j] != '>') ++j;
            if (j < end) ++j;
            emit(out, i, j - i, TokenType::Preprocessor);
            i = j;
            continue;
        }
        if (c == '<' && i + 1 < end && (isIdentStart(src[i + 1]) || src[i + 1] == '/')) {
            size_t nameStart = i + 1;
            if (src[nameStart] == '/') ++nameStart;
            size_t nameEnd = nameStart;
            while (nameEnd < end && (isIdentChar(src[nameEnd]) || src[nameEnd] == '-' ||
                                     src[nameEnd] == ':' || src[nameEnd] == '.')) {
                ++nameEnd;
            }
            emit(out, i, nameEnd - i, TokenType::Tag);

            bool closingTag = src[i + 1] == '/';
            size_t tagNameStart = nameStart;
            size_t tagNameLength = nameEnd - nameStart;

            // Attributes up to the end of the tag.
            size_t j = nameEnd;
            while (j < end) {
                if (src[j] == '>') {
                    emit(out, j, 1, TokenType::Tag);
                    ++j;
                    break;
                }
                if (src[j] == '/' && j + 1 < end && src[j + 1] == '>') {
                    emit(out, j, 2, TokenType::Tag);
                    j += 2;
                    break;
                }
                if (razor && src[j] == '@') {
                    j = lexRazorTransition(src, j, end, out);
                    continue;
                }
                if (src[j] == '"' || src[j] == '\'') {
                    size_t stop = scanQuoted(src, j, end, src[j], false, false);
                    emit(out, j, stop - j, TokenType::String);
                    j = stop;
                    continue;
                }
                if (isIdentStart(src[j])) {
                    size_t k = j;
                    while (k < end && (isIdentChar(src[k]) || src[k] == '-' || src[k] == ':' ||
                                       src[k] == '.')) {
                        ++k;
                    }
                    emit(out, j, k - j, TokenType::Attribute);
                    j = k;
                    continue;
                }
                ++j;
            }
            i = j;

            // Embedded script and style bodies get their own lexer.
            if (!closingTag && (nameEquals(src, tagNameStart, tagNameLength, "script") ||
                                nameEquals(src, tagNameStart, tagNameLength, "style"))) {
                bool script = nameEquals(src, tagNameStart, tagNameLength, "script");
                const char* closing = script ? "</script" : "</style";
                size_t closeAt = i;
                size_t closingLength = std::strlen(closing);
                while (closeAt + closingLength <= end) {
                    bool match = true;
                    for (size_t k = 0; k < closingLength; ++k) {
                        if (lower(src[closeAt + k]) != closing[k]) {
                            match = false;
                            break;
                        }
                    }
                    if (match) break;
                    ++closeAt;
                }
                if (closeAt + closingLength > end) closeAt = end;
                if (script) {
                    lexCLike(src, i, closeAt, javaScriptSpec(), out);
                } else {
                    lexCss(src, i, closeAt, out);
                }
                i = closeAt;
            }
            continue;
        }
        if (c == '&') {
            size_t j = i + 1;
            if (j < end && src[j] == '#') ++j;
            while (j < end && isIdentChar(src[j])) ++j;
            if (j < end && src[j] == ';' && j > i + 1) {
                emit(out, i, j - i + 1, TokenType::Number);
                i = j + 1;
                continue;
            }
        }
        if (razor && c == '@') {
            i = lexRazorTransition(src, i, end, out);
            continue;
        }
        ++i;
    }
}

std::string escapeForDump(const std::string& text) {
    std::string out;
    for (char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

char dumpLetter(TokenType type) {
    switch (type) {
        case TokenType::Keyword: return 'k';
        case TokenType::Type: return 'y';
        case TokenType::String: return 's';
        case TokenType::Number: return 'n';
        case TokenType::Comment: return 'c';
        case TokenType::Preprocessor: return 'p';
        case TokenType::Tag: return 'g';
        case TokenType::Attribute: return 'a';
        case TokenType::Function: return 'f';
        default: return 't';
    }
}

} // namespace

Language languageFromInfo(const std::string& info) {
    std::string name;
    for (char c : info) {
        if (isSpace(c) || c == ',') break;
        name.push_back(lower(c));
    }
    if (name.empty()) return Language::None;

    struct Alias {
        const char* name;
        Language language;
    };
    static const Alias kAliases[] = {
        {"html", Language::Markup},      {"htm", Language::Markup},
        {"xhtml", Language::Markup},     {"xml", Language::Markup},
        {"svg", Language::Markup},       {"xaml", Language::Markup},
        {"axml", Language::Markup},      {"axaml", Language::Markup},
        {"plist", Language::Markup},     {"csproj", Language::Markup},
        {"config", Language::Markup},    {"aspx", Language::Markup},
        {"ascx", Language::Markup},      {"asax", Language::Markup},
        {"asp", Language::Markup},       {"jsp", Language::Markup},
        {"vue", Language::Markup},       {"svelte", Language::Markup},
        {"rss", Language::Markup},       {"xsl", Language::Markup},
        {"xslt", Language::Markup},      {"wsdl", Language::Markup},

        {"cshtml", Language::Razor},     {"razor", Language::Razor},
        {"vbhtml", Language::Razor},

        {"css", Language::Css},          {"scss", Language::Css},
        {"sass", Language::Css},         {"less", Language::Css},

        {"js", Language::JavaScript},    {"javascript", Language::JavaScript},
        {"jsx", Language::JavaScript},   {"mjs", Language::JavaScript},
        {"cjs", Language::JavaScript},   {"ts", Language::JavaScript},
        {"tsx", Language::JavaScript},   {"typescript", Language::JavaScript},
        {"json", Language::JavaScript},

        {"cs", Language::CSharp},        {"c#", Language::CSharp},
        {"csharp", Language::CSharp},    {"dotnet", Language::CSharp},

        {"java", Language::Java},        {"android", Language::Java},

        {"swift", Language::Swift},      {"ios", Language::Swift},

        {"py", Language::Python},        {"python", Language::Python},
        {"python3", Language::Python},

        {"sql", Language::Sql},
        {"tsql", Language::TransactSql}, {"t-sql", Language::TransactSql},
        {"mssql", Language::TransactSql},{"sqlserver", Language::TransactSql},
        {"pgsql", Language::PostgresSql},{"postgres", Language::PostgresSql},
        {"postgresql", Language::PostgresSql},
        {"plpgsql", Language::PostgresSql},
    };

    for (const Alias& alias : kAliases) {
        if (name == alias.name) return alias.language;
    }
    return Language::None;
}

std::vector<Token> tokenize(Language language, const std::string& code) {
    std::vector<Token> classified;

    switch (language) {
        case Language::Markup:
            lexMarkup(code, 0, code.size(), false, classified);
            break;
        case Language::Razor:
            lexMarkup(code, 0, code.size(), true, classified);
            break;
        case Language::Css:
            lexCss(code, 0, code.size(), classified);
            break;
        case Language::JavaScript:
            lexCLike(code, 0, code.size(), javaScriptSpec(), classified);
            break;
        case Language::CSharp:
            lexCLike(code, 0, code.size(), cSharpSpec(), classified);
            break;
        case Language::Java:
            lexCLike(code, 0, code.size(), javaSpec(), classified);
            break;
        case Language::Swift:
            lexCLike(code, 0, code.size(), swiftSpec(), classified);
            break;
        case Language::Python:
            lexPython(code, 0, code.size(), classified);
            break;
        case Language::Sql:
            lexSql(code, 0, code.size(), SqlDialect::Generic, classified);
            break;
        case Language::TransactSql:
            lexSql(code, 0, code.size(), SqlDialect::Transact, classified);
            break;
        case Language::PostgresSql:
            lexSql(code, 0, code.size(), SqlDialect::Postgres, classified);
            break;
        default:
            break;
    }

    std::stable_sort(classified.begin(), classified.end(),
                     [](const Token& a, const Token& b) { return a.start < b.start; });

    std::vector<Token> tokens;
    tokens.reserve(classified.size() * 2 + 1);
    size_t cursor = 0;
    for (const Token& token : classified) {
        if (token.start < cursor) continue; // defensive: drop overlaps
        if (token.start > cursor) {
            emit(tokens, cursor, token.start - cursor, TokenType::Text);
        }
        tokens.push_back(token);
        cursor = token.start + token.length;
    }
    if (cursor < code.size()) {
        emit(tokens, cursor, code.size() - cursor, TokenType::Text);
    }
    return tokens;
}

std::string dumpTokens(Language language, const std::string& code) {
    std::string out;
    for (const Token& token : tokenize(language, code)) {
        if (token.type == TokenType::Text) continue;
        if (!out.empty()) out.push_back(' ');
        out.push_back(dumpLetter(token.type));
        out += ":\"";
        out += escapeForDump(code.substr(token.start, token.length));
        out += "\"";
    }
    return out;
}

} // namespace syntax
