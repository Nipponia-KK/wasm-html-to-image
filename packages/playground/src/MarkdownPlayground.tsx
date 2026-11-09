import React, { useEffect, useMemo, useRef, useState } from "react";
import { listTemplates, markdownToDocument } from "wasm-html-to-image/markdown";
import { PdfPreview } from "./PdfPreview";
import { usePdfRender } from "./usePdfRender";
import {
  PAGE_SIZES,
  panelFromParams,
  panelToParams,
  toMarkdownOptions,
  type MarkdownPanel,
  type Tri,
} from "./markdownSettings";
import reportSample from "./samples/report.md?raw";
import bookSample from "./samples/book.md?raw";
import proposalSample from "./samples/proposal.md?raw";
import diagramsSample from "./samples/diagrams.md?raw";
import technicalSample from "./samples/technical.md?raw";

const SAMPLES = [
  { id: "report", label: "Report (Japanese)", text: reportSample },
  { id: "proposal", label: "Proposal with diagrams (Japanese)", text: proposalSample },
  { id: "diagrams", label: "Mermaid diagrams", text: diagramsSample },
  { id: "book", label: "Book (Japanese)", text: bookSample },
  { id: "technical", label: "Maths and code", text: technicalSample },
] as const;

const STORAGE_KEY = "markdown-playground-text";

/** The text last typed, kept across reloads (a long text does not belong in the URL). */
const loadText = (): string => {
  try {
    return localStorage.getItem(STORAGE_KEY) ?? SAMPLES[0].text;
  } catch {
    return SAMPLES[0].text; // storage blocked (private window)
  }
};

const field = "w-full rounded border border-gray-300 bg-white px-2 py-1 text-sm";
const label = "flex flex-col gap-1 text-xs font-medium text-gray-600";

const TriSelect: React.FC<{ value: Tri; onChange: (v: Tri) => void; autoLabel?: string }> = ({
  value,
  onChange,
  autoLabel = "From document",
}) => (
  <select className={field} value={value} onChange={(e) => onChange(e.target.value as Tri)}>
    <option value="auto">{autoLabel}</option>
    <option value="on">On</option>
    <option value="off">Off</option>
  </select>
);

