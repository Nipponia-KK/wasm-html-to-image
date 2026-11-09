import { describe, expect, it } from "vitest";
import { decodeText, encodeText } from "./shareText";

describe("shareText", () => {
  it("round-trips a diagram, Japanese and line breaks included, through a URL-safe string", async () => {
    const text = "flowchart LR\n  A[開始] --> B{判定}\n  B -->|はい| C[完了]\n";
    const encoded = await encodeText(text);
    expect(encoded).toBeDefined();
    expect(encoded).toMatch(/^[A-Za-z0-9_-]+$/); // nothing that needs escaping in a URL
    expect(await decodeText(encoded!)).toBe(text);
  });

  it("is shorter than the text for a repetitive diagram", async () => {
    const text = "graph TD\n" + Array.from({ length: 60 }, (_, k) => `  n${k} --> n${k + 1}`).join("\n");
    expect((await encodeText(text))!.length).toBeLessThan(text.length / 2);
  });

  it("gives undefined for a damaged link instead of throwing", async () => {
    expect(await decodeText("not a link!!")).toBeUndefined();
    expect(await decodeText("AAAA")).toBeUndefined();
  });
});
