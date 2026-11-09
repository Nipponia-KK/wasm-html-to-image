/**
 * SVG image input that draws text.
 *
 * The converter decodes an SVG on its own, with no fonts, so every `<text>` came out missing. The page
 * pipeline loads fonts (the default set, `fonts`, `@font-face`) before it draws, so such an SVG is drawn
 * through it, at its own size, and the converter then works on that picture.
 */

const DEFAULT_SIZE = { width: 300, height: 150 };

/** Whether the SVG source contains text the converter could not draw without fonts. */
export function svgHasText(svg: string): boolean {
  return /<text[\s>]/.test(svg);
}

/** The pixel size an SVG is drawn at: its width and height, else its viewBox, else the CSS default 300x150. */
export function svgIntrinsicSize(svg: string): { width: number; height: number } {
  const root = /<svg\b[^>]*>/i.exec(svg)?.[0] ?? "";
  const px = (name: string): number | undefined => {
    const m = new RegExp(String.raw`\s${name}\s*=\s*["']\s*([0-9.]+)\s*(px)?\s*["']`, "i").exec(root);
    const v = m ? Number(m[1]) : NaN;
    return v > 0 ? v : undefined;
  };
  const w = px("width");
  const h = px("height");
  const vb = /\sviewBox\s*=\s*["']\s*[-0-9.]+[\s,]+[-0-9.]+[\s,]+([0-9.]+)[\s,]+([0-9.]+)\s*["']/i.exec(root);
  const vw = vb ? Number(vb[1]) : undefined;
  const vh = vb ? Number(vb[2]) : undefined;
  if (w && h) return { width: w, height: h };
  if (vw && vh) {
    if (w) return { width: w, height: (w * vh) / vw };
    if (h) return { width: (h * vw) / vh, height: h };
    return { width: vw, height: vh };
  }
  return DEFAULT_SIZE;
}

/** A page that shows the SVG, edge to edge, at its own size. */
export function svgPage(svgBytes: Uint8Array, size: { width: number; height: number }): string {
  let bin = "";
  for (let i = 0; i < svgBytes.length; i += 0x8000)
    bin += String.fromCharCode(...svgBytes.subarray(i, i + 0x8000));
  return (
    `<!DOCTYPE html><html><body style="margin:0">` +
    `<img src="data:image/svg+xml;base64,${btoa(bin)}" style="display:block;width:${size.width}px;height:${size.height}px">` +
    `</body></html>`
  );
}
