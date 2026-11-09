/**
 * Templates for Markdown -> PDF: a look you pick by name (`template: report` in the front matter, or
 * `template` in the options) and adjust with a few settings, without writing CSS.
 *
 * A template is plain data (it works in Node, browsers and Workers alike) made of three parts:
 *  - `type`: typography and structure (fonts, size, line height, how headings and the first page look);
 *  - `page`: the PDF page (size, margins, running header and footer);
 *  - `defaults`: document settings the template starts from (a cover, a contents page, an accent colour).
 * Colours are not part of a template: light and dark are a separate `theme`, so every template comes in both.
 */

/** Document settings: what the front matter, a template's `defaults` and the options all speak. */
export interface DocumentSettings {
  title?: string;
  /** A line under the title on the cover. */
  subtitle?: string;
  author?: string;
  /** Shown as written (on the cover and in `{{date}}`). */
  date?: string;
  /** Name of a bundled template (see {@link TEMPLATES}). */
  template?: string;
  /** A table of contents (see `MarkdownOptions.toc`). */
  toc?: boolean;
  /** A cover page with the title, subtitle, author, date and logo. Needs a title. */
  cover?: boolean;
  /** Logo image for the cover and the header: a URL or a `data:` URL. */
  logo?: string;
  /** Link, heading rule and cover accent colour, e.g. `#c2410c`. */
  accent?: string;
  /** Body font family (any family the renderer can load, or a generic `serif` / `sans-serif`). */
  font?: string;
  /** `A4`, `A5`, `B5`, `Letter`, ..., or two lengths such as `210mm 297mm`. */
  page_size?: string;
  /** Text printed in the header of every page (`DRAFT`, `Confidential`, ...). */
  stamp?: string;
  /** BCP 47 language tag of the text (`<html lang>`). */
  lang?: string;
  /** `ltr`, `rtl` or `auto`. */
  dir?: string;
}

/** Typography and structure of a template. Every field has a default, so a template lists only what differs. */
export interface TemplateType {
  /** Body font stack. `serif` / `sans-serif` resolve to the renderer's Noto Serif / Sans JP. */
  font: string;
  /** Heading font stack; the body font when omitted. */
  headingFont?: string;
  /** Code font stack. */
  mono: string;
  /** Body size in px. */
  size: number;
  /** Unitless line height. */
  lineHeight: number;
  /** Justify paragraphs (and hyphenate nothing: the renderer breaks CJK text anywhere). */
  justify: boolean;
  /** A rule under h1 / h2 (GitHub style). */
  h1Rule: boolean;
  h2Rule: boolean;
  /** Heading weight. */
  headingWeight: number;
  /** Colour headings with the accent colour instead of the text colour. */
  accentHeadings: boolean;
  /** Every h1 starts a new page (chapters of a book). */
  chapterBreak: boolean;
  /** Corner radius of code blocks and inline code, px. */
  radius: number;
}

export interface TemplatePage {
  /** Page size when the document says nothing (`A4`). */
  size: string;
  /** Page margin: a CSS `margin` value, e.g. `20mm 18mm`. */
  margin: string;
  /** Running header HTML: `{{pageNumber}}`, `{{totalPages}}`, `{{title}}`, `{{author}}`, `{{date}}`, `{{stamp}}`. */
  header?: string;
  /** Running footer HTML, same variables. */
  footer?: string;
}

export interface MarkdownTemplate {
  /** One line for `listTemplates()`. */
  description?: string;
  type?: Partial<TemplateType>;
  page?: Partial<TemplatePage>;
  defaults?: DocumentSettings;
  /** Extra CSS appended after the generated stylesheet (cover and contents tuning, ...). */
  css?: string;
}

export const DEFAULT_TYPE: TemplateType = {
  font: "sans-serif",
  mono: "monospace",
  size: 16,
  lineHeight: 1.5,
  justify: false,
  h1Rule: true,
  h2Rule: true,
  headingWeight: 600,
  accentHeadings: false,
  chapterBreak: false,
  radius: 6,
};

export const DEFAULT_PAGE: TemplatePage = { size: "A4", margin: "32px" };

const KNOWN_KEYS = new Set<string>([
  "title",
  "subtitle",
  "author",
  "date",
  "template",
  "toc",
  "cover",
  "logo",
  "accent",
  "font",
  "page_size",
  "stamp",
  "lang",
  "dir",
]);
const BOOLEAN_KEYS = new Set(["toc", "cover"]);

