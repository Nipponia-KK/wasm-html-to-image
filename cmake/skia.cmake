# cmake/skia.cmake — Skia FetchContent + skia_lib ソース列挙/ターゲット定義
#
# P3a 分割前: CMakeLists.txt の「Skia (FetchContent)」+「Target: skia_lib」ブロックを等価移動。
# glob パターン・除外フィルタ・定義・インクルードの変更なし。
# ソースファイルの追加削除なし (P2b 物理分割は別途。新規 cpp 登録はしない)。
#
# FetchContent バージョンは cmake/codecs.cmake 冒頭の固定表を正とする (skia: コミット固定 / アーカイブ取得)。
#
# 前提:
#  - include(FetchContent) 済み、LIBS_DIR / SKIA_CONFIG_DIR 定義済み (トップ)
#  - cmake/wasm-flags.cmake 適用済み (COMMON_COMPILE_OPTIONS)
#  - cmake/codecs.cmake 適用済み (WUFFS_RELEASE_DIR)
#  - cmake/fonts.cmake 適用済み (HTML_TO_IMAGE_LIBS)
# 提供: skia_lib ターゲット

# --- Skia (FetchContent: コミット固定) ---
# main 追従だと同一ソースから別結果になり得るため、検証済みコミットに固定する。
# GIT_SHALLOW + コミットハッシュは shallow clone に対象コミットが含まれず失敗するため、
# 履歴を持たない GitHub アーカイブ (URL) で取得する。更新時は SKIA_COMMIT と
# cmake/codecs.cmake の固定表を同時に更新すること。
set(SKIA_COMMIT baf1231c43cc9c1613702698ea64b33ba5d9c9aa)
FetchContent_Declare(
  skia
  URL https://github.com/google/skia/archive/${SKIA_COMMIT}.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(skia)

# SkLumaColorFilter::Make() (used by SVG <mask>) builds an SkSL runtime effect, which is
# stubbed out in this SkSL-less build and aborts the instance. Redirect it to a raster-pipeline
# implementation (src/cpp/utils/skia_luma.cpp). Idempotent: only patches an unpatched checkout.
set(_skia_rcf "${skia_SOURCE_DIR}/src/effects/colorfilters/SkRuntimeColorFilter.cpp")
file(READ "${_skia_rcf}" _skia_rcf_txt)
if(NOT _skia_rcf_txt MATCHES "satoru_make_luma_filter")
  string(REPLACE
    "    const SkRuntimeEffect* lumaEffect = GetKnownRuntimeEffect(StableKey::kLuma);

    return lumaEffect->makeColorFilter(SkData::MakeEmpty());"
    "    sk_sp<SkColorFilter> satoru_make_luma_filter();
    return satoru_make_luma_filter();"
    _skia_rcf_patched "${_skia_rcf_txt}")
  if(_skia_rcf_patched STREQUAL _skia_rcf_txt)
    message(FATAL_ERROR "skia.cmake: SkLumaColorFilter::Make() patch did not apply (Skia source changed?)")
  endif()
  file(WRITE "${_skia_rcf}" "${_skia_rcf_patched}")
endif()

# --- Target: skia_lib ---
# satoru正をベースに統合:
#  - FreeType ports / skshaper+harfbuzz+skunicode: WASM_ENABLE_TEXT_SHAPING=ON既定で常時 (image-optは無し)
#  - src/pdf+pathops: satoru側あり/WASM_ENABLE_PDFで制御 (image-optはpdf無し・pathops有り)
#  - src/sksl+SkRuntimeBlender.cpp: WASM_ENABLE_SKSL=OFF既定で除外 (image-optのみ有り)
#  - SkDocument_PDF_None除外フィルタはsatoru正を維持
add_library(skia_lib STATIC)
file(GLOB SKIA_CORE_SRC
    "${skia_SOURCE_DIR}/src/core/*.cpp"
    "${skia_SOURCE_DIR}/src/base/*.cpp"
    "${skia_SOURCE_DIR}/src/utils/*.cpp"
    "${skia_SOURCE_DIR}/src/ports/SkOSFile_posix.cpp"
    "${skia_SOURCE_DIR}/src/ports/SkOSFile_stdio.cpp"
    "${skia_SOURCE_DIR}/src/ports/SkTime_Unix.cpp"
    "${skia_SOURCE_DIR}/src/ports/SkMemory_malloc.cpp"
    "${skia_SOURCE_DIR}/src/ports/SkDiscardableMemory_none.cpp"
    "${skia_SOURCE_DIR}/src/ports/SkFontMgr_custom.cpp"
    "${skia_SOURCE_DIR}/src/ports/SkFontMgr_custom_empty.cpp"
    "${skia_SOURCE_DIR}/src/ports/SkLog_stdio.cpp"
    "${skia_SOURCE_DIR}/src/ports/SkGlobalInitialization_default.cpp"
    "${skia_SOURCE_DIR}/src/image/*.cpp"
    "${skia_SOURCE_DIR}/src/shaders/*.cpp"
    "${skia_SOURCE_DIR}/src/shaders/gradients/*.cpp"
    "${skia_SOURCE_DIR}/src/effects/*.cpp"
    "${skia_SOURCE_DIR}/src/effects/imagefilters/*.cpp"
    "${skia_SOURCE_DIR}/src/effects/colorfilters/*.cpp"
    "${skia_SOURCE_DIR}/src/svg/*.cpp"
    "${skia_SOURCE_DIR}/src/xml/*.cpp"
    "${skia_SOURCE_DIR}/src/text/*.cpp"
    "${skia_SOURCE_DIR}/modules/skcms/skcms.cc"
    "${skia_SOURCE_DIR}/modules/skcms/src/skcms_TransformBaseline.cc"
    "${skia_SOURCE_DIR}/src/encode/SkEncoder.cpp"
    "${skia_SOURCE_DIR}/src/encode/SkPngEncoderImpl.cpp"
    "${skia_SOURCE_DIR}/src/encode/SkPngEncoderBase.cpp"
    "${skia_SOURCE_DIR}/src/encode/SkJpegEncoderImpl.cpp"
    "${skia_SOURCE_DIR}/src/encode/SkJPEGWriteUtility.cpp"
    "${skia_SOURCE_DIR}/src/encode/SkWebpEncoderImpl.cpp"
    "${skia_SOURCE_DIR}/src/encode/SkICC.cpp"
    "${skia_SOURCE_DIR}/src/codec/SkCodec.cpp"
    "${skia_SOURCE_DIR}/src/codec/SkCodecColorProfile.cpp"
    "${skia_SOURCE_DIR}/src/codec/SkCodecImageGenerator.cpp"
    "${skia_SOURCE_DIR}/src/codec/SkImageGenerator_FromEncoded.cpp"
    "${skia_SOURCE_DIR}/src/codec/SkEncodedInfo.cpp"
    "${skia_SOURCE_DIR}/src/codec/SkPngCodec.cpp"
    "${skia_SOURCE_DIR}/src/codec/SkPngCodecBase.cpp"
    "${skia_SOURCE_DIR}/src/codec/SkPngCompositeChunkReader.cpp"
    "${skia_SOURCE_DIR}/src/codec/SkExif.cpp"
    "${skia_SOURCE_DIR}/src/codec/SkTiffUtility.cpp"
    "${skia_SOURCE_DIR}/src/codec/SkGainmapInfo.cpp"
    "${skia_SOURCE_DIR}/src/utils/SkOTUtils.cpp"
    "${skia_SOURCE_DIR}/src/codec/SkBmpBaseCodec.cpp"
    "${skia_SOURCE_DIR}/src/codec/SkBmpCodec.cpp"
    "${skia_SOURCE_DIR}/src/codec/SkBmpMaskCodec.cpp"
    "${skia_SOURCE_DIR}/src/codec/SkBmpRLECodec.cpp"
    "${skia_SOURCE_DIR}/src/codec/SkBmpStandardCodec.cpp"
    "${skia_SOURCE_DIR}/src/codec/SkIcoCodec.cpp"
    "${skia_SOURCE_DIR}/src/codec/SkParseEncodedOrigin.cpp"
    "${skia_SOURCE_DIR}/src/codec/SkMaskSwizzler.cpp"
    "${skia_SOURCE_DIR}/src/codec/SkSwizzler.cpp"
    "${skia_SOURCE_DIR}/src/codec/SkSampler.cpp"
    "${skia_SOURCE_DIR}/src/codec/SkColorPalette.cpp"
    "${skia_SOURCE_DIR}/src/codec/SkColorTable.cpp"
    "${skia_SOURCE_DIR}/src/codec/SkPixmapUtils.cpp"
    "${skia_SOURCE_DIR}/src/codec/SkHdrMetadata.cpp"
    "${skia_SOURCE_DIR}/src/codec/SkWuffsCodec.cpp"
    "src/cpp/libs/skia/wuffs_implementation.cpp"
    "${skia_SOURCE_DIR}/src/codec/SkJpegCodec.cpp"
    "${skia_SOURCE_DIR}/src/codec/SkJpegDecoderMgr.cpp"
    "${skia_SOURCE_DIR}/src/codec/SkJpegMetadataDecoderImpl.cpp"
    "${skia_SOURCE_DIR}/src/codec/SkJpegMultiPicture.cpp"
    "${skia_SOURCE_DIR}/src/codec/SkJpegSegmentScan.cpp"
    "${skia_SOURCE_DIR}/src/codec/SkJpegSourceMgr.cpp"
    "${skia_SOURCE_DIR}/src/codec/SkJpegUtility.cpp"
    "${skia_SOURCE_DIR}/src/codec/SkJpegXmp.cpp"
    "${skia_SOURCE_DIR}/src/codec/SkWebpCodec.cpp"
    "${skia_SOURCE_DIR}/src/codec/SkAvifCodec.cpp"
    "${skia_SOURCE_DIR}/modules/svg/src/*.cpp"
    "${skia_SOURCE_DIR}/modules/skresources/src/*.cpp"
)
if(WASM_ENABLE_TEXT_SHAPING)
    file(GLOB SKIA_SHAPER_SRC
        "${skia_SOURCE_DIR}/src/ports/SkFontHost_FreeType.cpp"
        "${skia_SOURCE_DIR}/src/ports/SkFontHost_FreeType_common.cpp"
        "${skia_SOURCE_DIR}/modules/skshaper/src/SkShaper.cpp"
        "${skia_SOURCE_DIR}/modules/skshaper/src/SkShaper_primitive.cpp"
        "${skia_SOURCE_DIR}/modules/skshaper/src/SkShaper_harfbuzz.cpp"
        "${skia_SOURCE_DIR}/modules/skshaper/src/SkShaper_skunicode.cpp"
        "${skia_SOURCE_DIR}/modules/skshaper/src/SkShaper_factory.cpp"
        "${skia_SOURCE_DIR}/modules/skunicode/src/SkUnicode.cpp"
        "${skia_SOURCE_DIR}/modules/skunicode/src/SkUnicode_hardcoded.cpp"
    )
    list(APPEND SKIA_CORE_SRC ${SKIA_SHAPER_SRC})
endif()
if(WASM_ENABLE_PDF)
    file(GLOB SKIA_PDF_SRC
        "${skia_SOURCE_DIR}/src/pdf/*.cpp"
        "${skia_SOURCE_DIR}/src/pathops/*.cpp"
    )
    list(APPEND SKIA_CORE_SRC ${SKIA_PDF_SRC})
else()
    # PDF無効時もpathopsは残す (image-opt構成との互換)
    file(GLOB SKIA_PATHOPS_SRC "${skia_SOURCE_DIR}/src/pathops/*.cpp")
    list(APPEND SKIA_CORE_SRC ${SKIA_PATHOPS_SRC})
endif()
if(WASM_ENABLE_SKSL)
    # image-opt派生 (既定OFF)。ON時のみSkSL系を取り込む
    file(GLOB SKIA_SKSL_SRC
        "${skia_SOURCE_DIR}/src/core/SkRuntimeBlender.cpp"
        "${skia_SOURCE_DIR}/src/sksl/*.cpp"
        "${skia_SOURCE_DIR}/src/sksl/analysis/*.cpp"
        "${skia_SOURCE_DIR}/src/sksl/codegen/SkSLRasterPipelineBuilder.cpp"
        "${skia_SOURCE_DIR}/src/sksl/codegen/SkSLRasterPipelineCodeGenerator.cpp"
        "${skia_SOURCE_DIR}/src/sksl/ir/*.cpp"
        "${skia_SOURCE_DIR}/src/sksl/transform/*.cpp"
        "${skia_SOURCE_DIR}/src/sksl/tracing/*.cpp"
    )
    list(APPEND SKIA_CORE_SRC ${SKIA_SKSL_SRC})
endif()
list(FILTER SKIA_CORE_SRC EXCLUDE REGEX ".*_win.cpp|.*_mac.cpp|.*_linux.cpp|.*_android.cpp|.*_ios.cpp")
# NOTE: SkSemaphoreは除外しない (converter_encode等の実働パスが参照し、
# 欠落するとwasm実行時にAborted(missing function)で落ちる。POSIX sem実装のため
# emscripten下でも追加依存なしでリンク可)。SkThreadは引き続き除外。
list(FILTER SKIA_CORE_SRC EXCLUDE REGEX ".*SkGetExecutablePath.*|.*SkThread.*|.*SkRuntimeEffect.*|.*SkMesh.*|.*SkRuntimeBlender.*|.*SkSL.*|.*SkGPU.*|.*SkGpu.*|.*SkGraphite.*|.*SkDocument_PDF_None.cpp|.*src/gpu/.*|.*src/graphite/.*")
if(WASM_ENABLE_SKSL)
    # SKSL有効時はRuntimeBlender/SkSL除外フィルタを外す (上記で追加した分を復活)
    list(FILTER SKIA_CORE_SRC EXCLUDE REGEX ".*_win.cpp|.*_mac.cpp|.*_linux.cpp|.*_android.cpp|.*_ios.cpp")
endif()

if(WASM_PDF_ONLY)
    list(FILTER SKIA_CORE_SRC EXCLUDE REGEX ".*/src/codec/Sk(Jpeg|Webp|Avif|Wuffs|Bmp|Ico)[A-Za-z]*\\.cpp|.*wuffs_implementation\\.cpp|.*/src/encode/Sk(Jpeg|JPEG|Webp)[A-Za-z]*\\.cpp")
endif()
target_sources(skia_lib PRIVATE ${SKIA_CORE_SRC})
target_include_directories(skia_lib PUBLIC
    "${SKIA_CONFIG_DIR}"
    "${skia_SOURCE_DIR}"
    "${WUFFS_RELEASE_DIR}"
    "${skia_SOURCE_DIR}/modules/svg/include"
    "${skia_SOURCE_DIR}/modules/skresources/include"
    "${skia_SOURCE_DIR}/modules/skshaper/include"
    "${skia_SOURCE_DIR}/modules/skunicode/include"
)
set(SKIA_UNIFIED_DEFS SK_USER_CONFIG_HEADER="SkUserConfig.h" SK_ENABLE_SVG SK_DISABLE_FILESYSTEM SKCMS_PORTABLE)
if(WASM_ENABLE_TEXT_SHAPING)
    list(APPEND SKIA_UNIFIED_DEFS SK_SHAPER_HARFBUZZ_AVAILABLE SK_SHAPER_UNICODE_AVAILABLE)
endif()
if(WASM_PDF_ONLY)
    list(APPEND SKIA_UNIFIED_DEFS HTML_TO_IMAGE_PDF_ONLY)
endif()
target_compile_definitions(skia_lib PUBLIC ${SKIA_UNIFIED_DEFS})
target_compile_options(skia_lib PRIVATE ${COMMON_COMPILE_OPTIONS} -w)
# SSE2/SSSE3/SSE4.1: Skia picks its optimized blitters / raster pipeline / memset from the compile-time
# CPU level. Emscripten maps those intrinsics onto wasm SIMD128 (already enabled by -msimd128);
# without them Skia falls back to the scalar "portable" code (store_8888/load_8888/blit_row ...).
# skcms probes AVX headers once it sees __SSE__ and breaks on Emscripten, so it stays scalar.
foreach(_skia_src IN LISTS SKIA_CORE_SRC)
  if(NOT _skia_src MATCHES "/modules/skcms/")
    set_property(SOURCE "${_skia_src}" APPEND PROPERTY COMPILE_OPTIONS -msse2 -mssse3 -msse4.1)
  endif()
endforeach()
target_link_libraries(skia_lib PUBLIC ${HTML_TO_IMAGE_LIBS})
