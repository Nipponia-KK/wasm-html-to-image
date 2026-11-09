#ifndef CONTAINER_SKIA_H
#ifndef LH_TYPES_H
#include "libs/litehtml/include/litehtml/types.h"
#endif
#define CONTAINER_SKIA_H

#include <map>
#include <set>
#include <string>
#include <vector>

#include "bridge/bridge_types.h"
#include "core/text/text_renderer.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkRRect.h"
#include "include/core/SkTypeface.h"
#include "libs/litehtml/include/litehtml.h"
#include "resource_manager.h"
#include "satoru_context.h"
#include "utils/logging.h"

// Default viewport height (px) used for layout when the caller omits `height`.
// Viewport-relative units (vh/vmin/vmax) and viewport media queries resolve
// against this value, so it must model a browser viewport rather than an
// arbitrary tall layout canvas. Matches Chromium's no-JS viewport 1000x800:
// e.g. `padding-top:25vh` must be 0.25*800 = 200px, not 0.25*3000 = 750px.
inline constexpr int k_default_viewport_height = 800;

// Default viewport width (px) used for layout when the caller omits `width`.
// Mirrors the TS API default (RenderOptions.width defaults to 800) and Chromium's
// no-JS viewport (1000x800), so the CSS viewport is never 0.
inline constexpr int k_default_viewport_width = 800;

// Non-positive width -> default viewport width. Applied at every C++ API / renderer
// entry: a caller that reaches the wasm module directly (bypassing the TS layer)
// with width 0/negative must still lay out in a real viewport, otherwise
// container_skia::get_viewport / get_media_features report width 0.
inline int normalize_viewport_width(int width) {
    return width > 0 ? width : k_default_viewport_width;
}

class container_skia : public litehtml::document_container {
    // ── P2a: 共有状態の責務別分離 ──
    // 5分割ファイルと対応: paint→container_skia.cpp (draw_*系)+helpers(無状態純粋関数),
    // clip(+mask)→container_skia_clip.cpp, filter→container_skia_filters.cpp,
    // transform(+layer)→container_skia_transforms.cpp, text→container_skia.cpp
    // (create_font/draw_text系)。振る舞い・公開getter・litehtml I/Fは不変。
    // 例外: m_usedGlyphs / m_usedGlyphDraws は core/text/tagging_context.h が
    // 参照束縛するためクラス直下に残す (scope外ファイルに触れないため)。
    // Pending text-clip gradients for PNG background-clip: text support
    struct pending_text_clip {
        litehtml::background_layer layer;
        litehtml::background_layer::linear_gradient gradient;
    };
    struct PaintState {
        std::vector<shadow_info> shadows;
        std::vector<text_shadow_info> textShadows;
        std::vector<image_draw_info> imageDraws;
        std::vector<conic_gradient_info> conicGradients;
        std::vector<radial_gradient_info> radialGradients;
        std::vector<linear_gradient_info> linearGradients;
        std::vector<text_draw_info> textDraws;
        std::vector<border_image_info> borderImages;
        std::vector<std::string> inlineSvgs;
        std::vector<litehtml::position> inlineSvgPositions;
        std::vector<pending_text_clip> pendingTextClips;
        std::vector<float> opacityStack;
    };
    struct ClipState {
        std::vector<std::pair<litehtml::position, litehtml::border_radiuses>> clips;
        std::vector<clip_info> usedClips;
        std::vector<clip_path_info> usedClipPaths;
        std::vector<std::pair<litehtml::css_token_vector, litehtml::position>> maskStack;
        std::vector<mask_info> usedMasks;
        int clipPathDepth = 0;
        int maskDepth = 0;
        mutable std::vector<bool> svgClipActive;
    };
    struct FilterState {
        std::vector<filter_info> filters;
        std::vector<backdrop_filter_info> backdropFilters;
        int depth = 0;
    };
    struct TransformState {
        int depth = 0;
        int offsetDepth = 0;
    };
    struct TextState {
        std::set<char32_t> usedCodepoints;
        std::set<font_request> requestedAttrs;
        std::set<font_request> missingFonts;
        int lastBidiLevel = -1;
        int lastBaseLevel = -1;
        std::vector<bool> asciiUsed;
        std::map<font_request, std::vector<font_info*>> createdFonts;
        std::map<font_request, std::set<char32_t>> measuredCodepoints;
        satoru::TextBatcher* batcher = nullptr;
    };

    SkCanvas* m_canvas;
    int m_width;
    int m_height;
    SatoruContext& m_context;
    ResourceManager* m_resourceManager;

    PaintState m_paint;
    ClipState m_clip;
    FilterState m_filter;
    TransformState m_transform;
    TextState m_text;

