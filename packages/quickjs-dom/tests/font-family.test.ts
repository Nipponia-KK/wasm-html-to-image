import { describe, expect, it } from "vitest";
import { JSDOM } from "../src/jsdom.js";

// What the CSSWG settled on (csswg-drafts#5846) and WPT css/cssom/font-family-serialization-001.html expects:
// quotes only where a name needs them. Released Chrome (152/156) still quotes multi-word names; Firefox keeps the
// author's quotes; jsdom 31 drops every quote (so it is wrong for names that need them).
const cases: Record<string, string> = {
  "Twisty Tie": "Twisty Tie",
  "'Twisty Tie'": "Twisty Tie",
  "Times New Roman": "Times New Roman",
  "Times  New   Roman": "Times New Roman",
  Veronica: "Veronica",
  "'Veronica'": "Veronica",
  '"Arial"': "Arial",
  "'34J'": '"34J"',
  "'1'": '"1"',
  "'A  B'": '"A  B"',
  '"a,b"': '"a,b"',
  serif: "serif",
  '"serif"': '"serif"',
  '"sans-serif"': '"sans-serif"',
  '"initial"': '"initial"',
  '"inherit"': '"inherit"',
  '"default"': '"default"',
  inherit: "inherit",
  "  Arial ,  Helvetica ": "Arial, Helvetica",
  "-apple-system, BlinkMacSystemFont": "-apple-system, BlinkMacSystemFont",
  "Twisty Tie, '34J', \"serif\", Veronica, sans-serif": 'Twisty Tie, "34J", "serif", Veronica, sans-serif',
  '"Hiragino Mincho ProN", "Yu Mincho", Source Han Serif, serif': "Hiragino Mincho ProN, Yu Mincho, Source Han Serif, serif",
};

describe("font-family serialization", () => {
  it("quotes only the names that need it, in style.fontFamily", () => {
    const dom = new JSDOM("<div></div>", { virtualConsole: null });
    try {
      const el = dom.window.document.querySelector("div") as HTMLElement;
      for (const [input, expected] of Object.entries(cases)) {
        el.setAttribute("style", `font-family: ${input}`);
        expect(el.style.fontFamily, `attr ${input}`).toBe(expected);
        el.style.fontFamily = "";
        el.style.fontFamily = input;
        expect(el.style.fontFamily, `prop ${input}`).toBe(expected);
      }
      el.setAttribute("style", 'font-family: "Hiragino Mincho ProN", Source Han Serif');
      expect(el.getAttribute("style")).toBe('font-family: "Hiragino Mincho ProN", Source Han Serif');
    } finally {
      dom.close();
    }
  });

  it("serializes the same way in getComputedStyle", () => {
    const dom = new JSDOM(
      '<style>p{font-family: Source Han Serif, "Yu Mincho", "34J", "serif"}</style><p>x</p><div style="font-family: \'Times New Roman\'"></div>',
      { virtualConsole: null },
    );
    try {
      const { window } = dom;
      const doc = window.document;
      expect(window.getComputedStyle(doc.querySelector("p")!).fontFamily).toBe('Source Han Serif, Yu Mincho, "34J", "serif"');
      expect(window.getComputedStyle(doc.querySelector("div")!).fontFamily).toBe("Times New Roman");
    } finally {
      dom.close();
    }
  });
});
