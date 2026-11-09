import React, { useState, useEffect, useCallback, useRef } from "react";
import { Header, type ViewMode } from "./components/Header.tsx";
import { Editor } from "./components/Editor.tsx";
import { OptionsPanel } from "./components/OptionsPanel.tsx";
import { MetricsPanel } from "./components/MetricsPanel.tsx";
import { ViewerSideBySide } from "./components/ViewerSideBySide.tsx";
import { ViewerSlider } from "./components/ViewerSlider.tsx";
import {
  CanvasToolbar,
  type ScaleMode,
  type CanvasBgSettings,
} from "./components/CanvasToolbar.tsx";
import type { PresetItem } from "./presets/index.ts";
import {
  renderWasm,
  type WasmRenderOptions,
  type WasmRenderResult,
  type MermaidTheme,
} from "./utils/renderWasm.ts";
import { renderOfficial, type OfficialRenderResult } from "./utils/renderOfficial.ts";
import { settingsFromParams, settingsToParams } from "./utils/urlState.ts";

export const App: React.FC = () => {
  // the settings live in the query string, so a link reproduces the screen
  const [initial] = useState(() => settingsFromParams(new URLSearchParams(window.location.search)));
  const [selectedPreset, setSelectedPreset] = useState<PresetItem>(initial.preset);
  const [code, setCode] = useState<string>(initial.code);
  const [selectedTheme, setSelectedTheme] = useState<MermaidTheme>(initial.theme);
  const [viewMode, setViewMode] = useState<ViewMode>(initial.view);
  const [scaleMode, setScaleMode] = useState<ScaleMode>(initial.scale);
  const [bgSettings, setBgSettings] = useState<CanvasBgSettings>(initial.bg);
  const [wasmOptions, setWasmOptions] = useState<WasmRenderOptions>(initial.options);

  const [wasmResult, setWasmResult] = useState<WasmRenderResult | null>(null);
  const [officialResult, setOfficialResult] = useState<OfficialRenderResult | null>(null);
  const [isRendering, setIsRendering] = useState<boolean>(false);

  // Keep track of latest run id to ignore stale responses
  const runIdRef = useRef<number>(0);

  const executeRender = useCallback(
    async (sourceCode: string, theme: MermaidTheme, options: WasmRenderOptions) => {
      const currentRunId = ++runIdRef.current;
      setIsRendering(true);

      try {
        const [wRes, oRes] = await Promise.all([
          renderWasm(sourceCode, { ...options, theme }),
          renderOfficial(sourceCode, theme),
        ]);

        if (runIdRef.current === currentRunId) {
          setWasmResult(wRes);
          setOfficialResult(oRes);
        }
      } finally {
        if (runIdRef.current === currentRunId) {
          setIsRendering(false);
        }
      }
    },
    []
  );

  // Trigger render on initial mount
  useEffect(() => {
    executeRender(code, selectedTheme, wasmOptions);
  }, []);

  // Keep the address bar in step with the settings (no new history entry per change)
  useEffect(() => {
    const query = settingsToParams({
      preset: selectedPreset,
      code,
      theme: selectedTheme,
      view: viewMode,
      scale: scaleMode,
      bg: bgSettings,
      options: wasmOptions,
    }).toString();
    window.history.replaceState(null, "", query ? `?${query}` : window.location.pathname);
  }, [selectedPreset, code, selectedTheme, viewMode, scaleMode, bgSettings, wasmOptions]);

  const handleSelectPreset = (preset: PresetItem) => {
    setSelectedPreset(preset);
    setCode(preset.code);

    const nextMode = preset.recommendedMode || "strict";
    const nextOptions = { ...wasmOptions, mode: nextMode };
    setWasmOptions(nextOptions);

    executeRender(preset.code, selectedTheme, nextOptions);
  };

  const handleChangeTheme = (theme: MermaidTheme) => {
    setSelectedTheme(theme);
    setWasmOptions((prev) => ({ ...prev, theme }));

    // Auto-suggest matching background if user is on default
    if (theme === "light" || theme === "solarized-light") {
      setBgSettings((prev) => (prev.type === "dark" ? { ...prev, type: "gray" } : prev));
    } else if (theme === "dark" || theme === "dracula" || theme === "nord") {
      setBgSettings((prev) => (prev.type === "light" ? { ...prev, type: "dark" } : prev));
    }

    executeRender(code, theme, { ...wasmOptions, theme });
  };

  const handleChangeOptions = (newOptions: WasmRenderOptions) => {
    setWasmOptions(newOptions);
    executeRender(code, selectedTheme, newOptions);
  };

  const handleManualRender = () => {
    executeRender(code, selectedTheme, wasmOptions);
  };

  return (
    <div className="flex flex-col h-screen overflow-hidden bg-slate-900 text-slate-100">
      {/* Top Header */}
      <Header
        selectedPresetId={selectedPreset.id}
        onSelectPreset={handleSelectPreset}
        selectedTheme={selectedTheme}
        onChangeTheme={handleChangeTheme}
        viewMode={viewMode}
        onChangeViewMode={setViewMode}
        onRender={handleManualRender}
        isRendering={isRendering}
      />

      {/* WASM Specific Options */}
      <OptionsPanel
        options={wasmOptions}
        onChangeOptions={handleChangeOptions}
      />

      {/* Main Workspace (Editor + Viewers) */}
      <div className="flex-1 flex flex-col md:flex-row overflow-hidden min-h-0">
        {/* Left Side: Code Editor */}
        <div className="w-full md:w-[380px] lg:w-[440px] shrink-0 h-2/5 md:h-full overflow-hidden min-h-0 flex flex-col">
          <Editor
            code={code}
            onChangeCode={setCode}
            description={selectedPreset.description}
            onKeyDownRun={handleManualRender}
          />
        </div>

        {/* Right Side: Visual Comparison Viewers */}
        <div className="flex-1 flex flex-col h-3/5 md:h-full overflow-hidden min-h-0">
          {/* Canvas Sub-toolbar for Background and Scaling */}
          <CanvasToolbar
            scaleMode={scaleMode}
            onChangeScaleMode={setScaleMode}
            bgSettings={bgSettings}
            onChangeBgSettings={setBgSettings}
          />

          {viewMode === "slider" ? (
            <ViewerSlider
              wasmResult={wasmResult}
              officialResult={officialResult}
              scaleMode={scaleMode}
              bgSettings={bgSettings}
            />
          ) : (
            <ViewerSideBySide
              wasmResult={wasmResult}
              officialResult={officialResult}
              mode={viewMode}
              scaleMode={scaleMode}
              bgSettings={bgSettings}
            />
          )}
        </div>
      </div>

      {/* Bottom Metrics & Warnings */}
      <MetricsPanel
        wasmResult={wasmResult}
        officialResult={officialResult}
      />
    </div>
  );
};
