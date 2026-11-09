import { describe, expect, it } from "vitest";
import { JSDOM } from "../src/jsdom.js";

// appendChild and createElement go by number once the tag is known; what they do must be what the ordinary request does.
const page = (html = "<!doctype html><body><div id=a></div><div id=b></div>") => new JSDOM(html, { virtualConsole: null });

describe("appendChild by number", () => {
  it("appends, returns the child and moves it from its old parent", () => {
    const dom = page();
    try {
      const d = dom.window.document;
      const a = d.getElementById("a")!, b = d.getElementById("b")!;
      const s = d.createElement("span");
      expect(a.appendChild(s)).toBe(s);
      expect(s.parentNode).toBe(a);
      expect(b.appendChild(s)).toBe(s);
      expect(a.childNodes.length).toBe(0);
      expect(b.firstChild).toBe(s);
    } finally {
      dom.close();
    }
  });

  it("appends a fragment's children, and empties it", () => {
    const dom = page();
    try {
      const d = dom.window.document;
      const a = d.getElementById("a")!;
      const f = d.createDocumentFragment();
      f.appendChild(d.createElement("i"));
      f.appendChild(d.createElement("b"));
      expect(a.appendChild(f)).toBe(f);
      expect(a.innerHTML).toBe("<i></i><b></b>");
      expect(f.childNodes.length).toBe(0);
    } finally {
      dom.close();
    }
  });

  it("throws the page's error for an impossible insertion, and changes nothing", () => {
    const dom = page();
    try {
      const d = dom.window.document;
      const a = d.getElementById("a")!;
      const inner = a.appendChild(d.createElement("p"));
      expect(() => inner.appendChild(a)).toThrow(/contains the parent/);
      expect(inner.parentNode).toBe(a);
      expect(a.parentNode).toBe(d.body);
      expect(() => a.appendChild(d)).toThrow(/document cannot be inserted/i);
    } finally {
      dom.close();
    }
  });

  it("still tells a MutationObserver, and a custom element, about the insertion", async () => {
    const dom = page();
    try {
      const w = dom.window;
      const d = w.document;
      const a = d.getElementById("a")!;
      const records: unknown[] = [];
      const seen = new w.MutationObserver((list: unknown[]) => records.push(...list));
      seen.observe(a, { childList: true });
      a.appendChild(d.createElement("span"));
      await new Promise((r) => setTimeout(r, 0));
      expect(records.length).toBe(1);
      seen.disconnect();

      const connected: string[] = [];
      w.customElements.define(
        "x-seen",
        class extends w.HTMLElement {
          connectedCallback() {
            connected.push(this.localName);
          }
        },
      );
      a.appendChild(d.createElement("x-seen"));
      expect(connected).toEqual(["x-seen"]);
    } finally {
      dom.close();
    }
  });

  it("sees the new tree in the reads that follow", () => {
    const dom = page();
    try {
      const d = dom.window.document;
      const a = d.getElementById("a")!;
      expect(a.firstChild).toBe(null);
      const s = d.createElement("span");
      a.appendChild(s);
      expect(a.firstChild).toBe(s);
      expect(a.lastChild).toBe(s);
      expect(a.childElementCount).toBe(1);
    } finally {
      dom.close();
    }
  });
});

describe("createElement by number", () => {
  it("answers as the first element of the tag did", () => {
    const dom = page();
    try {
      const d = dom.window.document;
      for (const tag of ["div", "DIV", "a", "input", "madeup"]) {
        for (let i = 0; i < 3; i++) {
          const e = d.createElement(tag);
          expect(e.localName).toBe(tag.toLowerCase());
          expect(e.tagName).toBe(tag.toUpperCase());
          expect(e.ownerDocument).toBe(d);
          expect(e.childNodes.length).toBe(0);
          expect(e.constructor).toBe(d.createElement(tag).constructor);
        }
      }
      expect(d.createElement("input")).toBeInstanceOf(dom.window.HTMLInputElement);
      expect(d.createElement("madeup")).toBeInstanceOf(dom.window.HTMLUnknownElement);
    } finally {
      dom.close();
    }
  });

  it("gives each element its own object, and keeps the case of an XML document's tag", () => {
    const dom = page();
    try {
      const w = dom.window;
      const d = w.document;
      expect(d.createElement("p")).not.toBe(d.createElement("p"));
      const xml = new w.DOMParser().parseFromString("<root/>", "application/xml");
      for (let i = 0; i < 3; i++) {
        const e = xml.createElement("MixedCase");
        expect(e.localName).toBe("MixedCase");
        expect(e.ownerDocument).toBe(xml);
      }
    } finally {
      dom.close();
    }
  });

  it("takes the ordinary way for a tag that is not ASCII", () => {
    const dom = page();
    try {
      const d = dom.window.document;
      for (let i = 0; i < 3; i++) expect(d.createElement("xé").localName).toBe("xé");
    } finally {
      dom.close();
    }
  });
});

describe("the event a listener of the host gets", () => {
  it("carries a number that is a float in the page, without another call", () => {
    const dom = page();
    try {
      const w = dom.window;
      const d = w.document;
      const a = d.getElementById("a")!;
      const made = new w.MouseEvent("click", { bubbles: true, button: 2, clientX: 1.5 });
      const stamp = made.timeStamp;
      let got: number[] = [];
      a.addEventListener("click", (e: any) => {
        got = [e.button, e.clientX, e.timeStamp];
      });
      a.dispatchEvent(made);
      expect(got).toEqual([2, 1.5, stamp]);
    } finally {
      dom.close();
    }
  });
});

describe("the reads of a property", () => {
  it("are told apart by object and by name, and forgotten when the page changes", () => {
    const dom = page();
    try {
      const d = dom.window.document;
      const a = d.getElementById("a")!, b = d.getElementById("b")!;
      expect(a.id).toBe("a");
      expect(b.id).toBe("b");
      expect(a.className).toBe("");
      a.setAttribute("class", "x");
      expect(a.className).toBe("x");
      expect(b.className).toBe("");
      expect(a.getAttribute("id")).toBe("a");
      expect(a.getAttribute("class")).toBe("x");
      a.setAttribute("class", "y");
      expect(a.getAttribute("class")).toBe("y");
    } finally {
      dom.close();
    }
  });
});

describe("the time budget of a call", () => {
  it("is not what is left of the last call's", async () => {
    const dom = new JSDOM("<!doctype html><body>", { virtualConsole: null, engine: { timeout: 1000 } } as any);
    try {
      const w = dom.window as any;
      w.eval("document.addEventListener('busy', function () { var s = Date.now(); while (Date.now() - s < 400); });");
      w.document.title = "armed";
      await new Promise((r) => setTimeout(r, 750));
      expect(() => w.document.dispatchEvent(new w.Event("busy"))).not.toThrow();
    } finally {
      dom.close();
    }
  });
});
