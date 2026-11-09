import React, { useState, useRef, useCallback, useEffect, useMemo } from "react";
import type { WasmRenderResult } from "../utils/renderWasm.ts";
import type { OfficialRenderResult } from "../utils/renderOfficial.ts";
import { type ScaleMode, type CanvasBgSettings, getCanvasBgStyle } from "./CanvasToolbar.tsx";

interface ViewerSliderProps {
  wasmResult: WasmRenderResult | null;
  officialResult: OfficialRenderResult | null;
  scaleMode: ScaleMode;
  bgSettings: CanvasBgSettings;
}

export const ViewerSlider: React.FC<ViewerSliderProps> = ({
  wasmResult,
  officialResult,
  scaleMode,
  bgSettings,
}) => {
  const [sliderPos, setSliderPos] = useState<number>(50); // percentage 0 - 100
  const [isDragging, setIsDragging] = useState<boolean>(false);
  const containerRef = useRef<HTMLDivElement>(null);

  const wasmImageUrl = useMemo(() => {
    if (!wasmResult?.data) return null;
    if (typeof wasmResult.data === "string") return null;
    const blob = new Blob([wasmResult.data as BlobPart], {
      type: wasmResult.format === "png" ? "image/png" : "image/webp",
    });
    return URL.createObjectURL(blob);
  }, [wasmResult]);

  const updatePosition = useCallback((clientX: number) => {
    if (!containerRef.current) return;
    const rect = containerRef.current.getBoundingClientRect();
    const x = Math.max(0, Math.min(clientX - rect.left, rect.width));
    const percent = Math.round((x / rect.width) * 100);
    setSliderPos(percent);
  }, []);

  const handleMouseDown = () => setIsDragging(true);

  useEffect(() => {
    const handleMouseMove = (e: MouseEvent) => {
      if (!isDragging) return;
      updatePosition(e.clientX);
    };

    const handleMouseUp = () => {
      setIsDragging(false);
    };

    if (isDragging) {
      window.addEventListener("mousemove", handleMouseMove);
      window.addEventListener("mouseup", handleMouseUp);
    }

    return () => {
      window.removeEventListener("mousemove", handleMouseMove);
      window.removeEventListener("mouseup", handleMouseUp);
    };
  }, [isDragging, updatePosition]);

  const handleTouchMove = (e: React.TouchEvent) => {
    if (e.touches.length > 0) {
      updatePosition(e.touches[0].clientX);
    }
  };

  if (!wasmResult?.data || !officialResult?.svg) {
    return (
      <div className="flex-1 flex items-center justify-center text-xs text-slate-500">
        Both engines must render successfully for slider comparison.
      </div>
    );
  }

  const layerClasses =
    scaleMode === "fit"
      ? "absolute inset-0 p-4 flex items-center justify-center [&>div]:w-full [&>div]:h-full [&>div]:max-w-full [&>div]:max-h-full [&>div]:flex [&>div]:items-center [&>div]:justify-center [&>div>svg]:w-full [&>div>svg]:h-full [&>div>svg]:max-w-full [&>div>svg]:max-h-full [&>div>svg]:object-contain"
      : "absolute inset-0 p-4 flex items-center justify-center [&>div]:min-w-fit [&>div]:min-h-fit [&>div]:flex [&>div]:items-center [&>div]:justify-center [&>div>svg]:max-w-none";

  const bgStyle = getCanvasBgStyle(bgSettings);

  return (
    <div className="flex-1 flex flex-col overflow-hidden min-h-0">
      <div className="flex items-center justify-between px-3 py-1.5 bg-slate-800/80 border-b border-slate-700 text-xs shrink-0">
        <span className="font-semibold text-slate-300">
          Split Overlay Slider
        </span>
        <div className="flex items-center gap-3 text-[11px]">
          <span className="text-teal-400">◄ Left: wasm ({sliderPos}%)</span>
          <span className="text-indigo-400">Right: Official ({100 - sliderPos}%) ►</span>
        </div>
      </div>

      <div
        ref={containerRef}
        onTouchMove={handleTouchMove}
        className="relative flex-1 w-full h-full overflow-hidden select-none min-h-0 transition-colors duration-200"
        style={bgStyle.style}
      >
        {/* Layer 1 (Official Mermaid - Underneath / Right-dominant) */}
        <div className={`${layerClasses} pointer-events-none`}>
          <div
            className="opacity-95"
            dangerouslySetInnerHTML={{ __html: officialResult.svg }}
          />
        </div>

        {/* Layer 2 (WASM - Clipped on Left) */}
        <div
          className={`${layerClasses} pointer-events-none`}
          style={{
            clipPath: `polygon(0 0, ${sliderPos}% 0, ${sliderPos}% 100%, 0 100%)`,
          }}
        >
          {typeof wasmResult.data === "string" ? (
            <div dangerouslySetInnerHTML={{ __html: wasmResult.data }} />
          ) : wasmImageUrl ? (
            <img
              src={wasmImageUrl}
              alt="WASM Rendered"
              className={
                scaleMode === "fit"
                  ? "max-w-full max-h-full object-contain"
                  : "max-w-none"
              }
            />
          ) : null}
        </div>

        {/* Draggable Divider Line */}
        <div
          className="absolute top-0 bottom-0 w-0.5 bg-indigo-400 z-10 shadow-[0_0_8px_rgba(99,102,241,0.8)] cursor-ew-resize"
          style={{ left: `${sliderPos}%` }}
          onMouseDown={handleMouseDown}
        >
          {/* Handle knob */}
          <div className="absolute top-1/2 -translate-y-1/2 -translate-x-1/2 w-7 h-7 bg-indigo-600 border-2 border-white rounded-full flex items-center justify-center text-[10px] text-white shadow-lg cursor-ew-resize">
            ◄►
          </div>
        </div>
      </div>
    </div>
  );
};
