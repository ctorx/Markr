# Markr Color Palette

The palette Markr uses in both themes, for reuse in other apps. It is based on
VS Code's default **Dark+** and **Light+** themes: the reading view mirrors the
VS Code *markdown preview*, the edit mode mirrors the VS Code *editor*, and the
window chrome is Markr's own neutral gray set.

Some values are opacity blends from VS Code's stylesheets, pre-flattened here
into solid colors (the formula is noted so you can re-blend over a different
background: `result = alpha * color + (1 - alpha) * background`).

Fonts, for completeness: reading view is Segoe UI 14px (headings at
2em / 1.5em / 1.25em / 1em / 0.875em / 0.85em, weight 600); all code and the
editor are Consolas 14px.

## Reading view (VS Code markdown preview)

| Role | Dark | Light | Source |
| --- | --- | --- | --- |
| Background | `#1E1E1E` | `#FFFFFF` | editor.background |
| Text / headings / quotes | `#D4D4D4` | `#000000` | editor.foreground |
| Muted (secondary text) | `#9D9D9D` | `#6E6E6E` | Markr |
| Link | `#3794FF` | `#006AB1` | textLink.foreground |
| Code block background | `#161616` | `#F1F1F1` | textCodeBlock.background: 40% `#0A0A0A` (dark) / 40% `#DCDCDC` (light) over Background |
| Inline code chip | `#343434` | `#E5E5E5` | Markr: 10% foreground over Background (VS Code has none) |
| Quote bar | `#0F4C75` | `#80BDE6` | textBlockQuote.border: 50% `#007ACC` over Background |
| Rule / table borders / h1-h2 underline | `#464646` | `#D1D1D1` | markdown.css: 18% white (dark) / 18% black (light) over Background |
| Table header background | none | none | markdown.css (header is bold text + border only) |
| Highlight `==mark==` | `#FFFF00` on `#000000` | `#FFFF00` on `#000000` | browser default `<mark>` |
| Checkbox border | `#9D9D9D` | `#6E6E6E` | Markr |
| Selection | `#264F78` | `#ADD6FF` | editor.selectionBackground |
| Search match | `#613214` | `#F8C9AB` | 33% `#EA5C00` over Background (findMatchHighlight) |
| Current search match | `#515C6A` | `#A8AC94` | findMatchBackground |

## Syntax tokens (fenced code, both view and editor)

VS Code Dark+ / Light+ token colors.

| Token | Dark | Light |
| --- | --- | --- |
| Plain text | `#D4D4D4` | `#000000` |
| Keyword | `#569CD6` | `#0000FF` |
| Type | `#4EC9B0` | `#267F99` |
| String | `#CE9178` | `#A31515` |
| Number | `#B5CEA8` | `#098658` |
| Comment | `#6A9955` | `#008000` |
| Preprocessor / directive | `#C586C0` | `#AF00DB` |
| Tag | `#569CD6` | `#800000` |
| Attribute | `#9CDCFE` | `#E50000` |
| Function | `#DCDCAA` | `#795E26` |

## Edit mode: markdown source styling (VS Code editor)

The editing surface uses the editor colors above (Background `#1E1E1E` /
`#FFFFFF`, Text `#D4D4D4` / `#000000`) plus these markdown token styles:

| Markdown element | Dark | Light | Style |
| --- | --- | --- | --- |
| Line numbers | `#858585` | `#237893` | — |
| Heading line | `#569CD6` | `#800000` | bold |
| `**bold**` | `#569CD6` | `#000080` | bold |
| `*italic*` | `#C586C0` | `#800080` | italic |
| `~~strikethrough~~` | inherit | inherit | strikethrough |
| Inline code / plain fenced block | `#CE9178` | `#800000` | — |
| List / task markers | `#6796E6` | `#0451A5` | — |
| Quote `>` markers | `#6A9955` | `#0451A5` | — |
| Link `[title]` | `#CE9178` | `#A31515` | — |
| Link `(url)` | inherit | inherit | underline |
| `==highlight==` | `#F0F6FC` on `#5C5020` | `#1F2328` on `#FFF8C5` | background |

## Window chrome (Markr's own)

Caption bar, menu strip, toolbar, status footer, search/find fields.

| Role | Dark | Light |
| --- | --- | --- |
| Caption background (active) | `#1A1A1A` | `#FCFCFC` |
| Caption background (inactive) | `#202020` | `#F6F6F6` |
| Caption text (active) | `#F0F6FC` | `#1A1A1A` |
| Caption text (inactive) | `#8C949E` | `#828282` |
| Caption button hover / pressed | `#3A3A3A` / `#484848` | `#E8E8E8` / `#D8D8D8` |
| Close button hover | `#C42B1C` (white glyph) | `#C42B1C` (white glyph) |
| Menu strip background | `#202020` | `#F6F6F6` |
| Menu text | `#E6EDF3` | `#1A1A1A` |
| Menu item hover | `#3C3C3C` | `#E6E6E6` |
| Bar background (toolbar, status, find bar) | `#202020` | `#F3F3F3` |
| Bar border | `#3A3A3A` | `#CDCDCD` |
| Field background | `#2D2D2D` | `#FFFFFF` |
| Field border | `#464646` | `#BCBCBC` |
| Field text | `#E6EDF3` | `#1F2328` |
| Field error text | `#FF7B72` | `#C42B1C` |
| Button face | `#333333` | `#FCFCFC` |
| Button hover / pressed | `#404040` / `#4A4A4A` | `#EEEEEE` / `#E2E2E2` |
| Button border | `#505050` | `#BCBCBC` |
| Button text | `#E6EDF3` | `#1F2328` |
| Accent (active toggles, checked boxes, active outline entry) | `#3794FF` | `#006AB1` |

## Application icon

Marker body `#0969DA`, chisel tip `#033C80`, band `#E6EDF3`, ink stroke
`#0969DA`, on a transparent background.
