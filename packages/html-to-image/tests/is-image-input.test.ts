import { describe, expect, it } from "vitest";
import { isImageInput } from "../src/core.js";

const u8 = (arr: number[]): Uint8Array => new Uint8Array(arr);
const ascii = (s: string): number[] => [...s].map((c) => c.charCodeAt(0));

const PNG = u8([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);
const GIF89 = u8([...ascii("GIF89a"), 0x01, 0x00]);
const AVIF = u8([0x00, 0x00, 0x00, 0x1c, ...ascii("ftypavif")]);

describe("isImageInput: magic bytes", () => {
  const cases: [string, Uint8Array][] = [
    ["png", PNG],
    ["gif89a", GIF89],
    ["avif", AVIF],
    ["svg", new TextEncoder().encode('<svg width="100" height="100"></svg>')],
  ];
  it.each(cases)("%s is image input", (_name, bytes) => {
    expect(isImageInput(bytes)).toBe(true);
  });

  it("throws on unknown magic bytes", () => {
    expect(() => isImageInput(u8([0x00, 0x01, 0x02, 0x03]))).toThrow(
      /unrecognized image input/,
    );
  });

  it("throws on truncated input", () => {
    expect(() => isImageInput(u8([0x89]))).toThrow(/unrecognized image input/);
    expect(() => isImageInput(u8([]))).toThrow(/unrecognized image input/);
  });
});

describe("isImageInput: strings", () => {
  it("treats data:image/ URLs and SVG elements as image input", () => {
    expect(isImageInput("data:image/png;base64,iVBORw0KGgo=")).toBe(true);
    expect(isImageInput("data:image/svg+xml,%3Csvg/%3E")).toBe(true);
    expect(isImageInput('<svg width="100" height="100"></svg>')).toBe(true);
    expect(isImageInput('<?xml version="1.0"?><svg></svg>')).toBe(true);
    expect(isImageInput('<?xml version="1.0"?><!-- c --><!DOCTYPE svg PUBLIC "-//W3C//DTD SVG 1.1//EN" "x.dtd"><svg></svg>')).toBe(true);
    expect(isImageInput("\n  <svg/>")).toBe(true);
  });

  it("keeps HTML that merely contains an inline <svg> as HTML", () => {
    expect(isImageInput('<!doctype html><html><body><svg width="10"></svg></body></html>')).toBe(false);
    expect(isImageInput('<!DOCTYPE html><svg></svg>')).toBe(false);
    expect(isImageInput('<div><svg></svg></div>')).toBe(false);
    expect(isImageInput('<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><svg></svg></html>')).toBe(false);
    expect(() => isImageInput(new TextEncoder().encode("<!doctype html><svg></svg>"))).toThrow(/unknown magic/);
  });

  it("treats other strings as HTML", () => {
    expect(isImageInput("<h1>hi</h1>")).toBe(false);
    expect(isImageInput("")).toBe(false);
    expect(isImageInput("data:text/html,<h1>hi</h1>")).toBe(false);
  });
});
