import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createDOM, type QuickJSDOM } from "../src/index.js";

// A callback the page calls without a receiver — a collection's forEach with no
// thisArg, a timer — runs with the window as its `this` in the page. That used to
// reject the compact token path in the guest (the global object has no registered
// prototype), so every such callback crossed in the JSON form instead. These tests
// pin the semantics the token path has to preserve for it: the receiver the host
// function sees, the arguments, and the answer it returns.

let dom: QuickJSDOM;
let w: any;
beforeAll(async () => {
  dom = await createDOM({ html: "<!doctype html><body><p>a</p><p>b</p><p>c</p></body>" }, { settleMs: 0 });
  w = dom.bridge().window;
});
afterAll(() => dom.close());

describe("host callbacks with the window as receiver", () => {
  it("NodeList.forEach: this is the window, args are (node, index, list)", () => {
    const list = w.document.querySelectorAll("p");
    const seen: unknown[][] = [];
    let receiver: unknown = null;
    list.forEach(function (this: unknown, node: any, index: number, lst: unknown) {
      receiver = this;
      seen.push([node.textContent, index, lst === list]);
    });
    expect(receiver).toBe(w);
    expect(seen).toEqual([
      ["a", 0, true],
      ["b", 1, true],
      ["c", 2, true],
    ]);
  });

  it("a host function called bare from page code gets the window and answers back", async () => {
    let receiver: unknown = null;
    w.__bare = function (this: unknown, x: number) {
      receiver = this;
      return x * 2;
    };
    const answer = await dom.eval("__bare(21)");
    expect(receiver).toBe(w);
    expect(answer).toBe(42);
  });

  it("the window as a plain argument decodes to the window", async () => {
    let got: unknown = null;
    w.__take = function (win: unknown) {
      got = win;
    };
    await dom.eval("__take(window)");
    expect(got).toBe(w);
  });

  it("an object the host already knows crosses by its handle", async () => {
    // the first <p> has crossed (the forEach above), so it has a handle on record:
    // a page-side call passing it as an argument must arrive as that same wrapper
    const first = w.document.querySelector("p");
    let got: unknown = null;
    w.__takeNode = function (n: unknown) {
      got = n;
    };
    await dom.eval("__takeNode(document.querySelector('p'))");
    expect(got).toBe(first);
  });
});

// A callback carries the page's mutation version, and the bridge drops its cached
// reads in full only when that version has moved. These tests pin the freshness
// that has to survive the lighter drop: a tree change by an earlier page listener
// (the version moves), and a form value written by page code (the page keeps it
// outside the tree, so the version does not move and the volatile drop covers it).
describe("callback invalidation follows the page's mutation version", () => {
  let dom2: QuickJSDOM;
  let w2: any;
  beforeAll(async () => {
    dom2 = await createDOM(
      { html: "<!doctype html><body><div id=box><span id=s>old</span></div><input id=i value=old><button id=b>go</button></body>" },
      { settleMs: 0 },
    );
    w2 = dom2.bridge().window;
  });
  afterAll(() => dom2.close());

  it("a tree change by an earlier page listener is read fresh by a host listener", async () => {
    const d = w2.document;
    const span = d.getElementById("s");
    expect(span.textContent).toBe("old"); // fill the host's read cache before the dispatch
    await dom2.eval(
      "document.getElementById('b').addEventListener('click', () => { document.getElementById('s').textContent = 'new'; })",
    );
    let seen: unknown = null;
    d.getElementById("b").addEventListener("click", () => {
      seen = span.textContent;
    });
    d.getElementById("b").dispatchEvent(new w2.MouseEvent("click"));
    expect(seen).toBe("new");
  });

  it("a form value written by page code is read fresh even when the tree did not change", async () => {
    const d = w2.document;
    const input = d.getElementById("i");
    expect(input.value).toBe("old"); // fill the volatile read cache before the dispatch
    await dom2.eval(
      "document.getElementById('b').addEventListener('change', () => { document.getElementById('i').value = 'typed'; })",
    );
    let seen: unknown = null;
    d.getElementById("b").addEventListener("change", () => {
      seen = input.value;
    });
    d.getElementById("b").dispatchEvent(new w2.Event("change"));
    expect(seen).toBe("typed");
  });

  it("a handler replaced by page code is not skipped by the host's set memo", async () => {
    const d = w2.document;
    const btn = d.getElementById("b");
    const first = () => {};
    btn.onclick = first; // the host's set, remembered by the bridge
    await dom2.eval(
      "document.getElementById('b').addEventListener('swap', () => { document.getElementById('b').onclick = () => 'page'; })",
    );
    let hits = 0;
    btn.addEventListener("swap", () => {
      hits++;
    });
    btn.dispatchEvent(new w2.Event("swap")); // the page listener replaces onclick, then the host callback runs
    expect(hits).toBe(1);
    btn.onclick = first; // the same host set again: it must land, not be memoized away
    expect(btn.onclick).toBe(first);
  });
});
