import React, { useEffect, useMemo, useRef, useState } from "react";
import { MERMAID_THEMES, mermaidToHtml, renderMermaid, type MermaidOptions } from "wasm-html-to-image/workers";
import { MERMAID_SAMPLES, type MermaidSample } from "./mermaidSamples";

const STORAGE_KEY = "mermaid-playground-text";
const FORMATS = ["svg", "png", "webp", "jpeg", "pdf"] as const;
type Format = (typeof FORMATS)[number];
type Mode = "strict" | "compat" | "recover";

const MIME: Record<Format, string> = { svg: "image/svg+xml", png: "image/png", webp: "image/webp", jpeg: "image/jpeg", pdf: "application/pdf" };

interface Panel {
  theme: string;
  mode: Mode;
  directives: boolean;
  format: Format;
  accent: string;
  padding: number;
  width: string; // "" = fit the diagram
  background: string; // "" = by format
}

const DEFAULT_PANEL: Panel = { theme: "light", mode: "strict", directives: false, format: "svg", accent: "", padding: 16, width: "", background: "" };

const panelFromParams = (params: URLSearchParams): Panel => {
  const p = { ...DEFAULT_PANEL };
  const theme = params.get("theme");
  if (theme && theme in MERMAID_THEMES) p.theme = theme;
  const mode = params.get("mmode");
  if (mode === "compat" || mode === "recover") p.mode = mode;
  p.directives = params.get("directives") === "on";
  const format = params.get("format");
  if (FORMATS.includes(format as Format)) p.format = format as Format;
  p.accent = params.get("accent") ?? "";
  const padding = parseInt(params.get("padding") ?? "", 10);
  if (Number.isFinite(padding)) p.padding = Math.min(200, Math.max(0, padding));
  p.width = params.get("width") ?? "";
  p.background = params.get("background") ?? "";
  return p;
};

const panelToParams = (p: Panel): URLSearchParams => {
  const params = new URLSearchParams();
  if (p.theme !== DEFAULT_PANEL.theme) params.set("theme", p.theme);
  if (p.mode !== "strict") params.set("mmode", p.mode);
  if (p.directives) params.set("directives", "on");
  if (p.format !== "svg") params.set("format", p.format);
  if (p.accent) params.set("accent", p.accent);
  if (p.padding !== DEFAULT_PANEL.padding) params.set("padding", String(p.padding));
  if (p.width) params.set("width", p.width);
  if (p.background) params.set("background", p.background);
  return params;
};

const toOptions = (p: Panel): MermaidOptions => ({
  theme: p.theme as MermaidOptions["theme"],
  mode: p.mode,
  directives: p.directives,
  format: p.format,
  accent: p.accent || undefined,
  padding: p.padding,
  width: p.width ? Math.max(50, parseInt(p.width, 10) || 0) || undefined : undefined,
  background: p.background || undefined,
});

const loadText = (): string => {
  try {
    return localStorage.getItem(STORAGE_KEY) ?? MERMAID_SAMPLES[0].text;
  } catch {
    return MERMAID_SAMPLES[0].text;
  }
};

const field = "w-full rounded border border-gray-300 bg-white px-2 py-1 text-sm";
const label = "flex flex-col gap-1 text-xs font-medium text-gray-600";

interface Output {
  url: string;
  bytes: number;
  text: string | null; // the SVG source
  time: number;
  format: Format;
}

const GROUPS: MermaidSample["group"][] = ["Diagrams", "Charts", "Structure", "Features"];

