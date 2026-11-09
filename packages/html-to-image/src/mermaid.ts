/**
 * Mermaid diagrams as images or SVG. The engine draws `<div class="mermaid">` itself (flowchart, sequence,
 * state, class, ER, pie and xy charts); this module wraps a diagram source in a small page and renders it,
 * sized to the diagram.
 *
 * ```ts
 * import { renderMermaid } from "wasm-html-to-image";
 *
 * const { data: svg } = await renderMermaid("graph LR\n  A --> B"); // an SVG string
 * const { data: png } = await renderMermaid("graph LR\n  A --> B", { format: "png", theme: "dark" });
 * ```
 */

import type { HtmlToImageOptions, OutputFormat, RenderResult } from "./core.js";

export interface MermaidOptions {
  /** Colour scheme: a key of {@link MERMAID_THEMES}. Defaults to `"light"`. */
  theme?: MermaidTheme;
  /** Output format. Defaults to `"svg"` (a string); `"png"`, `"webp"`, `"pdf"`, ... give bytes. */
  format?: OutputFormat;
  /** Space around the diagram in px. Defaults to 16. */
  padding?: number;
  /** Image width in px. Defaults to the diagram's width plus the padding. */
  width?: number;
  /** Page background: a CSS colour, or `"transparent"`. Defaults to the theme's background for raster output, transparent for SVG. */
  background?: string;
  /** The accent colour of the diagram (arrows, bars); the theme's derived colour when omitted. */
  accent?: string;
  /**
   * How much a broken source is forgiven. `"strict"` (default) reports every mistake; `"compat"` also accepts a
   * keyword in the wrong case and unfinished `end` / `}` blocks; `"recover"` also fixes a misspelt keyword and skips
   * lines the diagram cannot read. What was repaired is left in the SVG as a leading `<!-- recovered: ... -->` comment.
   */
  mode?: "strict" | "compat" | "recover";
  /** Honour `%%{init: {...}}%%` and frontmatter `config:` (theme, themeVariables) written in the source. Off by default. */
  directives?: boolean;
}

/** What a render function takes: `render` of `wasm-html-to-image`, or `htmlToImage` with its module applied. */
export type RenderFn = (options: HtmlToImageOptions) => Promise<RenderResult<Uint8Array | string>>;

/** The colours the engine reads from `--mermaid-*`; the same values as the Markdown stylesheet. */
export const MERMAID_THEMES = {
  light: { fg: "#1f2328", bg: "#ffffff", border: "#d1d9e0", surface: "#f6f8fa" },
  dark: { fg: "#e6edf3", bg: "#0d1117", border: "#3d444d", surface: "#151b23" },
  neutral: { fg: "#222222", bg: "#ffffff", border: "#999999", surface: "#eeeeee" },
  forest: { fg: "#1b3a2a", bg: "#ffffff", border: "#8fbf9f", surface: "#e8f5ec", accent: "#2e8b57", line: "#2e8b57" },
  nord: { fg: "#eceff4", bg: "#2e3440", border: "#4c566a", surface: "#3b4252", accent: "#88c0d0", line: "#81a1c1" },
  dracula: { fg: "#f8f8f2", bg: "#282a36", border: "#44475a", surface: "#343746", accent: "#bd93f9", line: "#6272a4" },
  "solarized-light": { fg: "#586e75", bg: "#fdf6e3", border: "#d3cbb7", surface: "#eee8d5", accent: "#268bd2", line: "#93a1a1" },
  "solarized-dark": { fg: "#93a1a1", bg: "#002b36", border: "#0f4b5b", surface: "#073642", accent: "#2aa198", line: "#586e75" },
  "rose-pine": { fg: "#e0def4", bg: "#191724", border: "#403d52", surface: "#1f1d2e", accent: "#c4a7e7", line: "#6e6a86" },
  sepia: { fg: "#433422", bg: "#f4ecd8", border: "#d6c7a1", surface: "#ebe0c4", accent: "#b45f06", line: "#8a7a5c" },
} as const;

export type MermaidTheme = keyof typeof MERMAID_THEMES;

type ThemeColours = { fg: string; bg: string; border: string; surface: string; accent?: string; line?: string };

const escapeHtml = (text: string) => text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");

/** The page that holds one diagram, for `render({ value, ... })`. */
export function mermaidToHtml(source: string, options: Pick<MermaidOptions, "theme" | "padding" | "background" | "accent" | "mode" | "directives"> & { raster?: boolean } = {}): string {
  const c: ThemeColours = MERMAID_THEMES[options.theme ?? "light"];
  if (!c) throw new Error(`unknown mermaid theme "${options.theme}". Choose from: ${Object.keys(MERMAID_THEMES).join(", ")}`);
  const padding = options.padding ?? 16;
  const background = options.background ?? (options.raster ? c.bg : "transparent");
  const accentColour = options.accent ?? c.accent;
  const accent = (accentColour ? ` --mermaid-accent: ${accentColour};` : "") + (c.line ? ` --mermaid-line: ${c.line};` : "") + (options.mode && options.mode !== "strict" ? ` --mermaid-mode: ${options.mode};` : "") + (options.directives ? " --mermaid-directives: on;" : "");
  return (
    `<!doctype html><html><head><meta charset="utf-8"><style>` +
    `html, body { margin: 0; padding: 0; background: ${background}; }` +
    `body { padding: ${padding}px; }` +
    `.mermaid { display: inline-block; color: ${c.fg}; --mermaid-fg: ${c.fg}; --mermaid-bg: ${c.bg}; --mermaid-border: ${c.border}; --mermaid-surface: ${c.surface};${accent} }` +
    `</style></head><body><div class="mermaid">${escapeHtml(source)}</div></body></html>`
  );
}

/** The width and height of the diagram in an SVG that was rendered from {@link mermaidToHtml}. */
function diagramSize(svg: string): { width: number; height: number } | undefined {
  const m = /<svg\b[^>]*?\bwidth="([\d.]+)"[^>]*?\bheight="([\d.]+)"/.exec(svg.slice(svg.indexOf("</defs>")));
  return m ? { width: Number(m[1]), height: Number(m[2]) } : undefined;
}

/**
 * Render a Mermaid diagram with the given render function. The page is first laid out wide to learn the
 * diagram's size, then rendered at exactly that width (unless `width` is given), so the result has no
 * stray margin.
 */
export async function renderMermaidWith(
  render: RenderFn,
  source: string,
  options: MermaidOptions = {},
): Promise<RenderResult<Uint8Array | string>> {
  const format = options.format ?? "svg";
  const padding = options.padding ?? 16;
  const raster = format !== "svg";
  const value = mermaidToHtml(source, { ...options, raster });
  let width = options.width;
  if (width === undefined) {
    const probe = await render({ value, width: 4096, format: "svg" });
    const size = typeof probe.data === "string" ? diagramSize(probe.data) : undefined;
    width = size ? Math.ceil(size.width + 2 * padding) : 800;
  }
  return render({ value, width, format });
}

/** The shape of `renderMermaid` in each entry point: SVG text by default, bytes for the other formats. */
export interface RenderMermaid {
  (source: string, options?: MermaidOptions & { format?: "svg" }): Promise<RenderResult<string>>;
  (source: string, options: MermaidOptions): Promise<RenderResult<Uint8Array | string>>;
}

/** Bind {@link renderMermaidWith} to an entry point's `render`. */
export function bindRenderMermaid(render: RenderFn): RenderMermaid {
  return ((source: string, options?: MermaidOptions) => renderMermaidWith(render, source, options)) as RenderMermaid;
}
