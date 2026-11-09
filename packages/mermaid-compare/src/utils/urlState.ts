import type { ViewMode } from "../components/Header.tsx";
import type { CanvasBgSettings, CanvasBgType, ScaleMode } from "../components/CanvasToolbar.tsx";
import { PRESETS, type PresetItem } from "../presets/index.ts";
import { MERMAID_THEMES, type MermaidTheme, type OutputFormat, type WasmRenderOptions } from "./renderWasm.ts";

/** Everything the app remembers in the address bar, so a link reproduces the screen. */
export interface AppSettings {
  preset: PresetItem;
  code: string;
  theme: MermaidTheme;
  view: ViewMode;
  scale: ScaleMode;
  bg: CanvasBgSettings;
  options: WasmRenderOptions;
}

const VIEWS: ViewMode[] = ["side-by-side", "slider", "wasm-only", "official-only"];
const SCALES: ScaleMode[] = ["fit", "original"];
const BG_TYPES: CanvasBgType[] = ["dark", "light", "gray", "checker", "custom"];
const MODES = ["strict", "compat", "recover"] as const;
const FORMATS: OutputFormat[] = ["svg", "png", "webp"];

const DEFAULT_CUSTOM_BG = "#334155";
const DEFAULT_PADDING = 16;

const pick = <T extends string>(value: string | null, allowed: readonly T[], fallback: T): T =>
  allowed.includes(value as T) ? (value as T) : fallback;

export function defaultSettings(preset: PresetItem = PRESETS[0]): AppSettings {
  return {
    preset,
    code: preset.code,
    theme: "light",
    view: "side-by-side",
    scale: "fit",
    bg: { type: "light", customColor: DEFAULT_CUSTOM_BG },
    options: {
      theme: "light",
      mode: preset.recommendedMode || "strict",
      directives: true,
      padding: DEFAULT_PADDING,
      format: "svg",
    },
  };
}

/** Read the settings from a query string; anything missing or not understood keeps its default. */
export function settingsFromParams(params: URLSearchParams): AppSettings {
  const preset = PRESETS.find((p) => p.id === params.get("preset")) ?? PRESETS[0];
  const base = defaultSettings(preset);
  const theme = pick(params.get("theme"), Object.keys(MERMAID_THEMES) as MermaidTheme[], base.theme);
  const padding = Number(params.get("padding"));
  const color = params.get("bgColor");
  return {
    preset,
    code: params.get("code") ?? preset.code,
    theme,
    view: pick(params.get("view"), VIEWS, base.view),
    scale: pick(params.get("scale"), SCALES, base.scale),
    bg: {
      type: pick(params.get("bg"), BG_TYPES, base.bg.type),
      customColor: color && /^#[0-9a-f]{6}$/i.test(color) ? color : base.bg.customColor,
    },
    options: {
      theme,
      mode: pick(params.get("mode"), MODES, base.options.mode ?? "strict"),
      directives: params.get("directives") === null ? base.options.directives : params.get("directives") !== "off",
      padding: params.get("padding") !== null && Number.isInteger(padding) && padding >= 0 && padding <= 200 ? padding : DEFAULT_PADDING,
      format: pick(params.get("format"), FORMATS, "svg"),
    },
  };
}

/** The query string for the settings; values equal to the defaults are left out to keep a link short. */
export function settingsToParams(s: AppSettings): URLSearchParams {
  const params = new URLSearchParams();
  const base = defaultSettings(s.preset);
  if (s.preset.id !== PRESETS[0].id) params.set("preset", s.preset.id);
  if (s.code !== s.preset.code) params.set("code", s.code);
  if (s.theme !== base.theme) params.set("theme", s.theme);
  if (s.view !== base.view) params.set("view", s.view);
  if (s.scale !== base.scale) params.set("scale", s.scale);
  if (s.bg.type !== base.bg.type) params.set("bg", s.bg.type);
  if (s.bg.type === "custom" && s.bg.customColor !== base.bg.customColor) params.set("bgColor", s.bg.customColor);
  if (s.options.mode !== base.options.mode) params.set("mode", s.options.mode ?? "strict");
  if (s.options.format !== "svg") params.set("format", s.options.format ?? "svg");
  if (s.options.directives === false) params.set("directives", "off");
  if (s.options.padding !== DEFAULT_PADDING) params.set("padding", String(s.options.padding));
  return params;
}
