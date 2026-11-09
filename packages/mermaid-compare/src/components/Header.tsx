import React from "react";
import { PRESETS, type PresetItem } from "../presets/index.ts";
import { MERMAID_THEMES, type MermaidTheme } from "../utils/renderWasm.ts";

export type ViewMode = "side-by-side" | "slider" | "wasm-only" | "official-only";

interface HeaderProps {
  selectedPresetId: string;
  onSelectPreset: (preset: PresetItem) => void;
  selectedTheme: MermaidTheme;
  onChangeTheme: (theme: MermaidTheme) => void;
  viewMode: ViewMode;
  onChangeViewMode: (mode: ViewMode) => void;
  onRender: () => void;
  isRendering: boolean;
}

export const Header: React.FC<HeaderProps> = ({
  selectedPresetId,
  onSelectPreset,
  selectedTheme,
  onChangeTheme,
  viewMode,
  onChangeViewMode,
  onRender,
  isRendering,
}) => {
  return (
    <header className="bg-slate-800 border-b border-slate-700 px-3 py-2 flex items-center justify-between gap-2 sticky top-0 z-20 shadow-md min-w-0">
      {/* Left: Brand & Preset Selector */}
      <div className="flex items-center gap-2 min-w-0 shrink">
        <div className="flex items-center gap-1.5 shrink-0">
          <span className="text-base font-bold bg-gradient-to-r from-teal-400 to-indigo-400 bg-clip-text text-transparent">
            Mermaid Compare
          </span>
          <span className="hidden sm:inline-block text-[10px] px-1.5 py-0.5 rounded bg-teal-900/60 text-teal-300 border border-teal-700 font-mono">
            WASM vs Official
          </span>
        </div>

        {/* Preset Selector */}
        <div className="flex items-center gap-1 min-w-0 shrink">
          <select
            value={selectedPresetId}
            onChange={(e) => {
              const p = PRESETS.find((x) => x.id === e.target.value);
              if (p) onSelectPreset(p);
            }}
            className="text-xs bg-slate-900 border border-slate-700 rounded px-2 py-1 text-slate-200 focus:outline-none focus:border-indigo-500 cursor-pointer max-w-[160px] sm:max-w-[220px] md:max-w-[280px] truncate"
          >
            {PRESETS.map((p) => (
              <option key={p.id} value={p.id}>
                [{p.category}] {p.name}
              </option>
            ))}
          </select>
        </div>
      </div>

      {/* Right: Theme, View Mode & Render */}
      <div className="flex items-center gap-2 shrink-0">
        {/* Theme Selector */}
        <div className="flex items-center gap-1">
          <select
            value={selectedTheme}
            onChange={(e) => onChangeTheme(e.target.value as MermaidTheme)}
            className="text-xs bg-slate-900 border border-slate-700 rounded px-2 py-1 text-slate-200 focus:outline-none focus:border-indigo-500 cursor-pointer"
          >
            {Object.keys(MERMAID_THEMES).map((themeName) => (
              <option key={themeName} value={themeName}>
                {themeName}
              </option>
            ))}
          </select>
        </div>

        {/* View Mode */}
        <div className="flex items-center bg-slate-900 border border-slate-700 rounded p-0.5 text-xs">
          <button
            type="button"
            title="Side-by-side comparison"
            onClick={() => onChangeViewMode("side-by-side")}
            className={`px-2 py-0.5 rounded transition-colors ${
              viewMode === "side-by-side"
                ? "bg-indigo-600 text-white font-medium"
                : "text-slate-400 hover:text-slate-200"
            }`}
          >
            Side
          </button>
          <button
            type="button"
            title="Split overlay slider"
            onClick={() => onChangeViewMode("slider")}
            className={`px-2 py-0.5 rounded transition-colors ${
              viewMode === "slider"
                ? "bg-indigo-600 text-white font-medium"
                : "text-slate-400 hover:text-slate-200"
            }`}
          >
            Slider
          </button>
          <button
            type="button"
            title="WASM only"
            onClick={() => onChangeViewMode("wasm-only")}
            className={`px-1.5 py-0.5 rounded transition-colors ${
              viewMode === "wasm-only"
                ? "bg-indigo-600 text-white font-medium"
                : "text-slate-400 hover:text-slate-200"
            }`}
          >
            WASM
          </button>
          <button
            type="button"
            title="Official Mermaid only"
            onClick={() => onChangeViewMode("official-only")}
            className={`px-1.5 py-0.5 rounded transition-colors ${
              viewMode === "official-only"
                ? "bg-indigo-600 text-white font-medium"
                : "text-slate-400 hover:text-slate-200"
            }`}
          >
            Official
          </button>
        </div>

        {/* Render Button */}
        <button
          type="button"
          onClick={onRender}
          disabled={isRendering}
          className="text-xs font-semibold px-3 py-1 rounded bg-emerald-600 hover:bg-emerald-500 active:bg-emerald-700 text-white shadow transition-colors flex items-center gap-1 disabled:opacity-50 cursor-pointer shrink-0"
        >
          {isRendering ? (
            <>
              <svg className="animate-spin h-3.5 w-3.5 text-white" viewBox="0 0 24 24">
                <circle className="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" strokeWidth="4" fill="none" />
                <path className="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8v4a4 4 0 00-4 4H4z" />
              </svg>
              <span>...</span>
            </>
          ) : (
            <span>Render</span>
          )}
        </button>
      </div>
    </header>
  );
};
