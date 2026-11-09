#include "satoru_context.h"

#include <algorithm>
#include <iostream>
#include <string_view>
#include <mutex>
#include <set>
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
#include "svg_builder_config.h"
#include "text_utils.h"
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

namespace {

// The characters inside the <text> elements of an SVG document (tags dropped, the predefined and numeric
// entities decoded). Not a parser: it only has to name the characters the fonts must cover.
void collect_svg_xml_text(const uint8_t* data, size_t size, std::set<char32_t>& out) {
    std::string xml(reinterpret_cast<const char*>(data), size);
    int depth = 0;
    size_t i = 0;
    while (i < xml.size()) {
        if (xml[i] == '<') {
            size_t end = xml.find('>', i);
            if (end == std::string::npos) break;
            if (xml.compare(i, 4, "<!--") == 0) {
                end = xml.find("-->", i);
                if (end == std::string::npos) break;
                end += 2;
            } else if (xml.compare(i, 9, "<![CDATA[") == 0) {
                size_t close = xml.find("]]>", i);
                if (close == std::string::npos) break;
                if (depth > 0) {
                    const char* p = xml.c_str() + i + 9;
                    const char* stop = xml.c_str() + close;
                    while (p < stop) {
                        char32_t cp = satoru::decode_utf8_char(&p);
                        if (cp == 0) break;
                        out.insert(cp);
                    }
                }
                end = close + 2;
            } else {
                bool closing = xml[i + 1] == '/';
                size_t n = i + (closing ? 2 : 1);
                size_t e = n;
                while (e < end && !isspace((unsigned char)xml[e]) && xml[e] != '/' && xml[e] != '>') e++;
                std::string tag = xml.substr(n, e - n);
                if (tag == "text" || tag == "tspan" || tag == "textPath") {
                    if (closing) depth = depth > 0 ? depth - 1 : 0;
                    else if (xml[end - 1] != '/') depth++;
                }
            }
            i = end + 1;
            continue;
        }
        if (depth > 0) {
            if (xml[i] == '&') {
                size_t semi = xml.find(';', i);
                if (semi != std::string::npos && semi - i <= 10 && xml[i + 1] == '#') {
                    bool hex = xml[i + 2] == 'x' || xml[i + 2] == 'X';
                    char32_t cp = (char32_t)strtoul(xml.c_str() + i + (hex ? 3 : 2), nullptr, hex ? 16 : 10);
                    if (cp) out.insert(cp);
                }
                i = semi == std::string::npos ? i + 1 : semi + 1;
                continue;
            }
            const char* p = xml.c_str() + i;
            char32_t cp = satoru::decode_utf8_char(&p);
            if (cp == 0) break;
            out.insert(cp);
            i = p - xml.c_str();
            continue;
        }
        i++;
    }
}

std::string trim_quotes(std::string v) {
    auto bad = [](char c) { return isspace((unsigned char)c) || c == '\'' || c == '"'; };
    while (!v.empty() && bad(v.front())) v.erase(v.begin());
    while (!v.empty() && bad(v.back())) v.pop_back();
    return v;
}

// What an SVG document refers to besides itself: the font families its text names, the external images of
// its <image> elements, and its @font-face rules (a url() there is fetched like one in the page's CSS).
struct SvgRefs {
    std::set<std::string> families;
    std::vector<std::string> image_urls;
    std::vector<std::string> font_faces;
    bool bold = false;
};

SvgRefs scan_svg_refs(const std::string& xml) {
    SvgRefs refs;
    for (size_t i = xml.find("font-family"); i != std::string::npos; i = xml.find("font-family", i + 11)) {
        size_t p = i + 11;
        while (p < xml.size() && (isspace((unsigned char)xml[p]) || xml[p] == ':' || xml[p] == '=')) p++;
        if (p < xml.size() && xml[p] == '"') p++;
        size_t end = xml.find_first_of(";\"}>", p);
        if (end == std::string::npos) continue;
        std::string list = xml.substr(p, end - p);
        size_t from = 0;
        while (from <= list.size()) {
            size_t comma = list.find(',', from);
            std::string name = trim_quotes(list.substr(from, comma == std::string::npos ? comma : comma - from));
            if (!name.empty()) refs.families.insert(name);
            if (comma == std::string::npos) break;
            from = comma + 1;
        }
    }
    refs.bold = xml.find("bold") != std::string::npos || xml.find("font-weight") != std::string::npos;
    for (size_t i = xml.find("<image"); i != std::string::npos; i = xml.find("<image", i + 6)) {
        size_t end = xml.find('>', i);
        if (end == std::string::npos) break;
        std::string tag = xml.substr(i, end - i);
        size_t h = tag.find("href");
        if (h == std::string::npos) continue;
        size_t q = tag.find_first_of("\"'", h);
        if (q == std::string::npos) continue;
        size_t q2 = tag.find(tag[q], q + 1);
        if (q2 == std::string::npos) continue;
        std::string url = tag.substr(q + 1, q2 - q - 1);
        if (!url.empty() && url[0] != '#' && url.compare(0, 5, "data:") != 0) refs.image_urls.push_back(url);
    }
    for (size_t i = xml.find("@font-face"); i != std::string::npos; i = xml.find("@font-face", i + 10)) {
        size_t close = xml.find('}', i);
        if (close == std::string::npos) break;
        refs.font_faces.push_back(xml.substr(i, close - i + 1));
    }
    return refs;
}

}  // namespace

