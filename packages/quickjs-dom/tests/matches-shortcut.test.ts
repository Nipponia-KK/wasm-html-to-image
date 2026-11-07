import { describe, expect, it } from "vitest";
import { JSDOM as RealJSDOM } from "jsdom";
import { JSDOM } from "../src/jsdom.js";

const html =
  '<!doctype html><body><div id="d" class="a b"></div><input id="i" type="submit"><input id="j" type="text"><script id="s"></script>' +
  '<svg id="v"><foreignObject id="f"></foreignObject><style id="st"></style></svg><my-el id="m"></my-el></body>';
const selectors = [
  "script, style",
  "div",
  "div.a",
  "div.c",
  "#d",
  "input[type=submit], input[type=button], input[type=reset]",
  "input[type=text]",
  "input, textarea",
  "my-el",
  "style",
  "div p",
  "div > p",
  "a:not(.x)",
  "DIV",
  "input:not([list])[type=text]",
  "input:not([type]):not([list]), button",
  "div:not(.a)",
  "p:not(.a), div",
];

describe("matches", () => {
  it("answers like jsdom for selectors the tag settles and for the rest", () => {
    const real = new RealJSDOM(html).window.document;
    const ours = new JSDOM(html, { virtualConsole: null });
    try {
      const doc = ours.window.document;
      for (const id of ["d", "i", "j", "s", "v", "f", "st", "m"]) {
        for (const sel of selectors) {
          expect(doc.getElementById(id)!.matches(sel), `${id} ${sel}`).toBe(real.getElementById(id)!.matches(sel));
        }
      }
    } finally {
      ours.close();
    }
  });
});
