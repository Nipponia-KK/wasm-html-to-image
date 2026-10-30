#include "converter_api.h"

#include <algorithm>
#include <cstring>

// 委譲先: decode=common/image_decoder / encode=common/skia_encode /
// thumbhash=common/thumbhash (skia_encode経由)。正本の SkCodec 直書き・
// AVIF直書き・thumbhash直呼びはすべて dispatcher 配下に移譲した。
#include "../common/image_decoder.h"
#ifndef HTML_TO_IMAGE_PDF_ONLY
#include "../common/pdf_cmyk.h"
#endif
#include "../common/skia_encode.h"
#include "api/js_logger.h"
#include "satoru_api.h"
#include "unified_api.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkData.h"
#include "include/core/SkRect.h"

// SVGパッチは common/image_decoder 側で適用済み (svg_patch.h 経由) のため
// 正本の patch_svg_data (ctre 3種) をここで重複適用しない。

ImageConverterInstance::ImageConverterInstance() { context.init(); }

ImageConverterInstance::~ImageConverterInstance() {}

bool ImageConverterInstance::load_image(const uint8_t* data, size_t size) {
    frames.clear();
    original_data = SkData::MakeWithCopy(data, size);
    original_width = 0;
    original_height = 0;
    original_animation = false;
    original_format = "unknown";

    // 寄せ: 正本 load_image (SkCodec直書き+EXIF補正+SVGラスタ化) →
    // html_to_image::ImageDecoder::decode (同一振る舞いを共通化済み)。
    auto decoded = html_to_image::ImageDecoder::decode(data, size, nullptr);
    if (!decoded.ok()) return false;

    original_width = decoded.width;
    original_height = decoded.height;
    original_animation = decoded.animated;
    original_format = decoded.format;
    frames.reserve(decoded.frames.size());
    for (auto& f : decoded.frames) {
        ConverterImageFrame frame;
        frame.bitmap = std::move(f.bitmap);
        frame.duration = f.duration_ms;
        frames.push_back(std::move(frame));
    }
    return !frames.empty();
}

const uint8_t* ImageConverterInstance::encode(RenderFormat format, float quality, int speed,
                                              bool animation, int animation_loop,
                                              int& out_size) {
    ConverterEncodeOptions options;
    options.format = format;
    options.quality = quality;
    options.speed = speed;
    options.animation = animation;
    options.animation_loop = animation_loop;
    options.dpi = dpi;
    return encode(options, out_size);
}

const uint8_t* ImageConverterInstance::encode(const ConverterEncodeOptions& options,
                                              int& out_size) {
    if (frames.empty()) {
        out_size = 0;
        return nullptr;
    }
    output_animation = false;
    output_format = "unknown";

    // 寄せ: 正本 encode() switch 全分岐 → html_to_image::encode_frames。
    // (PNG/WebP単帧+animated/JPEG/AVIF/RAW/ThumbHash。SVG/PDFは元通り未対応null)。
    std::vector<html_to_image::EncodeFrameView> views;
    views.reserve(frames.size());
    for (const auto& f : frames) views.push_back({&f.bitmap, f.duration});

    auto result =
        html_to_image::encode_frames(views.data(), views.size(), options, raw_data);
    if (!result.success()) {
        // None素通しで raw_data が無い場合もここ (正本と同一: out_size=0/null)。
        out_size = 0;
        return nullptr;
    }
    output_animation = result.is_animation;
    // None素通し時は元画像の animation/format を引き継ぐ (正本 192-193 行と同一)。
    if (options.format == RenderFormat::None) {
        output_animation = original_animation;
        output_format = original_format;
    } else {
        output_format = result.format_name;
    }
    out_size = (int)result.data->size();
    const uint8_t* bytes = result.data->bytes();
    context.set_last_output(std::move(result.data));
    return bytes;
}

bool ImageConverterInstance::crop(int x, int y, int width, int height) {
    if (frames.empty()) return false;

    int original_w = frames[0].bitmap.width();
    int original_h = frames[0].bitmap.height();

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + width > original_w) width = original_w - x;
    if (y + height > original_h) height = original_h - y;

    if (width <= 0 || height <= 0) return false;

    for (auto& frame : frames) {
        SkImageInfo info = frame.bitmap.info().makeWH(width, height);
        SkBitmap cropped;
        cropped.allocPixels(info);

        SkCanvas canvas(cropped);
        canvas.clear(SK_ColorTRANSPARENT);

        SkRect src_rect = SkRect::MakeXYWH((float)x, (float)y, (float)width, (float)height);
        SkRect dest_rect = SkRect::MakeWH((float)width, (float)height);
        canvas.drawImageRect(frame.bitmap.asImage(), src_rect, dest_rect, SkSamplingOptions(),
                             nullptr, SkCanvas::kStrict_SrcRectConstraint);

        frame.bitmap = cropped;
    }
    return true;
}

