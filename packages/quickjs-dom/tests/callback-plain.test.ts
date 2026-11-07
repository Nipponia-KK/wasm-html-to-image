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
