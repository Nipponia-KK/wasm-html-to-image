import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createDOM, type QuickJSDOM } from "../src/index.js";

// A callback into a function of the host's is shaped in C (__native.dom.fastCall)
// when its receiver and arguments fit the plain token form: the tokens, the event
// bundle and the call itself are made there, against rpc.js's own tables, and
// callHost's interpreter way serves anything the C side declines. These tests pin
// that the two ways are indistinguishable: what the host function receives (the
// receiver, node identity, an event's fields as they stand at each dispatch) and
// what the page gets back (values, objects, a thrown error), including the shapes
// that decline (a function, an array, a plain object, a Date as an argument).

let dom: QuickJSDOM;
let w: any;
let d: any;
beforeAll(async () => {
  dom = await createDOM(
    { html: "<!doctype html><body><button id=b>ok</button><a id=l href='#'>x</a><p id=t>hi</p></body>" },
    { settleMs: 0 },
  );
  w = dom.bridge().window;
  d = w.document;
});
afterAll(() => dom.close());

describe("callbacks shaped in C", () => {
  it("a listener receives the event with its fields and identities", () => {
    const btn = d.getElementById("b");
    const link = d.getElementById("l");
    // the fields are read inside the listener, while the dispatch stands: the
    // event's target and currentTarget are the dispatch's, and are cleared after
    let seen: any = null;
    btn.addEventListener("click", (ev: any) => {
      seen = {
        self: ev, type: ev.type, clientX: ev.clientX, clientY: ev.clientY, button: ev.button,
        bubbles: ev.bubbles, target: ev.target, currentTarget: ev.currentTarget,
        relatedTarget: ev.relatedTarget, defaultPrevented: ev.defaultPrevented,
      };
    });
    const ev = new w.MouseEvent("click", { bubbles: true, cancelable: true, clientX: 10, clientY: 20, button: 2, relatedTarget: link });
    btn.dispatchEvent(ev);
    expect(seen.self).toBe(ev);
    expect(seen.type).toBe("click");
    expect(seen.clientX).toBe(10);
    expect(seen.clientY).toBe(20);
    expect(seen.button).toBe(2);
    expect(seen.bubbles).toBe(true);
    expect(seen.target).toBe(btn);
    expect(seen.currentTarget).toBe(btn);
    expect(seen.relatedTarget).toBe(link);
    expect(seen.defaultPrevented).toBe(false);
  });

  it("the same event dispatched again reports the state of that dispatch", async () => {
    const btn = d.getElementById("b");
    // a page listener ahead of the host's cancels the event
    await dom.eval("document.getElementById('b').addEventListener('click', (e) => e.preventDefault())");
    let prevented: boolean | null = null;
    btn.addEventListener("click", (ev: any) => {
      prevented = ev.defaultPrevented;
    });
    const ev = new w.MouseEvent("click", { bubbles: true, cancelable: true, clientX: 1 });
    btn.dispatchEvent(ev);
    expect(prevented).toBe(true);
    expect(ev.defaultPrevented).toBe(true);
  });

  it("mixed primitive arguments and the window receiver arrive whole", async () => {
    let receiver: unknown = null;
    let got: unknown[] = [];
    w.__mix = function (this: unknown, ...args: unknown[]) {
      receiver = this;
      got = args;
      return args.length;
    };
    const n = await dom.eval("__mix(1, 'two', true, null)");
    expect(n).toBe(4);
    expect(receiver).toBe(w);
    expect(got).toEqual([1, "two", true, null]);
  });

  it("a node and a string as arguments keep their identity and value", async () => {
    const p = d.getElementById("t");
    let gotNode: unknown = null;
    let gotStr: unknown = null;
    w.__pair = function (node: unknown, s: unknown) {
      gotNode = node;
      gotStr = s;
    };
    await dom.eval("__pair(document.getElementById('t'), 'tag')");
    expect(gotNode).toBe(p);
    expect(gotStr).toBe("tag");
  });

  it("shapes the C side declines still cross: function, array, plain object, Date", async () => {
    let fnResult: unknown = null;
    w.__withFn = function (f: any) {
      fnResult = f();
    };
    await dom.eval("__withFn(() => 7)");
    expect(fnResult).toBe(7);

    let arr: unknown = null;
    w.__withArr = function (a: unknown) {
      arr = a;
    };
    await dom.eval("__withArr([1, 'x'])");
    expect(arr).toEqual([1, "x"]);

    let obj: any = null;
    w.__withObj = function (o: unknown) {
      obj = o;
    };
    await dom.eval("__withObj({ a: 1, b: 'two' })");
    expect(obj).toEqual({ a: 1, b: "two" });

    let when: unknown = null;
    w.__withDate = function (dt: unknown) {
      when = dt;
    };
    await dom.eval("__withDate(new Date(1234567890))");
    expect(when instanceof Date).toBe(true);
    expect((when as Date).getTime()).toBe(1234567890);
  });

  it("an answer that is an object, and a host error, reach the page as before", async () => {
    w.__make = function () {
      return { a: 1 };
    };
    await expect(dom.eval("__make().a")).resolves.toBe(1);
    w.__boom = function () {
      throw new Error("host boom");
    };
    await expect(dom.eval("__boom()")).rejects.toThrow("host boom");
  });

  it("a MutationObserver callback (an array argument) still delivers its records", async () => {
    const p = d.getElementById("t");
    let records: any[] = [];
    const mo = new w.MutationObserver((rs: any[]) => {
      records = rs;
    });
    mo.observe(p, { childList: true });
    p.appendChild(d.createElement("i"));
    await new Promise((r) => setTimeout(r, 30));
    expect(records.length).toBe(1);
    expect(records[0].type).toBe("childList");
    expect(records[0].target).toBe(p);
    mo.disconnect();
  });
});
