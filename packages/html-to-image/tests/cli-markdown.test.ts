import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { execFile } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { promisify } from "node:util";

const run = promisify(execFile);
const CLI = fileURLToPath(new URL("../dist/cli.js", import.meta.url));

/**
 * The real entry point: `cli.ts` rebuilds the flag object field by field before calling
 * `buildRenderOptions`, so a flag that is parsed but not forwarded is only caught here.
 * Needs the built package (`tsc -b`); skipped when `dist/cli.js` is missing.
 */
const built = fs.existsSync(CLI);

/** The palette (PLTE) of a PNG as `#rrggbb`: the encoder writes the background first. */
function palette(file: string): string[] {
  const b = fs.readFileSync(file);
  expect(b.subarray(1, 4).toString()).toBe("PNG");
  for (let i = 8; i < b.length; ) {
    const len = b.readUInt32BE(i);
    if (b.subarray(i + 4, i + 8).toString() === "PLTE") {
      const out: string[] = [];
      for (let k = 0; k < len; k += 3) {
        out.push("#" + [...b.subarray(i + 8 + k, i + 11 + k)].map((v) => v.toString(16).padStart(2, "0")).join(""));
      }
      return out;
    }
    i += 12 + len;
  }
  throw new Error("PNG has no palette");
}

