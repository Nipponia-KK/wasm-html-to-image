import { execFileSync } from "node:child_process";
import { readFileSync, writeFileSync } from "node:fs";
const cli = "packages/html-to-image/dist/cli.js";
const run = (...a) => execFileSync("node", [cli, ...a], { encoding: "utf8" }).trim();
writeFileSync(
  "skills/html-to-image/references/cli-options.md",
  `# CLI options\n\nGenerated from \`wasm-html-to-image --help\` (version ${JSON.parse(readFileSync("packages/html-to-image/package.json","utf8")).version}). Regenerate with \`node scripts/gen-skill-cli-options.mjs\`.\n\n\`\`\`text\n${run("--help")}\n\`\`\`\n`,
);