export const MermaidPlayground: React.FC = () => {
  const [text, setText] = useState(loadText);
  const [panel, setPanel] = useState<Panel>(() => panelFromParams(new URLSearchParams(window.location.search)));
  const [view, setView] = useState<"editor" | "gallery">("editor");
  const [out, setOut] = useState<Output | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [tab, setTab] = useState<"svg" | "html">("svg");
  const [size, setSize] = useState<{ w: number; h: number } | null>(null);
  const latest = useRef(0);
  const urlRef = useRef<string | null>(null);

  const set = <K extends keyof Panel>(key: K, value: Panel[K]) => setPanel((p) => ({ ...p, [key]: value }));

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
  useEffect(
    () => () => {
      if (urlRef.current) URL.revokeObjectURL(urlRef.current);
    },
    [],
  );

  // Render on every change, debounced; a result that arrives after a newer request is dropped.
  useEffect(() => {
    if (view !== "editor") return;
    const id = ++latest.current;
    setBusy(true);
    const timer = setTimeout(async () => {
      const started = performance.now();
      try {
        const result = await renderMermaid(text, toOptions(panel));
        if (id !== latest.current) return;
        const data = result.data as string | Uint8Array;
        const blob = new Blob([typeof data === "string" ? data : data.slice()], { type: MIME[panel.format] });
        const url = URL.createObjectURL(blob);
        if (urlRef.current) URL.revokeObjectURL(urlRef.current);
        urlRef.current = url;
        setOut({ url, bytes: blob.size, text: typeof data === "string" ? data : null, time: performance.now() - started, format: panel.format });
        setError(null);
      } catch (e) {
        if (id === latest.current) setError(e instanceof Error ? e.message : String(e));
      } finally {
        if (id === latest.current) setBusy(false);
      }
    }, 300);
    return () => clearTimeout(timer);
  }, [text, panel, view]);

  const pageHtml = useMemo(() => {
    try {
      return mermaidToHtml(text, { ...toOptions(panel), raster: panel.format !== "svg" });
    } catch (e) {
      return e instanceof Error ? e.message : String(e);
    }
  }, [text, panel]);

  const download = () => {
    if (!out) return;
    const a = document.createElement("a");
    a.href = out.url;
    a.download = `diagram.${out.format}`;
    a.click();
  };

  const loadSample = (s: MermaidSample) => {
    setText(s.text);
    setPanel((p) => ({ ...p, mode: s.mode ?? "strict", directives: s.directives ?? false, ...(s.theme ? { theme: s.theme } : {}) }));
    setView("editor");
  };

  const transparent = panel.format === "svg" || panel.format === "png" || panel.format === "webp";

  return (
    <div className="flex flex-col gap-4">
      <div className="flex gap-2 text-sm">
        {(["editor", "gallery"] as const).map((v) => (
          <button
            key={v}
            type="button"
            onClick={() => setView(v)}
            className={`cursor-pointer rounded-md border px-3 py-1 ${
              view === v ? "border-blue-600 bg-blue-50 font-semibold text-blue-700" : "border-gray-300 bg-white text-gray-600 hover:bg-gray-50"
            }`}
          >
            {v === "editor" ? "Editor" : `Gallery (${MERMAID_SAMPLES.length})`}
          </button>
        ))}
      </div>

      <fieldset className="rounded-lg border border-gray-200 bg-white p-3">
        <legend className="px-1 text-sm font-semibold text-gray-700">Render</legend>
        <div className="grid grid-cols-2 gap-3 sm:grid-cols-4 lg:grid-cols-6">
          <label className={label}>
            Theme
            <select className={field} value={panel.theme} onChange={(e) => set("theme", e.target.value)}>
              {Object.keys(MERMAID_THEMES).map((t) => (
                <option key={t} value={t}>
                  {t}
                </option>
              ))}
            </select>
          </label>
          <label className={label}>
            Input repair
            <select className={field} value={panel.mode} onChange={(e) => set("mode", e.target.value as Mode)}>
              <option value="strict">strict</option>
              <option value="compat">compat</option>
              <option value="recover">recover</option>
            </select>
          </label>
          <label className={label}>
            Format
            <select className={field} value={panel.format} onChange={(e) => set("format", e.target.value as Format)} disabled={view === "gallery"}>
              {FORMATS.map((f) => (
                <option key={f} value={f}>
                  {f}
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
                value={/^#[0-9a-f]{6}$/i.test(panel.accent) ? panel.accent : "#2563eb"}
                onChange={(e) => set("accent", e.target.value)}
              />
              <button
                type="button"
                className="cursor-pointer rounded border border-gray-300 bg-white px-2 text-xs text-gray-600 hover:bg-gray-50"
                onClick={() => set("accent", "")}
                title="Back to the theme's accent"
              >
                Reset
              </button>
            </div>
          </label>
          <label className={label}>
            Padding (px)
            <input
              type="number"
              min={0}
              max={200}
              className={field}
              value={panel.padding}
              onChange={(e) => set("padding", Math.min(200, Math.max(0, parseInt(e.target.value, 10) || 0)))}
            />
          </label>
          <label className={label}>
            Width (px)
            <input className={field} value={panel.width} placeholder="fit" inputMode="numeric" onChange={(e) => set("width", e.target.value.replace(/\D/g, ""))} />
          </label>
          <label className={`${label} sm:col-span-2`}>
            Background (CSS colour)
            <input className={field} value={panel.background} placeholder="auto" onChange={(e) => set("background", e.target.value)} />
          </label>
          <label className="flex cursor-pointer items-center gap-1.5 self-end pb-1 text-sm text-gray-700 sm:col-span-2">
            <input type="checkbox" checked={panel.directives} onChange={(e) => set("directives", e.target.checked)} />
            Directives ({"%%{init}%%"}, frontmatter)
          </label>
          <button
            type="button"
            className="ml-auto cursor-pointer self-end rounded border border-gray-300 bg-white px-2 py-1 text-xs text-gray-600 hover:bg-gray-50"
            onClick={() => setPanel(DEFAULT_PANEL)}
          >
            Reset all
          </button>
        </div>
      </fieldset>

      {view === "gallery" ? (
        <Gallery panel={panel} onOpen={loadSample} />
      ) : (
        <div className="grid gap-4 lg:grid-cols-2">
          <section className="flex min-w-0 flex-col gap-3">
            <div className="flex items-center justify-between gap-2">
              <h2 className="m-0 text-base font-semibold text-gray-800">Mermaid</h2>
              <select
                className="rounded border border-gray-300 bg-white px-2 py-1 text-sm"
                value=""
                onChange={(e) => {
                  const s = MERMAID_SAMPLES.find((x) => x.id === e.target.value);
                  if (s) loadSample(s);
                }}
              >
                <option value="">Load a sample…</option>
                {GROUPS.map((g) => (
                  <optgroup key={g} label={g}>
                    {MERMAID_SAMPLES.filter((s) => s.group === g).map((s) => (
                      <option key={s.id} value={s.id}>
                        {s.label}
                      </option>
                    ))}
                  </optgroup>
                ))}
              </select>
            </div>
            <textarea
              value={text}
              onChange={(e) => setText(e.target.value)}
              spellCheck={false}
              className="h-[28rem] w-full resize-y rounded-lg border border-gray-300 bg-white p-3 font-mono text-sm leading-relaxed"
            />
          </section>

          <section className="flex min-w-0 flex-col overflow-hidden rounded-lg border border-gray-200 bg-white">
            <div className="flex items-center justify-between gap-3 border-b border-gray-200 bg-gray-50 px-3 py-2 text-sm">
              <div className="flex items-center gap-3 text-gray-600">
                <span className="font-semibold text-gray-800">{panel.format.toUpperCase()}</span>
                {busy && <span className="text-blue-600">Rendering…</span>}
                {!busy && out && (
                  <>
                    <span>{Math.round(out.time)} ms</span>
                    <span>{(out.bytes / 1024).toFixed(1)} KB</span>
                    {size && out.format !== "pdf" && (
                      <span>
                        {size.w} × {size.h}
                      </span>
                    )}
                  </>
                )}
              </div>
              <button
                type="button"
                onClick={download}
                disabled={!out}
                className="cursor-pointer rounded bg-orange-500 px-3 py-1 text-white transition-colors hover:bg-orange-600 disabled:cursor-not-allowed disabled:bg-gray-300"
              >
                Download
              </button>
            </div>
            {error && <pre className="m-0 whitespace-pre-wrap border-b border-red-200 bg-red-50 p-3 text-xs text-red-700">{error}</pre>}
            <div
              className="min-h-[28rem] flex-1 overflow-auto p-3"
              style={
                transparent
                  ? { backgroundColor: "#fff", backgroundImage: "conic-gradient(#eee 25%, transparent 0 50%, #eee 0 75%, transparent 0)", backgroundSize: "16px 16px" }
                  : { backgroundColor: "#f3f4f6" }
              }
            >
              {out ? (
                out.format === "pdf" ? (
                  <embed src={out.url} type="application/pdf" className="h-[28rem] w-full border-0" />
                ) : (
                  <img src={out.url} alt="diagram" onLoad={(e) => setSize({ w: e.currentTarget.naturalWidth, h: e.currentTarget.naturalHeight })} className="max-w-none" />
                )
              ) : (
                <p className="text-sm text-gray-500">{busy ? "Rendering…" : "The diagram appears here."}</p>
              )}
            </div>
          </section>
        </div>
      )}

      {view === "editor" && (
        <section className="rounded-lg border border-gray-200 bg-white">
          <div className="flex border-b border-gray-200 text-sm">
            {(
              [
                ["svg", "Output SVG"],
                ["html", "Page HTML"],
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
            {tab === "svg" && (out?.text ?? "Only the SVG format has source to show.")}
            {tab === "html" && pageHtml}
          </pre>
        </section>
      )}
    </div>
  );
};

/** Every sample drawn with the current theme, mode and accent, one after another: a quick way to check them all. */
const Gallery: React.FC<{ panel: Panel; onOpen: (s: MermaidSample) => void }> = ({ panel, onOpen }) => {
  const [items, setItems] = useState<Record<string, { url: string; failed: boolean }>>({});
  const urls = useRef<string[]>([]);

  useEffect(() => {
    let cancelled = false;
    setItems({});
    for (const u of urls.current) URL.revokeObjectURL(u);
    urls.current = [];
    (async () => {
      for (const s of MERMAID_SAMPLES) {
        if (cancelled) return;
        try {
          const options = { ...toOptions(panel), format: "svg" as const, width: undefined, mode: s.mode ?? panel.mode, directives: s.directives ?? panel.directives };
          const { data } = await renderMermaid(s.text, options);
          if (cancelled) return;
          const url = URL.createObjectURL(new Blob([data], { type: "image/svg+xml" }));
          urls.current.push(url);
          setItems((prev) => ({ ...prev, [s.id]: { url, failed: data.includes("#cf222e") } }));
        } catch {
          if (!cancelled) setItems((prev) => ({ ...prev, [s.id]: { url: "", failed: true } }));
        }
      }
    })();
    return () => {
      cancelled = true;
    };
  }, [panel.theme, panel.accent, panel.padding, panel.background, panel.mode, panel.directives]);

  useEffect(
    () => () => {
      for (const u of urls.current) URL.revokeObjectURL(u);
    },
    [],
  );

  const done = Object.keys(items).length;
  return (
    <section className="flex flex-col gap-3">
      <p className="m-0 text-xs text-gray-500">
        {done} / {MERMAID_SAMPLES.length} drawn with theme <b>{panel.theme}</b>. The samples that show a repair mode use their own mode. Click one to open it in the editor.
      </p>
      <div className="grid gap-3 sm:grid-cols-2 xl:grid-cols-3">
        {MERMAID_SAMPLES.map((s) => {
          const item = items[s.id];
          return (
            <button
              key={s.id}
              type="button"
              onClick={() => onOpen(s)}
              className="flex cursor-pointer flex-col overflow-hidden rounded-lg border border-gray-200 bg-white text-left transition-shadow hover:shadow-md"
            >
              <span className="flex items-center justify-between border-b border-gray-200 bg-gray-50 px-3 py-1.5 text-xs">
                <span className="font-semibold text-gray-800">{s.label}</span>
                <span className="text-gray-500">{s.group}</span>
              </span>
              <span className="flex h-56 items-center justify-center overflow-hidden p-2" style={{ backgroundColor: panel.background || (MERMAID_THEMES as Record<string, { bg: string }>)[panel.theme]?.bg || "#fff" }}>
                {item?.url ? (
                  <img src={item.url} alt={s.label} className="max-h-full max-w-full object-contain" />
                ) : (
                  <span className="text-xs text-gray-400">{item?.failed ? "Failed" : "Rendering…"}</span>
                )}
              </span>
              {item?.failed && item.url && <span className="bg-red-50 px-3 py-1 text-xs text-red-700">The diagram did not parse in this mode.</span>}
            </button>
          );
        })}
      </div>
    </section>
  );
};

export default MermaidPlayground;
