#include "image_decoder.h"

#ifdef JXL_ENABLED
#include <jxl/encode.h>
#include <jxl/decode.h>
#endif

#include <memory>
#include <utility>

#include "include/codec/SkAvifDecoder.h"
#include "include/codec/SkBmpDecoder.h"
#include "include/codec/SkCodec.h"
#include "include/codec/SkEncodedImageFormat.h"
#include "include/codec/SkEncodedOrigin.h"
#include "include/codec/SkGifDecoder.h"
#include "include/codec/SkIcoDecoder.h"
#include "include/codec/SkJpegDecoder.h"
#include "include/codec/SkPixmapUtils.h"
#include "include/codec/SkPngDecoder.h"
#include "include/codec/SkWebpDecoder.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkData.h"
#include "include/core/SkStream.h"
#include "modules/skresources/include/SkResources.h"
#include "modules/svg/include/SkSVGDOM.h"
#include "modules/svg/include/SkSVGNode.h"
#include "modules/svg/include/SkSVGSVG.h"
#include "svg_patch.h"

namespace html_to_image {

DecodeResult ImageDecoder::decode(const uint8_t* data, size_t size, sk_sp<SkFontMgr> font_mgr) {
    DecodeResult result;
    if (!data || size == 0) return result;

    auto sk_data = SkData::MakeWithCopy(data, size);

    // 1. ラスタ (SkCodec 全フレーム + EXIF Orient 補正)。
    //    正本: converter側 load_image (image_converter_api.cpp:80-137)。
    //    satoru側 (image_decoder.cpp:40-53) は先頭フレームのみ・Orient 未補正のため不採用。
    if (std::unique_ptr<SkCodec> codec = SkCodec::MakeFromData(sk_data)) {
        const int frameCount = codec->getFrameCount();
        const SkEncodedOrigin origin = codec->getOrigin();
        const bool swapDimensions = SkEncodedOriginSwapsWidthHeight(origin);

        SkImageInfo info =
            codec->getInfo().makeColorType(kN32_SkColorType).makeAlphaType(kPremul_SkAlphaType);

        if (swapDimensions) {
            result.width = info.height();
            result.height = info.width();
        } else {
            result.width = info.width();
            result.height = info.height();
        }
        result.animated = frameCount > 1;

        switch (codec->getEncodedFormat()) {
            case SkEncodedImageFormat::kPNG: result.format = "png"; break;
            case SkEncodedImageFormat::kJPEG: result.format = "jpeg"; break;
            case SkEncodedImageFormat::kWEBP: result.format = "webp"; break;
            case SkEncodedImageFormat::kGIF: result.format = "gif"; break;
            case SkEncodedImageFormat::kAVIF: result.format = "avif"; break;
            case SkEncodedImageFormat::kBMP: result.format = "bmp"; break;
            default: result.format = "unknown"; break;
        }

        for (int i = 0; i < frameCount; ++i) {
            SkBitmap decodedBitmap;
            decodedBitmap.allocPixels(info);

            SkCodec::Options options;
            options.fFrameIndex = i;

            if (codec->getPixels(info, decodedBitmap.getPixels(), decodedBitmap.rowBytes(),
                                 &options) != SkCodec::kSuccess) {
                continue;
            }
            SkCodec::FrameInfo frameInfo;
            codec->getFrameInfo(i, &frameInfo);

            if (origin != kDefault_SkEncodedOrigin) {
                SkImageInfo orientedInfo = info;
                if (swapDimensions) {
                    orientedInfo = orientedInfo.makeWH(info.height(), info.width());
                }
                SkBitmap orientedBitmap;
                orientedBitmap.allocPixels(orientedInfo);
                if (SkPixmapUtils::Orient(orientedBitmap.pixmap(), decodedBitmap.pixmap(), origin)) {
                    result.frames.push_back({std::move(orientedBitmap), frameInfo.fDuration});
                } else {
                    result.frames.push_back({std::move(decodedBitmap), frameInfo.fDuration});
                }
            } else {
                result.frames.push_back({std::move(decodedBitmap), frameInfo.fDuration});
            }
        }
        if (!result.frames.empty()) return result;

        // codec は開けたが全フレーム失敗: 空のまま SVG フォールバックへ落とす。
        result.width = 0;
        result.height = 0;
        result.format = "unknown";
        result.animated = false;
    }

#ifdef JXL_ENABLED
    // 1b. JPEG XL (Skia has no JXL codec): decode with libjxl.
    const JxlSignature sig = JxlSignatureCheck(data, size);
    if ((sig == JXL_SIG_CODESTREAM || sig == JXL_SIG_CONTAINER) && decode_jxl(data, size, result)) {
        return result;
    }

#endif
    // 2. SVG (satoru側 decode_svg を統合。事前に共通 patch_svg_data を適用)。
    //    判定ヒューリスティックは satoru側 (先頭空白スキップ + '<') を採用。
    //    converter側は data[0] == '<' の直接比較 + api_load_image 側で事前パッチのため、
    //    共通化後はこちらの decode 内で判定+パッチを完結させる。
    if (is_likely_svg(data, size)) {
        return decode_svg(patch_svg_data(sk_data), std::move(font_mgr));
    }

    return result;
}

sk_sp<SkImage> ImageDecoder::decode_first(const uint8_t* data, size_t size, int& out_width,
                                          int& out_height, sk_sp<SkFontMgr> font_mgr,
                                          bool* out_ratio_only, bool* out_no_ratio,
                                          const SvgBuilderConfig& configure_svg) {
    DecodeResult result;
    if (is_likely_svg(data, size)) {
        // The layout path rasterizes a ratio-only SVG at its default object
        // size (see decode_svg); decode() keeps the shared 512 fallback that
        // the standalone converter's output size relies on.
        auto sk_data = SkData::MakeWithCopy(data, size);
        result = decode_svg(patch_svg_data(sk_data), std::move(font_mgr), true, configure_svg);
    } else {
        result = decode(data, size, std::move(font_mgr));
    }
    if (!result.ok()) return nullptr;
    out_width = result.frames[0].bitmap.width();
    out_height = result.frames[0].bitmap.height();
    if (out_ratio_only) *out_ratio_only = result.ratio_only;
    if (out_no_ratio) *out_no_ratio = result.no_ratio;
    return result.frames[0].bitmap.asImage();
}

bool ImageDecoder::is_likely_svg(const uint8_t* data, size_t size) {
    const uint8_t* p = data;
    size_t s = size;
    while (s > 0 && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
        ++p;
        --s;
    }
    return s >= 1 && p[0] == '<';
}

#ifdef JXL_ENABLED
// JPEG XL (still or animated) -> N32 premul frames, converted to sRGB, orientation applied.
bool ImageDecoder::decode_jxl(const uint8_t* data, size_t size, DecodeResult& result) {
    JxlDecoder* dec = JxlDecoderCreate(nullptr);
    if (!dec) return false;
    struct Guard {
        JxlDecoder* d;
        ~Guard() { JxlDecoderDestroy(d); }
    } guard{dec};

    if (JxlDecoderSubscribeEvents(dec, JXL_DEC_BASIC_INFO | JXL_DEC_FRAME | JXL_DEC_FULL_IMAGE) !=
        JXL_DEC_SUCCESS) {
        return false;
    }
    JxlDecoderSetInput(dec, data, size);
    JxlDecoderCloseInput(dec);

    const JxlPixelFormat format = {4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};
    JxlBasicInfo info{};
    bool have_info = false;
    int duration_ms = 0;
    SkBitmap rgba;  // straight RGBA8888 output buffer of the frame being decoded

    for (;;) {
        JxlDecoderStatus status = JxlDecoderProcessInput(dec);
        if (status == JXL_DEC_BASIC_INFO) {
            if (JxlDecoderGetBasicInfo(dec, &info) != JXL_DEC_SUCCESS) return false;
            if (info.xsize == 0 || info.ysize == 0 || info.xsize > 32768 || info.ysize > 32768)
                return false;
            have_info = true;
            // Output in sRGB regardless of the file's profile (best effort).
            JxlColorEncoding srgb;
            JxlColorEncodingSetToSRGB(&srgb, JXL_FALSE);
            JxlDecoderSetOutputColorProfile(dec, &srgb, nullptr, 0);
        } else if (status == JXL_DEC_FRAME) {
            JxlFrameHeader header;
            duration_ms = 0;
            if (JxlDecoderGetFrameHeader(dec, &header) == JXL_DEC_SUCCESS && info.have_animation &&
                info.animation.tps_numerator > 0) {
                duration_ms = (int)((double)header.duration * 1000.0 * info.animation.tps_denominator /
                                    info.animation.tps_numerator);
            }
        } else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
            if (!have_info) return false;
            size_t buffer_size = 0;
            if (JxlDecoderImageOutBufferSize(dec, &format, &buffer_size) != JXL_DEC_SUCCESS)
                return false;
            if (buffer_size != (size_t)info.xsize * info.ysize * 4) return false;
            rgba.reset();
            if (!rgba.tryAllocPixels(SkImageInfo::Make((int)info.xsize, (int)info.ysize,
                                                       kRGBA_8888_SkColorType,
                                                       kUnpremul_SkAlphaType))) {
                return false;
            }
            if (JxlDecoderSetImageOutBuffer(dec, &format, rgba.getPixels(), buffer_size) !=
                JXL_DEC_SUCCESS) {
                return false;
            }
        } else if (status == JXL_DEC_FULL_IMAGE) {
            SkBitmap frame;
            const SkImageInfo n32 = SkImageInfo::Make((int)info.xsize, (int)info.ysize,
                                                      kN32_SkColorType, kPremul_SkAlphaType);
            if (!frame.tryAllocPixels(n32) || !rgba.pixmap().readPixels(frame.pixmap())) return false;
            result.frames.push_back({std::move(frame), duration_ms});
        } else if (status == JXL_DEC_SUCCESS) {
            break;
        } else {
            // error / unexpected end of input
            if (result.frames.empty()) return false;
            break;
        }
    }
    if (result.frames.empty()) return false;
    result.width = (int)info.xsize;
    result.height = (int)info.ysize;
    result.format = "jxl";
    result.animated = result.frames.size() > 1;
    return true;
}
#endif

