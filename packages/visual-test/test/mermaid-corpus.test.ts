import { describe, expect, it } from "vitest";
import fs from "node:fs";
import { renderMermaid } from "wasm-html-to-image/single";

// Examples in the style of the Mermaid documentation (tests/mermaid-corpus/corpus.json). Each one must draw,
// not come back as the red error box, through the WASM path. known-gaps.json lists what is not supported yet:
// those must still fail, so the list cannot go stale when a gap is closed.
const dir = new URL("../../../tests/mermaid-corpus/", import.meta.url);
const corpus = JSON.parse(fs.readFileSync(new URL("corpus.json", dir), "utf-8")) as Record<string, string>;
const gaps = JSON.parse(fs.readFileSync(new URL("known-gaps.json", dir), "utf-8")) as Record<string, string>;

describe("Mermaid documentation examples", () => {
  for (const [name, source] of Object.entries(corpus)) {
    const gap = gaps[name];
    it(gap ? `${name} (known gap: ${gap})` : name, async () => {
      const svg = (await renderMermaid(source)).data;
      const failed = svg.includes("#cf222e"); // the error box's stroke
      expect(failed, failed ? svg.slice(svg.indexOf("Mermaid:"), svg.indexOf("Mermaid:") + 120) : "").toBe(Boolean(gap));
    });
  }

  it("lists only snippets that exist", () => {
    for (const name of Object.keys(gaps)) expect(corpus, name).toHaveProperty(name);
  });
});
