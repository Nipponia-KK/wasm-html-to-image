import React, { useEffect, useRef, useState } from "react";
import { listTemplates, markdownToDocument } from "wasm-html-to-image/markdown";
import { render } from "wasm-html-to-image/workers";
import { toMarkdownOptions, type MarkdownPanel } from "./markdownSettings";

interface Props {
  text: string;
  panel: MarkdownPanel;
  math: Parameters<typeof toMarkdownOptions>[1];
  /** Called with the template's name when its card is clicked. */
  onPick: (name: string) => void;
}

interface Card {
  name: string;
  description: string;
  url?: string;
  pages?: number;
  error?: string;
}

/**
 * The document drawn with every bundled template, one after another: each card shows the first page of the
 * PDF (the browser's own viewer), so the templates can be compared on the same text. A click picks one.
 */
export const TemplateGallery: React.FC<Props> = ({ text, panel, math, onPick }) => {
  const templates = useRef(listTemplates()).current;
  const [cards, setCards] = useState<Record<string, Card>>({});
  const urls = useRef<string[]>([]);

  useEffect(() => {
    let cancelled = false;
    const timer = setTimeout(async () => {
      setCards({});
      for (const u of urls.current) URL.revokeObjectURL(u);
      urls.current = [];
      for (const t of templates) {
        if (cancelled) return;
        try {
          const doc = markdownToDocument(text, { ...toMarkdownOptions(panel, math), template: t.name });
          const result = await render({
            value: doc.html,
            width: panel.width,
            format: "pdf",
            pdfOutline: false,
            ...doc.renderOptions,
          });
          if (cancelled) return;
          const bytes = result.data as Uint8Array;
          const url = URL.createObjectURL(new Blob([bytes.slice()], { type: "application/pdf" }));
          urls.current.push(url);
          setCards((c) => ({ ...c, [t.name]: { name: t.name, description: t.description, url } }));
        } catch (e) {
          if (!cancelled) setCards((c) => ({ ...c, [t.name]: { name: t.name, description: t.description, error: e instanceof Error ? e.message : String(e) } }));
        }
      }
    }, 600);
    return () => {
      cancelled = true;
      clearTimeout(timer);
    };
    // the template itself is what the gallery varies; every other setting applies to all cards
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [text, panel.theme, panel.pageSize, panel.accent, panel.font, panel.stamp, panel.cover, panel.toc, panel.tocDepth, panel.math, panel.highlight, panel.width, math, templates]);

  useEffect(
    () => () => {
      for (const u of urls.current) URL.revokeObjectURL(u);
    },
    [],
  );

  const done = Object.keys(cards).length;
  return (
    <div className="flex flex-col gap-3 rounded-lg border border-gray-200 bg-white p-3">
      <p className="m-0 text-xs text-gray-500">
        {done} / {templates.length} templates drawn from the Markdown on the left (the first page of each). Click a card to use that template.
      </p>
      <div className="grid grid-cols-2 gap-3 xl:grid-cols-3">
        {templates.map((t) => {
          const card = cards[t.name];
          const selected = panel.template === t.name;
          return (
            <button
              key={t.name}
              type="button"
              onClick={() => onPick(t.name)}
              className={`flex cursor-pointer flex-col overflow-hidden rounded-lg border text-left transition-shadow hover:shadow-md ${
                selected ? "border-blue-600 ring-2 ring-blue-200" : "border-gray-200"
              }`}
            >
              <span className="border-b border-gray-200 bg-gray-50 px-3 py-1.5 text-xs">
                <span className="font-semibold text-gray-800">{t.name}</span>
                <span className="block truncate text-gray-500" title={t.description}>
                  {t.description}
                </span>
              </span>
              <span className="relative block h-72 bg-gray-100">
                {card?.url ? (
                  <embed src={`${card.url}#page=1&view=Fit&toolbar=0&navpanes=0&scrollbar=0`} type="application/pdf" className="pointer-events-none h-full w-full border-0" />
                ) : (
                  <span className="flex h-full items-center justify-center px-2 text-center text-xs text-gray-400">{card?.error ?? "Rendering…"}</span>
                )}
              </span>
            </button>
          );
        })}
      </div>
    </div>
  );
};

export default TemplateGallery;
