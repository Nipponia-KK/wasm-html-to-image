# cmake/codecs.cmake — FetchContent 系コーデック取得 (AVIF/AOM・JXL・wuffs)
#
# P3a 分割前: CMakeLists.txt の「AVIF / codec backends」「JXL」「wuffs」ブロックを等価移動。
# ソースファイルの追加削除なし。値・順序の変更なし。
#
# --- FetchContent バージョン固定表 (P3a) ---
# | dep    | repository                                  | tag     | shallow | 備考                                    |
# |--------|-----------------------------------------------|---------|---------|-----------------------------------------|
# | skia   | https://github.com/google/skia/archive/<sha>.tar.gz | baf1231 | -       | コミット固定 (URL取得)。実体は cmake/skia.cmake の SKIA_COMMIT |
# | libavif| https://github.com/AOMediaCodec/libavif.git | v1.1.1  | (なし)  | SHALLOW指定なしのまま維持 (等価のため)  |
# | libaom | https://aomedia.googlesource.com/aom          | v3.11.0 | TRUE    | WASM_AVIF_BACKEND=AOM 時のみ取得        |
# | libjxl | https://github.com/libjxl/libjxl.git          | v0.11.1 | TRUE    | + GIT_SUBMODULES_RECURSE TRUE。WASM_ENABLE_JXL=ON時のみ |
# | wuffs  | raw.githubusercontent.com/google/wuffs      | v0.3    | -       | file(DOWNLOAD) で wuffs-v0.3.c を取得 (FetchContent外) |
# NOTE: バージョン更新時はこの表と各 FetchContent_Declare / file(DOWNLOAD) URL を同時更新すること。
#
# 前提:
#  - include(FetchContent) 済み (トップで実施)
#  - VCPKG_INSTALLED_DIR 定義済み (トップで定義)
# 提供:
#  - dav1d::dav1d (IMPORTED), aom / avif / jxl 系ターゲット, WUFFS_RELEASE_DIR

# --- AVIF / codec backends ---
link_directories("${VCPKG_INSTALLED_DIR}/lib")

if(NOT TARGET dav1d::dav1d)
    add_library(dav1d::dav1d STATIC IMPORTED GLOBAL)
    set_target_properties(dav1d::dav1d PROPERTIES
        IMPORTED_LOCATION "${VCPKG_INSTALLED_DIR}/lib/libdav1d.a"
        INTERFACE_INCLUDE_DIRECTORIES "${VCPKG_INSTALLED_DIR}/include"
    )
endif()

