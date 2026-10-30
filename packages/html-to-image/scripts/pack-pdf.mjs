// Assembles `publish-pdf/`: the WASM_PDF_ONLY build published as
// `@nipponia-kk/wasm-html-to-image`. Only the `./index` entry (explicit
// `loadHtmlToImageModule({ factory })`) and the glue are shipped; single /
// workers / cli / react / markdown need code this build leaves out.
//
//   cmake ... -DWASM_PDF_ONLY=ON && ninja   (writes dist/html-to-image.{js,wasm})
//   pnpm build                              (writes the rest of dist/)
//   node scripts/pack-pdf.mjs
//   cd publish-pdf && pnpm publish --tag latest --access public
import fs from "node:fs";
import path from "node:path";

const root = path.resolve(import.meta.dirname, "..");
const dist = path.join(root, "dist");
const out = path.join(root, "publish-pdf");
const base = JSON.parse(fs.readFileSync(path.join(root, "package.json"), "utf8"));

// Files reachable from dist/index.js and the glue (relative imports + new URL()).
const files = new Set();
const queue = ["index.js", "index.d.ts", "html-to-image.js"];
while (queue.length > 0) {
  const file = queue.pop();
  if (files.has(file) || !fs.statSync(path.join(dist, file), { throwIfNoEntry: false })?.isFile()) {
    continue;
  }
  files.add(file);
  if (!/\.(js|ts)$/.test(file)) continue;
  const source = fs.readFileSync(path.join(dist, file), "utf8");
  for (const [, spec] of source.matchAll(/(?:from|import)\s*\(?\s*["']\.\/([^"']+)["']/g)) {
    queue.push(spec);
    if (spec.endsWith(".js")) queue.push(spec.replace(/\.js$/, ".d.ts"));
  }
  for (const [, spec] of source.matchAll(/new URL\(\s*["']\.?\/?([^"']+)["']/g)) {
    queue.push(spec);
  }
}
if (!files.has("html-to-image.wasm")) {
  throw new Error("dist/html-to-image.wasm is not referenced from the glue");
}

fs.rmSync(out, { recursive: true, force: true });
fs.mkdirSync(path.join(out, "dist"), { recursive: true });
for (const file of files) {
  fs.copyFileSync(path.join(dist, file), path.join(out, "dist", file));
}
fs.copyFileSync(path.join(root, "LICENSE"), path.join(out, "LICENSE"));

const version = process.env.PDF_VERSION ?? `${base.version}-nipponia.1`;
const manifest = {
  name: "@nipponia-kk/wasm-html-to-image",
  version,
  description:
    "HTML to PDF only build of wasm-html-to-image (WASM_PDF_ONLY). Image encoders and non-PNG decoders are left out.",
  type: "module",
  author: base.author,
  license: base.license,
  repository: {
    type: "git",
    url: "git+https://github.com/Nipponia-KK/wasm-html-to-image.git",
    directory: "packages/html-to-image",
  },
  publishConfig: { "@nipponia-kk:registry": "https://npm.pkg.github.com" },
  engines: base.engines,
  exports: {
    "./index": {
      types: "./dist/index.d.ts",
      import: "./dist/index.js",
      default: "./dist/index.js",
    },
    "./html-to-image.js": {
      import: "./dist/html-to-image.js",
      default: "./dist/html-to-image.js",
    },
  },
  files: ["dist", "LICENSE"],
};
fs.writeFileSync(path.join(out, "package.json"), `${JSON.stringify(manifest, null, 2)}\n`);
fs.writeFileSync(
  path.join(out, "README.md"),
  `# @nipponia-kk/wasm-html-to-image

[wasm-html-to-image](https://github.com/SoraKumo001/wasm-html-to-image) ${base.version} built with \`-DWASM_PDF_ONLY=ON\`.
Only HTML -> PDF is kept (PNG images, fonts incl. woff2). Build: https://github.com/Nipponia-KK/wasm-html-to-image/tree/receipt-pdf

\`\`\`js
import { loadHtmlToImageModule, htmlToImage } from "@nipponia-kk/wasm-html-to-image/index";
import glue from "@nipponia-kk/wasm-html-to-image/html-to-image.js";

const mod = await loadHtmlToImageModule({ factory: () => glue() });
const { data } = await htmlToImage(mod, { value: html, width: 794, height: 1123, format: "pdf" });
\`\`\`
`,
);

const size = (file) => fs.statSync(path.join(out, "dist", file)).size;
console.log(`${manifest.name}@${version}: ${files.size} files, wasm ${size("html-to-image.wasm")} bytes -> ${out}`);
