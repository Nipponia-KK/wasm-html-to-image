import { JSDOM as RealJSDOM } from "jsdom";
import { describe, expect, it } from "vitest";
import { JSDOM } from "../src/jsdom.js";

const SVG = "http://www.w3.org/2000/svg";
const HTML = "http://www.w3.org/1999/xhtml";
const MATHML = "http://www.w3.org/1998/Math/MathML";

const withPage = (use: (w: any) => void) => {
  const dom = new JSDOM("<!doctype html><body>", { virtualConsole: null });
  try {
    use(dom.window);
  } finally {
    dom.close();
  }
};

describe("createElement: names (the DOM's valid element local name, as jsdom)", () => {
  const invalid = ["", "a b", "1a", "a/b", "a>b", "-a", "=a", "a\u0000b", "a\tb", "a\nb", "é<"];
  const valid = ["a", "a<b", "a&b", "é", "aé", ":a", "_a", "a:b", "a-b", "a.b", "a=", 'a"b'];

  it.each(invalid)("refuses %j", (name) => {
    const real = new RealJSDOM("<!doctype html>").window.document;
    expect(() => real.createElement(name)).toThrow();
    withPage((w) => {
      // more than once: the later calls of a tag take another route
      for (let i = 0; i < 3; i++) {
        let error: any;
        try {
          w.document.createElement(name);
        } catch (e) {
          error = e;
        }
        expect(error?.name).toBe("InvalidCharacterError");
      }
      expect(w.eval(`try { document.createElement(${JSON.stringify(name)}); "made" } catch (e) { e.name }`)).toBe("InvalidCharacterError");
    });
  });

  it.each(valid)("accepts %j", (name) => {
    const real = new RealJSDOM("<!doctype html>").window.document.createElement(name);
    withPage((w) => {
      for (let i = 0; i < 3; i++) {
        const e = w.document.createElement(name);
        expect(e.localName).toBe(real.localName);
        expect(e.tagName).toBe(real.tagName);
      }
    });
  });

  it("an error inside a transaction comes out of the transaction", () => {
    const dom = new JSDOM("<!doctype html><body>", { virtualConsole: null });
    try {
      expect(() => dom.transact(() => dom.window.document.createElement("a b"))).toThrow(/valid name/);
    } finally {
      dom.close();
    }
  });
});

describe("createElement: an HTML element, whatever the name", () => {
  it.each(["svg", "SVG", "math", "path", "circle", "g", "rect", "text", "use", "image", "linearGradient", "foreignObject"])("%s is as in jsdom", (name) => {
    const real = new RealJSDOM("<!doctype html>").window.document.createElement(name);
    withPage((w) => {
      for (let i = 0; i < 3; i++) {
        const e = w.document.createElement(name);
        expect(e.tagName).toBe(real.tagName);
        expect(e.localName).toBe(real.localName);
        expect(e.namespaceURI).toBe(HTML);
        expect(e.constructor.name).toBe("HTMLUnknownElement");
        expect(e).toBeInstanceOf(w.HTMLUnknownElement);
      }
    });
  });

  it("what sits in such an element is HTML too, also after cloning", () => {
    withPage((w) => {
      const d = w.document;
      const box = d.createElement("svg");
      const p = box.appendChild(d.createElement("p"));
      expect(p.tagName).toBe("P");
      expect(p.namespaceURI).toBe(HTML);
      expect(box.matches("svg")).toBe(true);
      expect(box.matches("SVG")).toBe(true);
      expect(box.querySelector("P")).toBe(p);
      expect(box.outerHTML).toBe("<svg><p></p></svg>");
      const copy = box.cloneNode(true);
      expect(copy.tagName).toBe("SVG");
      expect(copy.namespaceURI).toBe(HTML);
      expect(copy.firstChild.tagName).toBe("P");
    });
  });

  it("is seen the same from inside the page", () => {
    withPage((w) => {
      expect(w.eval(`var e = document.createElement("svg"); [e.tagName, e.namespaceURI, e.constructor.name].join()`)).toBe(`SVG,${HTML},HTMLUnknownElement`);
    });
  });
});

describe("SVG and MathML still come from where they should", () => {
  it("createElementNS", () => {
    withPage((w) => {
      const d = w.document;
      const svg = d.createElementNS(SVG, "svg");
      expect(svg.tagName).toBe("svg");
      expect(svg.namespaceURI).toBe(SVG);
      expect(svg).toBeInstanceOf(w.SVGSVGElement);
      const gradient = d.createElementNS(SVG, "linearGradient");
      expect(gradient.tagName).toBe("linearGradient");
      expect(gradient.localName).toBe("linearGradient");
      expect(gradient).toBeInstanceOf(w.SVGElement);
      expect(d.createElementNS(MATHML, "math").namespaceURI).toBe(MATHML);
      const child = svg.appendChild(d.createElementNS(SVG, "circle"));
      expect(child.constructor.name).toBe("SVGCircleElement");
      expect(child.tagName).toBe("circle");
    });
  });

  it("createElementNS of the HTML namespace is createElement", () => {
    withPage((w) => {
      const e = w.document.createElementNS(HTML, "svg");
      expect(e.tagName).toBe("SVG");
      expect(e.namespaceURI).toBe(HTML);
    });
  });

  it("the parser", () => {
    withPage((w) => {
      const d = w.document;
      d.body.innerHTML = "<svg><circle></circle><foreignObject><p></p></foreignObject></svg><math><mi></mi></math>";
      const svg = d.querySelector("svg");
      expect(svg.tagName).toBe("svg");
      expect(svg.namespaceURI).toBe(SVG);
      expect(svg).toBeInstanceOf(w.SVGSVGElement);
      expect(svg.firstChild.tagName).toBe("circle");
      expect(svg.firstChild.namespaceURI).toBe(SVG);
      expect(d.querySelector("p").tagName).toBe("P");
      expect(d.querySelector("math").namespaceURI).toBe(MATHML);
    });
  });

  it("an XML document's createElement keeps the name", () => {
    withPage((w) => {
      const xml = new w.DOMParser().parseFromString("<root/>", "application/xml");
      expect(xml.createElement("svg").tagName).toBe("svg");
    });
  });
});
