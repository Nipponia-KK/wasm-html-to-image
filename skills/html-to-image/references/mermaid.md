# Mermaid to image

No mermaid.js and no browser; the engine draws it.

```bash
npx wasm-html-to-image flow.mmd -o flow.svg                       # vector
npx wasm-html-to-image flow.mmd -o flow.png --mermaid-theme nord
npx wasm-html-to-image "graph LR; A-->B" --mermaid -o ab.png      # inline source
cat flow.mmd | npx wasm-html-to-image - --mermaid -o flow.webp --mermaid-mode recover
```

- Output size follows the diagram; `-w` fixes the width only when given. Formats: svg, png, jpeg, webp, avif, pdf, ...
- `--mermaid-theme`: light, dark, neutral, forest, nord, dracula, solarized-light, solarized-dark, rose-pine, sepia.
- `--mermaid-mode`: `strict` (default) errors on bad syntax; `compat` forgives case of the diagram name, BOM, CRLF, up to 3 missing `end`/`}`; `recover` also fixes small typos in the diagram type and skips unreadable lines. Use `recover` for LLM-written diagrams.
- `--mermaid-directives` honours `%%{init}%%` / front matter theme config. `--accent <colour>` sets arrow colour.
- Diagram types: flowchart/graph, sequenceDiagram, stateDiagram-v2, classDiagram, erDiagram, requirementDiagram, block-beta, C4*, architecture-beta, gantt, pie, quadrant, mindmap, and 30+ more. `*-beta` syntaxes may differ in detail from upstream Mermaid.
- Inside Markdown or HTML (`<div class="mermaid">`) the same engine is used.
- CJK labels need a CJK font: `--font`.