    std::vector<SkPath> m_usedGlyphs;
    std::vector<glyph_draw_info> m_usedGlyphDraws;

    bool m_tagging;
    bool m_textToPaths = false;

    litehtml::media_type m_media_type;

    const litehtml::document *m_doc = nullptr;

    float get_current_opacity() const {
        float opacity = 1.0f;
        for (float o : m_paint.opacityStack) {
            opacity *= o;
        }
        return opacity;
    }

   public:
    container_skia(int w, int h, SkCanvas *canvas, SatoruContext &context, ResourceManager *rm,
                   bool tagging = false,
                   litehtml::media_type media_type = litehtml::media_type_screen);
    virtual ~container_skia();

    void set_canvas(SkCanvas *canvas) {
        m_canvas = canvas;
        if (m_text.batcher) {
            m_text.batcher->flush();
            delete m_text.batcher;
        }
        m_text.batcher = new satoru::TextBatcher(&m_context, m_canvas);
    }
    void set_height(int h) { m_height = h; }
    void set_document(const litehtml::document *doc) { m_doc = doc; }
    void set_tagging(bool t) { m_tagging = t; }
    void set_media_type(litehtml::media_type type) { m_media_type = type; }
    litehtml::media_type get_media_type() const { return m_media_type; }
    void set_text_to_paths(bool to_paths) { m_textToPaths = to_paths; }
    void flush() {
        if (m_text.batcher && m_text.batcher->isActive()) {
            m_text.batcher->flush();
        }
    }
    void reset() {
        // P2a: struct分離後も clear 対象は旧 reset() と完全一致させる (振る舞い不変)。
        // clear しないもの: m_text.* (codepoints/要求families/createdFonts/measured/
        // asciiUsed/bidi level)/m_clip.clips rect stack/m_paint.opacityStack/
        // m_paint.inlineSvgPositions/m_clip.svgClipActive。
        if (m_text.batcher) m_text.batcher->flush();
        m_paint.shadows.clear();
        m_paint.textShadows.clear();
        m_paint.imageDraws.clear();
        m_paint.conicGradients.clear();
        m_paint.radialGradients.clear();
        m_paint.linearGradients.clear();
        m_paint.textDraws.clear();
        m_paint.inlineSvgs.clear();
        m_filter.filters.clear();
        m_filter.backdropFilters.clear();
        m_paint.borderImages.clear();
        m_clip.usedClips.clear();
        m_clip.usedClipPaths.clear();
        m_clip.usedMasks.clear();
        m_clip.maskStack.clear();
        m_usedGlyphs.clear();
        m_usedGlyphDraws.clear();
        m_filter.depth = 0;
        m_transform.depth = 0;
        m_transform.offsetDepth = 0;
        m_clip.clipPathDepth = 0;
        m_clip.maskDepth = 0;
        m_paint.pendingTextClips.clear();
    }

    SkCanvas *get_canvas() const { return m_canvas; }
    bool is_tagging() const { return m_tagging; }
    SatoruContext &get_context() { return m_context; }
    ResourceManager *get_resource_manager() const { return m_resourceManager; }

    int add_inline_svg(const std::string &xml, const litehtml::position &pos) {
        m_paint.inlineSvgs.push_back(xml);
        m_paint.inlineSvgPositions.push_back(pos);
        return (int)m_paint.inlineSvgs.size();
    }

    const std::vector<std::string> &get_used_inline_svgs() const { return m_paint.inlineSvgs; }

    const std::vector<image_draw_info> &get_used_image_draws() const { return m_paint.imageDraws; }
    const std::vector<conic_gradient_info> &get_used_conic_gradients() const {
        return m_paint.conicGradients;
    }
    const std::vector<radial_gradient_info> &get_used_radial_gradients() const {
        return m_paint.radialGradients;
    }
    const std::vector<linear_gradient_info> &get_used_linear_gradients() const {
        return m_paint.linearGradients;
    }
    const std::vector<shadow_info> &get_used_shadows() const { return m_paint.shadows; }
    const std::vector<text_shadow_info> &get_used_text_shadows() const { return m_paint.textShadows; }
    const std::vector<text_draw_info> &get_used_text_draws() const { return m_paint.textDraws; }
    const std::vector<filter_info> &get_used_filters() const { return m_filter.filters; }
    const std::vector<backdrop_filter_info> &get_used_backdrop_filters() const {
        return m_filter.backdropFilters;
    }
    const std::vector<border_image_info> &get_used_border_images() const {
        return m_paint.borderImages;
    }
    const std::vector<clip_info> &get_used_clips() const { return m_clip.usedClips; }
    void push_svg_clip_active(bool active) const { m_clip.svgClipActive.push_back(active); }
    bool pop_svg_clip_active() const {
        if (m_clip.svgClipActive.empty()) return false;
        bool active = m_clip.svgClipActive.back();
        m_clip.svgClipActive.pop_back();
        return active;
    }
    const std::vector<clip_path_info> &get_used_clip_paths() const { return m_clip.usedClipPaths; }
    const std::vector<mask_info> &get_used_masks() const { return m_clip.usedMasks; }
    const std::vector<SkPath> &get_used_glyphs() const { return m_usedGlyphs; }
    const std::vector<glyph_draw_info> &get_used_glyph_draws() const { return m_usedGlyphDraws; }

