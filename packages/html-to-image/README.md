# wasm-html-to-image

[![npm version](https://img.shields.io/npm/v/wasm-html-to-image.svg)](https://www.npmjs.com/package/wasm-html-to-image)
[![npm downloads](https://img.shields.io/npm/dw/wasm-html-to-image.svg)](https://www.npmjs.com/package/wasm-html-to-image)
[![license](https://img.shields.io/npm/l/wasm-html-to-image.svg)](https://github.com/SoraKumo001/wasm-html-to-image/blob/master/LICENSE)
[![Playground](https://img.shields.io/badge/Demo-Playground-blueviolet)](https://sorakumo001.github.io/wasm-html-to-image/master)
[![Documentation](https://img.shields.io/badge/Docs-Docusaurus-blue)](https://sorakumo001.github.io/wasm-html-to-image/master/docs/)
[![Samples](https://img.shields.io/badge/Examples-GitHub-green)](https://github.com/SoraKumo001/wasm-html-to-image-samples)

A high-performance HTML-to-image renderer and image processing engine powered by a single WebAssembly module.

**No Chromium / Puppeteer.** **No native dependencies (Sharp / ImageMagick).**  
Runs seamlessly across **Node.js**, **Cloudflare Workers**, **Edge runtimes**, **browsers**, and the **CLI**.

---

## ⚡ What Can It Do?

| Feature | Description | Formats |
| :--- | :--- | :--- |
| 🌐 **HTML / React to Image** | Render HTML/CSS strings or React components directly | PNG, JPEG, WebP, AVIF, JXL, SVG |
| 🔗 **URL to Screenshot** | Capture remote web pages without launching a browser | PNG, WebP, JPEG, AVIF |
| 📑 **Multi-Page Vector PDF** | Generate crisp, selectable vector PDFs from HTML or images | PDF |
| 🖼️ **Image Resizing & Optimization** | Fast format conversion, crop, and resize (Sharp alternative) | WebP, AVIF, JXL, PNG, JPEG |
| 🎬 **CSS Animation to WebP / GIF** | Record CSS `@keyframes` animations into animated images | Animated WebP, APNG, GIF |
| 📝 **Markdown to PDF** | GitHub-flavoured Markdown to a styled, paged PDF: 11 templates, cover, contents, bookmarks, maths, syntax highlighting, call-outs, footnotes | PDF, PNG, WebP |
| 🧜 **Mermaid Diagrams** | 33 diagram types drawn by the engine itself as vectors (no browser, no Mermaid JS): in Markdown, in HTML, or alone with `renderMermaid()` | SVG, PNG, WebP, JPEG, AVIF, JXL, PDF |
| 🪶 **ThumbHash Blur Placeholder** | Generate compact blur placeholders for instant progressive loading | ThumbHash |


Supports vertical writing mode and CSS animations:

<p align="center">
  <img src="./doc/image01.webp" alt="Vertical writing mode and animation preview" width="100%" />
</p>

---

## 📦 Installation

```bash
npm install wasm-html-to-image
```

---

## 🚀 Quick Start

One single function — `render()` — handles all HTML rendering, URL capturing, and image processing:

```typescript
import { render } from "wasm-html-to-image";

// 1. Render HTML to PNG / WebP / AVIF
const result = await render({
  value: `<div style="padding: 40px; background: #6366f1; color: white; border-radius: 12px; font-family: sans-serif;">
    <h1 style="margin: 0;">Hello wasm-html-to-image</h1>
    <p>High-performance serverless rendering</p>
  </div>`,
  width: 800,
  format: "png",
});
console.log(result.data); // Uint8Array
console.log(result.width, result.height); // 800, 600

// 2. Capture a URL screenshot
const screenshot = await render({
  url: "https://example.com",
  width: 1200,
  height: 630,
  format: "webp",
});

// 3. Convert & Resize an image (JPEG -> AVIF)
const avif = await render({
  value: imageBuffer, // Pass raw Uint8Array / Buffer directly
  width: 400,
  format: "avif",
  quality: 80,
});

// 4. Record CSS Animation -> Animated WebP / GIF / APNG
const animated = await render({
  value: `<style>
    @keyframes spin { to { transform: rotate(360deg); } }
    .box { width: 100px; height: 100px; background: red; animation: spin 2s linear infinite; }
  </style><div class="box"></div>`,
  format: "webp",
  animation: 2000, // duration in ms
  animationFps: 20,
});
```

---

## 💻 CLI Usage

Render from your terminal without writing code:

```bash
# Render HTML to WebP
npx wasm-html-to-image template.html -o banner.webp -w 1200 -h 630 -f webp -q 90

# Capture screenshot from URL
npx wasm-html-to-image https://example.com -o site.png -w 1280 -h 720

# Convert and resize an image (JPEG -> AVIF)
npx wasm-html-to-image photo.jpg -o photo.avif -w 800 -f avif -q 80

# Convert CSS animation into Animated WebP or GIF
npx wasm-html-to-image loader.html -o loader.webp -f webp --animation 2000 --animation-fps 20
npx wasm-html-to-image loader.html -o loader.gif -f gif --animation 2000
```

---

## 📊 Supported Formats

### Inputs (`value` / `url`)
- **HTML String**: Standard HTML & CSS (Flexbox, Grid, gradients, custom fonts, `@keyframes`).
- **HTML Array (`string[]`)**: Multi-page PDF generation (one page per array item).
- **URL (`url`)**: Automatically fetches and renders remote web pages.
- **Image Buffer / Data URL**: `PNG`, `JPEG`, `WebP`, `GIF`, `AVIF`, `JXL`, `BMP`.

### Outputs (`format`)
`png`, `jpeg`, `webp`, `avif`, `jxl`, `apng`, `gif`, `tiff`, `svg`, `pdf`, `thumbhash`, `raw` (uncompressed RGBA).

**Print colors:** `format: "tiff"` keeps RGBA; add `colorSpace: "cmyk"` (CLI: `--color-space cmyk`) to get a CMYK TIFF, or a PDF whose colors and images are DeviceCMYK. The conversion is the plain device formula (no ICC profile), so convert with your print shop's profile if you need exact press colors. For print resolution, add `scale` (CLI: `--scale`): an 8.33in-wide page at 300 dpi is `width: 800, scale: 3.125`, and the TIFF records `96 × scale` ppi (override with `dpi`).

Every call returns a `RenderResult` object:
```typescript
interface RenderResult {
  data: Uint8Array | string; // Binary bytes (or string for SVG)
  width: number;
  height: number;
  originalWidth: number;
  originalHeight: number;
  format: string;
  isAnimated: boolean;
}
```

---

## 🌐 Environments & Subpaths

| Subpath | Target Environment | Highlights |
| :--- | :--- | :--- |
| `wasm-html-to-image` | Universal (Node, Edge, Cloudflare, Deno) | **Zero-config default**: auto-routes to single or workerd |
| `wasm-html-to-image/single` | Node.js, Bundlers | Single bundled WASM binary |
| `wasm-html-to-image/index` | High-throughput servers | Explicit `loadHtmlToImageModule()` & instance reuse |
| `wasm-html-to-image/workerd` | Cloudflare Workers | Optimized for `WebAssembly.Module` compilation |
| `wasm-html-to-image/edge-light` | Vercel Edge Runtime | Tailored for strict Edge constraints |
| `wasm-html-to-image/workers` | Node.js, Web Workers | Multi-threaded worker pool for high concurrency |
| `wasm-html-to-image/react` | React integration | Directly render React JSX element trees |
| `wasm-html-to-image/preact` | Preact integration | Directly render Preact JSX element trees |
| `wasm-html-to-image/tailwind` | Utility styling | UnoCSS / Tailwind v4 utilities (`createCSS`, `tailwind()`) |

---

## 💡 Advanced Examples

<details>
<summary><b>1. High-Throughput Node.js Server (Module Instance Reuse)</b></summary>

Reuse the compiled WASM module across incoming HTTP requests for maximum performance:

```typescript
import { loadHtmlToImageModule, htmlToImage } from "wasm-html-to-image";
import express from "express";

const app = express();
const mod = await loadHtmlToImageModule(); // Load once at startup

app.get("/ogp", async (req, res) => {
  const result = await htmlToImage(mod, {
    value: `<h1>${req.query.title}</h1>`,
    width: 1200,
    height: 630,
    format: "webp",
    quality: 85,
  });

  res.type("image/webp").send(Buffer.from(result.data));
});

app.listen(3000);
```
</details>

<details>
<summary><b>2. Cloudflare Workers (Edge OGP Generator)</b></summary>

```typescript
import { render } from "wasm-html-to-image"; // auto-selects the workerd build

export default {
  async fetch(request: Request): Promise<Response> {
    const result = await render({
      value: `<div style="padding: 40px; font-family: sans-serif; background: #0f172a; color: white;">
        <h1>Edge OGP Generator</h1>
      </div>`,
      width: 1200,
      height: 630,
      format: "png",
    });

    return new Response(result.data, {
      headers: {
        "Content-Type": "image/png",
        "Cache-Control": "public, max-age=86400",
      },
    });
  },
};
```
</details>

<details>
<summary><b>3. Multi-Threaded Batch Generation (`workers`)</b></summary>

Leverage multi-core CPUs with worker threads to render hundreds of images in parallel:

```typescript
import { render } from "wasm-html-to-image/workers";

const tasks = items.map((item) =>
  render({
    value: `<h1>${item.title}</h1>`,
    width: 600,
    height: 400,
    format: "webp",
  }),
);

const images = await Promise.all(tasks);
```
</details>

<details>
<summary><b>4. Image Cropping & ThumbHash</b></summary>

```typescript
import { render } from "wasm-html-to-image";

// Crop and resize
const cropped = await render({
  value: imageBuffer,
  crop: { x: 50, y: 50, width: 300, height: 300 },
  width: 150,
  height: 150,
  format: "png",
});

// Generate ThumbHash blur placeholder
const thumbhash = await render({
  value: imageBuffer,
  width: 100,
  format: "thumbhash",
});
```
</details>

---

## 📖 Documentation & Examples

👉 **[Read the Official Documentation](https://sorakumo001.github.io/wasm-html-to-image/master/docs/)**  
👉 **[Sample Code & Projects (GitHub)](https://github.com/SoraKumo001/wasm-html-to-image-samples)**

- [Overview & Getting Started](https://sorakumo001.github.io/wasm-html-to-image/master/docs/docs/overview)
- [Architecture & Pipelines](https://sorakumo001.github.io/wasm-html-to-image/master/docs/docs/architecture)
- [API Reference](https://sorakumo001.github.io/wasm-html-to-image/master/docs/docs/api-reference)
- [Runtime Guide (Node / Workers / Edge / Browser)](https://sorakumo001.github.io/wasm-html-to-image/master/docs/docs/api-reference/runtime-guide)
- [Production Recipes](https://sorakumo001.github.io/wasm-html-to-image/master/docs/docs/recipes/ogp-production)

---

## 🛠️ Development

<details>
<summary>Click to view repository build and test instructions</summary>

### Prerequisites

- Node.js >= 20, pnpm >= 9
- Emscripten SDK (emsdk), CMake, Ninja, vcpkg (for C++ WASM builds)

### Building

```bash
# Install workspace dependencies
pnpm install

# Build TypeScript packages
pnpm --filter wasm-html-to-image build
pnpm build:js

# Build Documentation
pnpm docs:build
```

### Testing

```bash
# Unit tests (Vitest)
pnpm --filter wasm-html-to-image test

# Visual regression tests
pnpm --filter visual-test test
```

</details>

---

## 📄 License

MIT © [SoraKumo](https://github.com/SoraKumo001)
