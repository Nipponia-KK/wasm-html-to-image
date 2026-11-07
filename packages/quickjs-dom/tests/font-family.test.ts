import { describe, expect, it } from "vitest";
import { JSDOM } from "../src/jsdom.js";

// Expected values were read from Chrome 152 (element.style.fontFamily after assigning the key).
const chrome: Record<string, string> = {
  '"Arial"': "Arial",
  "'Arial'": "Arial",
  "Times New Roman": '"Times New Roman"',
  '"Times New Roman"': '"Times New Roman"',
  "Times  New   Roman": '"Times New Roman"',
  '"a,b"': '"a,b"',
  serif: "serif",
  '"serif"': '"serif"',
  '"Noto Serif JP",sans-serif': '"Noto Serif JP", sans-serif',
  "  Arial ,  Helvetica ": "Arial, Helvetica",
  '"1abc"': '"1abc"',
  "-apple-system, BlinkMacSystemFont": "-apple-system, BlinkMacSystemFont",
  inherit: "inherit",
  '"inherit"': '"inherit"',
  '"Hiragino Mincho ProN", "Yu Mincho", Source Han Serif, serif': '"Hiragino Mincho ProN", "Yu Mincho", "Source Han Serif", serif',
};

describe("font-family serialization", () => {
  it("reads back as Chrome serializes it", () => {
    const dom = new JSDOM("<div></div>", { virtualConsole: null });
    try {
      const el = dom.window.document.querySelector("div") as HTMLElement;
      for (const [input, expected] of Object.entries(chrome)) {
        el.style.fontFamily = "";
        el.style.fontFamily = input;
        expect(el.style.fontFamily, input).toBe(expected);
      }
      el.setAttribute("style", 'font-family: "Hiragino Mincho ProN", Source Han Serif');
      expect(el.style.fontFamily).toBe('"Hiragino Mincho ProN", "Source Han Serif"');
      expect(el.getAttribute("style")).toBe('font-family: "Hiragino Mincho ProN", Source Han Serif');
    } finally {
      dom.close();
    }
  });
});

describe("font-family in getComputedStyle", () => {
  it("is serialized as Chrome does (names of several words quoted)", () => {
    const dom = new JSDOM(
      '<style>p{font-family: Source Han Serif, "Yu Mincho", serif}</style><p>x</p><div style="font-family: Times New Roman"></div>',
      { virtualConsole: null },
    );
    try {
      const { window } = dom;
      const doc = window.document;
      expect(window.getComputedStyle(doc.querySelector("p")!).fontFamily).toBe('"Source Han Serif", "Yu Mincho", serif');
      expect(window.getComputedStyle(doc.querySelector("div")!).fontFamily).toBe('"Times New Roman"');
    } finally {
      dom.close();
    }
  });
});
