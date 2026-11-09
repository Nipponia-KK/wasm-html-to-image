---
sidebar_position: 4
title: Development Workflow
---

# Development Workflow

Guide to repository setup, build scripts, test suites, and documentation previewing.

## 1. Setup

```bash
pnpm install
```

## 2. Building

### TypeScript Packages

```bash
# Build html-to-image package
pnpm --filter wasm-html-to-image build

# Build all monorepo packages
pnpm build:js
```

### WASM Native Module (C++)

Requires CMake and Emscripten SDK:

```bash
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$EMSDK/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake
cmake --build build --target html-to-image
```

#### Building with the pnpm scripts

Usually you use these scripts. The output is `packages/html-to-image/dist/html-to-image.wasm`.

```bash
pnpm wasm:build       # release build (with ThinLTO; this is what you ship)
pnpm wasm:build:dev   # development build (no ThinLTO)
```

The development build leaves out only link-time optimisation (ThinLTO), so relinking after a one-line C++ change takes seconds instead of minutes. The optimisation level stays at `-O3` (lowering it, for example to `-O1`, breaks the embind glue). The binary is larger and a little slower. Both builds write to the same place, so **always rebuild with `pnpm wasm:build` before you publish**. The flags are set again on every run, so switching between the two does not rebuild more than it has to.

After rebuilding the WASM, rebuild the package that embeds it. If you do not, the JS and the WASM no longer match: the browser fails at run time, or a new option has no effect.

```bash
pnpm wasm:build:dev
pnpm --filter wasm-html-to-image build   # embed the WASM in the package again
```

#### Native (host) tests

The Mermaid engine (`src/cpp/mermaid`) is written with the standard library only, so the unit tests and the compatibility corpus run with the host compiler, without building the WASM.

```bash
cmake --build build-host-tests --config Debug --target test_mermaid mermaid_cli
./build-host-tests/Debug/test_mermaid.exe          # unit tests
python tests/mermaid-corpus/run.py                 # 110 examples taken from the official documentation (exit status 1 on a new failure)
```

`mermaid_cli` reads a diagram from standard input and writes the SVG. `tests/mermaid-corpus/known-gaps.json` lists the notations that are not supported yet. When you fix one, remove it from the list too (a fixed entry left in the list fails the run).

#### About timings

Times from the development build are longer than from the release build. When you record a timing (for example, how long a large flowchart takes to lay out), say which build you measured with.

---

## 3. Testing

### Unit Tests (Vitest)

```bash
pnpm --filter wasm-html-to-image test
```

### Visual Regression Tests

```bash
pnpm --filter visual-test test
```

---

## 4. Documentation Site

```bash
# Start local development server with live reload
pnpm docs:dev

# Build static documentation files
pnpm docs:build

# Preview built production site
pnpm --filter wasm-html-to-image-docs serve
```
