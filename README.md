# Simple Markdown Viewer

A fast, read-only Markdown viewer for Windows. Native C++ and Win32/GDI, no
frameworks and no runtime dependencies — the build produces one self-contained
`.exe` that starts in well under a second.

It renders Markdown; it never shows the source and never lets you edit it.

## Features

- **Full Markdown rendering** — CommonMark plus the GitHub extensions
  (see the table below)
- **Syntax highlighting** in fenced code blocks, for the languages listed below
- **Drop a file on the window** to open it
- **Reloads on change** — edit in another editor and the view follows, keeping
  your scroll position (`F5` forces it)
- **Working links** — in-document `#anchors`, links to other Markdown files, and
  `Alt+Left` / the mouse back button to retrace
- **Zoom** — `Ctrl`+scroll or `Ctrl`+`+`/`-`, remembered between runs
- **Follows the system theme** — light and dark, switching live when Windows does,
  title bar and menu strip included
- **Resize grip** in the bottom-right corner, and the usual drag borders
- **Reflows on resize** — narrowing the window rewraps the text; there is never a
  horizontal scrollbar, not even for long code lines or wide tables
- **Document outline** — a collapsible side panel listing the headings in their
  hierarchy; click one to jump, and the current section stays highlighted
- **Remembers where you left it** — window position, size, maximised state and
  the outline panel's state are restored on the next run
- **Notepad-style search** — hidden until you want it: `Ctrl+F` or the magnifier
  at the right of the menu bar drops the bar down, with find next/previous and
  wrap-around
- **Select and copy** — mouse selection of the *rendered* text; `Edit → Copy` is
  enabled only when something is selected
- **Copy as Markdown or formatted text** — right-click a selection for
  **Copy Markdown** (the markup behind what you selected) or **Copy Formatted**
  (rich text, so a paste into Word or Outlook keeps the headings, emphasis,
  lists and tables)
- **Clickable links** — opens in the default browser
- **High-DPI aware**, per-monitor v2

### Markdown coverage

| Area | Supported |
| --- | --- |
| Headings | ATX (`#`…`######`), closing sequences, setext (`===`, `---`) |
| Paragraphs | soft wrapping, lazy continuation, hard breaks (two spaces or `\`) |
| Emphasis | `*em*`, `_em_`, `**strong**`, `***both***`, full CommonMark delimiter rules |
| Extensions | `~~strikethrough~~`, `==highlight==`, task lists |
| Code | fenced (backtick or tilde, with language info), indented, inline code spans |
| Lists | bullet, ordered (any start, `.` or `)`), nested, tight vs loose, task items |
| Quotes | nesting, lazy continuation, any block inside |
| Tables | GFM pipe tables, per-column alignment, ragged rows, wrapped cells |
| Links | inline, reference (full/collapsed/shortcut), autolinks, bare URLs, emails |
| Images | local files decoded via WIC; alt text shown when the file is missing |
| Footnotes | `[^id]` references and definitions, numbered in reference order |
| Raw HTML | block elements render their text; `<b> <i> <u> <s> <mark> <sub> <sup> <code> <br>` are honoured inline, other tags stripped |
| Misc | backslash escapes, HTML entities, YAML front matter, thematic breaks |

### Syntax highlighting

Fenced blocks are highlighted when the info string names a supported language.
Anything else renders as plain code.

| Family | Fence tags |
| --- | --- |
| Markup | `html` `htm` `xhtml` `xml` `svg` `xaml` `axml` `axaml` `plist` `csproj` `config` `aspx` `ascx` `asax` `jsp` `vue` `svelte` `xsl` `xslt` `rss` `wsdl` |
| Razor / ASP.NET | `cshtml` `razor` `vbhtml` — markup with C# transitions, `@{ }` blocks, `@code`, directives and `@* comments *@` |
| CSS | `css` `scss` `sass` `less` |
| JavaScript | `js` `javascript` `jsx` `mjs` `cjs` `ts` `tsx` `typescript` `json` |
| C# | `cs` `c#` `csharp` `dotnet` |
| Java | `java` `android` |
| Swift | `swift` `ios` |
| Python | `py` `python` `python3` |
| SQL | `sql` (neutral), `tsql` `t-sql` `mssql` `sqlserver`, `pgsql` `postgres` `postgresql` `plpgsql` |

Dialect details are handled: C# verbatim/interpolated strings and `#directives`,
JavaScript template literals, Python decorators and triple-quoted/f-strings,
Java and Swift annotations, T-SQL `@variables` and `[bracketed]` identifiers,
PostgreSQL `$$ dollar quoting $$` and `::casts`, and HTML with embedded
`<script>` and `<style>` bodies lexed as JavaScript and CSS.

## Building

Requires Visual Studio (or the Build Tools) with **Desktop development with C++**.
No other dependencies.

