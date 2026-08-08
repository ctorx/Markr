---
title: Markdown Showcase
author: Simple Markdown Viewer
---

# Markdown Showcase

This document exercises every feature the viewer renders. Use it to eyeball
layout after a change: **bold**, *italic*, ***bold italic***, `inline code`,
~~strikethrough~~, ==highlight==, H<sub>2</sub>O and x<sup>2</sup>.

## Headings

### Level three
#### Level four
##### Level five
###### Level six

Setext heading
==============

Another setext
--------------

## Paragraphs and breaks

A paragraph that is long enough to demonstrate soft wrapping when the window is
resized. Resizing the window narrower must rewrap this text instead of adding a
horizontal scrollbar, no matter how narrow the window gets.

Line one with a hard break  
line two after the hard break.

Escapes work too: \*not emphasis\*, \# not a heading, and entities like &amp;,
&copy;, &rarr;, &mdash;, &hellip;.

## Lists

- Bullet item
- Second item with **formatting** and a [link](https://example.com)
  - Nested bullet
    - Deeper still
- Third item

1. Ordered item
2. Second ordered
   1. Nested ordered
5. Explicit numbering continues

- [ ] Unchecked task
- [x] Completed task

## Block quotes

> A quoted paragraph.
>
> > A nested quote inside the first one.
>
> - Quoted list item
> - Another item

## Code

Inline `code_span()` stays literal: `*not emphasis*`.

```cpp
// Fenced code with a language tag.
int main() {
    printf("hello, world\n");
    return 0;
}
```

    Indented code block
    keeps its own lines

Very long code lines wrap rather than adding a horizontal scrollbar:

```text
aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
```

## Tables

| Feature      | Supported | Notes                          |
| ------------ | :-------: | ------------------------------ |
| Headings     |    yes    | ATX and setext                 |
| Tables       |    yes    | with alignment                 |
| Task lists   |    yes    | checkboxes render natively     |

| Left | Center | Right |
| :--- | :----: | ----: |
| a    |   b    |     c |

## Links and images

- Inline: [Simple Markdown Viewer](https://github.com/ctorx/SimpleMarkDownViewer)
- Reference: [reference link][ref]
- Autolink: <https://example.com>
- Bare URL: https://example.com/page
- Email: <someone@example.com>
- Image (missing file falls back to alt text): ![an example image](missing.png)

[ref]: https://example.com "Reference title"

## Footnotes

Text with a footnote reference[^note] and another one[^second].

[^note]: The footnote text appears at the bottom.
[^second]: Footnotes are numbered in reference order.

## Horizontal rules

---

***

___

## Inline HTML subset

Raw <b>bold</b>, <i>italic</i>, <u>underline</u>, <mark>highlight</mark> and
<code>code</code> tags are honoured; unknown tags are stripped.

<div>
A block-level HTML element renders its text content.
</div>

The end.
