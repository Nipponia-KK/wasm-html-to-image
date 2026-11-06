---
name: html-to-image
description: Convert HTML (file, URL, inline string), Markdown, Mermaid diagrams or existing images into PNG, JPEG, WebP, AVIF, SVG, GIF/APNG or PDF with the wasm-html-to-image CLI (npx, no Chromium/Puppeteer). Use when asked to screenshot or render HTML, make an OGP/thumbnail image, create a PDF from HTML or Markdown (report, book, invoice...), draw a Mermaid diagram as an image, or resize/re-encode images (JPEG to WebP/AVIF).
---

# html-to-image (wasm-html-to-image CLI)

One command, no browser. Needs Node.js >= 20.

```bash
npx wasm-html-to-image <input> -o <output>
```

The output format comes from the `-o` extension (or `-f`). The input kind is detected:

| Input | How it is detected |
|---|---|
| HTML file / URL / inline string (starts with `<`) / `-` (stdin) | default |
| Image file (jpg, png, gif, webp ...) | magic bytes: layout is skipped, only resize / re-encode |
| Markdown | `.md` / `.markdown`, or `--markdown` for inline text and stdin |
| Mermaid | `.mmd` / `.mermaid`, or `--mermaid` for inline text and stdin |

## Recipes

HTML to an image (viewport width 800 by default, height auto):

```bash
npx wasm-html-to-image page.html -o page.png -w 1200 -h 630      # OGP size
npx wasm-html-to-image https://example.com -o shot.webp -q 85
npx wasm-html-to-image page.html -o hires.png --scale 2          # 2x pixel density
```

HTML to PDF (vector, multi-page):

```bash
npx wasm-html-to-image page.html -o page.pdf
```

Image conversion / optimization (no HTML layout):

```bash
npx wasm-html-to-image photo.jpg -o photo.webp -q 80
npx wasm-html-to-image photo.jpg -o small.avif -w 600 -q 70
```

Mermaid to an image:

```bash
npx wasm-html-to-image flow.mmd -o flow.png --mermaid-theme dark
npx wasm-html-to-image flow.mmd -o flow.svg
```

Markdown to a styled PDF (cover, table of contents, bookmarks, code highlighting, Mermaid blocks):

```bash
npx wasm-html-to-image report.md -o report.pdf --template report --page-size A4
npx wasm-html-to-image --list-templates
```

Animated output from CSS `@keyframes` (opacity / transform / color):

```bash
npx wasm-html-to-image anim.html -o anim.webp --animation 2000
```

## Things to know

- **Fonts**: no system font lookup guarantees. Register what you need: `--font "./fonts/*.woff2"` or `--font "Family=path/to/file.ttf"` (repeat per weight). CJK text needs a CJK font passed this way.
- **Markdown** needs the optional dependency `marked`; math (`$..$`) needs `katex`. Install them next to the CLI (`npm i marked katex`) if the CLI reports them missing.
- **`--js`** runs the page's scripts in QuickJS before rendering (`--js-wait <ms|selector>`). Trusted pages only.
- Relative resources resolve against `--base-url` (default: the file's directory, or the URL).
- Always check the result: the CLI prints `Successfully rendered to <path>`; open or inspect the output when layout matters.
- This is not Chromium. Complex pages can differ slightly; if a layout looks wrong, simplify the CSS or compare against a browser.

## References (read only when needed)

- [references/cli-options.md](references/cli-options.md): every CLI flag.
- [references/markdown-pdf.md](references/markdown-pdf.md): templates, cover, TOC, headers, math.
- [references/mermaid.md](references/mermaid.md): themes, modes, directives.
- [references/api.md](references/api.md): using `render()` from Node, Cloudflare Workers, Vercel Edge, React/Tailwind.