    int add_glyph(const SkPath &path) {
        for (size_t i = 0; i < m_usedGlyphs.size(); ++i) {
            if (m_usedGlyphs[i] == path) return (int)i + 1;
        }
        m_usedGlyphs.push_back(path);
        return (int)m_usedGlyphs.size();
    }

    int add_glyph_draw(const glyph_draw_info &info) {
        m_usedGlyphDraws.push_back(info);
        return (int)m_usedGlyphDraws.size();
    }

    const std::set<char32_t> &get_used_codepoints() const { return m_text.usedCodepoints; }
    /// Characters drawn outside the text pipeline (inline SVG <text>): the font slices that cover them
    /// have to be fetched like the ones for HTML text.
    void add_used_codepoints(const std::set<char32_t> &codepoints) {
        if (m_resourceManager) m_text.usedCodepoints.insert(codepoints.begin(), codepoints.end());
    }
    const std::set<font_request> &get_requested_font_attributes() const {
        return m_text.requestedAttrs;
    }

    const std::set<font_request> &get_missing_fonts() const { return m_text.missingFonts; }

    void collect_used_font_characters(const font_request &req, std::vector<char32_t> &out) const;
    void collect_measured_font_characters(const font_request &req,
                                          std::vector<char32_t> &out) const;
    const std::set<char32_t> *get_measured_font_codepoints(const font_request &req) const;

    // litehtml::document_container implementations
    virtual litehtml::uint_ptr create_font(const litehtml::font_description &desc,
                                           const litehtml::document *doc,
                                           litehtml::font_metrics *fm) override;
    virtual void delete_font(litehtml::uint_ptr hFont) override;
    virtual litehtml::pixel_t text_width(const char *text, litehtml::uint_ptr hFont,
                                         litehtml::direction dir,
                                         litehtml::writing_mode mode) override;
    virtual void draw_text(litehtml::uint_ptr hdc, const char *text, litehtml::uint_ptr hFont,
                           litehtml::web_color color, const litehtml::position &pos,
                           litehtml::text_overflow overflow, litehtml::direction dir,
                           litehtml::writing_mode mode) override;
    virtual litehtml::pixel_t pt_to_px(float pt) const override;
    virtual litehtml::pixel_t get_default_font_size() const override;
    virtual const char *get_default_font_name() const override;
    virtual void draw_list_marker(litehtml::uint_ptr hdc,
                                  const litehtml::list_marker &marker) override;
    virtual void load_image(const char *src, const char *baseurl, bool redraw_on_ready) override;
    virtual void get_image_size(const char *src, const char *baseurl, litehtml::size &sz) override;
    virtual bool get_image_ratio_only(const char *src, const char *baseurl) override;
    virtual bool get_image_no_ratio(const char *src, const char *baseurl) override;
    virtual void draw_image(
        litehtml::uint_ptr hdc, const litehtml::background_layer &layer, const std::string &url,
        const std::string &base_url, litehtml::object_fit fit = litehtml::object_fit_fill,
        const litehtml::css_token_vector &object_position = litehtml::css_token_vector(),
        litehtml::image_rendering rendering = litehtml::image_rendering_auto) override;
    virtual void draw_solid_fill(litehtml::uint_ptr hdc, const litehtml::background_layer &layer,
                                 const litehtml::web_color &color) override;
    virtual void draw_linear_gradient(
        litehtml::uint_ptr hdc, const litehtml::background_layer &layer,
        const litehtml::background_layer::linear_gradient &gradient) override;
    virtual void draw_radial_gradient(
        litehtml::uint_ptr hdc, const litehtml::background_layer &layer,
        const litehtml::background_layer::radial_gradient &gradient) override;
    virtual void draw_conic_gradient(
        litehtml::uint_ptr hdc, const litehtml::background_layer &layer,
        const litehtml::background_layer::conic_gradient &gradient) override;
    virtual void draw_borders(litehtml::uint_ptr hdc, const litehtml::borders &borders,
                              const litehtml::position &draw_pos, bool root) override;
    virtual void draw_border_image(litehtml::uint_ptr hdc,
                                   const litehtml::border_image &border_image,
                                   const litehtml::borders &borders,
                                   const litehtml::position &draw_pos, bool root) override;
    virtual void draw_box_shadow(litehtml::uint_ptr hdc, const litehtml::shadow_vector &shadows,
                                 const litehtml::position &pos,
                                 const litehtml::border_radiuses &radius, bool inset) override;