export const MarkdownPlayground: React.FC = () => {
  const [text, setText] = useState(loadText);
  const [panel, setPanel] = useState<MarkdownPanel>(() => panelFromParams(new URLSearchParams(window.location.search)));
  const [tab, setTab] = useState<"html" | "diagnostics" | "logs">("html");
  const [mathRenderer, setMathRenderer] = useState<Awaited<ReturnType<typeof loadMath>> | null>(null);
  const pdf = usePdfRender();
  const templates = useMemo(() => listTemplates(), []);

  const set = <K extends keyof MarkdownPanel>(key: K, value: MarkdownPanel[K]) =>
    setPanel((p) => ({ ...p, [key]: value }));

  // KaTeX is a few hundred kB: load it the first time maths is on, not with the page.
  useEffect(() => {
    if (panel.math && !mathRenderer) loadMath().then(setMathRenderer);
  }, [panel.math, mathRenderer]);

  // Settings live in the URL (shareable); the text lives in localStorage.
  useEffect(() => {
    const params = new URLSearchParams(window.location.search);
    for (const k of [...params.keys()]) if (k !== "mode") params.delete(k);
    for (const [k, v] of panelToParams(panel)) params.set(k, v);
    window.history.replaceState({}, "", `${window.location.pathname}?${params.toString()}`);
  }, [panel]);
  useEffect(() => {
    try {
      localStorage.setItem(STORAGE_KEY, text);
    } catch {
      /* storage blocked: the text is just not remembered */
    }
  }, [text]);

  // Markdown -> page + render options. A document that cannot be converted (an unknown template in the
  // front matter, ...) is shown as an error instead of crashing the page.
  const document_ = useMemo(() => {
    if (panel.math && !mathRenderer) return { pending: true as const };
    try {
      return { pending: false as const, doc: markdownToDocument(text, toMarkdownOptions(panel, mathRenderer ?? undefined)) };
    } catch (e) {
      return { pending: false as const, error: e instanceof Error ? e.message : String(e) };
    }
  }, [text, panel, mathRenderer]);

  // Convert on every change, debounced so that typing does not queue a conversion per key.
  const timer = useRef<ReturnType<typeof setTimeout>>(undefined);
  useEffect(() => {
    if (document_.pending || !("doc" in document_) || !document_.doc) return;
    const { doc } = document_;
    clearTimeout(timer.current);
    timer.current = setTimeout(() => {
      pdf.convert({
        html: doc.html,
        width: panel.width,
        options: { ...doc.renderOptions, pdfOutline: panel.outline },
      });
    }, 500);
    return () => clearTimeout(timer.current);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [document_, panel.width, panel.outline]);

  const effective = "doc" in document_ && document_.doc ? document_.doc.settings : undefined;
  const conversionError = "error" in document_ ? document_.error : null;

  return (
    <div className="flex flex-col gap-4">
      <div className="grid gap-4 lg:grid-cols-2">
        <section className="flex min-w-0 flex-col gap-3">
          <div className="flex items-center justify-between gap-2">
            <h2 className="m-0 text-base font-semibold text-gray-800">Markdown</h2>
            <select
              className="rounded border border-gray-300 bg-white px-2 py-1 text-sm"
              value=""
              onChange={(e) => {
                const s = SAMPLES.find((x) => x.id === e.target.value);
                if (s) setText(s.text);
              }}
            >
              <option value="">Load a sample…</option>
              {SAMPLES.map((s) => (
                <option key={s.id} value={s.id}>
                  {s.label}
                </option>
              ))}
            </select>
          </div>
          <textarea
            value={text}
            onChange={(e) => setText(e.target.value)}
            spellCheck={false}
            className="h-80 w-full resize-y rounded-lg border border-gray-300 bg-white p-3 font-mono text-sm leading-relaxed"
          />

          <fieldset className="rounded-lg border border-gray-200 bg-white p-3">
            <legend className="px-1 text-sm font-semibold text-gray-700">Look</legend>
            <div className="grid grid-cols-2 gap-3 sm:grid-cols-3">
              <label className={label}>
                Template
                <select className={field} value={panel.template} onChange={(e) => set("template", e.target.value)}>
                  <option value="">From document</option>
                  {templates.map((t) => (
                    <option key={t.name} value={t.name} title={t.description}>
                      {t.name}
                    </option>
                  ))}
                </select>
              </label>
              <label className={label}>
                Theme
                <select className={field} value={panel.theme} onChange={(e) => set("theme", e.target.value as "light" | "dark")}>
                  <option value="light">Light</option>
                  <option value="dark">Dark</option>
                </select>
              </label>
              <label className={label}>
                Page size
                <select className={field} value={panel.pageSize} onChange={(e) => set("pageSize", e.target.value)}>
                  <option value="">From template</option>
                  {PAGE_SIZES.map((s) => (
                    <option key={s} value={s}>
                      {s}
                    </option>
                  ))}
                </select>
              </label>
              <label className={label}>
                Accent colour
                <div className="flex gap-1">
                  <input
                    type="color"
                    className="h-7 w-9 cursor-pointer rounded border border-gray-300 bg-white p-0.5"
                    value={/^#[0-9a-f]{6}$/i.test(panel.accent) ? panel.accent : "#1d4ed8"}
                    onChange={(e) => set("accent", e.target.value)}
                  />
                  <button
                    type="button"
                    className="cursor-pointer rounded border border-gray-300 bg-white px-2 text-xs text-gray-600 hover:bg-gray-50"
                    onClick={() => set("accent", "")}
                    title="Back to the template's colour"
                  >
                    Reset
                  </button>
                </div>
              </label>
              <label className={label}>
                Body font
                <select className={field} value={panel.font} onChange={(e) => set("font", e.target.value)}>
                  <option value="">From template</option>
                  <option value="sans-serif">sans-serif</option>
                  <option value="serif">serif</option>
                </select>
              </label>
              <label className={label}>
                Stamp (every page header)
                <input className={field} value={panel.stamp} placeholder="DRAFT" onChange={(e) => set("stamp", e.target.value)} />
              </label>
              <label className={label}>
                Cover
                <TriSelect value={panel.cover} onChange={(v) => set("cover", v)} />
              </label>
              <label className={label}>
                Contents
                <TriSelect value={panel.toc} onChange={(v) => set("toc", v)} />
              </label>
              <label className={label}>
                Contents depth
                <input
                  type="number"
                  min={1}
                  max={6}
                  className={field}
                  value={panel.tocDepth}
                  onChange={(e) => set("tocDepth", Math.min(6, Math.max(1, parseInt(e.target.value, 10) || 3)))}
                />
              </label>
            </div>
            <div className="mt-3 flex flex-wrap gap-x-5 gap-y-2 text-sm text-gray-700">
              {(
                [
                  ["outline", "Bookmarks"],
                  ["math", "Maths (KaTeX)"],
                  ["highlight", "Code highlighting"],
                ] as const
              ).map(([key, text_]) => (
                <label key={key} className="flex cursor-pointer items-center gap-1.5">
                  <input type="checkbox" checked={panel[key]} onChange={(e) => set(key, e.target.checked)} />
                  {text_}
                </label>
              ))}
              <label className="flex items-center gap-1.5">
                Width
                <input
                  type="number"
                  min={200}
                  max={4000}
                  className="w-20 rounded border border-gray-300 px-1 py-0.5 text-sm"
                  value={panel.width}
                  onChange={(e) => set("width", Math.min(4000, Math.max(200, parseInt(e.target.value, 10) || 794)))}
                />
                px
              </label>
              <button
                type="button"
                className="ml-auto cursor-pointer rounded border border-gray-300 bg-white px-2 py-0.5 text-xs text-gray-600 hover:bg-gray-50"
                onClick={() => setPanel(panelFromParams(new URLSearchParams()))}
              >
                Reset all
              </button>
            </div>
            {effective && (
              <p className="mb-0 mt-3 text-xs text-gray-500">
                In effect:{" "}
                {[
                  `template ${effective.template ?? "default"}`,
                  `cover ${effective.cover ? "on" : "off"}`,
                  `contents ${effective.toc === false ? "off" : "on"}`,
                  effective.page_size ? `page ${effective.page_size}` : null,
                  effective.accent ? `accent ${effective.accent}` : null,
                ]
                  .filter(Boolean)
                  .join(" · ")}
              </p>
            )}
          </fieldset>
        </section>

        <section className="flex min-h-[560px] min-w-0 flex-col">
          <PdfPreview
            url={pdf.url}
            bytes={pdf.bytes}
            busy={pdf.busy}
            error={conversionError ?? pdf.error}
            time={pdf.time}
            fileName="document.pdf"
          />
        </section>
      </div>

      <section className="rounded-lg border border-gray-200 bg-white">
        <div className="flex border-b border-gray-200 text-sm">
          {(
            [
              ["html", "Generated HTML"],
              ["diagnostics", "Diagnostics"],
              ["logs", `Logs (${pdf.logs.length})`],
            ] as const
          ).map(([id, text_]) => (
            <button
              key={id}
              type="button"
              onClick={() => setTab(id)}
              className={`cursor-pointer border-b-2 px-4 py-2 ${
                tab === id ? "border-blue-600 font-semibold text-blue-600" : "border-transparent text-gray-600 hover:text-gray-900"
              }`}
            >
              {text_}
            </button>
          ))}
        </div>
        <pre className="m-0 max-h-72 overflow-auto p-3 text-xs leading-relaxed text-gray-700">
          {tab === "html" && ("doc" in document_ && document_.doc ? document_.doc.html : "")}
          {tab === "diagnostics" && (pdf.diagnostics ? JSON.stringify(pdf.diagnostics, null, 2) : "No diagnostics yet.")}
          {tab === "logs" && (pdf.logs.length ? pdf.logs.map((l) => l.message).join("\n") : "No log messages.")}
        </pre>
      </section>
    </div>
  );
};

/** KaTeX through the library's own renderer, loaded on demand. */
async function loadMath() {
  const { katexMath } = await import("wasm-html-to-image/markdown-math");
  return katexMath();
}

export default MarkdownPlayground;
