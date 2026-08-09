# Syntax and Navigation

Contents, as in-document links:

- [C Sharp](#c-sharp)
- [JavaScript](#javascript)
- [Python](#python)
- [SQL](#sql)
- [Markup](#markup)
- [Razor](#razor)
- [CSS](#css)
- [Java and Swift](#java-and-swift)
- [Back to the showcase](./showcase.md) (a link to another file)
- [The showcase tables section](./showcase.md#tables) (another file, at a heading)

## C Sharp

```cs
#region Startup
using System.Threading.Tasks;

// Builds the host and runs it.
public sealed record Options(string Path, int Retries = 3);

public static async Task<int> Main(string[] args) {
    var options = new Options(@"C:\temp\input.md", 5);
    var message = $"reading {options.Path}";
    Console.WriteLine(message);
    return await RunAsync(options) ? 0 : 1;
}
#endregion
```

## JavaScript

```js
// Fetches and renders a document.
import { render } from './render.js';

const cache = new Map();

export async function load(url, { retries = 3 } = {}) {
    if (cache.has(url)) return cache.get(url);
    const response = await fetch(`${url}?v=${Date.now()}`);
    if (!response.ok) throw new Error('failed: ' + response.status);
    const text = await response.text();
    cache.set(url, text);
    return render(text, { smartypants: false, tables: true });
}
```

TypeScript is highlighted with the same rules:

```ts
interface Options { retries: number; verbose?: boolean }
type Handler = (input: string) => Promise<void>;
```

## Python

```python
import re
from dataclasses import dataclass

HEADING = re.compile(r'^(#{1,6})\s+(.*)$')

@dataclass
class Heading:
    """A single markdown heading."""
    level: int
    text: str

def parse(source: str) -> list[Heading]:
    out = []
    for line in source.splitlines():
        match = HEADING.match(line)
        if match is not None:
            out.append(Heading(len(match.group(1)), match.group(2).strip()))
    return out
```

## SQL

T-SQL:

```tsql
-- Recent documents for one owner.
DECLARE @OwnerId int = 42;

SELECT TOP (50) d.[Id], d.[Title], COUNT(t.[Id]) AS TagCount
FROM [dbo].[Documents] AS d
LEFT JOIN [dbo].[Tags] AS t ON t.[DocumentId] = d.[Id]
WHERE d.[OwnerId] = @OwnerId AND d.[Title] LIKE N'%report%'
GROUP BY d.[Id], d.[Title]
ORDER BY d.[CreatedUtc] DESC;
```

PostgreSQL:

```pgsql
-- Upsert and return the row.
CREATE FUNCTION touch(doc_id uuid) RETURNS jsonb AS $$
BEGIN
    INSERT INTO documents (id, seen_at, payload)
    VALUES (doc_id, now(), '{"source":"viewer"}'::jsonb)
    ON CONFLICT (id) DO UPDATE SET seen_at = EXCLUDED.seen_at
    RETURNING to_jsonb(documents.*);
END;
$$ LANGUAGE plpgsql VOLATILE;
```

## Markup

```html
<!DOCTYPE html>
<!-- The shell page. -->
<html lang="en">
<head>
    <meta charset="utf-8" />
    <title>Viewer &amp; Renderer</title>
    <style>
        body { margin: 0; font-family: "Segoe UI", sans-serif; }
        .doc > h1 { color: #24292f; font-size: 2em; }
    </style>
</head>
<body>
    <div class="doc" data-id="7">Hello</div>
    <script>
        const root = document.querySelector('.doc');
        root.addEventListener('click', () => console.log(root.dataset.id));
    </script>
</body>
</html>
```

XAML and other XML dialects use the same rules:

```xml
<ResourceDictionary xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation">
    <Style TargetType="Button" x:Key="Primary">
        <Setter Property="Background" Value="#0969DA" />
    </Style>
</ResourceDictionary>
```

## Razor

```cshtml
@page "/documents/{id:int}"
@model DocumentModel
@using Viewer.Rendering

@* Only the owner sees the toolbar. *@
@{
    var title = Model.Document?.Title ?? "Untitled";
    ViewData["Title"] = title;
}

<article class="doc">
    <h1>@title</h1>
    @if (Model.CanEdit) {
        <a class="btn" href="@Url.Page("Edit", new { id = Model.Id })">Edit</a>
    }
    @foreach (var tag in Model.Tags) {
        <span class="tag">@tag.Name</span>
    }
</article>

@code {
    private bool expanded = false;

    private void Toggle() => expanded = !expanded;
}
```

## CSS

```css
/* Document surface. */
@media (prefers-color-scheme: dark) {
    :root { --surface: #0d1117; --text: #e6edf3; }
}

.doc h1,
.doc h2 {
    color: var(--text);
    margin-block: 1.5rem 0.5rem;
    border-bottom: 1px solid #30363d !important;
}
```

## Java and Swift

Android-style Java:

```java
package com.example.viewer;

import android.os.Bundle;
import androidx.appcompat.app.AppCompatActivity;

public final class MainActivity extends AppCompatActivity {
    private static final int MAX_ITEMS = 0x40;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
    }
}
```

iOS-style Swift:

```swift
import UIKit

@objc final class DocumentViewController: UIViewController {
    private let titles: [String] = ["one", "two"]
    var count: Int { titles.count }

    override func viewDidLoad() {
        super.viewDidLoad()
        guard let first = titles.first else { return }
        view.backgroundColor = .systemBackground
        print("first is \(first), total \(count)")
    }
}
```

## Unknown languages

Code without a language tag, or with one that is not supported, is shown plain:

```
plain text stays exactly as written: int x = 1; // not a comment here
```
