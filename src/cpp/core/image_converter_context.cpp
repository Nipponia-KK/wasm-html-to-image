#include "image_converter_context.h"

#include "include/codec/SkAvifDecoder.h"
#include "include/codec/SkBmpDecoder.h"
#include "include/codec/SkGifDecoder.h"
#include "include/codec/SkJpegDecoder.h"
#include "include/codec/SkPngDecoder.h"
#include "include/codec/SkWebpDecoder.h"

// For registering codecs
#include "include/codec/SkCodec.h"

void ImageConverterContext::init() {
    // Register codecs for SkCodec::MakeFromData
    SkCodecs::Register(SkPngDecoder::Decoder());
#ifndef HTML_TO_IMAGE_PDF_ONLY
    SkCodecs::Register(SkJpegDecoder::Decoder());
    SkCodecs::Register(SkWebpDecoder::Decoder());
    SkCodecs::Register(SkGifDecoder::Decoder());
    SkCodecs::Register(SkAvifDecoder::Decoder());
    SkCodecs::Register(SkBmpDecoder::Decoder());
#endif
}
