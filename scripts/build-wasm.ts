import { execSync } from "child_process";
import * as fs from "fs";
import * as path from "path";

// Load .env if present
const envCandidates = [
  path.resolve(process.cwd(), ".env"),
  path.resolve(import.meta.dirname, "../.env"),
];
for (const envFile of envCandidates) {
  if (
    fs.existsSync(envFile) &&
    typeof (process as unknown as { loadEnvFile?: (path?: string) => void })
      .loadEnvFile === "function"
  ) {
    (
      process as unknown as { loadEnvFile: (path?: string) => void }
    ).loadEnvFile(envFile);
    break;
  }
}

const isWin = process.platform === "win32";
const action = process.argv[2];

// Suppress verbose EMSDK setup messages
process.env.EMSDK_QUIET = "1";

// NOTE: satoru版を正とする。image-opt版との差分:
//  - EMSDK_VERSION env対応あり (image-optは latest 固定)
//  - OVERLAY_PORTS=ports あり (image-optは無し。ports/gumbo 用に必須)
//  - Git patchフォールバック / ninja解決あり
const EMSDK = process.env.EMSDK;
const EMSDK_VERSION = process.env.EMSDK_VERSION ?? "latest";
const VCPKG_ROOT = process.env.VCPKG_ROOT;

if (!EMSDK || !VCPKG_ROOT) {
  console.error(
    "Error: EMSDK and VCPKG_ROOT environment variables must be set.",
  );
  process.exit(1);
}

const emscriptenCmake = path
  .join(EMSDK, "upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")
  .replace(/\\/g, "/");
const vcpkgCmake = path
  .join(VCPKG_ROOT, "scripts/buildsystems/vcpkg.cmake")
  .replace(/\\/g, "/");
const emsdkEnv = isWin
  ? path.join(EMSDK, "emsdk_env.bat")
  : `. ${path.join(EMSDK, "emsdk_env.sh")}`;
const shell = isWin ? "cmd.exe" : "/bin/sh";

// Check if ninja is available
let useNinja = false;
let ninjaPath = "";
try {
  const result = execSync(isWin ? "where ninja" : "which ninja")
    .toString()
    .trim()
    .split(/\r?\n/)[0];
  if (result) {
    ninjaPath = result;
    execSync(`"${ninjaPath}" --version`, { stdio: "ignore" });
    useNinja = true;
  }
} catch (e) {
  // Ninja not found
}

// Fix for broken patch.exe (e.g. BusyBox shim in Scoop)
if (isWin) {
  try {
    execSync("patch --version", { stdio: "ignore" });
  } catch (e) {
    const commonPaths = [
      "C:\\Program Files\\Git\\usr\\bin\\patch.exe",
      path.join(
        process.env.USERPROFILE || "",
        "AppData\\Local\\Programs\\Git\\usr\\bin\\patch.exe",
      ),
    ];
    for (const p of commonPaths) {
      if (fs.existsSync(p)) {
        console.log(`Using Git patch: ${p}`);
        process.env.PATH = `${path.dirname(p)}${path.delimiter}${process.env.PATH}`;
        break;
      }
    }
  }
}

function run(cmd: string, cwd?: string) {
  const fullCmd = process.env.GITHUB_ACTIONS
    ? cmd
    : isWin
      ? `set EMSDK_QUIET=1 && call "${emsdkEnv}" && emsdk activate ${EMSDK_VERSION} && ${cmd}`
      : `export EMSDK_QUIET=1 && . ${path.join(EMSDK!, "emsdk_env.sh")} && emsdk activate ${EMSDK_VERSION} && ${cmd}`;

  console.log(`> ${cmd}`);
  execSync(fullCmd, { stdio: "inherit", shell, cwd });
}

const buildType = "Release";
const buildDir = "build";

if (action === "configure") {
  const force = process.argv.includes("--force");
  if (force && fs.existsSync(buildDir)) {
    try {
      fs.rmSync(buildDir, { recursive: true, force: true });
    } catch (e) {
      console.warn(
        `Warning: Could not remove ${buildDir} directory, attempting to continue.`,
      );
    }
  }
  if (!fs.existsSync(buildDir)) {
    fs.mkdirSync(buildDir);
  }

  const generator = useNinja ? "Ninja" : "Unix Makefiles";
  const projectRoot = process.cwd().replace(/\\/g, "/");

  const cmakeCmd =
    `cmake .. -G "${generator}" ` +
    `-Wno-dev ` +
    `-Wno-deprecated ` +
    `-DCMAKE_WARN_DEPRECATED=OFF ` +
    `-DCMAKE_BUILD_TYPE=${buildType} ` +
    `-DCMAKE_TOOLCHAIN_FILE="${vcpkgCmake}" ` +
    `-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="${emscriptenCmake}" ` +
    `-DVCPKG_TARGET_TRIPLET=wasm32-emscripten-wasm-eh ` +
    `-DVCPKG_OVERLAY_TRIPLETS="${projectRoot}/triplets" ` +
    `-DVCPKG_OVERLAY_PORTS="${projectRoot}/ports" ` +
    `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` +
    (useNinja && ninjaPath
      ? ` -DCMAKE_MAKE_PROGRAM="${ninjaPath.replace(/\\/g, "/")}"`
      : "");

  run(cmakeCmd, buildDir);
} else if (action === "build") {
  if (!fs.existsSync(buildDir)) {
    console.error(
      `Error: Build directory ${buildDir} does not exist. Run configure first.`,
    );
    process.exit(1);
  }

  // `--dev` drops link-time optimisation (see cmake/wasm-flags.cmake); the flag is re-applied on every
  // build so switching between dev and release only rebuilds what the flags touch.
  const dev = process.argv.includes("--dev") ? "ON" : "OFF";
  run(`cmake -DWASM_DEV_BUILD=${dev} .`, buildDir);

  if (useNinja) {
    run("ninja", buildDir);
  } else {
    run("emmake make -j16", buildDir);
  }
} else {
  console.error("Usage: tsx scripts/build-wasm.ts [configure|build] [--force]");
  process.exit(1);
}