    virtual int get_bidi_level(const char *text, int base_level) override;
    virtual void set_caption(const char * /*caption*/) override {}
    virtual void set_base_url(const char * /*base_url*/) override {}
    virtual void link(const std::shared_ptr<litehtml::document> &doc,
                      const litehtml::element::ptr &el) override;
    virtual void on_anchor_click(const char * /*url*/,
                                 const litehtml::element::ptr & /*el*/) override {}
    virtual void on_mouse_event(const litehtml::element::ptr & /*el*/,
                                litehtml::mouse_event /*event*/) override {}
    virtual void set_cursor(const char * /*cursor*/) override {}
    virtual void transform_text(litehtml::string &text, litehtml::text_transform tt) override;
    virtual void import_css(litehtml::string &text, const litehtml::string &url,
                            litehtml::string &baseurl) override;
    virtual void set_clip(const litehtml::position &pos,
                          const litehtml::border_radiuses &bdr_radius) override;
    virtual void del_clip() override;
    virtual void get_viewport(litehtml::position &viewport) const override;
    virtual void get_media_features(litehtml::media_features &features) const override;
    virtual void get_language(litehtml::string &language, litehtml::string &culture) const override;
    virtual litehtml::string resolve_color(const litehtml::string &color) const override;
    virtual void split_text(const char *text, const std::function<void(const char *)> &on_word,
                            const std::function<void(const char *)> &on_space) override;

    virtual void push_layer(litehtml::uint_ptr hdc, float opacity,
                            litehtml::blend_mode bm) override;
    virtual void push_layer(litehtml::uint_ptr hdc, float opacity, litehtml::blend_mode bm,
                            const litehtml::position& bounds) override;
    void push_layer_impl(float opacity, litehtml::blend_mode bm, const SkRect* bounds);
    virtual void pop_layer(litehtml::uint_ptr hdc) override;

    virtual void push_transform(litehtml::uint_ptr hdc, const litehtml::css_token_vector &transform,
                                const litehtml::css_token_vector &origin,
                                const litehtml::position &pos) override;
    virtual void pop_transform(litehtml::uint_ptr hdc) override;
    virtual void push_filter(litehtml::uint_ptr hdc,
                             const litehtml::css_token_vector &filter) override;
    virtual void pop_filter(litehtml::uint_ptr hdc) override;

    virtual void push_clip_path(litehtml::uint_ptr hdc, const litehtml::css_token_vector &clip_path,
                                const litehtml::position &pos) override;
    virtual void pop_clip_path(litehtml::uint_ptr hdc) override;

    virtual void push_offset_path(litehtml::uint_ptr hdc, const litehtml::css_token_vector &path,
                                  const litehtml::css_token_vector &offset_pos,
                                  const litehtml::css_token_vector &distance,
                                  const litehtml::css_token_vector &rotate,
                                  const litehtml::css_token_vector &anchor,
                                  const litehtml::css_token_vector &transform_origin,
                                  const litehtml::position &ref_box,
                                  const litehtml::position &box) override;
    virtual void pop_offset_path(litehtml::uint_ptr hdc) override;

    virtual void push_mask(litehtml::uint_ptr hdc, const litehtml::css_token_vector &mask,
                           const litehtml::position &pos) override;
    virtual void pop_mask(litehtml::uint_ptr hdc) override;

    virtual void on_unknown_property(const litehtml::string &name,
                                     const litehtml::css_token_vector &value) override;

    virtual void pop_backdrop_filter(litehtml::uint_ptr hdc) override;
    virtual void push_backdrop_filter(litehtml::uint_ptr hdc,
                                      const std::shared_ptr<litehtml::render_item> &el) override;

    virtual litehtml::element::ptr create_element(
        const char *tag_name, const litehtml::string_map &attributes,
        const std::shared_ptr<litehtml::document> &doc) override;

    static SkPath parse_clip_path(const litehtml::css_token_vector &tokens,
                                  const litehtml::position &pos);

    /// Convert litehtml::blend_mode to SkBlendMode.
    static SkBlendMode to_skia_blend_mode(litehtml::blend_mode bm);
};

#endif
