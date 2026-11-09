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
  /** Running header HTML for this document (replaces the template's); `{{title}}`, `{{pageNumber}}`, ... work. */
  header?: string;
  /** Running footer HTML for this document (replaces the template's). */
  footer?: string;
  /** Page margin: a CSS `margin` value such as `20mm 18mm`. */
  margin?: string;
  /** `portrait` or `landscape`. */
  orientation?: string;
  /** Deepest heading level listed in the contents (1 to 6). */
  toc_depth?: number;
  /** Heading of the contents page (`Contents`). */
  toc_title?: string;
  /** Number the headings: `1`, `1.1`, `1.1.1`. */
  numbering?: boolean;
  /** Where footnotes go in a PDF: `end` (a list after the text, the default) or `page` (the foot of the page that refers to them). */
  footnotes?: string;
  /** Colour theme of the Mermaid diagrams (`nord`, `dracula`, ...) instead of the document's light / dark colours. */
  mermaid_theme?: string;
  /** How much a broken diagram is forgiven: `strict`, `compat` or `recover`. */
  mermaid_mode?: string;
  /** Honour `%%{init}%%` and frontmatter `config:` inside the diagrams. */
  mermaid_directives?: boolean;
  /** Look of the cover: `band` (a bar at the left), `plain`, or `center`. A template picks its own. */
  cover_style?: string;
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
  /** Tables span the text width, and their header cells keep to one line (reports, proposals, books). */
  wideTables: boolean;
}

export interface TemplatePage {
  /** Page size when the document says nothing (`A4`). */
  size: string;
  /** Page margin: a CSS `margin` value, e.g. `20mm 18mm`. */
  margin: string;
  /** Running header HTML: `{{pageNumber}}`, `{{totalPages}}`, `{{chapter}}` (the h1 the page is in), `{{title}}`, `{{author}}`, `{{date}}`, `{{stamp}}`. */
  header?: string;
  /** Running footer HTML, same variables. */
  footer?: string;
}

export interface MarkdownTemplate {
  /** Start from a bundled template (`"report"`): this one's `type`, `page` and `defaults` are laid over it field by field, and its `css` follows the base's. */
  extends?: string;
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
  wideTables: false,
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
  "header",
  "footer",
  "margin",
  "orientation",
  "toc_depth",
  "toc_title",
  "cover_style",
  "numbering",
  "footnotes",
  "mermaid_theme",
  "mermaid_mode",
  "mermaid_directives",
]);
const BOOLEAN_KEYS = new Set(["toc", "cover", "numbering", "mermaid_directives"]);
const NUMBER_KEYS = new Set(["toc_depth"]);

/**
 * The flat `key: value` lines of a front matter block. Values may be quoted; `toc` and `cover` take
 * `true` / `false`. Lists, nested values, comments and keys this module does not know are skipped
 * (front matter is shared with other tools), so they never break a document. `page-size` is read as
 * `page_size`.
 */
