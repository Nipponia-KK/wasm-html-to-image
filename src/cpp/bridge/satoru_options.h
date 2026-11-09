#ifndef HTML_TO_IMAGE_SATORU_OPTIONS_H
#define HTML_TO_IMAGE_SATORU_OPTIONS_H

// satoru側 RenderOptions の改名版 (HTML描画用)。フィールドは satoru版をそのまま継承。
// Split from `bridge_types.h` (content verbatim).

#include <cstdint>
#include <string>

struct SatoruRenderOptions {
    bool svgTextToPaths = true;
    int fitType = 0;  // 0=contain, 1=cover, 2=fill (FitModeと対応)
    int outputWidth = 0;
    int outputHeight = 0;
    int cropX = 0;
    int cropY = 0;
    int cropWidth = 0;
    int cropHeight = 0;
    uint32_t backgroundColor = 0x00000000;
    float fitPositionX = 0.5f;
    float fitPositionY = 0.5f;
    int mediaType = 0;  // 0=screen, 1=print
    // Raster pixel density: the bitmap is `scale` times the layout size and the page is drawn
    // scaled (vector text and shapes stay sharp). Ignored when outputWidth/outputHeight is set.
    float scale = 1.0f;

    // Output colour space of PDF: 0 = sRGB (DeviceRGB), 1 = CMYK (DeviceCMYK, see common/pdf_cmyk.h)
    int colorSpace = 0;

    // PDF Metadata
    std::string pdfTitle;
    std::string pdfAuthor;
    std::string pdfSubject;
    std::string pdfKeywords;
    std::string pdfCreator;
    std::string pdfProducer;
    // PDF outline (bookmarks) from the document's h1-h6; on by default. The headings also get their
    // text, level, id and page reported back to the caller (see SatoruHeading).
    bool pdfOutline = true;

    // PDF Margins (pixels)
    int pdfMarginTop = 0;
    int pdfMarginRight = 0;
    int pdfMarginBottom = 0;
    int pdfMarginLeft = 0;

    // PDF Templates
    std::string pdfHeader;
    std::string pdfFooter;
    int pdfSkipRunning = 0;  // leading pages printed without header and footer
};

#endif  // HTML_TO_IMAGE_SATORU_OPTIONS_H
