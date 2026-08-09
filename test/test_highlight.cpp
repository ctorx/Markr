// Syntax highlighting requirements, per supported language family.
#include "../src/highlight.h"
#include "test_framework.h"

namespace {

// Classified tokens only; unclassified text is asserted by omission.
std::string H(syntax::Language language, const std::string& code) {
    return syntax::dumpTokens(language, code);
}

const syntax::Language kMarkup = syntax::Language::Markup;
const syntax::Language kRazor = syntax::Language::Razor;
const syntax::Language kCss = syntax::Language::Css;
const syntax::Language kJs = syntax::Language::JavaScript;
const syntax::Language kCs = syntax::Language::CSharp;
const syntax::Language kJava = syntax::Language::Java;
const syntax::Language kSwift = syntax::Language::Swift;
const syntax::Language kPy = syntax::Language::Python;
const syntax::Language kTsql = syntax::Language::TransactSql;
const syntax::Language kPg = syntax::Language::PostgresSql;

} // namespace

TEST(Languages, RecognisesTheSupportedInfoStrings) {
    CHECK_TRUE(syntax::languageFromInfo("html") == kMarkup);
    CHECK_TRUE(syntax::languageFromInfo("xml") == kMarkup);
    CHECK_TRUE(syntax::languageFromInfo("svg") == kMarkup);
    CHECK_TRUE(syntax::languageFromInfo("xaml") == kMarkup);
    CHECK_TRUE(syntax::languageFromInfo("aspx") == kMarkup);
    CHECK_TRUE(syntax::languageFromInfo("cshtml") == kRazor);
    CHECK_TRUE(syntax::languageFromInfo("razor") == kRazor);
    CHECK_TRUE(syntax::languageFromInfo("css") == kCss);
    CHECK_TRUE(syntax::languageFromInfo("scss") == kCss);
    CHECK_TRUE(syntax::languageFromInfo("js") == kJs);
    CHECK_TRUE(syntax::languageFromInfo("javascript") == kJs);
    CHECK_TRUE(syntax::languageFromInfo("jsx") == kJs);
    CHECK_TRUE(syntax::languageFromInfo("ts") == kJs);
    CHECK_TRUE(syntax::languageFromInfo("cs") == kCs);
    CHECK_TRUE(syntax::languageFromInfo("c#") == kCs);
    CHECK_TRUE(syntax::languageFromInfo("csharp") == kCs);
    CHECK_TRUE(syntax::languageFromInfo("java") == kJava);
    CHECK_TRUE(syntax::languageFromInfo("swift") == kSwift);
    CHECK_TRUE(syntax::languageFromInfo("python") == kPy);
    CHECK_TRUE(syntax::languageFromInfo("py") == kPy);
    CHECK_TRUE(syntax::languageFromInfo("tsql") == kTsql);
    CHECK_TRUE(syntax::languageFromInfo("pgsql") == kPg);
    CHECK_TRUE(syntax::languageFromInfo("postgresql") == kPg);
    CHECK_TRUE(syntax::languageFromInfo("sql") == syntax::Language::Sql);
}

TEST(Languages, IsCaseInsensitiveAndIgnoresTrailingAttributes) {
    CHECK_TRUE(syntax::languageFromInfo("JS") == kJs);
    CHECK_TRUE(syntax::languageFromInfo("C#") == kCs);
    CHECK_TRUE(syntax::languageFromInfo("html title=index.html") == kMarkup);
}

TEST(Languages, UnknownLanguageIsNotHighlighted) {
    CHECK_TRUE(syntax::languageFromInfo("brainfuck") == syntax::Language::None);
    CHECK_TRUE(syntax::languageFromInfo("") == syntax::Language::None);
    CHECK_EQ(H(syntax::Language::None, "int x = 1;"), "");
}

TEST(Tokens, CoverTheWholeInputWithoutGaps) {
    std::string code = "int x = 1; // done";
    std::vector<syntax::Token> tokens = syntax::tokenize(kCs, code);
    size_t cursor = 0;
    for (const syntax::Token& token : tokens) {
        CHECK_EQ(token.start, cursor);
        cursor += token.length;
    }
    CHECK_EQ(cursor, code.size());
}

// ------------------------------------------------------------------ C family

TEST(CSharp, KeywordsTypesAndLiterals) {
    CHECK_EQ(H(kCs, "public int x = 1;"), "k:\"public\" y:\"int\" n:\"1\"");
    CHECK_EQ(H(kCs, "var s = \"hi\";"), "k:\"var\" s:\"\\\"hi\\\"\"");
    CHECK_EQ(H(kCs, "// note"), "c:\"// note\"");
    CHECK_EQ(H(kCs, "/* a\nb */"), "c:\"/* a\\nb */\"");
}

