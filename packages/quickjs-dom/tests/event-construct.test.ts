import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createDOM, type QuickJSDOM } from "../src/index.js";

// Constructing an event class whose arguments fit the compact token form (a type
// string and one flat init dictionary) crosses in that form instead of the JSON
// ["new", ...] operation. These tests pin that what arrives is the same event
// either way: every init member, the defaults, a node carried as a value, and
// the shapes that fall back (a nested dictionary, a separator in a string).

let dom: QuickJSDOM;
let w: any;
beforeAll(async () => {
  dom = await createDOM({ html: "<!doctype html><body><button id=b>x</button></body>" }, { settleMs: 0 });
  w = dom.bridge().window;
});
afterAll(() => dom.close());

describe("event construction", () => {
  it("a MouseEvent with a full init dictionary reads back every member", () => {
    const btn = w.document.getElementById("b");
    const ev = new w.MouseEvent("click", {
      bubbles: true,
      cancelable: true,
      composed: true,
      button: 2,
      buttons: 5,
      clientX: 10,
      clientY: 20,
      screenX: 30,
      screenY: 40,
      ctrlKey: true,
      shiftKey: false,
      relatedTarget: btn,
    });
    expect(ev.type).toBe("click");
    expect(ev.bubbles).toBe(true);
    expect(ev.cancelable).toBe(true);
    expect(ev.composed).toBe(true);
    expect(ev.button).toBe(2);
    expect(ev.buttons).toBe(5);
    expect(ev.clientX).toBe(10);
    expect(ev.clientY).toBe(20);
    expect(ev.screenX).toBe(30);
    expect(ev.screenY).toBe(40);
    expect(ev.ctrlKey).toBe(true);
    expect(ev.shiftKey).toBe(false);
    expect(ev.altKey).toBe(false); // a member the dictionary did not carry keeps its default
    expect(ev.relatedTarget).toBe(btn);
    expect(ev instanceof w.MouseEvent).toBe(true);
    expect(ev instanceof w.Event).toBe(true);
  });

  it("defaults hold when the dictionary is absent or empty", () => {
    const bare = new w.MouseEvent("click");
    expect(bare.bubbles).toBe(false);
    expect(bare.button).toBe(0);
    expect(bare.clientX).toBe(0);
    const empty = new w.KeyboardEvent("keydown", {});
    expect(empty.key).toBe("");
    const keyed = new w.KeyboardEvent("keydown", { key: "Enter", code: "Enter", ctrlKey: true });
    expect(keyed.key).toBe("Enter");
    expect(keyed.code).toBe("Enter");
    expect(keyed.ctrlKey).toBe(true);
  });

  it("a nested dictionary falls back and still arrives whole", () => {
    const ev = new w.CustomEvent("x", { detail: { a: 1, b: "two" } });
    expect(ev.detail).toEqual({ a: 1, b: "two" });
  });

  it("a string carrying a separator falls back and still arrives whole", () => {
    const ev = new w.CustomEvent("x", { detail: "a\u0003b" });
    expect(ev.detail).toBe("a\u0003b");
  });

  it("a constructed event dispatches with its members intact", () => {
    const btn = w.document.getElementById("b");
    let seen: any = null;
    btn.addEventListener("click", (ev: any) => {
      seen = ev;
    });
    const ev = new w.MouseEvent("click", { bubbles: true, cancelable: true, clientX: 7 });
    btn.dispatchEvent(ev);
    expect(seen).toBe(ev);
    expect(seen.clientX).toBe(7);
    expect(seen.target).toBe(btn);
  });

  it("constructing without the required argument throws as before", () => {
    expect(() => new w.Event()).toThrow();
  });
});
