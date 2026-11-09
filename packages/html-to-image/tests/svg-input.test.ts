import { describe, expect, it } from "vitest";
import { svgHasText, svgIntrinsicSize, svgPage } from "../src/svg-input.js";

describe("svg input helpers", () => {
  it("finds text elements", () => {
    expect(svgHasText('<svg><text x="1">a</text></svg>')).toBe(true);
    expect(svgHasText("<svg><textPath>a</textPath></svg>")).toBe(false);
    expect(svgHasText("<svg><rect/></svg>")).toBe(false);
  });

  it("takes the size from width and height, else the viewBox, else 300x150", () => {
    expect(svgIntrinsicSize('<svg xmlns="http://www.w3.org/2000/svg" width="300" height="60px"/>')).toEqual({ width: 300, height: 60 });
    expect(svgIntrinsicSize('<svg viewBox="0 0 200 100"/>')).toEqual({ width: 200, height: 100 });
    expect(svgIntrinsicSize('<svg viewBox="0 0 200 100" width="400"/>')).toEqual({ width: 400, height: 200 });
    expect(svgIntrinsicSize('<svg width="50%"/>')).toEqual({ width: 300, height: 150 });
  });

  it("shows the svg edge to edge at its size", () => {
    const html = svgPage(new TextEncoder().encode("<svg/>"), { width: 30, height: 20 });
    expect(html).toContain("data:image/svg+xml;base64,PHN2Zy8+");
    expect(html).toContain("width:30px;height:20px");
  });
});