TEST(CSharp, VerbatimAndInterpolatedStrings) {
    CHECK_EQ(H(kCs, "var p = @\"C:\\temp\";"), "k:\"var\" s:\"@\\\"C:\\\\temp\\\"\"");
    CHECK_EQ(H(kCs, "var s = $\"x{y}\";"), "k:\"var\" s:\"$\\\"x{y}\\\"\"");
}

TEST(CSharp, PreprocessorDirectives) {
    CHECK_EQ(H(kCs, "#region Setup"), "p:\"#region Setup\"");
    CHECK_EQ(H(kCs, "#if DEBUG"), "p:\"#if DEBUG\"");
}

TEST(CSharp, CallsAreMarkedAsFunctions) {
    CHECK_EQ(H(kCs, "Console.WriteLine(x);"), "f:\"WriteLine\"");
}

TEST(JavaScript, KeywordsStringsAndTemplates) {
    CHECK_EQ(H(kJs, "const a = 1;"), "k:\"const\" n:\"1\"");
    CHECK_EQ(H(kJs, "let s = 'hi';"), "k:\"let\" s:\"'hi'\"");
    CHECK_EQ(H(kJs, "let s = `a${b}c`;"), "k:\"let\" s:\"`a${b}c`\"");
    CHECK_EQ(H(kJs, "async function go() {}"), "k:\"async\" k:\"function\" f:\"go\"");
}

TEST(JavaScript, TypeScriptKeywordsAreRecognised) {
    CHECK_EQ(H(kJs, "interface A { x: number }"), "k:\"interface\" y:\"number\"");
}

TEST(Java, KeywordsAnnotationsAndTypes) {
    CHECK_EQ(H(kJava, "@Override public void run() {}"),
             "p:\"@Override\" k:\"public\" y:\"void\" f:\"run\"");
    CHECK_EQ(H(kJava, "final int n = 0x1F;"), "k:\"final\" y:\"int\" n:\"0x1F\"");
}

TEST(Java, AndroidStyleSourceHighlights) {
    CHECK_EQ(H(kJava, "class MainActivity extends Activity {}"),
             "k:\"class\" k:\"extends\"");
}

TEST(Swift, KeywordsAttributesAndStrings) {
    CHECK_EQ(H(kSwift, "let name: String = \"a\""), "k:\"let\" y:\"String\" s:\"\\\"a\\\"\"");
    CHECK_EQ(H(kSwift, "@objc func viewDidLoad() {}"),
             "p:\"@objc\" k:\"func\" f:\"viewDidLoad\"");
    CHECK_EQ(H(kSwift, "guard let x = y else { return }"),
             "k:\"guard\" k:\"let\" k:\"else\" k:\"return\"");
}

// -------------------------------------------------------------------- Python

TEST(Python, KeywordsCommentsAndStrings) {
    CHECK_EQ(H(kPy, "def go(x):"), "k:\"def\" f:\"go\"");
    CHECK_EQ(H(kPy, "# note"), "c:\"# note\"");
    CHECK_EQ(H(kPy, "s = 'hi'"), "s:\"'hi'\"");
    CHECK_EQ(H(kPy, "s = \"\"\"a\nb\"\"\""), "s:\"\\\"\\\"\\\"a\\nb\\\"\\\"\\\"\"");
}

TEST(Python, DecoratorsAndBuiltins) {
    CHECK_EQ(H(kPy, "@staticmethod"), "p:\"@staticmethod\"");
    CHECK_EQ(H(kPy, "x = len(items)"), "y:\"len\"");
    CHECK_EQ(H(kPy, "if x is None:"), "k:\"if\" k:\"is\" k:\"None\"");
}

TEST(Python, FStringsAndRawStrings) {
    CHECK_EQ(H(kPy, "s = f'{a}'"), "s:\"f'{a}'\"");
    CHECK_EQ(H(kPy, "s = r'\\d+'"), "s:\"r'\\\\d+'\"");
}

// ----------------------------------------------------------------------- SQL

TEST(Sql, KeywordsAreCaseInsensitive) {
    CHECK_EQ(H(kTsql, "select * from Users"), "k:\"select\" k:\"from\"");
    CHECK_EQ(H(kTsql, "SELECT * FROM Users"), "k:\"SELECT\" k:\"FROM\"");
}

TEST(Sql, CommentsAndStrings) {
    CHECK_EQ(H(kTsql, "-- note"), "c:\"-- note\"");
    CHECK_EQ(H(kTsql, "/* note */"), "c:\"/* note */\"");
    CHECK_EQ(H(kTsql, "where n = 'a''b'"), "k:\"where\" s:\"'a''b'\"");
}