```powershell
.\build.ps1                    # run the tests, then build build\SimpleMarkdownViewer.exe
.\build.ps1 -SkipTests         # build only
.\build.ps1 -DebugBuild        # unoptimised build with debug info
.\build.ps1 -Output C:\Tools\smv.exe -Run
```

The script locates the toolchain itself (`vswhere`, then well-known install
paths), links the CRT statically and embeds the manifest, so the result is a
single portable binary.

## Testing

```powershell
.\test\run-tests.ps1           # all suites
.\test\run-tests.ps1 -Filter Tables
```

The parser, layout engine and search are free of Windows dependencies, so the
suite drives them directly with a synthetic text measurer. The markdown
behaviour was specified as tests first: `test\test_blocks.cpp`,
`test_inline.cpp`, `test_lists.cpp`, `test_links.cpp`, `test_tables.cpp` assert
against a structural dump of the parsed document, and `test_layout.cpp` /
`test_search.cpp` cover wrapping, padding, styling, selection and find.

`test\fixtures\showcase.md` exercises every rendered feature in one document and
`test\fixtures\syntax.md` covers every highlighted language plus in-document and
cross-file links — open them after a change to eyeball the result.

## Usage

| Action | How |
| --- | --- |
| Open a file | drop it on the window, `File → Open`, `Ctrl+O`, or pass a path on the command line |
| Reload | `F5` (external edits are picked up on their own, within a second) |
| Zoom | `Ctrl`+scroll, `Ctrl`+`+` / `Ctrl`+`-`, `Ctrl`+`0` to reset |
| Follow a link | click it — headings and other Markdown files open in place, everything else in your browser |
| Back / forward | `Alt+Left` / `Alt+Right`, `Backspace`, or the mouse thumb buttons |
| Show the outline | click the chevron at the top of the left strip; click it again to collapse |
| Jump to a heading | click it in the outline |
| Copy | select with the mouse, then `Edit → Copy` or `Ctrl+C` (`Ctrl+A` selects all) |
| Copy Markdown / Formatted | right-click the selection (or `Shift+F10`) and pick one |
| Find | `Ctrl+F` or the magnifier opens the box and focuses it; `Enter` or the **Search** button searches |
| Find next / previous | `F3` / `Shift+F3` (also `Enter` / `Shift+Enter` in the box) |
| Close the search box | `Esc`, or the magnifier again |
| Scroll | wheel, scrollbar, arrows, `PgUp`/`PgDn`, `Home`/`End` |
| Project page | `About → Simple Markdown Viewer` |

## Layout of the source

```
src\md_types.h      document model and the parse/dump entry points
src\md_parser.cpp   block and inline parser (CommonMark + GFM extensions)
src\md_debug.cpp    structural dump used by the tests
src\layout.h/.cpp   width-driven layout: runs, decorations, hit testing, selection
src\md_export.*     rebuilds markdown or HTML for a selected range of the view
src\highlight.*     per-language lexers for fenced code blocks
src\search.h/.cpp   find-all plus next/previous with wrap-around
src\win_theme.*     light/dark palettes and OS dark-mode integration
src\win_text.*      GDI font cache (text measurement) and WIC image loading
src\win_chrome.*    custom title bar (padded caption + buttons) and menu strip
src\win_outline.*   collapsible heading-outline panel
src\win_settings.*  window placement and panel state persistence (HKCU)
src\win_viewer.*    document view window and the frame that hosts menu + search bar
src\main.cpp        entry point
```

Window placement, outline state and zoom live in
`HKCU\Software\Simple Markdown Viewer`; delete that key to get the defaults
back. The outline starts collapsed on first run.

The open document is polled once a second for external edits — a single
`GetFileAttributesEx` call, so an idle viewer costs nothing measurable.

## The window frame

The title bar is drawn by the application, not by Windows: `WM_NCCALCSIZE`
hands the caption strip to the client area and `src\win_chrome.cpp` paints it.
That is the only way to control the padding around the filename and the
minimise / maximise / close buttons, which is why the menu bar is custom drawn
too — the system draws it in the same non-client space.

Everything the standard frame does still works, through hit testing: drag to
move, double-click to maximise, right-click for the system menu, Aero Snap,
snap layouts on the maximise button, and the resize borders. `Alt` reveals the
menu mnemonics, `Alt+F` / `Alt+E` / `Alt+A` and `F10` open the menus.

## Notes and limits

- Remote images (`http://`, `https://`, `data:`) are not fetched; their alt text
  is rendered instead. Local paths, relative to the document, are decoded with WIC.
- Raw HTML is deliberately not laid out as HTML — this is a Markdown viewer, not
  a browser. Block tags contribute their text content; the inline tags listed
  above map onto the equivalent Markdown styling.
- Copying a wrapped paragraph yields the logical (unwrapped) text.
- **Copy Markdown** rebuilds the markup from the parsed document rather than
  slicing the file, so it is canonical Markdown: emphasis always comes back as
  `*this*`, list indentation is normalised, and reference links come back
  inline. It says the same thing as the source, not always in the same
  characters.
