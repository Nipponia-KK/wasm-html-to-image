import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createDOM, type QuickJSDOM } from "../src/index.js";

// The bridge drops a setAttribute that repeats the value the page already has (host-written
// or read back). These tests pin the invalidation rules that keep the memo honest: any other
// write landing on the element, a removal, a page-side change, or a case variant of the name
// must make the next setAttribute cross again.

let dom: QuickJSDOM;
let w: any;
beforeAll(async () => {
  dom = await createDOM({ html: "<!doctype html><body><div id=r data-y='7'></div></body>" }, { settleMs: 0 });
  w = dom.bridge().window;
});
afterAll(() => dom.close());

describe("setAttribute memo", () => {
  it("repeats and changed values land correctly", () => {
    const el = w.document.getElementById("r");
    el.setAttribute("data-x", "1");
    el.setAttribute("data-x", "1");
    expect(el.getAttribute("data-x")).toBe("1");
    el.setAttribute("data-x", "2");
    expect(el.getAttribute("data-x")).toBe("2");
    el.setAttribute("data-x", "2");
    expect(el.getAttribute("data-x")).toBe("2");
  });

  it("a property write in between defeats the memo", () => {
    const el = w.document.getElementById("r");
    el.setAttribute("class", "a");
    el.className = "b";
    // if the memo for class=a survived the className write, this repeat would be
    // dropped and the class would stay "b"
    el.setAttribute("class", "a");
    expect(el.className).toBe("a");
    expect(el.getAttribute("class")).toBe("a");
  });

  it("a removal in between defeats the memo", () => {
    const el = w.document.getElementById("r");
    el.setAttribute("data-z", "9");
    el.removeAttribute("data-z");
    el.setAttribute("data-z", "9");
    expect(el.getAttribute("data-z")).toBe("9");
  });

  it("a page-side change defeats the memo", async () => {
    const el = w.document.getElementById("r");
    el.setAttribute("data-p", "1");
    await dom.eval("document.getElementById('r').setAttribute('data-p', '2')");
    el.setAttribute("data-p", "1");
    expect(el.getAttribute("data-p")).toBe("1");
  });

  it("case variants of the name cannot leave a stale entry", () => {
    const el = w.document.getElementById("r");
    el.setAttribute("DATA-Q", "1");
    el.setAttribute("data-q", "2");
    // the page's attribute is data-q=2 here; a memo that conflated the names would
    // drop this write as a repeat of "1"
    el.setAttribute("DATA-Q", "1");
    expect(el.getAttribute("data-q")).toBe("1");
  });

  it("a value read back seeds the memo without breaking later writes", () => {
    const el = w.document.getElementById("r");
    expect(el.getAttribute("data-y")).toBe("7");
    el.setAttribute("data-y", "7");
    expect(el.getAttribute("data-y")).toBe("7");
    el.setAttribute("data-y", "8");
    expect(el.getAttribute("data-y")).toBe("8");
  });

  it("inside a transaction the queued writes still land in order", () => {
    const el = w.document.getElementById("r");
    dom.transact(() => {
      el.setAttribute("data-t", "1");
      el.setAttribute("data-t", "1");
      el.setAttribute("data-t", "2");
    });
    expect(el.getAttribute("data-t")).toBe("2");
    el.setAttribute("data-t", "2");
    expect(el.getAttribute("data-t")).toBe("2");
  });
});