TEST(TransactSql, VariablesAndBracketedIdentifiers) {
    CHECK_EQ(H(kTsql, "declare @id int"), "k:\"declare\" p:\"@id\" y:\"int\"");
    CHECK_EQ(H(kTsql, "select @@ROWCOUNT"), "k:\"select\" p:\"@@ROWCOUNT\"");
    CHECK_EQ(H(kTsql, "from [dbo].[Users]"), "k:\"from\"");
}

TEST(PostgresSql, DollarQuotedBodySpansLines) {
    CHECK_EQ(H(kPg, "AS $$\nBEGIN\nEND;\n$$ LANGUAGE plpgsql"),
             "k:\"AS\" s:\"$$\\nBEGIN\\nEND;\\n$$\" k:\"LANGUAGE\" k:\"plpgsql\"");
}

TEST(PostgresSql, DollarQuotingAndTypes) {
    CHECK_EQ(H(kPg, "select 'a'::jsonb"), "k:\"select\" s:\"'a'\" y:\"jsonb\"");
    CHECK_EQ(H(kPg, "as $$ begin end $$"), "k:\"as\" s:\"$$ begin end $$\"");
    CHECK_EQ(H(kPg, "returning id"), "k:\"returning\"");
}

// ----------------------------------------------------------------------- CSS

TEST(Css, SelectorsPropertiesAndValues) {
    CHECK_EQ(H(kCss, ".btn { color: #fff; }"), "g:\".btn\" a:\"color\" n:\"#fff\"");
    CHECK_EQ(H(kCss, "/* note */"), "c:\"/* note */\"");
    CHECK_EQ(H(kCss, "@media screen {}"), "k:\"@media\" g:\"screen\"");
    CHECK_EQ(H(kCss, "a { width: 10px !important; }"),
             "g:\"a\" a:\"width\" n:\"10\" k:\"!important\"");
}

// -------------------------------------------------------------------- Markup

TEST(Markup, TagsAttributesAndValues) {
    CHECK_EQ(H(kMarkup, "<a href=\"x\">t</a>"),
             "g:\"<a\" a:\"href\" s:\"\\\"x\\\"\" g:\">\" g:\"</a\" g:\">\"");
}

TEST(Markup, CommentsDoctypeAndEntities) {
    CHECK_EQ(H(kMarkup, "<!-- hi -->"), "c:\"<!-- hi -->\"");
    CHECK_EQ(H(kMarkup, "<!DOCTYPE html>"), "p:\"<!DOCTYPE html>\"");
    CHECK_EQ(H(kMarkup, "a &amp; b"), "n:\"&amp;\"");
    CHECK_EQ(H(kMarkup, "<?xml version=\"1.0\"?>"), "p:\"<?xml version=\\\"1.0\\\"?>\"");
}

TEST(Markup, XmlDialectsUseTheSameRules) {
    CHECK_EQ(H(kMarkup, "<Button Text=\"Go\" />"),
             "g:\"<Button\" a:\"Text\" s:\"\\\"Go\\\"\" g:\"/>\"");
}

TEST(Markup, EmbeddedScriptAndStyleAreHighlighted) {
    CHECK_EQ(H(kMarkup, "<script>var a = 1;</script>"),
             "g:\"<script\" g:\">\" k:\"var\" n:\"1\" g:\"</script\" g:\">\"");
    CHECK_EQ(H(kMarkup, "<style>a { color: red; }</style>"),
             "g:\"<style\" g:\">\" g:\"a\" a:\"color\" g:\"</style\" g:\">\"");
}

// -------------------------------------------------------------------- Razor

TEST(Razor, DirectivesAndCodeBlocks) {
    CHECK_EQ(H(kRazor, "@page \"/x\""), "p:\"@page\" s:\"\\\"/x\\\"\"");
    CHECK_EQ(H(kRazor, "@model IndexModel"), "p:\"@model\"");
    CHECK_EQ(H(kRazor, "@{ var x = 1; }"), "p:\"@\" k:\"var\" n:\"1\"");
}

TEST(Razor, ExpressionsAndComments) {
    CHECK_EQ(H(kRazor, "<p>@Model.Name</p>"),
             "g:\"<p\" g:\">\" p:\"@\" g:\"</p\" g:\">\"");
    CHECK_EQ(H(kRazor, "@* hidden *@"), "c:\"@* hidden *@\"");
    CHECK_EQ(H(kRazor, "@@x"), "p:\"@@\"");
}

TEST(Razor, ControlFlowKeywordsAndMarkupAroundThem) {
    CHECK_EQ(H(kRazor, "@if (a) { <b>x</b> }"),
             "p:\"@\" k:\"if\" g:\"<b\" g:\">\" g:\"</b\" g:\">\"");
}

TEST(Razor, CodeSectionUsesCSharpRules) {
    CHECK_EQ(H(kRazor, "@code { int n = 2; }"), "p:\"@\" k:\"code\" y:\"int\" n:\"2\"");
}
