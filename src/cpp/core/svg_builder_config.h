#ifndef SVG_BUILDER_CONFIG_H
#define SVG_BUILDER_CONFIG_H

#include "modules/svg/include/SkSVGDOM.h"

class SatoruContext;

namespace litehtml {

/// Give an SVG builder what SkSVGDOM needs to draw text and nested images the way the page does:
/// a font manager that answers from the context's fonts, the HarfBuzz shaper, and the context's images.
/// Shared by inline <svg> and by an SVG loaded as an image (<img>, CSS background).
void configure_svg_builder(SkSVGDOM::Builder& builder, SatoruContext& context);

}  // namespace litehtml

#endif  // SVG_BUILDER_CONFIG_H
