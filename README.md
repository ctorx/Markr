# Simple Markdown Viewer

A fast, read-only Markdown viewer for Windows. Native C++ and Win32/GDI, no
frameworks and no runtime dependencies — the build produces one self-contained
`.exe` that starts in well under a second.

It renders Markdown; it never shows the source and never lets you edit it.

## Features

- **Full Markdown rendering** — CommonMark plus the GitHub extensions
  (see the table below)
- **Follows the system theme** — light and dark, switching live when Windows does
- **Reflows on resize** — narrowing the window rewraps the text; there is never a
  horizontal scrollbar, not even for long code lines or wide tables
- **Document outline** — a collapsible side panel listing the headings in their
  hierarchy; click one to jump, and the current section stays highlighted
- **Remembers where you left it** — window position, size, maximised state and
  the outline panel's state are restored on the next run
- **Notepad-style search** — a search bar under the menu with find next/previous
  and wrap-around
- **Select and copy** — mouse selection of the *rendered* text; `Edit → Copy` is
  enabled only when something is selected
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

`test\fixtures\showcase.md` exercises every rendered feature in one document —
open it after a change to eyeball the result.

## Usage

| Action | How |
| --- | --- |
| Open a file | `File → Open`, `Ctrl+O`, or pass a path on the command line |
| Show the outline | click the chevron at the top of the left strip; click it again to collapse |
| Jump to a heading | click it in the outline |
| Copy | select with the mouse, then `Edit → Copy` or `Ctrl+C` (`Ctrl+A` selects all) |
| Find | type in the search box, then `Enter` or the **Search** button |
| Find next / previous | `F3` / `Shift+F3` (also `Enter` / `Shift+Enter` in the box) |
| Focus the search box | `Ctrl+F` |
| Scroll | wheel, scrollbar, arrows, `PgUp`/`PgDn`, `Home`/`End` |
| Project page | `About → Simple Markdown Viewer` |

## Layout of the source

```
src\md_types.h      document model and the parse/dump entry points
src\md_parser.cpp   block and inline parser (CommonMark + GFM extensions)
src\md_debug.cpp    structural dump used by the tests
src\layout.h/.cpp   width-driven layout: runs, decorations, hit testing, selection
src\search.h/.cpp   find-all plus next/previous with wrap-around
src\win_theme.*     light/dark palettes and OS dark-mode integration
src\win_text.*      GDI font cache (text measurement) and WIC image loading
src\win_outline.*   collapsible heading-outline panel
src\win_settings.*  window placement and panel state persistence (HKCU)
src\win_viewer.*    document view window and the frame that hosts menu + search bar
src\main.cpp        entry point
```

Window state lives in `HKCU\Software\Simple Markdown Viewer`; delete that key to
get the defaults back. The outline starts collapsed on first run.

## Notes and limits

- Remote images (`http://`, `https://`, `data:`) are not fetched; their alt text
  is rendered instead. Local paths, relative to the document, are decoded with WIC.
- Raw HTML is deliberately not laid out as HTML — this is a Markdown viewer, not
  a browser. Block tags contribute their text content; the inline tags listed
  above map onto the equivalent Markdown styling.
- Copying a wrapped paragraph yields the logical (unwrapped) text.
