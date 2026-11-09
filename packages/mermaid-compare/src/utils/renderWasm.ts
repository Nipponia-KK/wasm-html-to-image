import {
  renderMermaid,
  MERMAID_THEMES,
  type MermaidTheme,
  type MermaidWarning,
} from "wasm-html-to-image/workers";
import type { OutputFormat } from "wasm-html-to-image";
import { normalizeSvg } from "./svgUtils.ts";

export { MERMAID_THEMES, type MermaidTheme, type MermaidWarning, type OutputFormat };

export interface WasmRenderOptions {
  theme?: MermaidTheme;
  format?: OutputFormat;
  mode?: "strict" | "compat" | "recover";
  directives?: boolean;
  padding?: number;
}

export interface WasmRenderResult {
  data: string | Uint8Array;
  format: OutputFormat;
  durationMs: number;
  warnings: MermaidWarning[];
  error?: string;
}

export async function renderWasm(
  source: string,
  options: WasmRenderOptions = {}
): Promise<WasmRenderResult> {
  const startTime = performance.now();
  const warnings: MermaidWarning[] = [];
  const format = options.format || "svg";

  try {
    const result = await renderMermaid(source, {
      format: format,
      theme: options.theme || "light",
      mode: options.mode || "strict",
      directives: options.directives ?? true,
      padding: options.padding ?? 16,
      onWarning: (w) => {
        warnings.push(w);
      },
    });

    const durationMs = performance.now() - startTime;
    const finalData =
      typeof result.data === "string" ? normalizeSvg(result.data) : result.data;

    return {
      data: finalData,
      format,
      durationMs,
      warnings,
    };
  } catch (err: unknown) {
    const durationMs = performance.now() - startTime;
    return {
      data: "",
      format,
      durationMs,
      warnings,
      error: err instanceof Error ? err.message : String(err),
    };
  }
}
