import { describe, expect, it } from "vitest";
import { PRESETS } from "../presets/index.ts";
import { defaultSettings, settingsFromParams, settingsToParams, type AppSettings } from "./urlState.ts";

const from = (query: string) => settingsFromParams(new URLSearchParams(query));

describe("urlState", () => {
  it("an empty query is the defaults, and the defaults make an empty query", () => {
    const s = from("");
    expect(s).toEqual(defaultSettings());
    expect(s.bg.type).toBe("light");
    expect(settingsToParams(s).toString()).toBe("");
  });

  it("round-trips every setting", () => {
    const s: AppSettings = {
      preset: PRESETS[4],
      code: "stateDiagram-v2\n  [*] --> A",
      theme: "forest",
      view: "slider",
      scale: "original",
      bg: { type: "custom", customColor: "#112233" },
      options: { theme: "forest", mode: "recover", directives: false, padding: 32, format: "png" },
    };
    expect(settingsFromParams(settingsToParams(s))).toEqual(s);
  });

  it("leaves out what equals the default, so a link stays short", () => {
    const s = { ...defaultSettings(PRESETS[4]), theme: "dark" as const };
    s.options = { ...s.options, theme: "dark" };
    expect(settingsToParams(s).toString()).toBe(`preset=${PRESETS[4].id}&theme=dark`);
  });

  it("takes the code from the preset unless the link carries its own", () => {
    expect(from(`preset=${PRESETS[2].id}`).code).toBe(PRESETS[2].code);
    expect(from(`preset=${PRESETS[2].id}&code=graph%20TD%0AA-->B`).code).toBe("graph TD\nA-->B");
  });

  it("ignores values it does not understand", () => {
    const s = from("preset=nope&theme=neon&view=3d&scale=huge&bg=red&bgColor=blue&mode=wild&format=gif&padding=-4&directives=maybe");
    expect(s).toEqual(defaultSettings());
    expect(from("padding=12.5").options.padding).toBe(16);
    expect(from("padding=900").options.padding).toBe(16);
    expect(from("padding=0").options.padding).toBe(0);
  });
});
