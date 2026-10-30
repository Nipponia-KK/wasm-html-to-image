#include "satoru_context.h"

#include <algorithm>
#include <iostream>
#include <mutex>
#include <sstream>

#include "include/codec/SkAvifDecoder.h"
#include "include/codec/SkBmpDecoder.h"
#include "include/codec/SkCodec.h"
#include "include/codec/SkGifDecoder.h"
#include "include/codec/SkIcoDecoder.h"
#include "include/codec/SkJpegDecoder.h"
#include "include/codec/SkPngDecoder.h"
#include "include/codec/SkWebpDecoder.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkSpan.h"
#include "modules/skshaper/include/SkShaper_harfbuzz.h"
#include "common/image_decoder.h"
#include "utils/skia_utils.h"
#include "utils/skunicode_satoru.h"

void SatoruContext::init() {
    static std::once_flag register_codecs_once;
    std::call_once(register_codecs_once, []() {
        SkCodecs::Register(SkPngDecoder::Decoder());
#ifndef HTML_TO_IMAGE_PDF_ONLY
        SkCodecs::Register(SkJpegDecoder::Decoder());
        SkCodecs::Register(SkWebpDecoder::Decoder());
        SkCodecs::Register(SkAvifDecoder::Decoder());
        SkCodecs::Register(SkBmpDecoder::Decoder());
        SkCodecs::Register(SkIcoDecoder::Decoder());
        SkCodecs::Register(SkGifDecoder::Decoder());
#endif
    });
}

satoru::UnicodeService &SatoruContext::getUnicodeService() {
    if (!m_unicodeService) {
        m_unicodeService = std::make_unique<satoru::UnicodeService>();
    }
    return *m_unicodeService;
}

SkShaper *SatoruContext::getShaper() {
    if (!m_shaper) {
        m_shaper =
            SkShapers::HB::ShapeDontWrapOrReorder(getUnicodeService().getSkUnicodeSp(), nullptr);
    }
    return m_shaper.get();
}

void SatoruContext::loadImage(const char *name, const char *data_url, int width, int height) {
    std::string key = name ? name : "";
    auto existing = imageCache.find(key);
    if (existing != imageCache.end() && existing->second.data_url == (data_url ? data_url : "") &&
        existing->second.width == width && existing->second.height == height) {
        return;
    }
    image_info info;
    info.data_url = data_url ? data_url : "";
    info.width = width;
    info.height = height;
    imageCache[key] = info;
    m_imageVersion++;
}

void SatoruContext::loadImageFromData(const char *name, const uint8_t *data, size_t size,
                                      const char *original_url) {
    int width = 0, height = 0;
    bool ratio_only = false;
    auto image = html_to_image::ImageDecoder::decode_first(data, size, width, height,
                                                            fontManager.getFontMgr(), &ratio_only);
    if (image) {
        image_info info;
        info.data_url = original_url ? original_url : "";
        info.width = width;
        info.height = height;
        info.ratio_only = ratio_only;
        info.skImage = image;
        imageCache[name] = info;
        m_imageVersion++;
        needsRelayout = true;
    }
}

void SatoruContext::loadImageFromPixels(const char *name, int width, int height,
                                        const uint8_t *pixels, const char *original_url) {
    SkImageInfo img_info =
        SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
    SkPixmap pixmap(img_info, pixels, width * 4);
    auto image = SkImages::RasterFromPixmapCopy(pixmap);
    if (image) {
        image_info info;
        info.data_url = original_url ? original_url : "";
        info.width = width;
        info.height = height;
        info.skImage = image;
        imageCache[name] = info;
        m_imageVersion++;
        needsRelayout = true;
    }
}

sk_sp<SkTypeface> SatoruContext::get_typeface(const std::string &family, int weight,
                                              SkFontStyle::Slant slant, bool &out_fake_bold,
                                              int stretch) {
    auto tfs = get_typefaces(family, weight, slant, out_fake_bold, stretch);
    return tfs.empty() ? nullptr : tfs[0];
}

std::vector<sk_sp<SkTypeface>> SatoruContext::get_typefaces(const std::string &family, int weight,
                                                            SkFontStyle::Slant slant,
                                                            bool &out_fake_bold, int stretch) {
    out_fake_bold = false;
    auto result = fontManager.matchFonts(family, weight, slant, stretch);

    if (result.empty() || fontManager.isGenericMetricFamily(family)) {
        // システムフォント非依存のため、Verdana / Arial 等は代替書体で描かれる。
        // 行の高さ (line-height: normal) だけは要求 family に合わせる。汎用 family は
        // 実体 (代替書体) が登録済みでも同じ差し替えが要る: その縦メトリクスは
        // 汎用プロファイルのものではないため。
        auto adjusted = fontManager.matchMetricFallbackFonts(family, weight, slant, stretch);
        if (!adjusted.empty()) result = adjusted;
    }

    if (!result.empty()) {
        // Fake bold check (only for non-variable fonts, though this flag will be
        // overridden in container_skia if Variable Font cloning succeeds)
        if (weight >= 600 && result[0]->fontStyle().weight() < 500) {
            out_fake_bold = true;
        }
    }
    return result;
}

bool SatoruContext::get_image_size(const std::string &url, int &w, int &h) {
    auto it = imageCache.find(url);
    if (it != imageCache.end()) {
        w = it->second.width;
        h = it->second.height;
        return true;
    }
    return false;
}

bool SatoruContext::get_image_ratio_only(const std::string &url) {
    auto it = imageCache.find(url);
    return it != imageCache.end() && it->second.ratio_only;
}
