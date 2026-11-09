import React from "react";
import type { WasmRenderResult } from "../utils/renderWasm.ts";
import type { OfficialRenderResult } from "../utils/renderOfficial.ts";

interface MetricsPanelProps {
  wasmResult: WasmRenderResult | null;
  officialResult: OfficialRenderResult | null;
}

export const MetricsPanel: React.FC<MetricsPanelProps> = ({
  wasmResult,
  officialResult,
}) => {
  if (!wasmResult && !officialResult) return null;

  const wasmTime = wasmResult?.durationMs;
  const officialTime = officialResult?.durationMs;

  let speedDiffText = "";
  if (wasmTime != null && officialTime != null && wasmTime > 0 && officialTime > 0) {
    if (wasmTime < officialTime) {
      const ratio = (officialTime / wasmTime).toFixed(1);
      speedDiffText = `WASM is ~${ratio}x faster`;
    } else {
      const ratio = (wasmTime / officialTime).toFixed(1);
      speedDiffText = `Official is ~${ratio}x faster`;
    }
  }

  return (
    <div className="bg-slate-950/70 border-t border-slate-800 px-4 py-2 flex flex-wrap items-center justify-between gap-3 text-xs">
      <div className="flex items-center gap-6">
        {/* WASM Stats */}
        <div className="flex items-center gap-2">
          <span className="font-semibold text-teal-400">wasm-html-to-image:</span>
          {wasmResult ? (
            wasmResult.error ? (
              <span className="text-red-400 font-mono">Error</span>
            ) : (
              <span className="font-mono text-slate-200">
                {wasmResult.durationMs.toFixed(1)} ms
              </span>
            )
          ) : (
            <span className="text-slate-500">-</span>
          )}
        </div>

        {/* Official Stats */}
        <div className="flex items-center gap-2">
          <span className="font-semibold text-indigo-400">Official Mermaid:</span>
          {officialResult ? (
            officialResult.error ? (
              <span className="text-red-400 font-mono">Error</span>
            ) : (
              <span className="font-mono text-slate-200">
                {officialResult.durationMs.toFixed(1)} ms
              </span>
            )
          ) : (
            <span className="text-slate-500">-</span>
          )}
        </div>

        {officialResult?.approximateOf && (
          <span
            className="px-2 py-0.5 rounded text-[11px] font-medium bg-slate-800 text-slate-300 border border-slate-600"
            title="The official package has no theme of this name; colors are not comparable"
          >
            Reference only: official "{officialResult.approximateOf}" stands in
          </span>
        )}

        {/* Comparative Badge */}
        {speedDiffText && !wasmResult?.error && !officialResult?.error && (
          <span className="px-2 py-0.5 rounded text-[11px] font-medium bg-emerald-950 text-emerald-300 border border-emerald-800">
            {speedDiffText}
          </span>
        )}
      </div>

      {/* Warnings & Recoveries */}
      {wasmResult?.warnings && wasmResult.warnings.length > 0 && (
        <div className="flex items-center gap-2">
          <span className="text-amber-400 font-medium">Recoveries / Warnings:</span>
          <div className="flex gap-1.5 overflow-x-auto max-w-md">
            {wasmResult.warnings.map((w, idx) => (
              <span
                key={idx}
                className="px-1.5 py-0.5 bg-amber-950/80 text-amber-300 border border-amber-800 rounded text-[10px] font-mono whitespace-nowrap"
                title={w.message}
              >
                {w.code}: {w.message}
              </span>
            ))}
          </div>
        </div>
      )}
    </div>
  );
};
