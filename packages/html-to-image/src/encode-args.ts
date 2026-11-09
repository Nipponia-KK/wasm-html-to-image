/**
 * Encode-argument conversion (moved verbatim from `./core.ts`; behavior
 * unchanged): WASM int enums and render-option object construction.
 */
import type { HtmlToImageOptions, OutputFormat } from "./core.js";

/** `RenderFormat` enum values (mirrors `bridge_types.h`). */
export const FORMAT_INT: Record<OutputFormat, number> = {
  svg: 0,
  png: 1,
  webp: 2,
  pdf: 3,
  jpeg: 4,
  avif: 5,
  jxl: 8,
  apng: 9,
  gif: 10,
  tiff: 11,
  raw: 6,
  thumbhash: 7,
  none: -1,
};

/** `RenderFormat::TIFFCmyk`: the CMYK variant of `tiff` (the color space rides in the format int). */
export const TIFF_CMYK_INT = 12;

export const FIT_INT: Record<NonNullable<HtmlToImageOptions["fit"]>, number> = {
  contain: 0,
  cover: 1,
  fill: 2,
};

/** Map `crop`/`fit` to satoru render-option fields (`parse_satoru_options`). */
export function buildSatoruOptions(
  crop: HtmlToImageOptions["crop"],
  fit: HtmlToImageOptions["fit"],
): Record<string, unknown> {
  const o: Record<string, unknown> = {};
  if (crop) {
    o.cropX = crop.x;
    o.cropY = crop.y;
    o.cropWidth = crop.width;
    o.cropHeight = crop.height;
  }
  if (fit) o.fitType = FIT_INT[fit];
  return o;
}

/** Full satoru render-option fields (text, media, PDF metadata). */
export interface FullSatoruOptionsInput {
  crop: HtmlToImageOptions["crop"];
  fit: HtmlToImageOptions["fit"];
  textToPaths?: boolean;
  mediaTypeInt: number;
  colorSpace?: "srgb" | "cmyk";
  scale?: number;
  pdfOutline?: boolean;
  pdfBackground?: string;
  pdfTitle?: string;
  pdfAuthor?: string;
  pdfSubject?: string;
  pdfKeywords?: string;
  pdfCreator?: string;
  pdfProducer?: string;
  pdfMargin?: { top?: number; right?: number; bottom?: number; left?: number };
  pdfHeader?: string;
  pdfFooter?: string;
  pdfSkipRunning?: number;
}

/**
 * Canonical full satoru render-option builder (正本はここ).
 * `core.ts` HTML パスはこの関数のみを使い、個別組み立てをしない。
 */
export function buildFullSatoruOptions(
  input: FullSatoruOptionsInput,
): Record<string, unknown> {
  const {
    textToPaths,
    mediaTypeInt,
    pdfOutline,
    pdfBackground,
    pdfTitle,
    pdfAuthor,
    pdfSubject,
    pdfKeywords,
    pdfCreator,
    pdfProducer,
    pdfMargin,
    pdfHeader,
    pdfFooter,
    pdfSkipRunning,
  } = input;
  return {
    ...buildSatoruOptions(input.crop, input.fit),
    svgTextToPaths: textToPaths ?? true,
    mediaType: mediaTypeInt,
    ...(input.colorSpace === "cmyk" ? { colorSpace: 1 } : {}),
    ...(input.scale !== undefined && input.scale !== 1 ? { scale: input.scale } : {}),
    // The engine's default is on: only an explicit false has to be sent.
    ...(pdfOutline === false ? { pdfOutline: false } : {}),
    ...(pdfBackground !== undefined ? { backgroundColor: parseCssColor(pdfBackground) } : {}),
    pdfTitle: pdfTitle ?? "",
    pdfAuthor: pdfAuthor ?? "",
    pdfSubject: pdfSubject ?? "",
    pdfKeywords: pdfKeywords ?? "",
    pdfCreator: pdfCreator ?? "",
    pdfProducer: pdfProducer ?? "",
    pdfMarginTop: pdfMargin?.top ?? 0,
    pdfMarginRight: pdfMargin?.right ?? 0,
    pdfMarginBottom: pdfMargin?.bottom ?? 0,
    pdfMarginLeft: pdfMargin?.left ?? 0,
    pdfHeader: pdfHeader ?? "",
    pdfFooter: pdfFooter ?? "",
    ...(pdfSkipRunning ? { pdfSkipRunning: Math.max(0, Math.floor(pdfSkipRunning)) } : {}),
  };
}

/**
 * `#rgb`, `#rrggbb` or `#rrggbbaa` as the engine's ARGB integer (`0xAARRGGBB`). Anything else is an error:
 * a mistyped colour should not silently paint nothing.
 */
export function parseCssColor(color: string): number {
  const m = /^#([0-9a-f]{3}|[0-9a-f]{6}|[0-9a-f]{8})$/i.exec(color.trim());
  if (!m) throw new Error(`invalid colour "${color}": use #rgb, #rrggbb or #rrggbbaa`);
  let hex = m[1];
  if (hex.length === 3) hex = hex.split("").map((c) => c + c).join("");
  const a = hex.length === 8 ? hex.slice(6, 8) : "ff";
  return parseInt(a + hex.slice(0, 6), 16) >>> 0;
}
