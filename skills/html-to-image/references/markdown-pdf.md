# Markdown to PDF / image

```bash
npx wasm-html-to-image report.md -o report.pdf --template report --page-size A4 --accent "#2563eb"
npx wasm-html-to-image report.md -o report.png --markdown-theme dark -w 900     # single image
cat note.md | npx wasm-html-to-image - --markdown -o note.pdf                    # stdin needs --markdown
```

Requires the optional dependency `marked`. `$..$` / `$$..$$` math needs `katex` (disable with `--no-math`).

## Templates (`--template <name|file>`, `--list-templates`)

default (GitHub-like, images / quick PDF), report (cover + contents + header/footer), proposal, book (A5 serif, chapter per page), paper, compact (notes, no cover), manual (numbered headings), minutes, letter, slides (A4 landscape, each h2 a page), invoice.
A `.json` or `.css` file can be passed as a custom template; `--markdown-css <file>` appends CSS to any template.

## Front matter (top of the .md)

```yaml
---
title: Quarterly report
subtitle: FY2026 Q3
author: Name
date: 2026-10-29
template: report
accent: "#2563eb"
stamp: Confidential
---
```

CLI flags override front matter: `--cover` / `--no-cover`, `--no-toc`, `--toc-depth <n>`, `--no-outline`, `--stamp <text>`, `--body-font <family>`, `--page-size`.

## Built in

Auto table of contents with correct page numbers, PDF bookmarks, code highlighting (20+ languages), GFM tables, and ` ```mermaid ` blocks drawn as vector diagrams (see mermaid.md). CJK body text needs a CJK font: add `--font`.