describe.skipIf(!built)("wasm-html-to-image CLI with Markdown", () => {
  let dir: string;
  beforeAll(() => {
    dir = fs.mkdtempSync(path.join(os.tmpdir(), "md-cli-"));
  });
  afterAll(() => {
    fs.rmSync(dir, { recursive: true, force: true });
  });

  const cli = (args: string[], cwd = dir) =>
    run(process.execPath, [CLI, ...args], { cwd, timeout: 120_000 });

  it("renders a .md file, resolving relative images against the file, from any cwd", async () => {
    fs.mkdirSync(path.join(dir, "assets"));
    // An 8x8 #dd2222 PNG, stretched by the width/height attributes. (Keep the CRCs valid when
    // editing the bytes: the engine silently skips a PNG whose chunk CRC is wrong.)
    fs.writeFileSync(
      path.join(dir, "assets", "red.png"),
      Buffer.from(
        "iVBORw0KGgoAAAANSUhEUgAAAAgAAAAICAMAAADz0U65AAAAA1BMVEXdIiJQ+Zx9AAAADElEQVR4nGNgoA4AAABIAAEuuDx+AAAAAElFTkSuQmCC",
        "base64",
      ),
    );
    fs.writeFileSync(path.join(dir, "doc.md"), '# Title\n\n<img src="assets/red.png" width="40" height="40">\n');
    const withImage = path.join(dir, "doc.png");
    await cli([path.join(dir, "doc.md"), "-o", withImage, "-w", "300"], os.tmpdir());
    const isRed = ([r, g, b]: number[]) => r > 200 && g < 60 && b < 60;
    const rgb = (hex: string) => [1, 3, 5].map((i) => parseInt(hex.slice(i, i + 2), 16));
    expect(palette(withImage).some((c) => isRed(rgb(c)))).toBe(true);
    // Control: the same document without the image has no red at all.
    fs.writeFileSync(path.join(dir, "plain.md"), "# Title\n");
    const plain = path.join(dir, "plain.png");
    await cli([path.join(dir, "plain.md"), "-o", plain, "-w", "300"], os.tmpdir());
    expect(palette(plain).some((c) => isRed(rgb(c)))).toBe(false);
  });

  it("forwards --markdown (inline text) and --markdown-theme to the renderer", async () => {
    const light = path.join(dir, "light.png");
    const dark = path.join(dir, "dark.png");
    await cli(["# Hi", "--markdown", "-o", light, "-w", "200"]);
    await cli(["# Hi", "--markdown", "--markdown-theme", "dark", "-o", dark, "-w", "200"]);
    expect(palette(light)[0]).toBe("#ffffff");
    expect(palette(dark)[0]).toBe("#0d1117");
  });

  it("forwards --markdown-css after the built-in stylesheet", async () => {
    const cssFile = path.join(dir, "extra.css");
    fs.writeFileSync(cssFile, "html, body { background: #ff0000; }");
    const out = path.join(dir, "css.png");
    await cli(["# Hi", "--markdown", "--markdown-css", cssFile, "-o", out, "-w", "200"]);
    expect(palette(out)[0]).toBe("#ff0000");
  });

  it("writes a paged PDF for a .md input", async () => {
    fs.writeFileSync(path.join(dir, "long.md"), Array.from({ length: 60 }, (_, i) => `Paragraph ${i}.\n`).join("\n"));
    const out = path.join(dir, "long.pdf");
    await cli([path.join(dir, "long.md"), "-o", out, "-w", "400"]);
    const text = fs.readFileSync(out).toString("latin1");
    expect(text.startsWith("%PDF-")).toBe(true);
    expect((text.match(/\/Type \/Page\b(?!s)/g) ?? []).length).toBeGreaterThan(1);
  });

  it("typesets $...$ / $$...$$ with KaTeX, and --no-math leaves the dollars as text", async () => {
    // A display fraction is far taller than the plain line of text the same source gives with --no-math.
    const md = path.join(dir, "math.md");
    fs.writeFileSync(md, "$$\n\\frac{a}{b}\n$$\n");
    const withMath = path.join(dir, "with-math.png");
    const withoutMath = path.join(dir, "no-math.png");
    await cli([md, "-o", withMath, "-w", "300"]);
    await cli([md, "-o", withoutMath, "-w", "300", "--no-math"]);
    const height = (file: string) => fs.readFileSync(file).readUInt32BE(20);
    expect(height(withMath)).toBeGreaterThan(height(withoutMath) + 20);
  });

  it("--template changes the PDF: its page size, a cover, and the header and footer on every page", async () => {
    const md = path.join(dir, "tpl.md");
    fs.writeFileSync(md, "---\ntitle: Quarterly\nauthor: Ann\n---\n\n# One\n\ntext\n\n# Two\n\ntext\n");
    const plain = path.join(dir, "plain.pdf");
    const report = path.join(dir, "report.pdf");
    await cli([md, "-o", plain]);
    await cli([md, "-o", report, "--template", "report", "--stamp", "DRAFT"]);
    const pdf = (f: string) => fs.readFileSync(f).toString("latin1");
    const pageBoxes = (f: string) => [...pdf(f).matchAll(/\/MediaBox\s*\[\s*0 0 ([\d.]+) ([\d.]+)\s*\]/g)].map((m) => [+m[1], +m[2]]);
    // report is A4 (595 x 842 pt); the plain document takes the render width (800px -> 600pt) and 1123px (842pt).
    expect(pageBoxes(report).every(([w, h]) => Math.abs(w - 595.3) < 1 && Math.abs(h - 841.9) < 1)).toBe(true);
    expect(pageBoxes(report).length).toBeGreaterThan(2); // cover, contents, text
    expect(pageBoxes(plain).length).toBeGreaterThanOrEqual(1);
    // The report has a header and footer on every page: more text objects than the same pages without.
    expect(fs.statSync(report).size).toBeGreaterThan(fs.statSync(plain).size);
    await expect(cli([md, "-o", path.join(dir, "x.pdf"), "--template", "nope"])).rejects.toMatchObject({
      stderr: expect.stringContaining("Choose from: default, report, proposal, book, paper, compact"),
    });
  });

  it("--list-templates prints the bundled templates and needs no input", async () => {
    const { stdout } = await cli(["--list-templates"]);
    expect(stdout.split("\n").filter(Boolean).map((l) => l.split(/\s+/)[0])).toEqual(["default", "report", "proposal", "book", "paper", "compact"]);
  });

  it("rejects an unknown --markdown-theme with a readable error", async () => {
    await expect(
      cli(["# Hi", "--markdown", "--markdown-theme", "blue", "-o", path.join(dir, "x.png")]),
    ).rejects.toMatchObject({ stderr: expect.stringContaining("unsupported markdown theme") });
  });
});