if(NOT WASM_PDF_ONLY)
if(WASM_AVIF_BACKEND STREQUAL "AOM")
    # image-opt派生: libaom FetchContent (OPT-INのみ。既定DAV1Dでは取得しない)
    find_package(Perl REQUIRED)
    set(PERL_EXECUTABLE ${PERL_EXECUTABLE} CACHE FILEPATH "" FORCE)
    set(AOM_TARGET_CPU "generic" CACHE STRING "" FORCE)
    set(ENABLE_DOCS OFF CACHE BOOL "" FORCE)
    set(ENABLE_TESTS OFF CACHE BOOL "" FORCE)
    set(ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(ENABLE_TOOLS OFF CACHE BOOL "" FORCE)
    set(CONFIG_RUNTIME_CPU_DETECT 0 CACHE BOOL "" FORCE)
    set(CONFIG_MULTITHREAD 0 CACHE BOOL "" FORCE)
    set(CONFIG_WEBM_IO 0 CACHE BOOL "" FORCE)
    FetchContent_Declare(
      libaom
      GIT_REPOSITORY https://aomedia.googlesource.com/aom
      GIT_TAG        v3.11.0
      GIT_SHALLOW    TRUE
    )
    FetchContent_GetProperties(libaom)
    if(NOT libaom_POPULATED)
        FetchContent_Populate(libaom)
        set(OLD_CMAKE_C_FLAGS ${CMAKE_C_FLAGS})
        set(OLD_CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -w")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w")
        add_subdirectory(${libaom_SOURCE_DIR} ${libaom_BINARY_DIR} EXCLUDE_FROM_ALL)
        set(CMAKE_C_FLAGS ${OLD_CMAKE_C_FLAGS})
        set(CMAKE_CXX_FLAGS ${OLD_CMAKE_CXX_FLAGS})
    endif()
    set(AVIF_CODEC_AOM SYSTEM CACHE STRING "" FORCE)
    set(AVIF_CODEC_AOM_DECODE ON CACHE BOOL "" FORCE)
    set(AVIF_CODEC_AOM_ENCODE ON CACHE BOOL "" FORCE)
    set(AVIF_ENABLE_WASM ON CACHE BOOL "" FORCE)
else()
    # satoru派生・既定: DAV1D
    set(AVIF_CODEC_DAV1D SYSTEM CACHE STRING "" FORCE)
    set(AVIF_ENABLE_WASM OFF CACHE BOOL "" FORCE)
endif()
set(AVIF_LIBYUV OFF CACHE BOOL "" FORCE)
set(AVIF_BUILD_APPS OFF CACHE BOOL "" FORCE)
set(AVIF_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE) # Force static for libavif

FetchContent_Declare(
  libavif
  GIT_REPOSITORY https://github.com/AOMediaCodec/libavif.git
  GIT_TAG        v1.1.1
)

set(CMAKE_SKIP_INSTALL_RULES ON)
set(OLD_CMAKE_C_FLAGS ${CMAKE_C_FLAGS})
set(OLD_CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -w")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w")
FetchContent_MakeAvailable(libavif)
set(CMAKE_C_FLAGS ${OLD_CMAKE_C_FLAGS})
set(CMAKE_CXX_FLAGS ${OLD_CMAKE_CXX_FLAGS})
set(CMAKE_SKIP_INSTALL_RULES OFF)

if(WASM_AVIF_BACKEND STREQUAL "AOM" AND TARGET avif_obj)
    target_include_directories(avif_obj PRIVATE
        ${libaom_SOURCE_DIR}
        ${libaom_BINARY_DIR}
        ${libaom_SOURCE_DIR}/aom
    )
endif()

endif()

# --- JXL (libjxl FetchContent) ---
if(WASM_ENABLE_JXL)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(JPEGXL_ENABLE_TOOLS OFF CACHE BOOL "" FORCE)
    set(JPEGXL_ENABLE_BENCHMARK OFF CACHE BOOL "" FORCE)
    set(JPEGXL_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(JPEGXL_ENABLE_MANPAGES OFF CACHE BOOL "" FORCE)
    set(JPEGXL_ENABLE_DOXYGEN OFF CACHE BOOL "" FORCE)
    set(JPEGXL_ENABLE_PLUGINS OFF CACHE BOOL "" FORCE)
    set(JPEGXL_ENABLE_VIEWERS OFF CACHE BOOL "" FORCE)
    set(JPEGXL_ENABLE_DEVTOOLS OFF CACHE BOOL "" FORCE)
    set(JPEGXL_ENABLE_JNI OFF CACHE BOOL "" FORCE)
    set(JPEGXL_ENABLE_FUZZERS OFF CACHE BOOL "" FORCE)
    set(JPEGXL_ENABLE_SJPEG OFF CACHE BOOL "" FORCE)
    set(JPEGXL_ENABLE_OPENEXR OFF CACHE BOOL "" FORCE)
    set(JPEGXL_ENABLE_TRANSCODE_JPEG OFF CACHE BOOL "" FORCE)
    set(JPEGXL_ENABLE_BOXES OFF CACHE BOOL "" FORCE)
    set(JPEGXL_ENABLE_WASM_THREADS OFF CACHE BOOL "" FORCE)
    set(JPEGXL_BUNDLE_LIBPNG OFF CACHE BOOL "" FORCE)
    set(JPEGXL_ENABLE_LTO OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        libjxl
        GIT_REPOSITORY https://github.com/libjxl/libjxl.git
        GIT_TAG        v0.11.1
        GIT_SHALLOW    TRUE
        GIT_SUBMODULES_RECURSE TRUE
    )
    set(CMAKE_SKIP_INSTALL_RULES ON)
    FetchContent_GetProperties(libjxl)
    if(NOT libjxl_POPULATED)
        FetchContent_Populate(libjxl)
        set(OLD_CMAKE_C_FLAGS ${CMAKE_C_FLAGS})
        set(OLD_CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -w")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w")
        add_subdirectory(${libjxl_SOURCE_DIR} ${libjxl_BINARY_DIR} EXCLUDE_FROM_ALL)
        set(CMAKE_C_FLAGS ${OLD_CMAKE_C_FLAGS})
        set(CMAKE_CXX_FLAGS ${OLD_CMAKE_CXX_FLAGS})
    endif()
    set(CMAKE_SKIP_INSTALL_RULES OFF)
endif()

# --- wuffs (両リポ共通) ---
set(WUFFS_RELEASE_DIR "${CMAKE_BINARY_DIR}/wuffs")
if(NOT EXISTS "${WUFFS_RELEASE_DIR}/wuffs-v0.3.c")
    file(MAKE_DIRECTORY "${WUFFS_RELEASE_DIR}")
    message(STATUS "Downloading wuffs-v0.3.c...")
    file(DOWNLOAD https://raw.githubusercontent.com/google/wuffs/main/release/c/wuffs-v0.3.c
         "${WUFFS_RELEASE_DIR}/wuffs-v0.3.c")
endif()
