let ctx: CanvasRenderingContext2D | null = null;

/** Any CSS color to `#rrggbb`; null for none / transparent / unparsable. */
export function toHex(value: string | null | undefined): string | null {
  if (!value) return null;
  const v = value.trim().toLowerCase();
  if (v === "" || v === "none" || v === "transparent" || v === "currentcolor") return null;
  ctx ??= document.createElement("canvas").getContext("2d");
  if (!ctx) return null;
  ctx.fillStyle = "#010203";
  ctx.fillStyle = v;
  const out = ctx.fillStyle;
  if (out === "#010203" && v !== "#010203" && v !== "rgb(1, 2, 3)") return null;
  if (out.startsWith("#")) return out;
  const m = out.match(/rgba?\(([^)]+)\)/);
  if (!m) return null;
  const [r, g, b, a] = m[1].split(",").map((s) => parseFloat(s));
  if (a !== undefined && a === 0) return null;
  return "#" + [r, g, b].map((n) => Math.round(n).toString(16).padStart(2, "0")).join("");
}

function lab(hex: string): [number, number, number] {
  const c = [1, 3, 5].map((i) => {
    const s = parseInt(hex.slice(i, i + 2), 16) / 255;
    return s <= 0.04045 ? s / 12.92 : ((s + 0.055) / 1.055) ** 2.4;
  });
  const x = (0.4124 * c[0] + 0.3576 * c[1] + 0.1805 * c[2]) / 0.95047;
  const y = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
  const z = (0.0193 * c[0] + 0.1192 * c[1] + 0.9505 * c[2]) / 1.08883;
  const f = (t: number) => (t > 0.008856 ? Math.cbrt(t) : 7.787 * t + 16 / 116);
  return [116 * f(y) - 16, 500 * (f(x) - f(y)), 200 * (f(y) - f(z))];
}

/** CIE76 colour difference. */
export function deltaE(a: string, b: string): number {
  const p = lab(a);
  const q = lab(b);
  return Math.hypot(p[0] - q[0], p[1] - q[1], p[2] - q[2]);
}
