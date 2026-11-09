/**
 * Input classification and byte normalization (moved verbatim from
 * `./core.ts`; behavior unchanged).
 */

/**
 * Normalize byte-like input to `Uint8Array` without copying when the input
 * already is one. Read-only downstream uses (sniffing, caching, embind
 * input which copies itself) make sharing safe. NOTE: this must NOT be
 * used for Embind output views, which require an owning copy.
 */
export function toBytes(data: Uint8Array | ArrayBuffer): Uint8Array {
  return data instanceof Uint8Array ? data : new Uint8Array(data);
}

function ascii(
  bytes: Uint8Array,
  start: number,
  end: number,
): string | null {
  if (bytes.length < end) return null;
  let s = "";
  for (let i = start; i < end; i++) s += String.fromCharCode(bytes[i]);
  return s;
}

/**
 * A document whose root element is `<svg>`: only an XML prolog, comments and an `svg` doctype may
 * come before it. An HTML page that merely contains an inline `<svg>` (an icon in the header) has
 * `<!doctype html>` or an `<html>` root first, so it stays HTML.
 */
const SVG_DOCUMENT =
  /^(?:\s*(?:<\?[\s\S]*?\?>|<!--[\s\S]*?-->|<!doctype\s+svg[^>]*>))*\s*<svg[\s/>]/i;

/** Check if byte sequence looks like an SVG document. */
function isSvgBytes(bytes: Uint8Array): boolean {
  // Skip UTF-8 BOM if present
  const start =
    bytes.length >= 3 && bytes[0] === 0xef && bytes[1] === 0xbb && bytes[2] === 0xbf
      ? 3
      : 0;
  const head = ascii(bytes, start, Math.min(bytes.length, start + 1024));
  return !!head && SVG_DOCUMENT.test(head);
}

/** Check if string looks like an SVG document. */
function isSvgString(s: string): boolean {
  return SVG_DOCUMENT.test(s.slice(0, 1024));
}

/** Detect a known image container by magic bytes; `null` when unknown. */
export function sniffImageFormat(bytes: Uint8Array): string | null {
  if (
    bytes.length >= 4 &&
    bytes[0] === 0x89 &&
    bytes[1] === 0x50 &&
    bytes[2] === 0x4e &&
    bytes[3] === 0x47
  )
    return "png";
  if (
    bytes.length >= 3 &&
    bytes[0] === 0xff &&
    bytes[1] === 0xd8 &&
    bytes[2] === 0xff
  )
    return "jpeg";
  if (ascii(bytes, 0, 4) === "RIFF" && ascii(bytes, 8, 12) === "WEBP")
    return "webp";
  if (ascii(bytes, 0, 6) === "GIF87a" || ascii(bytes, 0, 6) === "GIF89a")
    return "gif";
  if (
    ascii(bytes, 4, 8) === "ftyp" &&
    (ascii(bytes, 8, 12) === "avif" || ascii(bytes, 8, 12) === "avis")
  )
    return "avif";
  if (bytes.length >= 2 && bytes[0] === 0x42 && bytes[1] === 0x4d)
    return "bmp";
  // JPEG XL: bare codestream (FF 0A) or ISO BMFF container signature box.
  if (bytes.length >= 2 && bytes[0] === 0xff && bytes[1] === 0x0a) return "jxl";
  if (
    bytes.length >= 12 &&
    bytes[0] === 0 &&
    bytes[1] === 0 &&
    bytes[2] === 0 &&
    bytes[3] === 0x0c &&
    ascii(bytes, 4, 8) === "JXL "
  )
    return "jxl";
  if (isSvgBytes(bytes)) return "svg";
  return null;
}

/**
 * Whether `value` is image input.
 *
 * - Binary (`Uint8Array` / `ArrayBuffer`): magic-byte sniffing for
 *   PNG/JPEG/WebP/GIF/AVIF/JXL/BMP/SVG. Unknown magic throws.
 * - `string`: `data:image/` prefix or `<svg` element means image input; anything else is HTML.
 * - `string[]` (HTML vector) / `undefined`: not image input.
 */
export function isImageInput(value: unknown): boolean {
  if (value === undefined || value === null || Array.isArray(value))
    return false;
  if (typeof value === "string") {
    if (value.startsWith("data:image/")) return true;
    if (isSvgString(value)) return true;
    return false;
  }
  const bytes =
    value instanceof Uint8Array || value instanceof ArrayBuffer
      ? toBytes(value)
      : null;
  if (bytes === null) {
    throw new Error(
      "wasm-html-to-image: unsupported input type (expected HTML string, data URL, or image bytes)",
    );
  }
  if (sniffImageFormat(bytes) !== null) return true;
  throw new Error(
    `wasm-html-to-image: unrecognized image input (unknown magic bytes, len=${bytes.length})`,
  );
}

export function dataUrlToBytes(s: string): Uint8Array {
  const comma = s.indexOf(",");
  if (comma < 0 || !s.startsWith("data:")) {
    throw new Error("wasm-html-to-image: malformed data URL");
  }
  const meta = s.slice(0, comma);
  const body = s.slice(comma + 1);
  if (meta.includes(";base64")) {
    const bin = atob(body.replace(/\s/g, ""));
    const out = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
    return out;
  }
  return new TextEncoder().encode(decodeURIComponent(body));
}

/** Normalize image input to raw bytes (data URLs are decoded, SVG strings encoded to UTF-8). */
export function imageInputToBytes(
  value: string | Uint8Array | ArrayBuffer,
): Uint8Array {
  if (typeof value === "string") {
    if (value.startsWith("data:")) return dataUrlToBytes(value);
    return new TextEncoder().encode(value);
  }
  return toBytes(value);
}

/** Image-input value shape accepted by `imageInputToBytes`. */
export type ImageInputValue = string | Uint8Array | ArrayBuffer;

/**
 * `unknown`-safe variant of {@link imageInputToBytes} for post-`isImageInput`
 * paths: narrows with `instanceof`/`typeof` instead of `as` casts.
 * Throws the same "unsupported input type" error for non-image shapes.
 */
export function imageBytesFromUnknown(value: unknown): Uint8Array {
  if (
    typeof value === "string" ||
    value instanceof Uint8Array ||
    value instanceof ArrayBuffer
  ) {
    return imageInputToBytes(value);
  }
  throw new Error(
    "wasm-html-to-image: unsupported input type (expected HTML string, data URL, or image bytes)",
  );
}
