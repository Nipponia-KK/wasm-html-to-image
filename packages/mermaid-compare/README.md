# mermaid-compare

Interactive comparison app comparing [`wasm-html-to-image`](https://www.npmjs.com/package/wasm-html-to-image)'s native Mermaid diagram renderer with the official [`mermaid`](https://www.npmjs.com/package/mermaid) package.

---

## 🌟 Features

- **Side-by-Side & Split Slider Views**:
  - Compare diagram layout, node spacing, font rendering, and arrow routing side-by-side or using a real-time draggable overlay slider.
- **Support for Key Diagram Types**:
  - Pre-configured presets for Flowcharts, Sequence Diagrams, Class Diagrams, State Diagrams, ER Diagrams, and Charts (Pie, XY Chart).
- **Theme Testing**:
  - Compare themes: `light`, `dark`, `neutral`, `forest`, `nord`, `dracula`, `sepia`, etc.
- **Performance Profiling**:
  - Real-time conversion latency comparison (ms) between WASM and DOM/D3-based rendering.
- **Recovery & Tolerant Parsing**:
  - Test `wasm-html-to-image`'s `mode: "strict" | "compat" | "recover"` with syntax deviations and view recovery/warning logs.
- **Multi-Format Export**:
  - Export diagram outputs directly as SVG or raster formats (PNG, WebP).

---

## 🚀 Getting Started

This package is part of the monorepo and uses the local `wasm-html-to-image` build (`workspace:*`),
so rebuild the library (`pnpm build` in `packages/html-to-image`, or `pnpm build` at the root) to see engine changes.

From the repository root:

```bash
# Run local dev server
pnpm --filter mermaid-compare dev

# Build production bundle
pnpm --filter mermaid-compare build
```

Then open `http://localhost:3001` in your browser.

The settings are kept in the address bar, so a link reproduces the screen. Values equal to the default are left out.

| Query | Values | Default |
| :--- | :--- | :--- |
| `preset` | a preset id (for example `state-order`) | `flowchart-simple` |
| `code` | the Mermaid source, when it differs from the preset | the preset's |
| `theme` | a theme name (`light`, `dark`, `forest`, ...) | `light` |
| `view` | `side-by-side`, `slider`, `wasm-only`, `official-only` | `side-by-side` |
| `scale` | `fit`, `original` | `fit` |
| `bg`, `bgColor` | `light`, `dark`, `gray`, `checker`, `custom`; `#rrggbb` for `custom` | `light` |
| `mode` | `strict`, `compat`, `recover` | the preset's recommended mode |
| `format` | `svg`, `png`, `webp` | `svg` |
| `directives` | `off` to ignore `%%{init}%%` | on |
| `padding` | 0 to 200 | `16` |

Open `http://localhost:3001/parity.html` for the theme colour report: it renders every preset in `light`, `dark`,
`forest` and `neutral` with both renderers and lists the colour difference (CIE76 ΔE) of node, edge and text
colours, plus the official theme variables (`window.__parity`). See `doc/design/mermaid-theme-parity.md`.

## Notation differential test

`pnpm --filter mermaid-compare notation-diff` feeds about 1600 notation variants (link tokens with and without spaces, labels holding delimiters, quotes, line breaks, per-diagram syntax) to the official Mermaid parser and to the host `build-host-tests/Release/mermaid_cli.exe`, and fails on an input the official parser takes and the engine rejects, or on a flowchart whose node / edge / subgraph counts differ. Deliberate differences are listed in `scripts/notation-known-gaps.json`. Rebuild `mermaid_cli` first (`cmake --build build-host-tests --config Release --target mermaid_cli`).