export function parseFrontMatter(block: string): DocumentSettings {
  const out: Record<string, string | boolean | number> = {};
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
    } else if (NUMBER_KEYS.has(key)) {
      const n = Number(value);
      if (Number.isInteger(n) && n >= 1 && n <= 6) out[key] = n;
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
    type: { size: 15, lineHeight: 1.65, h1Rule: false, h2Rule: false, accentHeadings: true, headingWeight: 700, radius: 4, wideTables: true },
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
    type: { size: 14, lineHeight: 1.7, h1Rule: false, h2Rule: false, accentHeadings: true, headingWeight: 700, radius: 4, wideTables: true },
    page: {
      margin: "24mm 20mm 22mm",
      header: `<div style="${RUNNING}text-align:right;padding-top:8mm">{{stamp}}</div>`,
      footer: `<div style="${RUNNING}display:flex;justify-content:space-between;padding-bottom:8mm"><span>{{title}}</span><span>{{pageNumber}} / {{totalPages}}</span></div>`,
    },
    defaults: { cover: true, toc: false, accent: "#0f766e" },
    css: `.markdown-body h1 { margin-top: 36px; padding: 2px 0 2px 12px; border-left: 6px solid var(--accent, #0f766e); font-size: 1.7em; }
.markdown-body h2 { margin-top: 28px; font-size: 1.25em; }
.markdown-body th { color: #ffffff; background: var(--accent, #0f766e); }
.markdown-body tr:nth-child(2n) { background: var(--surface, #f1f5f9); }
.markdown-body blockquote { padding: 10px 16px; border-left: 4px solid var(--accent, #0f766e); background: var(--surface, #f1f5f9); }
.cover { margin: 70mm 0 0; padding: 0 0 0 14px; border-left: 8px solid var(--accent, #0f766e); }`,
  },

  book: {
    description: "Book: serif text, justified, every chapter (h1) on a new page, inner-margin friendly page",
    type: { font: "serif", size: 15, lineHeight: 1.8, justify: true, h1Rule: false, h2Rule: false, headingWeight: 700, chapterBreak: true, radius: 3, wideTables: true },
    page: {
      size: "A5",
      margin: "20mm 17mm 22mm",
      header: `<div style="${RUNNING}display:flex;justify-content:space-between;padding-top:9mm"><span>{{title}}</span><span>{{chapter}}</span></div>`,
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
    type: { font: "serif", size: 13, lineHeight: 1.55, justify: true, h1Rule: false, h2Rule: false, headingWeight: 700, radius: 2, wideTables: true },
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

  manual: {
    description: "Manual / specification: chapters on new pages, numbered headings, cover, contents, call-outs",
    type: { size: 14, lineHeight: 1.7, h1Rule: false, h2Rule: false, accentHeadings: true, headingWeight: 700, chapterBreak: true, radius: 4, wideTables: true },
    page: {
      margin: "24mm 20mm 22mm",
      header: `<div style="${RUNNING}display:flex;justify-content:space-between;padding-top:8mm"><span>{{title}}</span><span>{{stamp}}</span></div>`,
      footer: `<div style="${RUNNING}display:flex;justify-content:space-between;padding-bottom:8mm"><span>{{author}}</span><span>{{pageNumber}} / {{totalPages}}</span></div>`,
    },
    defaults: { cover: true, toc: true, numbering: true, accent: "#0369a1" },
    css: `.markdown-body h1 { margin: 0 0 20px; padding-bottom: 8px; font-size: 1.8em; border-bottom: 3px solid var(--accent, #0369a1); }
.markdown-body h2 { margin-top: 28px; padding-bottom: 4px; font-size: 1.3em; border-bottom: 1px solid var(--border); }
.markdown-body h3 { margin-top: 22px; font-size: 1.1em; }
.markdown-body th { background: var(--surface); }
.markdown-body pre { border-left: 3px solid var(--accent, #0369a1); }
.cover { margin: 55mm 0 0; padding-left: 14px; border-left: 8px solid var(--accent, #0369a1); }`,
  },

  minutes: {
    description: "Meeting minutes: no cover, a ruled title, tinted table headers, check lists for the to-dos",
    type: { size: 13, lineHeight: 1.65, h1Rule: false, h2Rule: false, accentHeadings: false, headingWeight: 700, radius: 4, wideTables: true },
    page: {
      margin: "22mm 20mm 20mm",
      header: `<div style="${RUNNING}display:flex;justify-content:space-between;padding-top:8mm"><span>{{title}}</span><span>{{date}}</span></div>`,
      footer: `<div style="${RUNNING}text-align:right;padding-bottom:8mm">{{pageNumber}} / {{totalPages}}</div>`,
    },
    defaults: { cover: false, toc: false, accent: "#475569" },
    css: `.markdown-body h1 { margin: 0 0 16px; padding-bottom: 8px; font-size: 1.6em; border-bottom: 2px solid var(--accent, #475569); }
.markdown-body h2 { margin-top: 22px; margin-bottom: 8px; padding-left: 10px; font-size: 1.1em; border-left: 4px solid var(--accent, #475569); }
.markdown-body th { background: var(--surface); }
.markdown-body ul { margin-bottom: 10px; }`,
  },

  letter: {
    description: "Business letter: serif, wide margins, no cover or contents; .right and .center classes for the address block",
    type: { font: "serif", size: 14, lineHeight: 1.9, h1Rule: false, h2Rule: false, headingWeight: 700, radius: 2 },
    page: { margin: "28mm 25mm 25mm" },
    defaults: { cover: false, toc: false },
    css: `.markdown-body h1 { margin: 1.4em 0 1.2em; font-size: 1.4em; text-align: center; letter-spacing: 0.2em; }
.markdown-body h2 { margin: 1.4em 0 0.4em; font-size: 1.1em; }
.markdown-body p { margin-bottom: 1em; }
.markdown-body .right { text-align: right; }
.markdown-body .center { text-align: center; }
.markdown-body hr { margin: 1.6em 0; border-top: 1px solid var(--border); }`,
  },

  slides: {
    description: "Slide-like pages: A4 landscape, large text, each h2 on its own page (a --- rule is ignored)",
    type: { size: 28, lineHeight: 1.5, h1Rule: false, h2Rule: false, accentHeadings: true, headingWeight: 700, radius: 6, wideTables: true },
    page: {
      size: "A4 landscape",
      margin: "16mm 20mm 18mm",
      footer: `<div style="${RUNNING}display:flex;justify-content:space-between;padding-bottom:7mm"><span>{{title}}</span><span>{{pageNumber}}</span></div>`,
    },
    defaults: { cover: true, toc: false, accent: "#1d4ed8" },
    css: `.markdown-body h1 { margin: 0 0 0.6em; font-size: 2.2em; break-before: page; }
.markdown-body h2 { margin: 0 0 0.6em; padding-bottom: 6px; font-size: 1.6em; border-bottom: 3px solid var(--accent, #1d4ed8); break-before: page; }
.markdown-body h1 + h2 { break-before: auto; }
.markdown-body h3 { font-size: 1.2em; }
.markdown-body hr { display: none; }
.markdown-body li + li { margin-top: 0.4em; }
.cover { margin: 40mm 0 0; padding-left: 18px; border-left: 10px solid var(--accent, #1d4ed8); }
.cover-title { font-size: 2.4em; }`,
  },

  invoice: {
    description: "Invoice / quotation: a title at the right, a ruled items table with the total emphasised, notes in a box",
    type: { size: 13, lineHeight: 1.6, h1Rule: false, h2Rule: false, accentHeadings: false, headingWeight: 700, radius: 4, wideTables: true },
    page: {
      margin: "22mm 20mm 20mm",
      header: `<div style="${RUNNING}text-align:right;padding-top:8mm">{{stamp}}</div>`,
      footer: `<div style="${RUNNING}display:flex;justify-content:space-between;padding-bottom:8mm"><span>{{author}}</span><span>{{pageNumber}} / {{totalPages}}</span></div>`,
    },
    defaults: { cover: false, toc: false, accent: "#0f172a" },
    css: `.markdown-body h1 { margin: 0 0 24px; font-size: 2em; text-align: right; letter-spacing: 0.3em; }
.markdown-body h2 { margin-top: 24px; margin-bottom: 8px; padding-bottom: 4px; font-size: 1.05em; border-bottom: 2px solid var(--accent, #0f172a); }
.markdown-body th { color: #ffffff; background: var(--accent, #0f172a); }
.markdown-body tr:nth-child(2n) { background: transparent; }
.markdown-body tr:last-child td { font-weight: 700; background: var(--surface); }
.markdown-body .right { text-align: right; }`,
  },
};

/** The names of the bundled templates, each with its one-line description. */
export function listTemplates(): { name: string; description: string }[] {
  return Object.entries(TEMPLATES).map(([name, t]) => ({ name, description: t.description ?? "" }));
}

/** A template that `extends` a bundled one, flattened: its own fields laid over the base's. */
function extend(t: MarkdownTemplate): MarkdownTemplate {
  const base = resolveTemplate(t.extends);
  return {
    description: t.description ?? base.description,
    type: { ...base.type, ...t.type },
    page: { ...base.page, ...t.page },
    defaults: { ...base.defaults, ...t.defaults },
    css: [base.css, t.css].filter(Boolean).join("\n"),
  };
}

/** Type-checks a template you write (`extends`, `type`, `page`, `defaults`, `css`) and returns it unchanged. */
export const defineTemplate = (template: MarkdownTemplate): MarkdownTemplate => template;

/** A template by name (a bundled one) or as given. An unknown name is an error that lists the choices. */
export function resolveTemplate(template: string | MarkdownTemplate | undefined): MarkdownTemplate {
  if (template === undefined) return TEMPLATES.default;
  if (typeof template !== "string") return template.extends === undefined ? template : extend(template);
  const found = TEMPLATES[template];
  if (!found) throw new Error(`unknown template "${template}". Choose from: ${Object.keys(TEMPLATES).join(", ")}`);
  return found;
}
