import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createDOM, type QuickJSDOM } from "../src/index.js";

// A node that answers a host call is answered in C (qd_rpc: rpc_answer_node)
// when the prototype it carries is one the C side registered: the id, class,
// nodeType, nodeName and localName are read off the ND struct there, and
// rpc.js's tables are kept as handleString would keep them. These tests pin
// that an answer made there is indistinguishable from one made in rpc.js:
// the wrapper's class and identity, the names, and the live-handle count.
// Anything the C side cannot name (a custom element's upgraded prototype)
// takes the rpc.js answer and must arrive the same way.

let dom: QuickJSDOM;
let w: any;
let d: any;
beforeAll(async () => {
  dom = await createDOM({ html: "<!doctype html><html><body><div id=r></div></body></html>" }, { settleMs: 0 });
  w = dom.bridge().window;
  d = w.document;
});
afterAll(() => dom.close());

describe("node answers made in C", () => {
  it("an element answers with its class and names", () => {
    const div = d.createElement("div");
    expect(div instanceof w.HTMLDivElement).toBe(true);
    expect(div.tagName).toBe("DIV");
    expect(div.nodeName).toBe("DIV");
    expect(div.localName).toBe("div");
    const input = d.createElement("input");
    expect(input instanceof w.HTMLInputElement).toBe(true);
    const unknown = d.createElement("wizzle");
    expect(unknown instanceof w.HTMLUnknownElement).toBe(true);
    expect(unknown.tagName).toBe("WIZZLE");
  });

  it("text, comment, fragment, document and doctype answer with theirs", () => {
    const text = d.createTextNode("hi");
    expect(text instanceof w.Text).toBe(true);
    expect(text.nodeName).toBe("#text");
    const comment = d.createComment("c");
    expect(comment instanceof w.Comment).toBe(true);
    expect(comment.nodeName).toBe("#comment");
    const frag = d.createDocumentFragment();
    expect(frag instanceof w.DocumentFragment).toBe(true);
    expect(frag.nodeName).toBe("#document-fragment");
    expect(d instanceof w.Document).toBe(true);
    expect(d.nodeName).toBe("#document");
    expect(d.doctype instanceof w.DocumentType).toBe(true);
    expect(d.doctype.nodeName).toBe("html");
  });

  it("the same node keeps the same wrapper across crossings", () => {
    const host = d.getElementById("r");
    const div = d.createElement("div");
    host.appendChild(div);
    expect(host.firstChild).toBe(div);
    expect(div.parentNode).toBe(host);
    expect(d.getElementById("r")).toBe(host);
  });

  it("namespace-made elements keep the classes the JS answer gives them", () => {
    const svg = d.createElementNS("http://www.w3.org/2000/svg", "svg");
    expect(svg instanceof w.SVGSVGElement).toBe(true);
    const circle = d.createElementNS("http://www.w3.org/2000/svg", "circle");
    expect(circle instanceof w.SVGCircleElement).toBe(true);
    // the tag table is what names an <a>, in either namespace
    const a = d.createElementNS("http://www.w3.org/2000/svg", "a");
    expect(a instanceof w.HTMLAnchorElement).toBe(true);
  });

  it("an XML document's element keeps its case", () => {
    const xd = new w.DOMParser().parseFromString("<Root><Child/></Root>", "text/xml");
    expect(xd.documentElement.tagName).toBe("Root");
    expect(xd.documentElement.nodeName).toBe("Root");
    expect(xd.documentElement.localName).toBe("Root");
  });

  it("a custom element's upgraded prototype answers as before", () => {
    w.customElements.define("x-c-answer", class extends w.HTMLElement {});
    const el = d.createElement("x-c-answer");
    expect(el instanceof w.HTMLElement).toBe(true);
    expect(Object.getPrototypeOf(el).constructor.name).not.toBe("HTMLElement");
  });

  it("the live-handle count counts C-made answers", () => {
    const before = dom.bridge().liveHandles();
    d.createElement("span");
    d.createElement("span");
    expect(dom.bridge().liveHandles()).toBeGreaterThanOrEqual(before + 2);
  });
});
