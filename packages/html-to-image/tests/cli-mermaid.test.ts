import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { execFile } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { promisify } from "node:util";
import { isMermaidInput, isMermaidPath, mermaidOptionsFromFlags, type CliFlags } from "../src/cli-options";

const run = promisify(execFile);
const CLI = fileURLToPath(new URL("../dist/cli.js", import.meta.url));
const built = fs.existsSync(CLI);
const flags: CliFlags = { width: 800, format: "png", quality: 85 };

describe("Mermaid input for the CLI", () => {
  it("takes .mmd and .mermaid files, and --mermaid, as diagrams", () => {
    expect(isMermaidPath("a.mmd")).toBe(true);
    expect(isMermaidPath("dir/b.MERMAID")).toBe(true);
    expect(isMermaidPath("c.md")).toBe(false);
    expect(isMermaidPath("https://example.com/a.mmd")).toBe(false);
    expect(isMermaidInput("graph TD", { mermaid: true })).toBe(true);
    expect(isMermaidInput("page.html", {})).toBe(false);
  });

  it("maps the flags to renderMermaid options, and lists the choices for a wrong theme or mode", () => {
    expect(mermaidOptionsFromFlags({ ...flags, mermaidTheme: "nord", mermaidMode: "recover", mermaidDirectives: true, accent: "#ff0000" }, false)).toEqual({
      format: "png",
      theme: "nord",
      mode: "recover",
      directives: true,
      accent: "#ff0000",
    });
    expect(mermaidOptionsFromFlags(flags, true).width).toBe(800); // -w was given
    expect(mermaidOptionsFromFlags(flags, false).width).toBeUndefined(); // the default width does not apply
    expect(() => mermaidOptionsFromFlags({ ...flags, mermaidTheme: "nope" }, false)).toThrow(/Choose from: light, dark/);
    expect(() => mermaidOptionsFromFlags({ ...flags, mermaidMode: "x" }, false)).toThrow(/Choose from: strict, compat, recover/);
  });
});

describe.skipIf(!built)("wasm-html-to-image CLI with Mermaid", () => {
  let dir: string;
  beforeAll(() => {
    dir = fs.mkdtempSync(path.join(os.tmpdir(), "mmd-cli-"));
  });
  afterAll(() => fs.rmSync(dir, { recursive: true, force: true }));

  it("renders a .mmd file to SVG, sized to the diagram, and honours the theme", async () => {
    const input = path.join(dir, "flow.mmd");
    fs.writeFileSync(input, "graph LR\n  A[開始] --> B[完了]\n");
    const out = path.join(dir, "flow.svg");
    await run(process.execPath, [CLI, input, "-o", out, "--mermaid-theme", "dracula"]);
    const svg = fs.readFileSync(out, "utf-8");
    expect(svg).toContain("<svg");
    expect(svg).toContain("開始");
    expect(svg.toLowerCase()).toContain("#bd93f9"); // dracula's accent on the arrow heads
    expect(Number(/<svg[^>]*\bwidth="(\d+)"/.exec(svg)![1])).toBeLessThan(500); // not the 800 px default
  });

  it("renders inline text with --mermaid and stdin, to a PNG", async () => {
    const a = path.join(dir, "inline.png");
    await run(process.execPath, [CLI, "graph TD\n  A --> B", "--mermaid", "-o", a]);
    expect(fs.readFileSync(a).subarray(1, 4).toString()).toBe("PNG");
  });

  it("fixes a misspelt keyword only with --mermaid-mode recover", async () => {
    const input = path.join(dir, "typo.mmd");
    fs.writeFileSync(input, "flowchrt LR\n  A --> B\n");
    const strict = path.join(dir, "strict.svg");
    // the error box is still written, and the exit status says the diagram did not parse
    await expect(run(process.execPath, [CLI, input, "-o", strict])).rejects.toMatchObject({ stderr: expect.stringContaining("MERMAID_ERROR") });
    expect(fs.readFileSync(strict, "utf-8")).not.toContain("<polygon");
    const recovered = path.join(dir, "recovered.svg");
    const ran = await run(process.execPath, [CLI, input, "-o", recovered, "--mermaid-mode", "recover"]);
    expect(fs.readFileSync(recovered, "utf-8")).toContain("<polygon");
    expect(ran.stderr).toContain("MERMAID_RECOVERED"); // what was repaired is said on stderr
  });

  it("reports an unknown theme and exits with an error", async () => {
    const input = path.join(dir, "t.mmd");
    fs.writeFileSync(input, "graph LR\n  A --> B\n");
    await expect(run(process.execPath, [CLI, input, "-o", path.join(dir, "t.svg"), "--mermaid-theme", "nope"])).rejects.toMatchObject({
      stderr: expect.stringContaining("unsupported mermaid theme"),
    });
  });
});
