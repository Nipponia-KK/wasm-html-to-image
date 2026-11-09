import { describe, expect, it } from "vitest";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { markdownStylesheet, markdownToHtml } from "../src/markdown";
import { buildRenderOptions, isInlineMarkdown, isMarkdownPath } from "../src/cli-options";

describe("markdownToHtml", () => {
  it("renders GitHub flavoured Markdown inside a styled document", () => {
    const html = markdownToHtml(
      "# Title\n\n- [x] done\n\n| a | b |\n|---|---|\n| 1 | 2 |\n\n```js\nlet x = 1;\n```\n",
      { title: "A <b>" },
    );
    expect(html).toMatch(/^<!doctype html>/);
    expect(html).toContain("<title>A &lt;b&gt;</title>");
    expect(html).toContain("<h1 id=\"title\">Title</h1>");
    expect(html).toContain("<table>");
    expect(html).toContain('type="checkbox"');
    expect(html).toContain('<pre><code class="language-js">');
    expect(html).toContain('<article class="markdown-body">');
  });

  it("themes and extends the stylesheet", () => {
    expect(markdownToHtml("x", { theme: "dark" })).toContain("background: #0d1117");
    expect(markdownToHtml("x", { css: "h1{color:red}" })).toContain("h1{color:red}</style>");
    expect(markdownToHtml("x", { stylesheet: "p{}", theme: "dark" })).not.toContain("#0d1117");
    expect(markdownStylesheet("light", 10, 600)).toContain("max-width: 600px");
  });

  it("paged output moves the padding into an @page margin", () => {
    const paged = markdownToHtml("x", { paged: true, padding: 40 });
    expect(paged).toContain("@page { margin: 40px; }");
    expect(paged).toMatch(/\.markdown-body \{ padding: 0px;/);
    const plain = markdownToHtml("x", { padding: 40 });
    expect(plain).not.toContain("@page");
    expect(plain).toMatch(/\.markdown-body \{ padding: 40px;/);
  });

  it("turns mermaid fences into diagram blocks unless switched off", () => {
    const md = "```mermaid\ngraph TD\n  A --> B & C\n```\n";
    const html = markdownToHtml(md);
    expect(html).toContain('<div class="mermaid">graph TD\n  A --&gt; B &amp; C</div>');
    expect(html).not.toContain("<pre><code");
    expect(markdownToHtml(md, { mermaid: false })).toContain('<pre><code class="language-mermaid">');
    expect(markdownToHtml(md, { highlight: false })).toContain('class="mermaid"');
  });

  it("highlights fenced code with a known language and leaves the rest escaped", () => {
    const html = markdownToHtml(
      '```ts\nconst s: string = "a<b"; // c\n```\n\n```nope\n<b>x</b>\n```\n\n```\nplain\n```',
    );
    expect(html).toContain('<code class="language-ts"><span class="hljs-keyword">const</span>');
    expect(html).toContain('<span class="hljs-comment">// c</span>');
    expect(html).toContain("&lt;"); // string content stays escaped inside the highlighted markup
    expect(html).toContain('<code class="language-nope">&lt;b&gt;x&lt;/b&gt;');
    expect(html).toContain("<pre><code>plain");
    // (the built-in stylesheet itself mentions .hljs-keyword, so look at the code block)
    expect(markdownToHtml("```ts\nconst a = 1;\n```", { highlight: false })).not.toContain(
      '<span class="hljs-keyword">',
    );
    expect(markdownToHtml("```js\nlet a;\n```")).toContain("hljs-keyword"); // alias of javascript
  });

  it("strips YAML front matter and uses its title", () => {
    const md = '---\ntitle: "My doc"\nauthor: x\n---\n\n# Body\n';
    const html = markdownToHtml(md);
    expect(html).toContain("<title>My doc</title>");
    expect(html).toContain("<h1 id=\"body\">Body</h1>");
    expect(html).not.toContain("author");
    expect(markdownToHtml(md, { title: "Own" })).toContain("<title>Own</title>");
    expect(markdownToHtml("---\r\ntitle: A\r\n---\r\nx")).toContain("<title>A</title>");
    // Off, or not at the very start: it is ordinary Markdown (a rule and text).
    expect(markdownToHtml(md, { frontMatter: false })).toContain("<hr>");
    expect(markdownToHtml("text\n\n---\ntitle: no\n---\n")).not.toContain("<title>");
  });

  it("needs nothing but marked and highlight.js, so it runs in browsers and Workers", () => {
    const source = fs.readFileSync(
      fileURLToPath(new URL("../src/markdown.ts", import.meta.url)),
      "utf8",
    );
    const imports = [...source.matchAll(/^import .* from "([^"]+)"/gm)].map((m) => m[1]);
    // (its own ./markdown-math-syntax.js is the maths syntax: it imports only marked's types; KaTeX is not here)
    expect(imports.every((m) => m === "marked" || m.startsWith("highlight.js/") || m === "./markdown-math-syntax.js")).toBe(true);
    const syntax = fs.readFileSync(fileURLToPath(new URL("../src/markdown-math-syntax.ts", import.meta.url)), "utf8");
    expect([...syntax.matchAll(/^import .* from "([^"]+)"/gm)].map((m) => m[1])).toEqual(["marked"]);
    expect(source).not.toMatch(/\bprocess\.|\brequire\(|node:/);
  });
});

describe("markdown CLI input", () => {
  const flags = { width: 800, format: "png" as const, quality: 85 };

  it("recognises Markdown paths and inline text", async () => {
    expect(isMarkdownPath("doc/README.md")).toBe(true);
    expect(isMarkdownPath("a.MARKDOWN")).toBe(true);
    expect(isMarkdownPath("a.html")).toBe(false);
    expect(isMarkdownPath("https://example.com/a.md")).toBe(false);
    expect(await isInlineMarkdown("# hi", { markdown: true })).toBe(true);
    expect(await isInlineMarkdown("# hi", {})).toBe(false);
  });

  it("converts a .md file and inline --markdown text before rendering", async () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "md-"));
    const file = path.join(dir, "a.md");
    fs.writeFileSync(file, "# From file\n");
    const fromFile = await buildRenderOptions(file, flags);
    expect(String(fromFile.value)).toContain("<h1 id=\"from-file\">From file</h1>");
    expect(fromFile.baseUrl).toBe(dir);
    const pdf = await buildRenderOptions(file, { ...flags, format: "pdf" });
    expect(String(pdf.value)).toContain("@page { margin: 32px; }");
    expect(String(fromFile.value)).not.toContain("@page");
    const inline = await buildRenderOptions("**bold**", { ...flags, markdown: true, markdownTheme: "dark" });
    expect(String(inline.value)).toContain("<strong>bold</strong>");
    expect(String(inline.value)).toContain("#0d1117");
    await expect(
      buildRenderOptions("x", { ...flags, markdown: true, markdownTheme: "blue" }),
    ).rejects.toThrow(/unsupported markdown theme/);
  });
});
