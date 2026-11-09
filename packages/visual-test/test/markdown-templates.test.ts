import { describe, expect, it } from "vitest";
import fs from "node:fs";
import { render } from "wasm-html-to-image/single";
import { listTemplates, markdownToDocument } from "wasm-html-to-image/markdown";

const DOC = `---
title: Template check
subtitle: Every bundled template
author: Tester
date: 2026-10-30
stamp: DRAFT
---

# Chapter one

Body text with **bold**, \`code\` and a [link](#chapter-one).

## Section

> [!NOTE]
> A call-out box.

| Item | Value |
|---|---:|
| One | 1 |
| Two | 2 |

\`\`\`ts
const answer = 42;
\`\`\`

## Another section

- first
- second

# Chapter two

More text on a later page.
`;

const pdfjs = async (data: Uint8Array) => {
  const lib = await import("pdfjs-dist/legacy/build/pdf.mjs");
  const doc = await lib.getDocument({ data: new Uint8Array(data), useSystemFonts: false, isEvalSupported: false }).promise;
  const page = async (n: number) => {
    const p = await doc.getPage(n);
    const content = await p.getTextContent();
    const items = content.items.filter((i): i is typeof i & { str: string; transform: number[] } => "str" in i && i.str.trim() !== "");
    const text = content.items.map((i) => ("str" in i ? i.str : "")).join(" ");
    const ink = items.reduce((n, i) => n + i.str.replace(/\s/g, "").length, 0); // characters, however the line was split into runs
    const v = p.getViewport({ scale: 1 });
    // where the text starts and ends on the page, to the nearest 2pt: a fingerprint of the layout
    const at = (i: { transform: number[] } | undefined) => (i ? [Math.round(i.transform[4] / 2) * 2, Math.round(i.transform[5] / 2) * 2] : null);
    return { text, width: v.width, height: v.height, ink, first: at(items[0]), last: at(items[items.length - 1]) };
  };
  return { pages: doc.numPages, page };
};

// What each template makes of the same document, as a layout fingerprint per page (page size, how much text,
// where it starts and ends; the first line's x is left out because a justified line may be split into runs
// differently). A CSS change that moves things shows up as a diff of this file, to be looked at
// and, if meant, accepted with UPDATE_TEMPLATE_LAYOUTS=1.
const LAYOUTS = new URL("./data/template-layouts.json", import.meta.url);
const layouts: Record<string, unknown> = {};

// Every bundled template, light and dark: it must produce a PDF whose pages all have text, a cover without
// running header and footer, and the page shape (A4 landscape for slides) the template asks for.
describe("bundled Markdown templates", () => {
  for (const { name } of listTemplates()) {
    for (const theme of ["light", "dark"] as const) {
      it(`${name} (${theme})`, async () => {
        const doc = markdownToDocument(DOC, { template: name, theme, paged: true });
        const pdf = (await render({ value: doc.html, width: 794, format: "pdf", pdfOutline: false, ...doc.renderOptions })).data as Uint8Array;
        const r = await pdfjs(pdf);
        expect(r.pages).toBeGreaterThan(0);
        for (let n = 1; n <= r.pages; n++) expect((await r.page(n)).text.trim().length, `page ${n}`).toBeGreaterThan(0);

        const first = await r.page(1);
        if (name === "slides") expect(first.width).toBeGreaterThan(first.height);
        else expect(first.height).toBeGreaterThan(first.width);

        if (theme === "light") {
          const pagesInfo = [];
          for (let n = 1; n <= r.pages; n++) {
            const { width, height, ink, first, last } = await r.page(n);
            pagesInfo.push({ size: [Math.round(width), Math.round(height)], ink, first: first && first[1], last: last && last[1] });
          }
          layouts[name] = pagesInfo;
        }

        const footer = /\d+ \/ \d+/;
        if (doc.settings.cover) {
          expect(first.text.replace(/\s+/g, "")).toContain("Templatecheck"); // the spacing between runs varies with justification
          expect(first.text, "the cover has no running footer").not.toMatch(footer);
          expect(first.text, "the cover has no stamp header").not.toContain("DRAFT");
        }
      });
    }
  }
});

describe("template layouts", () => {
  it("match the recorded fingerprints", () => {
    const names = listTemplates().map((t) => t.name);
    expect(Object.keys(layouts).sort()).toEqual([...names].sort()); // the per-template tests above ran first
    const sorted = Object.fromEntries(Object.entries(layouts).sort(([a], [b]) => (a < b ? -1 : 1)));
    if (process.env.UPDATE_TEMPLATE_LAYOUTS || !fs.existsSync(LAYOUTS)) {
      fs.mkdirSync(new URL("./data/", import.meta.url), { recursive: true });
      fs.writeFileSync(LAYOUTS, JSON.stringify(sorted, null, 1) + "\n");
      return;
    }
    expect(sorted).toEqual(JSON.parse(fs.readFileSync(LAYOUTS, "utf-8")));
  });
});
