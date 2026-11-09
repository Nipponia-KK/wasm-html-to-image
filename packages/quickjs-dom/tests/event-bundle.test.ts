import { describe, expect, it } from "vitest";
import { JSDOM as RealJSDOM } from "jsdom";
import { JSDOM } from "../src/jsdom.js";

// A host listener gets its event argument with the event's properties attached (one bundle per call). The properties
// are read from it on demand; these tests pin that what a listener reads is what the dispatch is at that moment, also
// for an event that was read before and is dispatched again.

const HTML = "<!doctype html><body><div id=a><button id=b>x</button></div></body>";

function trace(make: (html: string) => any): unknown[] {
  const dom = make(HTML);
  const w = dom.window, d = w.document;
  const out: unknown[] = [];
  const b = d.getElementById("b"), a = d.getElementById("a");
  const ev = new w.Event("ping", { bubbles: true, cancelable: true });
  out.push([ev.eventPhase, ev.defaultPrevented, ev.target, ev.currentTarget]); // read before any dispatch
  const seen = (tag: string) => (e: any) => {
    out.push([tag, e.eventPhase, e.defaultPrevented, e.target === b, e.currentTarget === (tag === "b" ? b : a), e.type, e.bubbles]);
    if (tag === "a") e.preventDefault();
  };
  b.addEventListener("ping", seen("b"));
  a.addEventListener("ping", seen("a"));
  out.push(b.dispatchEvent(ev));
  out.push([ev.eventPhase, ev.defaultPrevented, ev.target === b, ev.currentTarget]);
  const ev2 = new w.Event("ping", { bubbles: true, cancelable: true });
  out.push(a.dispatchEvent(ev2)); // a different target: the same listener, another event
  out.push([ev2.defaultPrevented, ev2.target === a]);
  return out;
}

describe("event properties read in a host listener", () => {
  it("follow the dispatch as jsdom's do, also for an event read before it was dispatched", () => {
    const ours = trace((html) => new JSDOM(html));
    const ref = trace((html) => new RealJSDOM(html));
    expect(ours).toEqual(ref);
  });

  it("are current when one event object visits several listeners", () => {
    const dom = new JSDOM(HTML);
    const w = dom.window, d = w.document;
    const b = d.getElementById("b");
    const phases: number[] = [];
    const ev = new w.Event("ping", { bubbles: true });
    b.addEventListener("ping", (e: any) => phases.push(e.eventPhase));
    d.body.addEventListener("ping", (e: any) => phases.push(e.eventPhase));
    d.addEventListener("ping", (e: any) => phases.push(e.eventPhase), true);
    b.dispatchEvent(ev);
    b.dispatchEvent(ev);
    expect(phases).toEqual([1, 2, 3, 1, 2, 3]);
  });
});
