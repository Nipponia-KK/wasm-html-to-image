/**
 * Playground-parity diagnostics (satoru-compatible shapes).
 *
 * `LogLevel` mirrors `satoru/log-level.ts` verbatim (numeric levels shared
 * with the WASM `set_log_level` binding). `RenderDiagnostics` mirrors
 * satoru's `RenderDiagnostics` so playground-style consumers can switch
 * imports without reshaping reports.
 */

import type { OutputFormat } from "./core.js";

/** Log severity, shared with the WASM `set_log_level` binding. */
export enum LogLevel {
  None = 0,
  Error = 1,
  Warning = 2,
  Info = 3,
  Debug = 4,
}

/** Status of a single discovered resource. */
export interface ResourceDiagnostic {
  type: "font" | "css" | "image";
  url: string;
  name?: string;
  status: "pending" | "loaded" | "failed" | "skipped";
  bytes?: number;
  reason?: string;
}

/**
 * Font record (best effort, JS-side only).
 * NOTE: per-glyph C++ diagnostics (`get_font_diagnostics`) have no binding
 * in this build (see `src/cpp/main.cpp`), so entries describe fonts this
 * layer loaded (option preloads, fetched font resources) rather than the
 * renderer's matched faces.
 */
export interface FontDiagnostic {
  family: string;
  weight?: number;
  style?: "normal" | "italic" | "oblique";
  status: "loaded" | "fallback" | "missing";
  source?: string;
  characters?: string;
}

/** A warning/error entry in the diagnostics report. */
export interface DiagnosticMessage {
  code: string;
  message: string;
  source?: string;
}

/** Full render diagnostics report (satoru-compatible). */
export interface RenderDiagnostics {
  version: 1;
  format: OutputFormat;
  width: number;
  height?: number;
  mediaType: "screen" | "print";
  timings: Record<string, number>;
  resources: ResourceDiagnostic[];
  fonts: FontDiagnostic[];
  warnings: DiagnosticMessage[];
  errors: DiagnosticMessage[];
}

/** Machine-readable diagnostic codes (satoru parity). */
export const DIAGNOSTIC_CODES = {
  LIMIT_TIMEOUT: "LIMIT_TIMEOUT",
  LIMIT_RESOURCE_SIZE: "LIMIT_RESOURCE_SIZE",
  LIMIT_TOTAL_SIZE: "LIMIT_TOTAL_SIZE",
  LIMIT_RESOURCE_COUNT: "LIMIT_RESOURCE_COUNT",
  LIMIT_PROTOCOL_BLOCKED: "LIMIT_PROTOCOL_BLOCKED",
  LIMIT_HOST_BLOCKED: "LIMIT_HOST_BLOCKED",
  /** Extension: a resource fetch (or resolveResource hook) failed. */
  RESOURCE_FETCH_FAILED: "RESOURCE_FETCH_FAILED",
  /** C++ `[CODE]` prefix family (see `src/cpp/utils/logging.h` convention). */
  MAGIC_COLOR_PARSE_FAILED: "MAGIC_COLOR_PARSE_FAILED",
  FONT_UNICODE_RANGE_PARSE_FAILED: "FONT_UNICODE_RANGE_PARSE_FAILED",
  COLLECT_RESOURCES_FAILED: "COLLECT_RESOURCES_FAILED",
  JS_LOG_FORWARD_FAILED: "JS_LOG_FORWARD_FAILED",
  PDF_MERGE_FAILED: "PDF_MERGE_FAILED",
  /** A Mermaid diagram that did not parse (shown as an error box). */
  MERMAID_ERROR: "MERMAID_ERROR",
  /** A Mermaid source the input repair (`compat` / `recover`) had to change. */
  MERMAID_RECOVERED: "MERMAID_RECOVERED",
} as const;

/**
 * C++ log convention (`src/cpp/utils/logging.h`): error/warning messages
 * carry a `[CODE] human-readable detail` prefix where CODE maps to
 * `DiagnosticMessage.code`. This thin layer parses that prefix back into
 * `{code, message, source}` without touching C++ or message text.
 */
export const CPP_LOG_PREFIX_RE = /^\[([A-Z0-9_]+)\]\s*(.*)$/;

/** Split a `[CODE] detail` string; `null` when no prefix is present. */
export function parseBracketPrefix(text: string): {
  code: string;
  message: string;
} | null {
  const m = CPP_LOG_PREFIX_RE.exec(text);
  if (!m) return null;
  return { code: m[1], message: m[2] };
}

/**
 * Parse a raw C++-style log line into a `DiagnosticMessage`.
 * No-prefix input falls back to `fallbackCode` with the raw text intact.
 */
export function parseCppLogToDiagnostic(
  raw: string,
  fallbackCode: string,
  source?: string,
): DiagnosticMessage {
  const parsed = parseBracketPrefix(raw);
  if (!parsed) return { code: fallbackCode, message: raw, source };
  return { code: parsed.code, message: parsed.message, source };
}

/**
 * Normalize a caught value into a `DiagnosticMessage`, preserving C++
 * `[CODE]` prefixes when present. Non-prefixed messages keep
 * `fallbackCode`, so existing JS-only paths are byte-identical.
 */
export function errorToDiagnostic(
  e: unknown,
  fallbackCode: string,
  source?: string,
): DiagnosticMessage {
  const raw = e instanceof Error ? e.message : String(e);
  return parseCppLogToDiagnostic(raw, fallbackCode, source);
}

/** Safety/performance limits, enforced in JS around resource resolution. */
export interface RenderLimits {
  /** Timeout for the whole render in milliseconds. */
  timeoutMs?: number;
  /** Maximum bytes for a single resource. */
  maxResourceBytes?: number;
  /** Maximum total bytes for all resources. */
  maxTotalResourceBytes?: number;
  /** Maximum number of resources to load. */
  maxResourceCount?: number;
  /**
   * Throw (code `LIMIT_RESOURCE_COUNT`) instead of rendering when font files had to be skipped
   * because of `maxResourceCount`, so text is never silently drawn as missing glyphs. Default `false`.
   */
  strictFonts?: boolean;
  /** Allowed URL protocols (e.g. `["http:", "https:"]`). */
  allowedProtocols?: string[];
  /** Allowed hostnames. */
  allowedHosts?: string[];
  /** Blocked hostnames. */
  blockedHosts?: string[];
  /**
   * Node only, opt-in. Restrict local file reads to files inside `baseUrl`
   * (symlinks resolved): `../` escapes are refused, and with no `baseUrl`
   * no local file is read at all. Enable it when rendering untrusted HTML.
   */
  confineFilesToBaseUrl?: boolean;
}

/**
 * Override hook for resource resolution. Receives the absolute (or
 * baseUrl-relative) URL and a fallback that runs the built-in resolution;
 * return bytes to inject, or `null` to skip the resource.
 */
export type ResolveResourceHook = (
  url: string,
  fallback: () => Promise<Uint8Array | null>,
) => Promise<Uint8Array | null>;
