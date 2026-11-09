import React from "react";

export type ScaleMode = "fit" | "original";

export type CanvasBgType = "dark" | "light" | "gray" | "checker" | "custom";

export interface CanvasBgSettings {
  type: CanvasBgType;
  customColor: string;
}

interface CanvasToolbarProps {
  scaleMode: ScaleMode;
  onChangeScaleMode: (mode: ScaleMode) => void;
  bgSettings: CanvasBgSettings;
  onChangeBgSettings: (settings: CanvasBgSettings) => void;
}

export const CanvasToolbar: React.FC<CanvasToolbarProps> = ({
  scaleMode,
  onChangeScaleMode,
  bgSettings,
  onChangeBgSettings,
}) => {
  return (
    <div className="bg-slate-800/80 border-b border-slate-700/60 px-3 py-1.5 flex items-center justify-between gap-2 text-[11px] text-slate-300 shrink-0 min-w-0">
      {/* Background Color Selector */}
      <div className="flex items-center gap-1.5 shrink-0">
        <span className="text-slate-400 font-medium">Background:</span>
        <div className="flex items-center gap-1">
          {/* Dark */}
          <button
            type="button"
            title="Dark (#0b0f19)"
            onClick={() => onChangeBgSettings({ ...bgSettings, type: "dark" })}
            className={`w-4 h-4 rounded-full border transition-all cursor-pointer bg-[#0b0f19] ${
              bgSettings.type === "dark"
                ? "border-teal-400 scale-115 ring-2 ring-teal-500/30"
                : "border-slate-600 hover:border-slate-400"
            }`}
          />

          {/* Slate Gray */}
          <button
            type="button"
            title="Slate Gray (#1e293b)"
            onClick={() => onChangeBgSettings({ ...bgSettings, type: "gray" })}
            className={`w-4 h-4 rounded-full border transition-all cursor-pointer bg-[#1e293b] ${
              bgSettings.type === "gray"
                ? "border-teal-400 scale-115 ring-2 ring-teal-500/30"
                : "border-slate-600 hover:border-slate-400"
            }`}
          />

          {/* Light */}
          <button
            type="button"
            title="Light (#ffffff)"
            onClick={() => onChangeBgSettings({ ...bgSettings, type: "light" })}
            className={`w-4 h-4 rounded-full border transition-all cursor-pointer bg-white ${
              bgSettings.type === "light"
                ? "border-teal-400 scale-115 ring-2 ring-teal-500/30"
                : "border-slate-600 hover:border-slate-400"
            }`}
          />

          {/* Checkerboard (Transparent) */}
          <button
            type="button"
            title="Transparent Checkerboard"
            onClick={() => onChangeBgSettings({ ...bgSettings, type: "checker" })}
            className={`w-4 h-4 rounded-full border transition-all cursor-pointer bg-[conic-gradient(#94a3b8_90deg,#e2e8f0_90deg_180deg,#94a3b8_180deg_270deg,#e2e8f0_270deg)] bg-[size:6px_6px] ${
              bgSettings.type === "checker"
                ? "border-teal-400 scale-115 ring-2 ring-teal-500/30"
                : "border-slate-600 hover:border-slate-400"
            }`}
          />

          {/* Custom Color Picker */}
          <label
            title="Custom Color"
            className={`relative w-4 h-4 rounded-full border transition-all cursor-pointer overflow-hidden ${
              bgSettings.type === "custom"
                ? "border-teal-400 scale-115 ring-2 ring-teal-500/30"
                : "border-slate-600 hover:border-slate-400"
            }`}
            style={{ backgroundColor: bgSettings.customColor }}
          >
            <input
              type="color"
              value={bgSettings.customColor}
              onChange={(e) =>
                onChangeBgSettings({
                  type: "custom",
                  customColor: e.target.value,
                })
              }
              className="opacity-0 absolute inset-0 cursor-pointer w-full h-full"
            />
          </label>
        </div>
      </div>

      {/* View Scale Mode */}
      <div className="flex items-center gap-1 bg-slate-900 border border-slate-700 rounded p-0.5 shrink-0">
        <button
          type="button"
          onClick={() => onChangeScaleMode("fit")}
          className={`px-2 py-0.5 rounded cursor-pointer transition-colors ${
            scaleMode === "fit"
              ? "bg-indigo-600 text-white font-medium"
              : "text-slate-400 hover:text-slate-200"
          }`}
        >
          Fit
        </button>
        <button
          type="button"
          onClick={() => onChangeScaleMode("original")}
          className={`px-2 py-0.5 rounded cursor-pointer transition-colors ${
            scaleMode === "original"
              ? "bg-indigo-600 text-white font-medium"
              : "text-slate-400 hover:text-slate-200"
          }`}
        >
          100%
        </button>
      </div>
    </div>
  );
};

export function getCanvasBgStyle(settings: CanvasBgSettings): {
  className?: string;
  style?: React.CSSProperties;
} {
  switch (settings.type) {
    case "dark":
      return { style: { backgroundColor: "#0b0f19" } };
    case "light":
      return { style: { backgroundColor: "#ffffff" } };
    case "gray":
      return { style: { backgroundColor: "#1e293b" } };
    case "custom":
      return { style: { backgroundColor: settings.customColor } };
    case "checker":
      return {
        style: {
          backgroundColor: "#1e293b",
          backgroundImage: `
            linear-gradient(45deg, #0f172a 25%, transparent 25%),
            linear-gradient(-45deg, #0f172a 25%, transparent 25%),
            linear-gradient(45deg, transparent 75%, #0f172a 75%),
            linear-gradient(-45deg, transparent 75%, #0f172a 75%)
          `,
          backgroundSize: "16px 16px",
          backgroundPosition: "0 0, 0 8px, 8px -8px, -8px 0px",
        },
      };
    default:
      return { style: { backgroundColor: "#0b0f19" } };
  }
}
