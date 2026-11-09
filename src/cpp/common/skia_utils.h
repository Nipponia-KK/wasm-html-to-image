#ifndef HTML_TO_IMAGE_SKIA_UTILS_H
#define HTML_TO_IMAGE_SKIA_UTILS_H

// common/skia_utils: satoru版supersetを正として1本化。
// 正本: `satoru/src/cpp/utils/skia_utils.h/.cpp`
// 不採用: image-opt版 `utils/skia_utils.h` (base64_encode/decode + url_decode のみで、
//          下記の clean_font_name / make_rrect を欠くサブセットのため)。
// image-opt版にあって satoru版にない関数は存在しない (superset関係) ので、
// satoru版の持ち込みで欠落なし。

#include <cstdint>
#include <string>
#include <vector>

#include "include/core/SkImage.h"
#include "include/core/SkPath.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRefCnt.h"
#include "litehtml.h"

struct image_info {
    int width;
    int height;
    std::string data_url;
    sk_sp<SkImage> skImage;
    // The source is an SVG with a viewBox but no intrinsic size: width/height
    // are the default-object raster size and only the ratio is intrinsic.
    bool ratio_only = false;
    // The source is an SVG with no absolute dimensions and no viewBox: it
    // has no intrinsic ratio either; width/height are the CSS default
    // object size (300x150) and each auto axis takes its own default.
    bool no_ratio = false;
    // The SVG source, kept so the image can be drawn again once the fonts its <text> needs have arrived
    // (SkSVGDOM bakes the text into the bitmap when the image is decoded).
    sk_sp<SkData> svg_source;
};

std::string clean_font_name(const char* name);
std::string base64_encode(const uint8_t* data, size_t len);
std::vector<uint8_t> base64_decode(const std::string& in);
std::string url_decode(const std::string& in);

SkRRect make_rrect(const litehtml::position& pos, const litehtml::border_radiuses& radius);

// Outline of the box with `border-radius` + `corner-shape` applied. Without any corner-shape
// this is exactly the rounded rect of make_rrect(); otherwise each corner is built from its
// own shape (scoop / bevel / notch / squircle / square) using the radii as shape parameters.
SkPath make_shape_path(const litehtml::position& pos, const litehtml::border_radiuses& radius);

#endif  // HTML_TO_IMAGE_SKIA_UTILS_H
