import { deltaE, toHex } from "./color.ts";

export type Engine = "wasm" | "official";

export interface Role {
  role: string;
  color: string | null;
}

export interface Palette {
  fills: [string, number][];
  strokes: [string, number][];
  texts: [string, number][];
}

const SHAPES = "rect, polygon, circle, ellipse, path, polyline";

const ROLE_SELECTORS: Record<Engine, { role: string; sel: string; prop: "fill" | "stroke" | "color" }[]> = {
  wasm: [
    { role: "node fill", sel: "g.node > :is(rect, polygon, circle, ellipse, path)", prop: "fill" },
    { role: "node stroke", sel: "g.node > :is(rect, polygon, circle, ellipse, path)", prop: "stroke" },
    { role: "node text", sel: "g.node > text", prop: "fill" },
    { role: "edge", sel: "polyline.edge, path.edge", prop: "stroke" },
    { role: "cluster fill", sel: "g.subgraph > rect:first-of-type", prop: "fill" },
    { role: "cluster stroke", sel: "g.subgraph > rect:first-of-type", prop: "stroke" },
  ],
  official: [
    { role: "node fill", sel: "g.node .label-container", prop: "fill" },
    { role: "node stroke", sel: "g.node .label-container", prop: "stroke" },
    { role: "node text", sel: "g.node .nodeLabel, g.node text", prop: "color" },
    { role: "edge", sel: "path.flowchart-link", prop: "stroke" },
    { role: "cluster fill", sel: "g.cluster > rect", prop: "fill" },
    { role: "cluster stroke", sel: "g.cluster > rect", prop: "stroke" },
  ],
};

function mount(svg: string): HTMLElement {
  const host = document.createElement("div");
  host.style.cssText = "position:absolute;left:0;top:0;width:1200px;visibility:hidden;pointer-events:none";
  host.innerHTML = svg;
  document.body.appendChild(host);
  return host;
}

function mostCommon(values: (string | null)[]): string | null {
  const count = new Map<string, number>();
  for (const v of values) if (v) count.set(v, (count.get(v) ?? 0) + 1);
  let best: string | null = null;
  let n = 0;
  for (const [k, c] of count) if (c > n) [best, n] = [k, c];
  return best;
}

function paint(el: Element, prop: "fill" | "stroke" | "color"): string | null {
  const cs = getComputedStyle(el);
  const raw = prop === "color" ? (el.tagName.toLowerCase() === "text" ? cs.fill : cs.color) : cs[prop];
  // mermaid 12 strokes some themes with a gradient: stand in the first stop
  const ref = raw.match(/^url\(["']?#([^"')]+)["']?\)/);
  if (ref) {
    const stop = el.closest("svg")?.querySelector(`[id="${ref[1]}"] stop`);
    return stop ? toHex(getComputedStyle(stop).stopColor) : null;
  }
  return toHex(raw);
}

function inDefs(el: Element): boolean {
  return !!el.closest("defs, marker, style, clipPath, mask");
}

function top(map: Map<string, number>, n: number): [string, number][] {
  return [...map].sort((a, b) => b[1] - a[1]).slice(0, n).map(([k, w]) => [k, Math.round(w * 100) / 100]);
}

export function probe(svg: string, engine: Engine): { roles: Role[]; palette: Palette } {
  const host = mount(svg);
  try {
    const root = host.querySelector("svg") ?? host;
    const roles = ROLE_SELECTORS[engine].map(({ role, sel, prop }) => ({
      role,
      color: mostCommon(Array.from(root.querySelectorAll(sel)).map((el) => paint(el, prop))),
    }));

    const fills = new Map<string, number>();
    const strokes = new Map<string, number>();
    const texts = new Map<string, number>();
    let total = 0;
    const shapes = Array.from(root.querySelectorAll(SHAPES)).filter((el) => !inDefs(el));
    for (const el of shapes) {
      let area = 1;
      try {
        const b = (el as SVGGraphicsElement).getBBox();
        area = Math.max(b.width * b.height, 1);
      } catch {
        /* not rendered */
      }
      const f = paint(el, "fill");
      if (f) {
        fills.set(f, (fills.get(f) ?? 0) + area);
        total += area;
      }
      const s = paint(el, "stroke");
      if (s) strokes.set(s, (strokes.get(s) ?? 0) + 1);
    }
    for (const el of Array.from(root.querySelectorAll("text, foreignObject span, foreignObject p, foreignObject div"))) {
      if (inDefs(el)) continue;
      if (!(el.textContent ?? "").trim()) continue;
      const t = paint(el, el.tagName.toLowerCase() === "text" ? "fill" : "color");
      if (t) texts.set(t, (texts.get(t) ?? 0) + 1);
    }
    // area weights as shares of the painted area
    if (total > 0) for (const [k, v] of fills) fills.set(k, v / total);
    return { roles, palette: { fills: top(fills, 6), strokes: top(strokes, 4), texts: top(texts, 3) } };
  } finally {
    host.remove();
  }
}

/** Weighted mean of the distance from each colour of `a` to the nearest colour of `b`. */
export function nearestMean(a: [string, number][], b: [string, number][]): number | null {
  if (a.length === 0 || b.length === 0) return null;
  let sum = 0;
  let w = 0;
  for (const [c, weight] of a) {
    sum += weight * Math.min(...b.map(([d]) => deltaE(c, d)));
    w += weight;
  }
  return w > 0 ? sum / w : null;
}
