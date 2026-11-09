# cmake/wasm-flags.cmake — wasm 共通コンパイル/リンクフラグ
#
# P3a 分割前: CMakeLists.txt の COMMON_COMPILE_OPTIONS / COMMON_LINK_OPTIONS ブロック
# (および COMMON/SINGLE 差分メモ) を等価移動。値の変更なし。
#
# Provenance:
#  - -mbulk-memory ありに統一 (image-opt の COMMON には無かった分を satoru 側に合わせる)
#  - -Oz / -msimd128 / -flto=thin は wasm バイナリサイズ最適化の要。変更時は
#    triplets/wasm32-emscripten-wasm-eh.cmake の COMMON_FLAGS との整合を確認すること。

# -mbulk-memoryありに統一 (image-optのCOMMONには無かった分をsatoru側に合わせる)
# --- 例外/longjmp の方式を全ターゲットで統一 ---
# ネイティブ wasm 例外 (-fwasm-exceptions) + wasm SjLj。リンク時の方式と食い違う
# オブジェクト (旧 JS 方式の invoke_*) が1つでも混ざるとリンクに失敗するため、
# COMMON_COMPILE_OPTIONS の対象外 (FetchContent の libaom/libavif/libjxl 等) にも
# CMAKE_C/CXX_FLAGS で行き渡らせる。vcpkg 側は triplets/emscripten-wasm-eh-toolchain.cmake。
set(WASM_EH_FLAGS "-fwasm-exceptions -sSUPPORT_LONGJMP=wasm")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${WASM_EH_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${WASM_EH_FLAGS}")

set(COMMON_COMPILE_OPTIONS
    -Oz
    -g0
    -fwasm-exceptions
    -msimd128
    -flto=thin
    -mbulk-memory
)
# STACK_SIZE: Emscripten's default is 64 KB and ASSERTIONS=0 does not detect an overflow,
# which silently corrupts adjacent memory. libaom's slower encode paths (AVIF speed 4/5)
# overflow it, so the next encode in the same module crashed ("memory access out of
# bounds"). 256 KB is enough for every AVIF/JXL speed on large images; 1 MB keeps a 4x margin.
set(COMMON_LINK_OPTIONS
    "--bind"
    "-O3"
    "-g0"
    -fwasm-exceptions
    -msimd128
    "-flto=thin"
    "-mbulk-memory"
    "-sWASM_BIGINT=1"
    "-sSUPPORT_LONGJMP=wasm"
    "-sDYNAMIC_EXECUTION=0"
    "-sASSERTIONS=0"
    "-sSTACK_SIZE=1048576"
    "-sMODULARIZE=1"
    "-sEXPORT_ES6=1"
    "-sEXPORT_NAME='${WASM_EXPORT_NAME}'"
    "-sENVIRONMENT=web,worker"
    "-sFILESYSTEM=0"
    # Some Skia symbols (SkSL, RuntimeEffect) are undefined because we exclude their sources
    # to keep the binary size small. They are not used in our current rendering path.
    "-sERROR_ON_UNDEFINED_SYMBOLS=0"
    "-sWARN_ON_UNDEFINED_SYMBOLS=0"
    "-sALLOW_MEMORY_GROWTH=1"
)

# Development build (`pnpm wasm:build --dev`): no ThinLTO, so a one-line change relinks in
# seconds instead of minutes. The binary is larger and slower; release builds never set this.
option(WASM_DEV_BUILD "Skip link-time optimisation for fast development builds" OFF)
if(WASM_DEV_BUILD)
    list(REMOVE_ITEM COMMON_COMPILE_OPTIONS "-flto=thin")
    list(REMOVE_ITEM COMMON_LINK_OPTIONS "-flto=thin")
endif()
