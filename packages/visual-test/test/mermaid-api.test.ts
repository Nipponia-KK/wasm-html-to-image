import { describe, expect, it } from "vitest";
import { PNG } from "pngjs";
import { renderMermaid } from "wasm-html-to-image/single";

const GRAPH = "graph LR\n  A[開始] --> B{判定}\n  B -->|はい| C[完了]";

describe("renderMermaid", () => {
  it("returns SVG text, sized to the diagram, with the drawing as vector shapes", async () => {
    const { data } = await renderMermaid(GRAPH);
    expect(typeof data).toBe("string");
    expect(data).toContain("<svg");
    expect(data).toContain("<polygon"); // the diamond and the arrow heads
    expect(data).toContain("開始");
    const outer = /<svg[^>]*\bwidth="(\d+)"[^>]*\bheight="(\d+)"/.exec(data)!;
    expect(Number(outer[1])).toBeLessThan(700); // not the 4096 px probe width
    expect(Number(outer[2])).toBeGreaterThan(40);
  });

  it("keeps accTitle and accDescr out of the picture and in the SVG", async () => {
    const plain = await renderMermaid(GRAPH, { format: "png" });
    const described = await renderMermaid(`graph LR\n  accTitle: 判定の流れ\n  accDescr {\n  開始から完了まで\n  }\n${GRAPH.split("\n").slice(1).join("\n")}`, { format: "png" });
    expect(Buffer.from(described.data).equals(Buffer.from(plain.data))).toBe(true);
    const svg = (await renderMermaid(`graph LR\n  accTitle: 判定の流れ\n${GRAPH.split("\n").slice(1).join("\n")}`)).data;
    expect(svg).toContain("開始");
    expect(svg).not.toContain("accTitle");
  });

  it("takes a named theme: its background on the page and its accent on the arrows", async () => {
    const nord = PNG.sync.read(Buffer.from((await renderMermaid(GRAPH, { format: "png", theme: "nord" })).data));
    expect([nord.data[0], nord.data[1], nord.data[2]]).toEqual([0x2e, 0x34, 0x40]);
    const svg = (await renderMermaid(GRAPH, { theme: "dracula" })).data;
    expect(svg.toLowerCase()).toContain("#bd93f9"); // the accent colours the arrow heads
    await expect(renderMermaid(GRAPH, { theme: "nope" as never })).rejects.toThrow(/unknown mermaid theme/);
  });

  it("forgives a misspelt keyword only in recover mode", async () => {
    const source = "flowchrt LR\n  A[開始] --> B[完了]";
    const strict = await renderMermaid(source);
    expect(strict.data).not.toContain("<polygon"); // the red error box, no arrow head
    const compat = await renderMermaid(source, { mode: "compat" });
    expect(compat.data).not.toContain("<polygon");
    const recovered = await renderMermaid(source, { mode: "recover" });
    expect(recovered.data).toContain("<polygon");
    expect(recovered.data).toContain("開始");
  });

  it("returns a PNG that is the size of the diagram, light and dark", async () => {
    const light = await renderMermaid(GRAPH, { format: "png" });
    const dark = await renderMermaid(GRAPH, { format: "png", theme: "dark" });
    const a = PNG.sync.read(Buffer.from(light.data));
    const b = PNG.sync.read(Buffer.from(dark.data));
    expect(a.width).toBeLessThan(700);
    expect(a.width).toBe(b.width);
    // the page takes the theme's background: white vs near black in the corner
    expect(a.data[0]).toBeGreaterThan(240);
    expect(b.data[0]).toBeLessThan(40);
  });

  it("honours width, padding and an explicit background", async () => {
    const { data } = await renderMermaid("graph TD\n  A --> B", { format: "png", width: 300, padding: 0, background: "#ff0000" });
    const png = PNG.sync.read(Buffer.from(data));
    expect(png.width).toBe(300);
    expect([png.data[0], png.data[1], png.data[2]]).toEqual([255, 0, 0]);
  });

  it("escapes the source and shows a bad diagram as an error box", async () => {
    const { data } = await renderMermaid("graph LR\n  A[\"a < b & c\"] --> B");
    expect(data).toContain("a &lt; b &amp; c");
    const bad = await renderMermaid("zenuml", { format: "png" });
    const png = PNG.sync.read(Buffer.from(bad.data));
    let red = 0;
    for (let i = 0; i < png.data.length; i += 4) if (png.data[i] > 180 && png.data[i + 1] < 80 && png.data[i + 2] < 80) red++;
    expect(red).toBeGreaterThan(20);
  });
});