bool ImageConverterInstance::resize(int width, int height, FitMode fit) {
    if (frames.empty()) return false;

    int original_w = frames[0].bitmap.width();
    int original_h = frames[0].bitmap.height();

    if (width <= 0 && height <= 0) return true;
    if (width <= 0) width = (int)(original_w * ((float)height / original_h));
    if (height <= 0) height = (int)(original_h * ((float)width / original_w));

    float scale_x = (float)width / original_w;
    float scale_y = (float)height / original_h;

    int canvas_w = width;
    int canvas_h = height;
    float target_w = (float)width;
    float target_h = (float)height;
    float offset_x = 0;
    float offset_y = 0;

    if (fit == FitMode::Contain) {
        float scale = std::min(scale_x, scale_y);
        target_w = original_w * scale;
        target_h = original_h * scale;
        canvas_w = (int)target_w;
        canvas_h = (int)target_h;
    } else if (fit == FitMode::Cover) {
        float scale = std::max(scale_x, scale_y);
        target_w = original_w * scale;
        target_h = original_h * scale;
        offset_x = ((float)width - target_w) / 2.0f;
        offset_y = ((float)height - target_h) / 2.0f;
    }

    for (auto& frame : frames) {
        SkImageInfo info = frame.bitmap.info().makeWH(canvas_w, canvas_h);
        SkBitmap resized;
        resized.allocPixels(info);

        SkCanvas canvas(resized);
        canvas.clear(SK_ColorTRANSPARENT);

        SkRect dest_rect = SkRect::MakeXYWH(offset_x, offset_y, target_w, target_h);
        canvas.drawImageRect(frame.bitmap.asImage(), dest_rect,
                             SkSamplingOptions(SkFilterMode::kLinear));

        frame.bitmap = resized;
    }
    return true;
}

std::string ImageConverterInstance::encode_svg() {
    if (frames.empty()) return "";
    return html_to_image::encode_image_to_svg(frames[0].bitmap);
}

const uint8_t* ImageConverterInstance::encode_pdf(int& out_size, bool cmyk) {
    if (frames.empty()) {
        out_size = 0;
        return nullptr;
    }
    sk_sp<SkData> data = html_to_image::encode_image_to_pdf(frames[0].bitmap);
#ifdef HTML_TO_IMAGE_PDF_ONLY
    if (cmyk) data = nullptr;
#else
    if (data && cmyk) data = html_to_image::pdf_to_cmyk(*data);
#endif
    if (!data || data->size() == 0) {
        out_size = 0;
        return nullptr;
    }
    out_size = (int)data->size();
    const uint8_t* bytes = (const uint8_t*)data->data();
    context.set_last_output(std::move(data));
    return bytes;
}

int ImageConverterInstance::get_original_width() const { return original_width; }
int ImageConverterInstance::get_original_height() const { return original_height; }
bool ImageConverterInstance::is_original_animation() const { return original_animation; }
std::string ImageConverterInstance::get_original_format() const { return original_format; }
int ImageConverterInstance::get_width() const {
    return frames.empty() ? 0 : frames[0].bitmap.width();
}
int ImageConverterInstance::get_height() const {
    return frames.empty() ? 0 : frames[0].bitmap.height();
}
bool ImageConverterInstance::is_animation() const { return output_animation; }
std::string ImageConverterInstance::get_format() const { return output_format; }

// --- API Functions (Wrappers: 正本名 api_* → converter_api_* に改名) ---

ImageConverterInstance* converter_api_create_instance() { return new ImageConverterInstance(); }

void converter_api_destroy_instance(ImageConverterInstance* inst) { delete inst; }

const uint8_t* converter_api_load_image(ImageConverterInstance* inst, const uint8_t* data,
                                        size_t size) {
    if (!inst) return nullptr;
    inst->raw_data = SkData::MakeWithCopy(data, size);
    if (inst->load_image((const uint8_t*)inst->raw_data->data(), inst->raw_data->size())) {
        return (const uint8_t*)1;  // Success indicator (正本と同一)
    }
    return nullptr;
}

const uint8_t* converter_api_encode(ImageConverterInstance* inst, int format, float quality,
                                    int speed, bool animation, int animation_loop,
                                    int& out_size) {
    if (!inst) {
        out_size = 0;
        return nullptr;
    }
    return inst->encode((RenderFormat)format, quality, speed, animation, animation_loop,
                        out_size);
}

bool converter_api_crop(ImageConverterInstance* inst, int x, int y, int width, int height) {
    return inst ? inst->crop(x, y, width, height) : false;
}

bool converter_api_resize(ImageConverterInstance* inst, int width, int height, int fit) {
    return inst ? inst->resize(width, height, (FitMode)fit) : false;
}

int converter_api_get_original_width(ImageConverterInstance* inst) {
    return inst ? inst->get_original_width() : 0;
}
int converter_api_get_original_height(ImageConverterInstance* inst) {
    return inst ? inst->get_original_height() : 0;
}
bool converter_api_is_original_animation(ImageConverterInstance* inst) {
    return inst ? inst->is_original_animation() : false;
}
std::string converter_api_get_original_format(ImageConverterInstance* inst) {
    return inst ? inst->get_original_format() : "unknown";
}
int converter_api_get_width(ImageConverterInstance* inst) {
    return inst ? inst->get_width() : 0;
}
int converter_api_get_height(ImageConverterInstance* inst) {
    return inst ? inst->get_height() : 0;
}
bool converter_api_is_animation(ImageConverterInstance* inst) {
    return inst ? inst->is_animation() : false;
}
std::string converter_api_get_format(ImageConverterInstance* inst) {
    return inst ? inst->get_format() : "unknown";
}

void converter_api_set_log_level(int level) {
    // P1: 未定義 `html_to_image_log` (宣言のみで定義なし) への参照を廃止し、
    // satoru側と同一の共有JsLogger (satoru_api.cpp の g_js_logger) へ集約。
    // 公開シンボル・bindings登録 (main.cpp)・正常系は不変。新規throwなし。
    if (auto* logger = satoru_api_get_js_logger()) logger->setLogLevel((LogLevel)level);
}
