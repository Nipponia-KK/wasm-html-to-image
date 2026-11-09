import mermaid from "mermaid";
import { PRESETS } from "./presets/index.ts";
import { renderWasm, type MermaidTheme } from "./utils/renderWasm.ts";
import { renderOfficial } from "./utils/renderOfficial.ts";
import { deltaE } from "./utils/color.ts";
import { nearestMean, probe, type Palette, type Role } from "./utils/extract.ts";

// Themes that exist in the official package under the same name (see officialTheme.ts).
const THEMES: MermaidTheme[] = ["light", "dark", "forest", "neutral"];
const OFFICIAL_NAME: Record<string, "default" | "dark" | "forest" | "neutral"> = {
  light: "default",
  dark: "dark",
  forest: "forest",
  neutral: "neutral",
};

interface RoleRow { theme: string; preset: string; role: string; wasm: string | null; official: string | null; dE: number | null }
interface PaletteRow { theme: string; preset: string; kind: keyof Palette; wasm: [string, number][]; official: [string, number][]; dE: number | null }

const swatch = (c: string | null) =>
  c ? `<span class="sw" style="background:${c}"></span>${c}` : "<span style='color:#64748b'>—</span>";

async function officialVars(): Promise<Record<string, unknown>> {
  const out: Record<string, unknown> = {};
  for (const theme of THEMES) {
    const name = OFFICIAL_NAME[theme];
    mermaid.initialize({ startOnLoad: false, theme: name });
    await mermaid.render(`vars-${name}`, "flowchart LR\n A-->B");
    out[name] = JSON.parse(JSON.stringify(mermaid.mermaidAPI.getConfig().themeVariables ?? {}));
  }
  return out;
}

async function main() {
  const status = document.getElementById("status")!;
  const out = document.getElementById("out")!;
  const roleRows: RoleRow[] = [];
  const paletteRows: PaletteRow[] = [];
  const failures: string[] = [];
  const presets = PRESETS.filter((p) => p.category !== "Recovery");

  for (const theme of THEMES) {
    for (const preset of presets) {
      status.textContent = `${theme} / ${preset.id}`;
      const [w, o] = await Promise.all([
        renderWasm(preset.code, { theme, mode: "strict" }),
        renderOfficial(preset.code, theme),
      ]);
      if (w.error || o.error || typeof w.data !== "string") {
        failures.push(`${theme}/${preset.id}: ${w.error ?? o.error ?? "binary"}`);
        continue;
      }
      const a = probe(w.data, "wasm");
      const b = probe(o.svg, "official");
      if (preset.category === "Flowchart") {
        a.roles.forEach((r: Role, i) => {
          const off = b.roles[i].color;
          roleRows.push({ theme, preset: preset.id, role: r.role, wasm: r.color, official: off, dE: r.color && off ? deltaE(r.color, off) : null });
        });
      }
      for (const kind of ["fills", "strokes", "texts"] as const) {
        paletteRows.push({
          theme,
          preset: preset.id,
          kind,
          wasm: a.palette[kind],
          official: b.palette[kind],
          dE: nearestMean(a.palette[kind], b.palette[kind]),
        });
      }
    }
  }

  const vars = await officialVars();
  const result = { mermaid: (mermaid as unknown as { version?: string }).version ?? "", roleRows, paletteRows, vars, failures };
  (window as unknown as { __parity: unknown }).__parity = result;

  const cls = (d: number | null, limit: number) => (d == null ? "" : d <= limit ? "ok" : "bad");
  let html = `<h2>Flowchart roles (wasm vs official), ΔE76</h2><table><tr><th>theme</th><th>preset</th><th>role</th><th>wasm</th><th>official</th><th>ΔE</th></tr>`;
  for (const r of roleRows)
    html += `<tr><td>${r.theme}</td><td>${r.preset}</td><td>${r.role}</td><td>${swatch(r.wasm)}</td><td>${swatch(r.official)}</td><td class="${cls(r.dE, 5)}">${r.dE?.toFixed(1) ?? "—"}</td></tr>`;
  html += `</table><h2>Palette (nearest-colour mean ΔE, wasm → official)</h2><table><tr><th>theme</th><th>preset</th><th>kind</th><th>wasm</th><th>official</th><th>ΔE</th></tr>`;
  for (const r of paletteRows)
    html += `<tr><td>${r.theme}</td><td>${r.preset}</td><td>${r.kind}</td><td>${r.wasm.map(([c]) => swatch(c)).join(" ")}</td><td>${r.official.map(([c]) => swatch(c)).join(" ")}</td><td class="${cls(r.dE, kindLimit(r.kind))}">${r.dE?.toFixed(1) ?? "—"}</td></tr>`;
  html += `</table>`;
  if (failures.length) html += `<h2>Failures</h2><pre>${failures.join("\n")}</pre>`;
  out.innerHTML = html;
  const save = (name: string, data: unknown) =>
    fetch(`/__parity-save?name=${name}`, { method: "POST", body: JSON.stringify(data) }).catch(() => undefined);
  await save("official-theme-vars", { vars });
  await save("report", { roleRows, paletteRows, failures });
  status.textContent = `done: ${roleRows.length} role rows, ${paletteRows.length} palette rows, ${failures.length} failures`;
}

function kindLimit(kind: keyof Palette) {
  return kind === "fills" ? 10 : 5;
}

// for poking at the official renderer from the console
(window as unknown as { __mermaid: unknown }).__mermaid = mermaid;

main().catch((e) => {
  document.getElementById("status")!.textContent = "error: " + (e instanceof Error ? e.stack : String(e));
});
