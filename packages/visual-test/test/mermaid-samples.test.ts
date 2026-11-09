import { describe, expect, it } from "vitest";
import { renderMermaid } from "wasm-html-to-image/single";
import { MERMAID_SAMPLES } from "../../playground/src/mermaidSamples";

// The playground offers every sample in its Mermaid mode: each one has to draw (no red error box).
describe("playground Mermaid samples", () => {
  for (const sample of MERMAID_SAMPLES) {
    it(sample.id, async () => {
      const { data } = await renderMermaid(sample.text, {
        mode: sample.mode,
        directives: sample.directives,
        theme: (sample.theme as never) ?? undefined,
      });
      expect(data).not.toContain("#cf222e"); // the error box's stroke
      expect(data).toContain("<svg");
    });
  }

  it("covers every diagram keyword the engine draws", () => {
    const text = MERMAID_SAMPLES.map((s) => s.text).join("\n");
    for (const keyword of [
      "flowchart",
      "sequenceDiagram",
      "stateDiagram-v2",
      "classDiagram",
      "erDiagram",
      "requirementDiagram",
      "block-beta",
      "architecture-beta",
      "C4Context",
      "gantt",
      "pie",
      "xychart-beta",
      "quadrantChart",
      "radar-beta",
      "sankey-beta",
      "treemap-beta",
      "venn-beta",
      "mindmap",
      "timeline",
      "journey",
      "gitGraph",
      "kanban",
      "packet-beta",
      "ishikawa-beta",
      "treeView-beta",
      "info",
    ]) {
      expect(text, keyword).toContain(keyword);
    }
  });
});