void SatoruContext::loadImageFromData(const char *name, const uint8_t *data, size_t size,
                                      const char *original_url) {
    int width = 0, height = 0;
    bool ratio_only = false;
    bool no_ratio = false;
    auto image = html_to_image::ImageDecoder::decode_first(data, size, width, height,
                                                            fontManager.getFontMgr(), &ratio_only,
                                                            &no_ratio, [this](SkSVGDOM::Builder& b) {
                                                                litehtml::configure_svg_builder(b, *this);
                                                            });
    if (image) {
        const bool is_svg =
            std::string_view(reinterpret_cast<const char*>(data), size).find("<svg") != std::string_view::npos;
        image_info info;
        if (is_svg) {
            collect_svg_xml_text(data, size, svgImageCodepoints);
            info.svg_source = SkData::MakeWithCopy(data, size);
            SvgRefs refs = scan_svg_refs(std::string(reinterpret_cast<const char*>(data), size));
            svgImageFamilies.insert(refs.families.begin(), refs.families.end());
            if (refs.bold) svgImageBold = true;
            for (const auto& url : refs.image_urls) svgImageUrls.push_back(url);
            for (const auto& face : refs.font_faces) {
                if (addCss(face, CssChangeKind::UserScan)) fontManager.scanFontFaces(face);
            }
        } else if (std::any_of(imageCache.begin(), imageCache.end(),
                               [](const auto& e) { return e.second.svg_source != nullptr; })) {
            m_svgDirty = true;  // an image an SVG refers to has arrived: draw those SVGs again
        }
        info.data_url = original_url ? original_url : "";
        info.width = width;
        info.height = height;
        info.ratio_only = ratio_only;
        info.no_ratio = no_ratio;
        info.skImage = image;
        imageCache[name] = info;
        m_imageVersion++;
        needsRelayout = true;
    }
}

void SatoruContext::refreshSvgImages() {
    if (!m_svgDirty) return;
    m_svgDirty = false;
    for (auto &[name, info] : imageCache) {
        if (!info.svg_source) continue;
        int width = 0, height = 0;
        auto image = html_to_image::ImageDecoder::decode_first(
            info.svg_source->bytes(), info.svg_source->size(), width, height, fontManager.getFontMgr(),
            nullptr, nullptr,
            [this](SkSVGDOM::Builder &b) { litehtml::configure_svg_builder(b, *this); });
        if (image && width == info.width && height == info.height) info.skImage = image;
    }
    m_imageVersion++;
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

bool SatoruContext::get_image_no_ratio(const std::string &url) {
    auto it = imageCache.find(url);
    return it != imageCache.end() && it->second.no_ratio;
}
