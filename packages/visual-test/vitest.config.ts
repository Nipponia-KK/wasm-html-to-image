import { defineConfig } from "vitest/config";

export default defineConfig({
  test: {
    // Visual rendering tests load and compile the WASM module and perform complex layout/drawing.
    // Under CI runner load, initial WASM setup and heavy tests take longer than the default 5s.
    testTimeout: 60_000,
  },
});
