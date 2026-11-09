import { describe, expect, it } from "vitest";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { markdownToDocument, markdownToHtml } from "../src/markdown";
import { fillRunningText, listTemplates, parseFrontMatter, resolveTemplate, TEMPLATES } from "../src/markdown-template";
import { buildRenderOptions, loadTemplate, type CliFlags } from "../src/cli-options";
import { parseCssColor } from "../src/encode-args";

const style = (html: string) => /<style>([\s\S]*?)<\/style>/.exec(html)![1];
const article = (html: string) => /<article class="markdown-body">([\s\S]*)<\/article>/.exec(html)![1];

describe("front matter", () => {
  it("reads flat key: value lines: quotes, comments, hyphenated keys", () => {
    expect(
      parseFrontMatter(
        [
          "title: \"A: B\"",
          "author: Taro  # who",
          "page-size: A5",
          "toc: false",
          "cover: true",
          "# a comment",
          "stamp: 'DRAFT'",
          "",
        ].join("\n"),
      ),
    ).toEqual({ title: "A: B", author: "Taro", page_size: "A5", toc: false, cover: true, stamp: "DRAFT" });
  });

  it("skips what it does not know (lists, nested values, other tools' keys) and non-boolean toc/cover", () => {
    const s = parseFrontMatter("tags: [a, b]\nmeta:\n  nested: 1\n- item\ntoc: maybe\ncover: \"true\"\ntitle: T");
    expect(s).toEqual({ title: "T" });
  });
});

describe("templates", () => {
  it("lists the bundled templates with a description each", () => {
    const names = listTemplates().map((t) => t.name);
    expect(names).toEqual(["default", "report", "proposal", "book", "paper", "compact"]);
    for (const t of listTemplates()) expect(t.description.length).toBeGreaterThan(10);
  });

  it("an unknown template name is an error that lists the choices", () => {
    expect(() => resolveTemplate("nope")).toThrow(/Choose from: default, report, proposal, book, paper, compact/);
    expect(() => markdownToHtml("x", { template: "nope" })).toThrow(/unknown template/);
  });

  it("without a template the page is the plain one: no @page size, no cover", () => {
    const html = markdownToHtml("---\ntitle: T\n---\n# H", { paged: true });
    expect(style(html)).toContain("@page { margin: 32px; }");
    expect(/@page \{[^}]*size:/.test(style(html))).toBe(false); // a margin only, no page size
    expect(article(html)).not.toContain('class="cover"');
  });

  it("each template gets its own typography, page size and margins", () => {
    const sheet = (template: string) => style(markdownToHtml("# H\n\ntext", { template, paged: true }));
    expect(sheet("book")).toContain("@page { size: A5; margin: 20mm 17mm 22mm; }");
    expect(sheet("book")).toContain("font-family: serif");
    expect(sheet("book")).toMatch(/h1 \{[^}]*break-before: page/); // a chapter per page
    expect(sheet("report")).toContain("@page { size: A4; margin: 26mm 22mm 24mm; }");
    expect(sheet("compact")).toContain("font-size: 12px");
    expect(sheet("paper")).toContain("text-align: justify");
    expect(sheet("default")).not.toContain("break-before: page");
  });

  it("the theme is separate from the template: every template has a dark variant", () => {
    for (const name of Object.keys(TEMPLATES)) {
      const html = markdownToHtml("# H", { template: name, theme: "dark", paged: true });
      expect(style(html)).toContain("background: #0d1117");
    }
  });

  it("a template given as an object works like a bundled one and fills in what it leaves out", () => {
    const html = markdownToHtml("# H", {
      paged: true,
      template: { type: { size: 20 }, page: { size: "Letter", margin: "10mm" }, css: ".x{y:z}" },
    });
    expect(style(html)).toContain("font-size: 20px");
    expect(style(html)).toContain("@page { size: Letter; margin: 10mm; }");
    expect(style(html)).toContain("font-family: sans-serif"); // from the default type
    expect(style(html)).toContain(".x{y:z}");
  });
});

