import type { MarkedExtension } from "marked";
import { escapeHtml } from "./markdown-template.js";

/** The kinds of call-out box: GitHub's `> [!NOTE]` alerts. */
export const CALLOUT_KINDS = ["note", "tip", "important", "warning", "caution"] as const;
export type CalloutKind = (typeof CALLOUT_KINDS)[number];

const TITLES: Record<"en" | "ja", Record<CalloutKind, string>> = {
  en: { note: "Note", tip: "Tip", important: "Important", warning: "Warning", caution: "Caution" },
  ja: { note: "補足", tip: "ヒント", important: "重要", warning: "注意", caution: "警告" },
};

const MARKER = /^<p>\s*\[!(NOTE|TIP|IMPORTANT|WARNING|CAUTION)\][ \t]*(?:<br\s*\/?>|\n)?\s*/i;

/**
 * A block quote that starts with `[!NOTE]` (or TIP, IMPORTANT, WARNING, CAUTION) becomes a call-out:
 * a titled box. Returns undefined for an ordinary quote. @p body is the quote's rendered HTML.
 */
export function calloutHtml(body: string, lang: string | undefined): string | undefined {
  const m = MARKER.exec(body);
  if (!m) return undefined;
  const kind = m[1].toLowerCase() as CalloutKind;
  const rest = body.slice(m[0].length);
  const inner = rest.startsWith("</p>") ? rest.slice(4) : `<p>${rest}`;
  const title = TITLES[lang?.toLowerCase().startsWith("ja") ? "ja" : "en"][kind];
  return `<div class="callout callout-${kind}"><p class="callout-title">${title}</p>${inner}</div>\n`;
}

/** The label of a numbered figure (`Figure 1`, `図 1`). */
export function figureLabel(n: number, lang: string | undefined): string {
  return lang?.toLowerCase().startsWith("ja") ? `図 ${n}` : `Figure ${n}`;
}

export function pagebreakHtml(raw: string): string | undefined {
  return /^\s*<!--\s*(?:page-?break|new-?page)\s*-->\s*$/i.test(raw) ? `<div class="pagebreak"></div>\n` : undefined;
}

export interface Footnotes {
  /** Marked extension: `[^id]` turns into a numbered reference when the id has a definition. */
  extension: MarkedExtension;
  /** Takes the `[^id]: text` definitions out of the source (code fences are left alone) and returns the rest. */
  extract(source: string): string;
  /** The footnotes list for the references seen so far, or an empty string. @p inline renders a footnote's text. */
  list(inline: (text: string) => string, heading?: string, page?: boolean): string;
}

/** Footnotes collected at the end of the document: `[^1]` in the text, `[^1]: the note` anywhere. */
export function createFootnotes(): Footnotes {
  const defs = new Map<string, string>();
  const numbers = new Map<string, number>();
  const order: string[] = [];
  const uses = new Map<string, number>();
  return {
    extract(source) {
      const kept: string[] = [];
      let fence = "";
      let current: string | undefined;
      for (const line of source.split("\n")) {
        const f = /^\s{0,3}(```+|~~~+)/.exec(line);
        if (f) {
          if (!fence) fence = f[1][0];
          else if (f[1][0] === fence) fence = "";
        }
        const d = fence ? null : /^\[\^([^\]\s]+)\]:[ \t]*(.*)$/.exec(line);
        if (d) {
          current = d[1];
          defs.set(current, d[2].trim());
          continue;
        }
        if (current !== undefined && /^(?: {2,}|\t)\S/.test(line)) {
          defs.set(current, `${defs.get(current)} ${line.trim()}`);
          continue;
        }
        current = undefined;
        kept.push(line);
      }
      return kept.join("\n");
    },
    extension: {
      extensions: [
        {
          name: "footnoteRef",
          level: "inline",
          start: (src: string) => src.indexOf("[^"),
          tokenizer(src: string) {
            const m = /^\[\^([^\]\s]+)\]/.exec(src);
            if (!m || !defs.has(m[1])) return undefined;
            return { type: "footnoteRef", raw: m[0], id: m[1] };
          },
          renderer(token) {
            const id = String((token as unknown as { id: string }).id);
            if (!numbers.has(id)) {
              numbers.set(id, numbers.size + 1);
              order.push(id);
            }
            const n = numbers.get(id)!;
            const k = (uses.get(id) ?? 0) + 1;
            uses.set(id, k);
            return `<sup class="fnref"><a id="fnref-${n}-${k}" href="#fn-${n}">${n}</a></sup>`;
          },
        },
      ],
    },
    list(inline, heading, page) {
      if (!order.length) return "";
      const items = order.map((id) => `<li id="fn-${numbers.get(id)}">${inline(defs.get(id) ?? "")}</li>`).join("");
      // `page`: the PDF engine sets each footnote at the foot of the page that refers to it, and does not print the list itself
      if (page) return `<section class="footnotes" data-pdf-footnotes><ol>${items}</ol></section>\n`;
      return `<section class="footnotes">${heading ? `<p class="footnotes-title">${escapeHtml(heading)}</p>` : ""}<ol>${items}</ol></section>\n`;
    },
  };
}
