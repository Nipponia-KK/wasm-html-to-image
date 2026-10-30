# cmake/fonts.cmake — vcpkg フォント/テキスト系依存 + HTML_TO_IMAGE_LIBS 集約
#
# P3a 分割前: CMakeLists.txt の「Dependencies」ブロック (find_package 群 +
# harfbuzz ワークアラウンド + HTML_TO_IMAGE_LIBS) を等価移動。順序・値の変更なし。
#
# Provenance (satoru正 + image-opt分は内包済み):
#  satoru: freetype/png/jpeg/webp/dav1d/zlib/gumbo/bzip2/brotli/expat/ctre/utf8proc/libunibreak/qpdf/harfbuzz
#  image-opt: png/jpeg/webp/dav1d/expat/zlib/ctre (+aom FetchContent) -> すべて上記に含まれるため追加vcpkg依存なし
#  vcpkg.json / triplets は参照のみ (変更なし)。
#
# 前提: VCPKG_INSTALLED_DIR 定義済み、codecs.cmake 適用済み (aom/dav1d/avif ターゲット解決のため)
# 提供: HTML_TO_IMAGE_LIBS

# satoru: freetype/png/jpeg/webp/dav1d/zlib/gumbo/bzip2/brotli/expat/ctre/utf8proc/libunibreak/qpdf/harfbuzz
# image-opt: png/jpeg/webp/dav1d/expat/zlib/ctre (+aom FetchContent) -> すべて上記に含まれるため追加vcpkg依存なし
find_package(Freetype REQUIRED)
find_package(PNG REQUIRED)
find_package(JPEG REQUIRED)
find_package(WebP REQUIRED)
find_package(expat CONFIG REQUIRED)
find_package(unofficial-gumbo CONFIG REQUIRED)
find_package(unofficial-brotli CONFIG REQUIRED)
find_package(BZip2 REQUIRED)
find_package(ctre CONFIG REQUIRED)
find_package(utf8proc CONFIG REQUIRED)
find_package(libunibreak CONFIG REQUIRED)
find_package(harfbuzz REQUIRED)
find_package(qpdf CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

# Workaround for vcpkg harfbuzz path bug (satoru正)
if(TARGET harfbuzz)
    set_target_properties(harfbuzz PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${VCPKG_INSTALLED_DIR}/include/harfbuzz")
endif()

set(HTML_TO_IMAGE_LIBS
    Freetype::Freetype
    PNG::PNG
    WebP::webp
    WebP::webpdecoder
    WebP::webpdemux
    WebP::libwebpmux
    WebP::sharpyuv
    avif
    expat::expat
    unofficial::gumbo::gumbo
    unofficial::brotli::brotlidec
    unofficial::brotli::brotlienc
    unofficial::brotli::brotlicommon
    BZip2::BZip2
    ctre::ctre
    utf8proc::utf8proc
    libunibreak::libunibreak
    harfbuzz::harfbuzz
    qpdf::libqpdf
    JPEG::JPEG
    ZLIB::ZLIB
)
if(WASM_PDF_ONLY)
    list(REMOVE_ITEM HTML_TO_IMAGE_LIBS
        WebP::webp WebP::webpdecoder WebP::webpdemux WebP::libwebpmux WebP::sharpyuv
        avif unofficial::brotli::brotlienc qpdf::libqpdf JPEG::JPEG
    )
elseif(WASM_AVIF_BACKEND STREQUAL "DAV1D")
    list(APPEND HTML_TO_IMAGE_LIBS dav1d::dav1d)
else()
    list(APPEND HTML_TO_IMAGE_LIBS aom)
endif()
