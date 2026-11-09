/**
 * A short text (a diagram) as a URL-safe string and back: raw deflate through the browser's own
 * CompressionStream, then base64url. Both return `undefined` where CompressionStream is missing, so the
 * caller simply goes without a shareable link.
 */

const toBase64Url = (bytes: Uint8Array): string => {
  let binary = "";
  for (const b of bytes) binary += String.fromCharCode(b);
  return btoa(binary).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
};

const fromBase64Url = (text: string): Uint8Array => {
  const padded = text.replace(/-/g, "+").replace(/_/g, "/") + "=".repeat((4 - (text.length % 4)) % 4);
  const binary = atob(padded);
  const bytes = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
  return bytes;
};

const pipe = async (input: Uint8Array, stream: { writable: WritableStream<BufferSource>; readable: ReadableStream<Uint8Array> }): Promise<Uint8Array> => {
  const writer = stream.writable.getWriter();
  // a damaged input fails on the reading side, which the caller catches; the writing side must not leave its own rejection behind
  void writer.write(input as Uint8Array<ArrayBuffer>).then(() => writer.close()).catch(() => undefined);
  return new Uint8Array(await new Response(stream.readable).arrayBuffer());
};

export async function encodeText(text: string): Promise<string | undefined> {
  if (typeof CompressionStream === "undefined") return undefined;
  try {
    return toBase64Url(await pipe(new TextEncoder().encode(text), new CompressionStream("deflate-raw")));
  } catch {
    return undefined;
  }
}

export async function decodeText(encoded: string): Promise<string | undefined> {
  if (typeof DecompressionStream === "undefined") return undefined;
  try {
    return new TextDecoder().decode(await pipe(fromBase64Url(encoded), new DecompressionStream("deflate-raw")));
  } catch {
    return undefined; // a damaged or foreign link: start from what is stored
  }
}

/** Longest `src=` value the playground puts in a URL; a longer text is kept in the browser only. */
export const MAX_SHARED_LENGTH = 6000;
