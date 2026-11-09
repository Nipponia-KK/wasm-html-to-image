import mermaid from "mermaid";
import { normalizeSvg } from "./svgUtils.ts";
import { officialThemeFor } from "./officialTheme.ts";

export interface OfficialRenderResult {
  svg: string;
  durationMs: number;
  error?: string;
  /** set when the official package has no theme of the requested name: the stand-in theme used */
  approximateOf?: string;
}

let mermaidCounter = 0;

export async function renderOfficial(
  source: string,
  theme: string = "default"
): Promise<OfficialRenderResult> {
  const startTime = performance.now();
  const renderId = `mermaid-official-${++mermaidCounter}`;

  const choice = officialThemeFor(theme);
  const approximateOf = choice.exact ? undefined : choice.name;

  try {
    mermaid.initialize({
      startOnLoad: false,
      theme: choice.name,
      securityLevel: "loose",
      fontFamily: "ui-sans-serif, system-ui, sans-serif",
    });

    const { svg } = await mermaid.render(renderId, source);
    const durationMs = performance.now() - startTime;
    return {
      svg: normalizeSvg(svg),
      durationMs,
      approximateOf,
    };
  } catch (err: unknown) {
    const durationMs = performance.now() - startTime;
    // Mermaid creates an error element in the DOM on failure, remove it if present
    const el = document.getElementById(renderId);
    if (el) el.remove();
    const dEl = document.getElementById(`d${renderId}`);
    if (dEl) dEl.remove();

    return {
      svg: "",
      durationMs,
      error: err instanceof Error ? err.message : String(err),
    };
  }
}