describe("settings: template defaults < front matter < options", () => {
  const md = "---\ntitle: From front matter\nauthor: Front\n---\n# H";
  it("the template supplies what the document does not say", () => {
    expect(markdownToDocument(md, { template: "report", paged: true }).settings).toMatchObject({
      cover: true,
      toc: true,
      accent: "#1d4ed8",
      title: "From front matter",
    });
  });

  it("front matter beats the template, options beat front matter", () => {
    const fm = "---\ntitle: T\ncover: false\naccent: '#111111'\ntemplate: report\n---\n# H";
    const s = markdownToDocument(fm, { paged: true }).settings;
    expect(s).toMatchObject({ cover: false, accent: "#111111", toc: true });
    const o = markdownToDocument(fm, { paged: true, settings: { cover: true, accent: "#222222" }, toc: false }).settings;
    expect(o).toMatchObject({ cover: true, accent: "#222222", toc: false });
  });

  it("settings.template names the template that was used, whichever of the option and the front matter chose it", () => {
    const fm = "---\ntitle: T\ntemplate: report\n---\n# H";
    expect(markdownToDocument(fm, { paged: true }).settings.template).toBe("report");
    expect(markdownToDocument(fm, { paged: true, template: "book" }).settings.template).toBe("book");
    // a template given as an object has no name; the front matter's is not what was used either
    expect(markdownToDocument(fm, { paged: true, template: { css: "" } }).settings.template).toBeUndefined();
    expect(markdownToDocument("# H", { paged: true }).settings.template).toBeUndefined();
  });

  it("a front matter template: is used, and the option wins over it", () => {
    const fm = "---\ntitle: T\ntemplate: book\n---\n# H";
    expect(style(markdownToHtml(fm, { paged: true }))).toContain("@page { size: A5;");
    expect(style(markdownToHtml(fm, { paged: true, template: "compact" }))).toContain("margin: 12mm 12mm");
  });

  it("page_size, font and accent apply to the stylesheet", () => {
    const html = markdownToHtml("---\ntitle: T\npage_size: 210mm 297mm\nfont: serif\naccent: '#c2410c'\n---\n# H", {
      template: "default",
      paged: true,
    });
    expect(style(html)).toContain("size: 210mm 297mm;");
    expect(style(html)).toMatch(/body \{[^}]*font-family: serif/);
    expect(style(html)).toContain("--accent: #c2410c");
    expect(style(html)).toContain("a { color: #c2410c");
  });
});

describe("cover", () => {
  const doc = (extra = "") =>
    markdownToHtml(`---\ntitle: First<br>Second\nsubtitle: Sub & more\nauthor: Ann\ndate: 2026-10-29\n${extra}---\n# Chapter`, {
      template: "report",
      paged: true,
    });

  it("has the title (with <br> breaks), subtitle, author and date, and comes before the contents", () => {
    const cover = /<header class="cover">[\s\S]*?<\/header>/.exec(doc())![0];
    expect(cover).toContain('<p class="cover-title">First<br>Second</p>');
    expect(cover).toContain('<p class="cover-subtitle">Sub &amp; more</p>');
    expect(cover).toContain('<span class="cover-author">Ann</span>');
    expect(cover).toContain('<span class="cover-date">2026-10-29</span>');
    expect(doc().indexOf('class="cover"')).toBeLessThan(doc().indexOf('class="toc"'));
  });

  it("is not a heading: it stays out of the contents and the outline", () => {
    expect(article(doc())).not.toMatch(/<h1[^>]*>First/);
    expect(doc().match(/data-toc-page=/g)?.length).toBe(1); // only "Chapter"
  });

  it("needs a title, and a logo is added to it", () => {
    expect(article(markdownToHtml("# Chapter", { template: "report", paged: true }))).not.toContain('class="cover"');
    expect(doc("logo: https://example.com/l.png\n")).toContain('<img class="cover-logo" src="https://example.com/l.png" alt="">');
  });

  it("cover: false removes the template's cover", () => {
    expect(doc("cover: false\n")).not.toContain('<header class="cover">');
  });
});

