import fs from "node:fs";
import path from "node:path";
import { describe, expect, it } from "vitest";
import { renderMermaid } from "wasm-html-to-image/single";
import { MERMAID_SAMPLES } from "../../playground/src/mermaidSamples";

// A drawing that changes by accident is caught here. The SVG of every playground sample, in two themes, is reduced to
// what a regression moves: the size (with a margin, since text widths depend on the fonts), how many of each element
// there are, and the set of colours. UPDATE_SNAPSHOTS=1 rewrites the baselines after an intended change.
const BASELINE_PATH = path.join(__dirname, "data/mermaid-baselines.json");
const SIZE_TOLERANCE = 0.04;
const THEMES = ["light", "dark"] as const;

interface Fingerprint {
  width: number;
  height: number;
  tags: Record<string, number>;
  colors: string[];
}

function fingerprint(svg: string): Fingerprint {
  const view = svg.match(/viewBox="[\d.\-]+ [\d.\-]+ ([\d.]+) ([\d.]+)"/);
  const width = view ? Number(view[1]) : Number(svg.match(/\bwidth="([\d.]+)/)?.[1] ?? 0);
  const height = view ? Number(view[2]) : Number(svg.match(/\bheight="([\d.]+)/)?.[1] ?? 0);
  const tags: Record<string, number> = {};
  for (const m of svg.matchAll(/<([a-zA-Z][\w-]*)/g)) tags[m[1]] = (tags[m[1]] ?? 0) + 1;
  const colors = [...new Set([...svg.matchAll(/#[0-9a-fA-F]{6}\b/g)].map((m) => m[0].toLowerCase()))].sort();
  return { width, height, tags, colors };
}

const update = !!process.env.UPDATE_SNAPSHOTS;
const baselines: Record<string, Fingerprint> = fs.existsSync(BASELINE_PATH) ? JSON.parse(fs.readFileSync(BASELINE_PATH, "utf8")) : {};
const measured: Record<string, Fingerprint> = {};

describe("Mermaid drawing baselines", () => {
  for (const sample of MERMAID_SAMPLES) {
    for (const theme of THEMES) {
      const key = `${sample.id}@${theme}`;
      it(key, async () => {
        const { data } = await renderMermaid(sample.text, {
          mode: sample.mode,
          directives: sample.directives,
          theme: (sample.theme as never) ?? theme,
        });
        const now = fingerprint(String(data));
        measured[key] = now;
        if (update) return;
        const base = baselines[key];
        if (!base) throw new Error(`No baseline for ${key}; run with UPDATE_SNAPSHOTS=1.`);
        expect(now.width).toBeGreaterThan(base.width * (1 - SIZE_TOLERANCE));
        expect(now.width).toBeLessThan(base.width * (1 + SIZE_TOLERANCE));
        expect(now.height).toBeGreaterThan(base.height * (1 - SIZE_TOLERANCE));
        expect(now.height).toBeLessThan(base.height * (1 + SIZE_TOLERANCE));
        expect(now.tags).toEqual(base.tags);
        expect(now.colors).toEqual(base.colors);
      });
    }
  }

  it("writes the baselines when asked", () => {
    if (!update) return;
    fs.mkdirSync(path.dirname(BASELINE_PATH), { recursive: true });
    fs.writeFileSync(BASELINE_PATH, JSON.stringify({ ...baselines, ...measured }, null, 2) + "\n");
  });
});