/**
 * The flat `key: value` lines of a front matter block. Values may be quoted; `toc` and `cover` take
 * `true` / `false`. Lists, nested values, comments and keys this module does not know are skipped
 * (front matter is shared with other tools), so they never break a document. `page-size` is read as
 * `page_size`.
 */
export function parseFrontMatter(block: string): DocumentSettings {
  const out: Record<string, string | boolean> = {};
  for (const line of block.split(/\r?\n/)) {
    const m = /^([A-Za-z_][\w-]*):[ \t]*(.*?)[ \t]*$/.exec(line);
    if (!m) continue; // blank, comment, indented (nested) or a list item
    const key = m[1].replace(/-/g, "_");
    if (!KNOWN_KEYS.has(key)) continue;
    let value = m[2];
    const quoted = /^(["'])(.*)\1$/.exec(value);
    if (quoted) value = quoted[2];
    else value = value.replace(/[ \t]+#.*$/, ""); // a trailing comment on an unquoted value
    if (value === "") continue;
    if (BOOLEAN_KEYS.has(key)) {
      if (!quoted && (value === "true" || value === "false")) out[key] = value === "true";
    } else out[key] = value;
  }
  return out as DocumentSettings;
}

/** `<br>` and `<br/>` break a title line, everything else is text. */
export function titleHtml(title: string): string {
  return title
    .split(/<br\s*\/?>/i)
    .map(escapeHtml)
    .join("<br>");
}

export const escapeHtml = (text: string) =>
  text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");

/**
 * Fill `{{title}}`, `{{author}}`, `{{date}}` and `{{stamp}}` of a running header or footer. The page
 * numbers (`{{pageNumber}}`, `{{totalPages}}`) are left for the renderer, which knows the pages.
 */
export function fillRunningText(template: string, settings: DocumentSettings): string {
  const vars: Record<string, string | undefined> = {
    title: settings.title && titleHtml(settings.title).replace(/<br>/g, " "),
    author: settings.author && escapeHtml(settings.author),
    date: settings.date && escapeHtml(settings.date),
    stamp: settings.stamp && escapeHtml(settings.stamp),
  };
  return template.replace(/\{\{(title|author|date|stamp)\}\}/g, (_, k: string) => vars[k] ?? "");
}

const RUNNING = "font-size:11px;color:#6b7280;font-family:sans-serif;line-height:1.2;";

/**
 * The bundled templates. A name picks one (`template: book`); anything it does not set comes from the
 * default. Typography is original to this package, not borrowed from another tool.
 */
export const TEMPLATES: Record<string, MarkdownTemplate> = {
  default: {
    description: "GitHub-like page, good for images and quick PDFs",
  },

  report: {
    description: "Business document: cover, contents, the title in the header and the page number in the footer",
    type: { size: 15, lineHeight: 1.65, h1Rule: false, h2Rule: false, accentHeadings: true, headingWeight: 700, radius: 4 },
    page: {
      margin: "26mm 22mm 24mm",
      header: `<div style="${RUNNING}display:flex;justify-content:space-between;padding-top:8mm"><span>{{title}}</span><span>{{stamp}}</span></div>`,
      footer: `<div style="${RUNNING}display:flex;justify-content:space-between;padding-bottom:8mm"><span>{{author}}</span><span>{{pageNumber}} / {{totalPages}}</span></div>`,
    },
    defaults: { cover: true, toc: true, accent: "#1d4ed8" },
    css: `.markdown-body h2 { margin-top: 32px; padding-bottom: 6px; border-bottom: 2px solid var(--accent, #1d4ed8); }
.markdown-body h1 { margin-top: 40px; }
.cover { margin: 60mm 0 0; padding-left: 14px; border-left: 6px solid var(--accent, #1d4ed8); }`,
  },

  proposal: {
    description: "Proposal / quotation: cover, accent bars on sections, tinted table headers and call-out quotes",
    type: { size: 14, lineHeight: 1.7, h1Rule: false, h2Rule: false, accentHeadings: true, headingWeight: 700, radius: 4 },
    page: {
      margin: "24mm 20mm 22mm",
      header: `<div style="${RUNNING}text-align:right;padding-top:8mm">{{stamp}}</div>`,
      footer: `<div style="${RUNNING}display:flex;justify-content:space-between;padding-bottom:8mm"><span>{{title}}</span><span>{{pageNumber}} / {{totalPages}}</span></div>`,
    },
    defaults: { cover: true, toc: false, accent: "#0f766e" },
    css: `.markdown-body h1 { margin-top: 36px; padding: 2px 0 2px 12px; border-left: 6px solid var(--accent, #0f766e); font-size: 1.7em; }
.markdown-body h2 { margin-top: 28px; font-size: 1.25em; }
.markdown-body th { color: #ffffff; background: var(--accent, #0f766e); }
.markdown-body tr:nth-child(2n) { background: #f1f5f9; }
.markdown-body blockquote { padding: 10px 16px; border-left: 4px solid var(--accent, #0f766e); background: #f1f5f9; }
.cover { margin: 70mm 0 0; padding: 0 0 0 14px; border-left: 8px solid var(--accent, #0f766e); }`,
  },

  book: {
    description: "Book: serif text, justified, every chapter (h1) on a new page, inner-margin friendly page",
    type: { font: "serif", size: 15, lineHeight: 1.8, justify: true, h1Rule: false, h2Rule: false, headingWeight: 700, chapterBreak: true, radius: 3 },
    page: {
      size: "A5",
      margin: "20mm 17mm 22mm",
      footer: `<div style="${RUNNING}text-align:center;padding-bottom:9mm">{{pageNumber}}</div>`,
    },
    defaults: { cover: true, toc: true, accent: "#7c2d12" },
    css: `.markdown-body h1 { margin: 0 0 1.4em; font-size: 1.9em; text-align: center; }
.markdown-body h2 { margin-top: 1.8em; font-size: 1.3em; }
.markdown-body p { margin-bottom: 0; text-indent: 1em; }
.markdown-body p:first-child, .markdown-body h1 + p, .markdown-body h2 + p, .markdown-body h3 + p { text-indent: 0; }
.markdown-body p + p { margin-top: 0; }
.cover { margin: 45mm 0 0; text-align: center; }`,
  },

  paper: {
    description: "Paper / technical note: serif, justified, tight leading, numbers in the footer",
    type: { font: "serif", size: 13, lineHeight: 1.55, justify: true, h1Rule: false, h2Rule: false, headingWeight: 700, radius: 2 },
    page: {
      margin: "22mm 20mm",
      footer: `<div style="${RUNNING}text-align:center;padding-bottom:8mm">{{pageNumber}}</div>`,
    },
    defaults: { cover: false, toc: false },
    css: `.markdown-body h1 { margin: 0 0 0.6em; font-size: 1.8em; text-align: center; }
.markdown-body h2 { margin-top: 1.4em; margin-bottom: 0.5em; font-size: 1.2em; }
.markdown-body h3 { margin-top: 1.1em; margin-bottom: 0.4em; font-size: 1.05em; font-style: italic; }
.markdown-body p { margin-bottom: 0.7em; }
.markdown-body table { font-size: 0.92em; }`,
  },

  compact: {
    description: "Notes and cheat sheets: small text, narrow margins, no cover or contents",
    type: { size: 12, lineHeight: 1.4, h1Rule: false, h2Rule: false, headingWeight: 600, radius: 3 },
    page: { margin: "12mm 12mm" },
    defaults: { cover: false, toc: false },
    css: `.markdown-body h1 { margin: 0.8em 0 0.4em; font-size: 1.5em; }
.markdown-body h2 { margin: 0.9em 0 0.35em; font-size: 1.25em; }
.markdown-body h3 { margin: 0.8em 0 0.3em; font-size: 1.1em; }
.markdown-body p, .markdown-body ul, .markdown-body ol, .markdown-body blockquote, .markdown-body pre, .markdown-body table { margin-bottom: 8px; }
.markdown-body th, .markdown-body td { padding: 3px 8px; }
.markdown-body pre { padding: 8px 10px; }`,
  },
};

/** The names of the bundled templates, each with its one-line description. */
export function listTemplates(): { name: string; description: string }[] {
  return Object.entries(TEMPLATES).map(([name, t]) => ({ name, description: t.description ?? "" }));
}

/** A template by name (a bundled one) or as given. An unknown name is an error that lists the choices. */
export function resolveTemplate(template: string | MarkdownTemplate | undefined): MarkdownTemplate {
  if (template === undefined) return TEMPLATES.default;
  if (typeof template !== "string") return template;
  const found = TEMPLATES[template];
  if (!found) throw new Error(`unknown template "${template}". Choose from: ${Object.keys(TEMPLATES).join(", ")}`);
  return found;
}
