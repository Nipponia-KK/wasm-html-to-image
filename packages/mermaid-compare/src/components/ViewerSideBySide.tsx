import React, { useMemo } from "react";
import type { WasmRenderResult } from "../utils/renderWasm.ts";
import type { OfficialRenderResult } from "../utils/renderOfficial.ts";
import { type ScaleMode, type CanvasBgSettings, getCanvasBgStyle } from "./CanvasToolbar.tsx";

interface ViewerSideBySideProps {
  wasmResult: WasmRenderResult | null;
  officialResult: OfficialRenderResult | null;
  mode: "side-by-side" | "wasm-only" | "official-only";
  scaleMode: ScaleMode;
  bgSettings: CanvasBgSettings;
}

export const ViewerSideBySide: React.FC<ViewerSideBySideProps> = ({
  wasmResult,
  officialResult,
  mode,
  scaleMode,
  bgSettings,
}) => {
  // Create object URL for wasm binary raster formats (PNG, WebP)
  const wasmImageUrl = useMemo(() => {
    if (!wasmResult?.data) return null;
    if (typeof wasmResult.data === "string") return null;
    const blob = new Blob([wasmResult.data as BlobPart], {
      type: wasmResult.format === "png" ? "image/png" : "image/webp",
    });
    return URL.createObjectURL(blob);
  }, [wasmResult]);

  const handleDownloadWasm = () => {
    if (!wasmResult?.data) return;
    const a = document.createElement("a");
    if (typeof wasmResult.data === "string") {
      const blob = new Blob([wasmResult.data], { type: "image/svg+xml" });
      a.href = URL.createObjectURL(blob);
      a.download = "mermaid-wasm.svg";
    } else if (wasmImageUrl) {
      a.href = wasmImageUrl;
      a.download = `mermaid-wasm.${wasmResult.format}`;
    }
    a.click();
  };

  const handleDownloadOfficial = () => {
    if (!officialResult?.svg) return;
    const blob = new Blob([officialResult.svg], { type: "image/svg+xml" });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = "mermaid-official.svg";
    a.click();
  };

  const svgWrapperClasses =
    scaleMode === "fit"
      ? "w-full h-full max-w-full max-h-full flex items-center justify-center [&>svg]:w-full [&>svg]:h-full [&>svg]:max-w-full [&>svg]:max-h-full [&>svg]:object-contain"
      : "flex items-center justify-center [&>svg]:max-w-none";

  const bgStyle = getCanvasBgStyle(bgSettings);

  return (
    <div className="flex-1 grid grid-cols-1 md:grid-cols-2 divide-y md:divide-y-0 md:divide-x divide-slate-700/80 min-h-0 overflow-hidden">
      {/* WASM Column */}
      {(mode === "side-by-side" || mode === "wasm-only") && (
        <div
          className={`flex flex-col min-h-0 overflow-hidden ${
            mode === "wasm-only" ? "col-span-2" : ""
          }`}
        >
          {/* Column Header */}
          <div className="flex items-center justify-between px-3 py-1.5 bg-slate-800/80 border-b border-slate-700 text-xs shrink-0">
            <div className="flex items-center gap-2">
              <span className="font-semibold text-teal-400">wasm-html-to-image</span>
              {wasmResult && (
                <span className="text-[10px] text-slate-400 bg-slate-900 px-1.5 py-0.5 rounded border border-slate-700">
                  {wasmResult.format.toUpperCase()}
                </span>
              )}
            </div>
            {wasmResult?.data && !wasmResult.error && (
              <button
                type="button"
                onClick={handleDownloadWasm}
                className="text-[11px] text-slate-400 hover:text-slate-200 transition-colors cursor-pointer"
              >
                Download
              </button>
            )}
          </div>

          {/* Canvas Scroll Area */}
          <div
            className="flex-1 overflow-auto min-h-0 transition-colors duration-200"
            style={bgStyle.style}
          >
            <div className="min-w-full min-h-full flex items-center justify-center p-4">
              {wasmResult?.error ? (
                <div className="p-4 bg-red-950/90 border border-red-700/80 rounded-lg text-red-200 text-xs font-mono max-w-md shadow-xl">
                  <div className="flex items-center gap-2 mb-1 text-red-400 font-semibold">
                    <span>WASM Render Error:</span>
                  </div>
                  <p className="break-words leading-relaxed text-[11px]">{wasmResult.error}</p>
                </div>
              ) : typeof wasmResult?.data === "string" ? (
                <div
                  className={svgWrapperClasses}
                  dangerouslySetInnerHTML={{ __html: wasmResult.data }}
                />
              ) : wasmImageUrl ? (
                <img
                  src={wasmImageUrl}
                  alt="WASM Rendered Diagram"
                  className={
                    scaleMode === "fit"
                      ? "max-w-full max-h-full object-contain"
                      : "max-w-none"
                  }
                />
              ) : (
                <span className="text-xs text-slate-500">No output yet</span>
              )}
            </div>
          </div>
        </div>
      )}

      {/* Official Column */}
      {(mode === "side-by-side" || mode === "official-only") && (
        <div
          className={`flex flex-col min-h-0 overflow-hidden ${
            mode === "official-only" ? "col-span-2" : ""
          }`}
        >
          {/* Column Header */}
          <div className="flex items-center justify-between px-3 py-1.5 bg-slate-800/80 border-b border-slate-700 text-xs shrink-0">
            <div className="flex items-center gap-2">
              <span className="font-semibold text-indigo-400">Official Mermaid</span>
              <span className="text-[10px] text-slate-400 bg-slate-900 px-1.5 py-0.5 rounded border border-slate-700">
                SVG
              </span>
            </div>
            {officialResult?.svg && !officialResult.error && (
              <button
                type="button"
                onClick={handleDownloadOfficial}
                className="text-[11px] text-slate-400 hover:text-slate-200 transition-colors cursor-pointer"
              >
                Download
              </button>
            )}
          </div>

          {/* Canvas Scroll Area */}
          <div
            className="flex-1 overflow-auto min-h-0 transition-colors duration-200"
            style={bgStyle.style}
          >
            <div className="min-w-full min-h-full flex items-center justify-center p-4">
              {officialResult?.error ? (
                <div className="p-4 bg-red-950/90 border border-red-700/80 rounded-lg text-red-200 text-xs font-mono max-w-md shadow-xl">
                  <div className="flex items-center gap-2 mb-1 text-red-400 font-semibold">
                    <span>Official Mermaid Error:</span>
                  </div>
                  <p className="break-words leading-relaxed text-[11px]">{officialResult.error}</p>
                </div>
              ) : officialResult?.svg ? (
                <div
                  className={svgWrapperClasses}
                  dangerouslySetInnerHTML={{ __html: officialResult.svg }}
                />
              ) : (
                <span className="text-xs text-slate-500">No output yet</span>
              )}
            </div>
          </div>
        </div>
      )}
    </div>
  );
};
