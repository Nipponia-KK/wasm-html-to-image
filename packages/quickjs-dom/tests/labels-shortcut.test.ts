import { describe, expect, it } from "vitest";
import { JSDOM as RealJSDOM } from "jsdom";
import { JSDOM } from "../src/jsdom.js";

const html =
  '<!doctype html><body><div id="d"></div><span id="s"></span><label for="i">L</label><input id="i"><button id="b"></button>' +
  '<select id="se"></select><textarea id="t"></textarea><output id="o"></output><meter id="m"></meter><progress id="p"></progress><a id="a"></a></body>';

describe("labels", () => {
  it("is present on the labelable elements only, as in jsdom", () => {
    const real = new RealJSDOM(html).window.document;
    const ours = new JSDOM(html, { virtualConsole: null });
    try {
      const doc = ours.window.document;
      for (const id of ["d", "s", "i", "b", "se", "t", "o", "m", "p", "a"]) {
        const a = (real.getElementById(id) as any).labels;
        const b = (doc.getElementById(id) as any).labels;
        expect(b === undefined, id).toBe(a === undefined);
        if (a) expect(b.length, id).toBe(a.length);
      }
    } finally {
      ours.close();
    }
  });
});
