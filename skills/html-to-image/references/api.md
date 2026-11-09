# Using render() from code

Use the CLI first; use the API when rendering inside an app or server.

```ts
import { render } from "wasm-html-to-image";
const { data, width, height } = await render({
  value: "<h1>Hello</h1>",   // HTML string | string[] (multi-page PDF) | URL | image bytes | data: URL
  width: 1200, height: 630,
  format: "png",             // png jpeg webp avif jxl svg pdf raw thumbhash
  quality: 85,
});
```

- Image bytes as `value` skip layout: resize with `width`/`height`, `crop: {x,y,width,height}`, re-encode with `format`, `quality`, AVIF `speed` (0 best .. 10 fast).
- Multi-page PDF: `value: [page1, page2]`, `format: "pdf"`, `pdfTitle`, `pdfAuthor`, `pdfMargin`, `pdfHeader`/`pdfFooter` (`{{pageNumber}}`, `{{totalPages}}`).
- Mermaid: `import { renderMermaid } from "wasm-html-to-image/single"; await renderMermaid(src, { format: "png", theme: "dark" })`.
- Markdown: `markdownToDocument(md, { paged: true })` from `wasm-html-to-image/markdown` gives `{ html, renderOptions }`; pass `{ value: html, format: "pdf", ...renderOptions }` to `render`.

## Entry points

| Import | Runtime |
|---|---|
| `wasm-html-to-image` | Auto: workerd on Cloudflare, single elsewhere |
| `/single` | Node.js, bundlers |
| `/workerd` | Cloudflare Workers (no wrangler `rules` needed on current Wrangler) |
| `/edge-light` | Vercel Edge: pass a compiled `WebAssembly.Module` as 2nd arg to `render` |
| `/workers` | Worker pool for batch throughput |
| `/react`, `/preact` | Render JSX element trees |
| `/tailwind` | Tailwind v4 / UnoCSS utilities as CSS |
| `/javascript` | Run page scripts in QuickJS (`applyJavaScript`, `createDOM`) |

Full docs: https://github.com/SoraKumo001/wasm-html-to-image (llms.txt in the repo root).