DecodeResult ImageDecoder::decode_svg(const sk_sp<SkData>& data, sk_sp<SkFontMgr> font_mgr,
                                      bool default_object_raster,
                                      const SvgBuilderConfig& configure_svg) {
    DecodeResult result;
    SkMemoryStream stream(data);
    auto resource_provider = skresources::DataURIResourceProviderProxy::Make(
        nullptr, skresources::ImageDecodeStrategy::kLazyDecode);

    SkSVGDOM::Builder builder;
    builder.setResourceProvider(std::move(resource_provider))
        .setFontManager(font_mgr ? font_mgr : SkFontMgr::RefEmpty());
    if (configure_svg) configure_svg(builder);
    auto svg_dom = builder.make(stream);
    if (!svg_dom) return result;

    // A viewBox but no absolute width/height on the root: the SVG has an
    // aspect ratio and no intrinsic size (the same test el_svg applies to an
    // inline <svg>). Width/height default to 100%, i.e. a percentage.
    bool ratio_only = false;
    float vb_w = 0, vb_h = 0;
    float abs_w = 0, abs_h = 0;  // root width/height when absolute, else 0
    if (auto* root = svg_dom->getRoot()) {
        auto absolute = [](const SkSVGLength& len) {
            return len.unit() != SkSVGLength::Unit::kPercentage &&
                   len.unit() != SkSVGLength::Unit::kUnknown && len.value() > 0;
        };
        if (absolute(root->getWidth())) abs_w = root->getWidth().value();
        if (absolute(root->getHeight())) abs_h = root->getHeight().value();
        const auto& vb = root->getViewBox();
        if (vb && vb->width() > 0 && vb->height() > 0) {
            vb_w = vb->width();
            vb_h = vb->height();
            if (!abs_w && !abs_h) ratio_only = true;
        }
    }
    result.ratio_only = ratio_only;
    result.no_ratio = !abs_w && !abs_h && vb_w == 0;

    auto container_size = svg_dom->containerSize();
    if (ratio_only && default_object_raster) {
        // Rasterize at the CSS default object size (300x150) fitted to the
        // ratio, so the bitmap's own size carries the ratio and the graphic
        // fills it exactly, instead of a letterboxed 512x512. Prefer an
        // integer multiple of the viewBox so the bitmap's ratio is EXACT
        // (80x10 at fit 3.75 would otherwise round to 300x38 = ratio 7.89
        // where the true ratio is 8).
        float fit = std::min(300.0f / vb_w, 150.0f / vb_h);
        int factor = (int)fit;
        if (factor >= 1) {
            container_size = SkSize::Make(vb_w * factor, vb_h * factor);
        } else {
            container_size = SkSize::Make(std::max(1.0f, (float)std::lround(vb_w * fit)),
                                          std::max(1.0f, (float)std::lround(vb_h * fit)));
        }
    } else if (default_object_raster && (bool)abs_w != (bool)abs_h) {
        // Exactly one absolute dimension: the intrinsic size derives the
        // other from the viewBox ratio, or from the CSS default object
        // size (300x150) when there is no viewBox. Skia's containerSize()
        // is empty for this shape and the old code fell to 512x512, where
        // a browser shows e.g. width=80 + viewBox 80x10 as 80x10.
        // (Layout sizing path only; decode() keeps the 512 fallback.)
        if (abs_w) {
            float h = vb_w > 0 ? abs_w * vb_h / vb_w : 150.0f;
            container_size = SkSize::Make(abs_w, std::max(1.0f, (float)std::lround(h)));
        } else {
            float w = vb_h > 0 ? abs_h * vb_w / vb_h : 300.0f;
            container_size = SkSize::Make(std::max(1.0f, (float)std::lround(w)), abs_h);
        }
    } else if (default_object_raster && !abs_w && !abs_h && vb_w == 0) {
        // No intrinsic size and no ratio (no absolute dimensions, no
        // viewBox): the layout size is the CSS default object size
        // (300x150), where Skia's empty containerSize() used to fall to
        // the 512x512 raster fallback below.
        container_size = SkSize::Make(300, 150);
    } else if (container_size.isEmpty()) {
        container_size = SkSize::Make(512, 512);  // satoru/converter 共通のフォールバック
    }
    svg_dom->setContainerSize(container_size);

    result.width = (int)container_size.width();
    result.height = (int)container_size.height();
    result.format = "svg";
    result.animated = false;

    SkBitmap bitmap;
    if (!bitmap.tryAllocPixels(SkImageInfo::MakeN32Premul(result.width, result.height))) {
        result.width = 0;
        result.height = 0;
        result.format = "unknown";
        return result;
    }
    SkCanvas canvas(bitmap);
    canvas.clear(SK_ColorTRANSPARENT);
    svg_dom->render(&canvas);

    result.frames.push_back({std::move(bitmap), 0});
    return result;
}

}  // namespace html_to_image
