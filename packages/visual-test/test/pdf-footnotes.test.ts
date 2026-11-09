import { describe, expect, it } from "vitest";
import { render } from "wasm-html-to-image/single";
import { markdownToDocument } from "wasm-html-to-image/markdown";

const paragraph = (n: number) => `Paragraph ${n} runs on for a few lines so that the page fills up, with plain words and nothing else in it, only enough text to take a good part of the line width and wrap onto the next line and the one after.`;

const source = (mode: "page" | "end", long = false) => {
  const body = Array.from({ length: 40 }, (_, k) => {
    const n = k + 1;
    const ref = n === 3 ? " Alpha[^a]." : n === 22 ? " Beta[^b]." : n === 23 ? " Again[^a]." : "";
    return paragraph(n) + ref;
  }).join("\n\n");
  const note = (name: string) => `[^${name}]: FOOTNOTE${name.toUpperCase()}BODY ${long ? "very long ".repeat(500) : "a short note."}`;
  return `---\ntitle: Notes\nfootnotes: ${mode}\n---\n\n# Notes\n\n${body}\n\n${note("a")}\n\n${note("b")}\n`;
};

const pdfPages = async (md: string) => {
  const doc = markdownToDocument(md, { paged: true });
  const pdf = (await render({ value: doc.html, width: 794, format: "pdf", pdfOutline: false, ...doc.renderOptions })).data as Uint8Array;
  const lib = await import("pdfjs-dist/legacy/build/pdf.mjs");
  const pdfDoc = await lib.getDocument({ data: new Uint8Array(pdf), useSystemFonts: false, isEvalSupported: false }).promise;
  const pages = [];
  for (let n = 1; n <= pdfDoc.numPages; n++) {
    const content = await (await pdfDoc.getPage(n)).getTextContent();
    const items = content.items
      .filter((i): i is typeof i & { str: string; transform: number[] } => "str" in i && i.str.trim() !== "")
      .map((i) => ({ str: i.str, y: i.transform[5] }));
    pages.push({ text: items.map((i) => i.str).join("").replace(/\s+/g, ""), items });
  }
  return pages;
};

describe("footnotes at the foot of the page", () => {
  it("sets each footnote on the page of its first reference, under the text", async () => {
    const pages = await pdfPages(source("page"));
    const where = (name: string) => pages.map((p, k) => (p.text.includes(`FOOTNOTE${name}BODY`) ? k : -1)).filter((k) => k >= 0);
    expect(where("A")).toHaveLength(1); // once, though it is referred to twice
    expect(where("B")).toHaveLength(1);
    const pa = pages[where("A")[0]];
    expect(pa.text).toContain("Alpha");
    const note = pa.items.filter((i) => i.str.includes("FOOTNOTE") || i.str.includes("short note"));
    const noteTop = Math.max(...note.map((i) => i.y));
    const lowestBody = Math.min(...pa.items.filter((i) => i.str.includes("Paragraph")).map((i) => i.y));
    expect(lowestBody, "no body line under the footnote").toBeGreaterThan(noteTop);
    expect(noteTop).toBeLessThan(200); // the foot of an A4 page (842pt tall)
    expect(where("B")[0]).toBeGreaterThan(where("A")[0]);
  });

  it("leaves the list at the end without footnotes: page", async () => {
    const pages = await pdfPages(source("end"));
    const last = pages[pages.length - 1].text;
    expect(last).toContain("FOOTNOTEABODY");
    expect(last).toContain("FOOTNOTEBBODY");
    expect(pages.slice(0, -1).some((p) => p.text.includes("FOOTNOTE"))).toBe(false);
  });

  it("falls back to the list at the end when a footnote is too tall for the foot", async () => {
    const pages = await pdfPages(source("page", true));
    const first = (needle: string) => pages.findIndex((p) => p.text.includes(needle));
    expect(first("FOOTNOTEABODY")).toBeGreaterThanOrEqual(first("Paragraph40runs")); // after the text, not at the foot of an earlier page
    expect(first("FOOTNOTEBBODY")).toBeGreaterThan(-1);
  });

  it("prints every body line once and loses none of the text", async () => {
    const pages = await pdfPages(source("page"));
    const all = pages.map((p) => p.text).join("");
    for (let n = 1; n <= 40; n++) expect(all.split(`Paragraph${n}runs`).length - 1, `paragraph ${n}`).toBe(1);
  });
});