describe("running header and footer, PDF title", () => {
  it("fills {{title}}, {{author}}, {{date}} and {{stamp}} (escaped) and leaves the page numbers to the renderer", () => {
    const text = fillRunningText("{{title}}|{{author}}|{{date}}|{{stamp}}|{{pageNumber}}/{{totalPages}}", {
      title: "A<br>B",
      author: "<b>x</b>",
      stamp: "DRAFT",
    });
    expect(text).toBe("A B|&lt;b&gt;x&lt;/b&gt;||DRAFT|{{pageNumber}}/{{totalPages}}");
  });

  it("a template's header and footer come back as render options, with the PDF's title and author", () => {
    const { renderOptions } = markdownToDocument("---\ntitle: Report\nauthor: Ann\nstamp: DRAFT\n---\n# H", {
      template: "report",
      paged: true,
    });
    expect(renderOptions.pdfHeader).toContain("Report");
    expect(renderOptions.pdfHeader).toContain("DRAFT");
    expect(renderOptions.pdfFooter).toContain("{{pageNumber}} / {{totalPages}}");
    expect(renderOptions.pdfFooter).toContain("Ann");
    expect(renderOptions.pdfTitle).toBe("Report");
    expect(renderOptions.pdfAuthor).toBe("Ann");
  });

  it("a stamp alone gives a header even without a template; no title or template gives no render options", () => {
    expect(markdownToDocument("# H", { settings: { stamp: "DRAFT" }, paged: true }).renderOptions.pdfHeader).toContain("DRAFT");
    expect(markdownToDocument("# H", { paged: true }).renderOptions).toEqual({});
  });
});

describe("dark pages", () => {
  const bg = (o: Parameters<typeof markdownToDocument>[1]) => markdownToDocument("# H", o).renderOptions.pdfBackground;
  it("a dark PDF asks for the whole page to be painted; light, images and own stylesheets do not", () => {
    expect(bg({ theme: "dark", paged: true })).toBe("#0d1117");
    expect(bg({ theme: "light", paged: true })).toBeUndefined();
    expect(bg({ theme: "dark" })).toBeUndefined();
    expect(bg({ theme: "dark", paged: true, stylesheet: "body{}" })).toBeUndefined();
  });

  it("parseCssColor gives the engine's ARGB integer and rejects what it cannot read", () => {
    expect(parseCssColor("#0d1117")).toBe(0xff0d1117);
    expect(parseCssColor("#fff")).toBe(0xffffffff);
    expect(parseCssColor("#11223380")).toBe(0x80112233);
    expect(() => parseCssColor("red")).toThrow(/invalid colour/);
    expect(() => parseCssColor("#12")).toThrow(/invalid colour/);
  });
});

describe("the template from the CLI", () => {
  const flags: CliFlags = { width: 800, format: "pdf", quality: 85, markdown: true };
  it("--template, --page-size, --accent, --body-font, --stamp and --no-cover reach the page", async () => {
    const o = await buildRenderOptions("---\ntitle: T\n---\n# H", {
      ...flags,
      template: "report",
      pageSize: "A3",
      accent: "#abcdef",
      bodyFont: "serif",
      stamp: "SECRET",
      cover: false,
    });
    const html = String(o.value);
    expect(style(html)).toContain("size: A3;");
    expect(style(html)).toContain("--accent: #abcdef");
    expect(style(html)).toMatch(/body \{[^}]*font-family: serif/);
    expect(html).not.toContain('<header class="cover">');
    expect(o.pdfHeader).toContain("SECRET");
    expect(o.pdfFooter).toContain("{{pageNumber}}");
  });

  it("loads a .json template (type, page, defaults, css) and a .css file laid over the default", async () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "tpl-"));
    const json = path.join(dir, "mine.json");
    fs.writeFileSync(json, JSON.stringify({ type: { size: 18 }, page: { size: "B5", margin: "9mm" }, defaults: { cover: true } }));
    const css = path.join(dir, "extra.css");
    fs.writeFileSync(css, ".marker { color: red }");
    const a = await buildRenderOptions("---\ntitle: T\n---\n# H", { ...flags, template: json });
    expect(style(String(a.value))).toContain("font-size: 18px");
    expect(style(String(a.value))).toContain("@page { size: B5; margin: 9mm; }");
    expect(String(a.value)).toContain('<header class="cover">');
    const b = await buildRenderOptions("# H", { ...flags, template: css });
    expect(style(String(b.value))).toContain(".marker { color: red }");
    fs.writeFileSync(path.join(dir, "bad.json"), "{ nope");
    await expect(loadTemplate(path.join(dir, "bad.json"))).rejects.toThrow(/not valid JSON/);
    expect(await loadTemplate("book")).toBe("book"); // a name is resolved later, so the error lists the choices
    fs.rmSync(dir, { recursive: true, force: true });
  });
});
