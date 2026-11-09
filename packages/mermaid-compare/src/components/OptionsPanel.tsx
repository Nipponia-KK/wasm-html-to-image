import React from "react";
import type { WasmRenderOptions } from "../utils/renderWasm.ts";

interface OptionsPanelProps {
  options: WasmRenderOptions;
  onChangeOptions: (options: WasmRenderOptions) => void;
}

export const OptionsPanel: React.FC<OptionsPanelProps> = ({
  options,
  onChangeOptions,
}) => {
  return (
    <div className="bg-slate-800/70 border-b border-slate-700 px-3 py-1.5 flex flex-wrap items-center gap-x-4 gap-y-1.5 text-xs min-w-0">
      <span className="font-semibold text-slate-300">WASM Settings:</span>

      {/* Mode */}
      <div className="flex items-center gap-1.5">
        <label className="text-slate-400">Mode:</label>
        <select
          value={options.mode || "strict"}
          onChange={(e) =>
            onChangeOptions({
              ...options,
              mode: e.target.value as "strict" | "compat" | "recover",
            })
          }
          className="bg-slate-900 border border-slate-700 rounded px-2 py-1 text-slate-200 cursor-pointer focus:outline-none focus:border-indigo-500"
        >
          <option value="strict">strict (Default)</option>
          <option value="compat">compat (Tolerant)</option>
          <option value="recover">recover (Auto-fix)</option>
        </select>
      </div>

      {/* Format */}
      <div className="flex items-center gap-1.5">
        <label className="text-slate-400">WASM Format:</label>
        <select
          value={options.format || "svg"}
          onChange={(e) =>
            onChangeOptions({
              ...options,
              format: e.target.value as any,
            })
          }
          className="bg-slate-900 border border-slate-700 rounded px-2 py-1 text-slate-200 cursor-pointer focus:outline-none focus:border-indigo-500"
        >
          <option value="svg">SVG (Vector)</option>
          <option value="png">PNG (Raster)</option>
          <option value="webp">WebP (Raster)</option>
        </select>
      </div>

      {/* Directives */}
      <label className="flex items-center gap-1.5 cursor-pointer select-none">
        <input
          type="checkbox"
          checked={options.directives ?? true}
          onChange={(e) =>
            onChangeOptions({
              ...options,
              directives: e.target.checked,
            })
          }
          className="rounded border-slate-700 text-indigo-600 focus:ring-0 bg-slate-900 cursor-pointer"
        />
        <span className="text-slate-300">Allow %%directives%%</span>
      </label>

      {/* Padding */}
      <div className="flex items-center gap-1.5">
        <label className="text-slate-400">Padding:</label>
        <input
          type="number"
          min="0"
          max="64"
          step="4"
          value={options.padding ?? 16}
          onChange={(e) =>
            onChangeOptions({
              ...options,
              padding: parseInt(e.target.value, 10) || 0,
            })
          }
          className="w-14 bg-slate-900 border border-slate-700 rounded px-2 py-1 text-slate-200 focus:outline-none focus:border-indigo-500"
        />
        <span className="text-slate-500 text-[10px]">px</span>
      </div>
    </div>
  );
};
