// P2a責務: core lifecycle + text + paint + misc (詳細は L42 付近の責務ブロック)。
#include "container_skia.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string_view>

#include <utf8proc.h>

#include "bridge/magic_tags.h"
#include "container_skia_helpers.h"
#include "core/text/text_layout.h"
#include "core/text/text_renderer.h"
#include "el_svg.h"
#include "include/core/SkBitmap.h"
#include "include/core/SkBlurTypes.h"
#include "include/core/SkClipOp.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkMaskFilter.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRRect.h"
#include "include/core/SkShader.h"
#include "include/core/SkString.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTileMode.h"
#include "include/effects/SkColorMatrix.h"
#include "include/effects/SkDashPathEffect.h"
#include "include/effects/SkGradient.h"
#include "include/effects/SkImageFilters.h"
#include "include/effects/SkRuntimeEffect.h"
#include "litehtml/css_parser.h"
#include "litehtml/el_table.h"
#include "litehtml/el_td.h"
#include "litehtml/el_tr.h"
#include "litehtml/render_item.h"
#include "text_utils.h"
#include "utils/skia_utils.h"
#include "utils/skunicode_satoru.h"

// ────────────────────────────────────────────────────────────────────────────
// 責務 (P2a/P4): core lifecycle + text + misc document_container I/F
//  - ctor/dtor, create_font/delete_font/text_width/draw_text/bidi/split系 (text)
//  - paint(draw_box_shadow/draw_image/draw_*_gradient/draw_borders/
//    draw_border_image/draw_solid_fill/draw_list_marker)は
//    container_skia_paint.cpp に分離済み (P4: 関数移動のみ)。
//  - viewport/media/language/import_css/resolve_color/load_image等の雑多I/F
// clip(+mask)→container_skia_clip.cpp, filter→filters.cpp,
// transform(+layer)→transforms.cpp, 無状態純粋関数→helpers(.h/.cpp) に分離済み。
// 公開シグネチャ・litehtml I/F・返却値・throw挙動は不変 (P4は移動のみ)。
// ────────────────────────────────────────────────────────────────────────────

namespace litehtml {
vector<css_token_vector> parse_comma_separated_list(const css_token_vector& tokens);
}

namespace {
char ascii_lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c; }

bool starts_with_ascii_ci(std::string_view s, size_t pos, const char* needle) {
    size_t needle_len = std::strlen(needle);
    if (pos + needle_len > s.size()) return false;
    for (size_t i = 0; i < needle_len; ++i) {
        if (ascii_lower(s[pos + i]) != ascii_lower(needle[i])) return false;
    }
    return true;
}

bool contains_any_ascii_ci(std::string_view s, const char* const* needles, size_t needle_count) {
    for (size_t i = 0; i < s.size(); ++i) {
        for (size_t j = 0; j < needle_count; ++j) {
            if (starts_with_ascii_ci(s, i, needles[j])) return true;
        }
    }
    return false;
}

bool looks_like_font_url(std::string_view url) {
    static constexpr const char* needles[] = {".woff2", ".woff", ".ttf", ".otf", ".ttc"};
    return contains_any_ascii_ci(url, needles, sizeof(needles) / sizeof(needles[0]));
}

void collect_text_codepoints(SatoruContext& context, const char* text,
                             std::set<char32_t>& codepoints) {
    if (!text) return;
    const char* p = text;
    auto& unicode = context.getUnicodeService();
    while (*p) {
        char32_t cp = unicode.decodeUtf8(&p);
        if (cp != 0) codepoints.insert(cp);
    }
}

std::vector<char32_t> decode_text_codepoints(SatoruContext& context, const char* text) {
    std::vector<char32_t> codepoints;
    if (!text) return codepoints;
    const char* p = text;
    auto& unicode = context.getUnicodeService();
    while (*p) {
        char32_t cp = unicode.decodeUtf8(&p);
        if (cp != 0) codepoints.push_back(cp);
    }
    return codepoints;
}
}  // namespace

namespace {

std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n'\"");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n'\"");
    return s.substr(start, end - start + 1);
}
}  // namespace

// ── §1 core lifecycle (ctor/dtor) ──
container_skia::container_skia(int w, int h, SkCanvas* canvas, SatoruContext& context,
                               ResourceManager* rm, bool tagging, litehtml::media_type media_type)
    : m_canvas(canvas),
      m_width(w),
      m_height(h),
      m_context(context),
      m_resourceManager(rm),
      m_tagging(tagging),
      m_media_type(media_type) {
    // P2a: bidi level 初期値 (-1) は TextState のメンバ初期化子へ移動。
    m_text.asciiUsed.resize(128, false);
    m_text.batcher = new satoru::TextBatcher(&m_context, m_canvas);
}

container_skia::~container_skia() {
    if (m_text.batcher) {
        m_text.batcher->flush();
        delete m_text.batcher;
    }
}

// ── §2 text (create_font/delete_font/text_width/draw_text/bidi/split) ──
litehtml::uint_ptr container_skia::create_font(const litehtml::font_description& desc,
                                               const litehtml::document* doc,
                                               litehtml::font_metrics* fm) {
    auto profile_start = std::chrono::high_resolution_clock::time_point{};
    if (m_context.layoutProfile.enabled) {
        profile_start = std::chrono::high_resolution_clock::now();
        m_context.layoutProfile.container_create_font_count++;
    }
    SkFontStyle::Slant slant = desc.style == litehtml::font_style_normal
                                   ? SkFontStyle::kUpright_Slant
                                   : SkFontStyle::kItalic_Slant;

    std::vector<sk_sp<SkTypeface>> typefaces;
    bool fake_bold = false;
    std::vector<std::string> requestedFamilies;

    std::stringstream ss(desc.family);
    std::string item;
    while (std::getline(ss, item, ',')) {
        std::string family = trim(item);
        if (family.empty()) continue;

        bool fb = false;
        auto tfs = m_context.get_typefaces(family, desc.weight, slant, fb, desc.stretch);
        for (auto& tf : tfs) {
            typefaces.push_back(tf);
        }
        if (fb) fake_bold = true;

        if (m_resourceManager) {
            font_request req;
            req.family = family;
            req.weight = desc.weight;
            req.slant = slant;
            m_text.requestedAttrs.insert(req);
            requestedFamilies.push_back(family);
        }
    }

    if (typefaces.empty()) {
        m_text.missingFonts.insert({desc.family, desc.weight, slant});
        typefaces = m_context.get_typefaces("sans-serif", desc.weight, slant, fake_bold, desc.stretch);
    }

    // font-optical-sizing: auto drives the opsz axis from the font size (CSS px); an explicit
    // font-variation-settings "opsz" always wins.
    std::vector<litehtml::font_variation_setting> variations = desc.variations;
    if (desc.optical_sizing_auto) {
        const uint32_t opsz = SkSetFourByteTag('o', 'p', 's', 'z');
        bool explicit_opsz = false;
        for (const auto& v : variations) explicit_opsz = explicit_opsz || v.tag == opsz;
        if (!explicit_opsz) {
            litehtml::font_variation_setting v;
            v.tag = opsz;
            v.value = (float)desc.size;
            variations.push_back(v);
        }
    }

    font_info* fi = new font_info;
    fi->desc = desc;
    fi->fake_bold = fake_bold;
    fi->selected_font_cache.reserve(256);
    fi->glyph_width_cache.reserve(256);

    // Check direction from element's property (if available)
    fi->is_rtl = false;

    for (auto& typeface : typefaces) {
        SkFont* font = m_context.fontManager.createSkFont(typeface, (float)desc.size, desc.weight,
                                                          desc.stretch, variations);
        if (font) {
            fi->fonts.push_back(font);
        }
    }

    // Add global fallbacks
    for (auto& tf : m_context.fontManager.getFallbackTypefaces()) {
        bool duplicate = false;
        for (auto& existing : fi->fonts) {
            if (existing->getTypeface()->uniqueID() == tf->uniqueID()) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            SkFont* font = m_context.fontManager.createSkFont(tf, (float)desc.size, desc.weight,
                                                               desc.stretch, variations);
            if (font) {
                fi->fonts.push_back(font);
            }
        }
    }

    if (fi->fonts.empty()) {
        sk_sp<SkTypeface> def = m_context.fontManager.getDefaultTypeface();
        if (def) {
            fi->fonts.push_back(m_context.fontManager.createSkFont(def, (float)desc.size, desc.weight,
                                                                     desc.stretch, variations));
        } else {
            fi->fonts.push_back(new SkFont(SkTypeface::MakeEmpty(), (float)desc.size));
        }
    }

    fi->fake_italic = false;
    if (slant == SkFontStyle::kItalic_Slant) {
        fi->fake_italic = true;
    }

    if (!fi->fonts.empty()) {
        auto typeface = fi->fonts[0]->getTypeface();
        // Use getMatchedWeight to get the INTENDED weight (handles subset fonts with broken
        // metadata)
        int actual_weight =
            m_context.fontManager.getMatchedWeight(sk_ref_sp(typeface), desc.family);

        int actual_slant = m_context.fontManager.getMatchedSlant(sk_ref_sp(typeface), desc.family);

        if (actual_slant == SkFontStyle::kItalic_Slant) {
            fi->fake_italic = false;
        }

        // font-synthesis: no synthetic bold / italic when the property turns it off.
        if (!(desc.synthesis & 1)) fi->fake_bold = false;
        if (!(desc.synthesis & 2)) fi->fake_italic = false;

        // If the matched weight is sufficient, disable fake_bold
        if (actual_weight >= desc.weight) {
            fi->fake_bold = false;
        } else {
            // Check if it's a variable font by looking for variation axes
            int count = typeface->getVariationDesignPosition(
                SkSpan<SkFontArguments::VariationPosition::Coordinate>());
            if (count > 0) {
                // If it's a variable font, we trust that createSkFont handled the weight axis
                fi->fake_bold = false;
            }
        }
    }

    SkFontMetrics skfm;
    fi->fonts[0]->getMetrics(&skfm);
    // Blink rounds the ascent and the descent to whole pixels separately (and the line gap too), which
    // is what places the baseline of every line of text.
    float ascent = std::round(-skfm.fAscent);
    float descent = std::round(skfm.fDescent);
    float leading = std::round(skfm.fLeading);

    if (fm) {
        float css_line_height = ascent + descent + leading;
        if (css_line_height <= 0) css_line_height = (float)desc.size * 1.2f;

        fm->font_size = (float)desc.size;
        fm->ascent = ascent;
        fm->descent = descent;
        fm->height = css_line_height;
        fm->x_height = skfm.fXHeight;
        // Same baseline shifts as Blink: superscripts rise by a third of the font size and
        // subscripts drop by a fifth.
        fm->super_shift = (float)desc.size / 3.0f + 1.0f;
        fm->sub_shift = (float)desc.size / 5.0f + 1.0f;
        fm->ch_width = (litehtml::pixel_t)fi->fonts[0]->measureText("0", 1, SkTextEncoding::kUTF8);
    }
    float css_line_height = ascent + descent + leading;
    if (css_line_height <= 0) css_line_height = (float)desc.size * 1.2f;

    fi->fm_ascent = (int)(ascent + (css_line_height - (ascent + descent)) / 2.0f + 1.0f);
    fi->fm_ascent_raw = ascent;
    fi->fm_height = (int)css_line_height;

    // Font underline metrics (used by text-underline-position: from-font and
    // text-decoration-thickness: from-font).
    // Skia reports the underline position as the distance from the baseline to
    // the top of the underline stroke; it is only valid when the flag is set.
    fi->fm_underline_position = 0.0f;
    if (skfm.hasUnderlinePosition(&skfm.fUnderlinePosition) && skfm.fUnderlinePosition > 0) {
        fi->fm_underline_position = skfm.fUnderlinePosition;
    }
    fi->fm_underline_thickness = 0.0f;
    if (skfm.hasUnderlineThickness(&skfm.fUnderlineThickness) &&
        skfm.fUnderlineThickness > 0) {
        fi->fm_underline_thickness = skfm.fUnderlineThickness;
    }

    if (requestedFamilies.empty()) {
        requestedFamilies.push_back(desc.family);
    }
    for (const auto& family : requestedFamilies) {
        font_request req;
        req.family = family;
        req.weight = desc.weight;
        req.slant = slant;
        fi->requests.push_back(req);
        m_text.createdFonts[req].push_back(fi);
    }

    if (m_context.layoutProfile.enabled) {
        auto profile_end = std::chrono::high_resolution_clock::now();
        m_context.layoutProfile.container_create_font_ms +=
            std::chrono::duration<double, std::milli>(profile_end - profile_start).count();
    }
    return (litehtml::uint_ptr)fi;
}

void container_skia::delete_font(litehtml::uint_ptr hFont) {
    font_info* fi = (font_info*)hFont;
    if (fi) {
        for (auto& entry : m_text.createdFonts) {
            auto& v = entry.second;
            v.erase(std::remove(v.begin(), v.end(), fi), v.end());
        }
        for (auto font : fi->fonts) delete font;
        delete fi;
    }
}

litehtml::pixel_t container_skia::text_width(const char* text, litehtml::uint_ptr hFont,
                                             litehtml::direction dir, litehtml::writing_mode mode) {
    auto profile_start = std::chrono::high_resolution_clock::time_point{};
    if (m_context.layoutProfile.enabled) {
        profile_start = std::chrono::high_resolution_clock::now();
        m_context.layoutProfile.container_text_width_count++;
        std::string profile_key;
        profile_key.reserve(strlen(text ? text : "") + 64);
        profile_key.append(std::to_string(hFont));
        profile_key.push_back('|');
        profile_key.append(std::to_string((int)dir));
        profile_key.push_back('|');
        profile_key.append(std::to_string((int)mode));
        profile_key.push_back('|');
        if (text) profile_key.append(text);
        if (m_context.layoutProfile.container_text_width_keys.insert(profile_key).second) {
            m_context.layoutProfile.container_text_width_unique_count++;
        } else {
            m_context.layoutProfile.container_text_width_duplicate_count++;
        }
    }
    font_info* fi = (font_info*)hFont;
    if (fi) {
        fi->is_rtl = (dir == litehtml::direction_rtl);
        if (m_resourceManager && !fi->requests.empty()) {
            const char32_t emphasis_cp = satoru::TextRenderer::emphasisCodepoint(fi);
            if (fi->requests.size() == 1) {
                collect_text_codepoints(m_context, text, m_text.measuredCodepoints[fi->requests[0]]);
                if (emphasis_cp)
                    m_text.measuredCodepoints[fi->requests[0]].insert(emphasis_cp);
            } else {
                auto codepoints = decode_text_codepoints(m_context, text);
                if (emphasis_cp) codepoints.push_back(emphasis_cp);
                for (const auto& req : fi->requests) {
                    auto& measured = m_text.measuredCodepoints[req];
                    measured.insert(codepoints.begin(), codepoints.end());
                }
            }
        }
    }
    auto result = satoru::TextLayout::measureText(&m_context, text, fi, mode, -1.0,
                                                  m_resourceManager ? &m_text.usedCodepoints : nullptr);
    if (m_context.layoutProfile.enabled) {
        auto profile_end = std::chrono::high_resolution_clock::now();
        m_context.layoutProfile.container_text_width_ms +=
            std::chrono::duration<double, std::milli>(profile_end - profile_start).count();
    }
    return (litehtml::pixel_t)result.width;
}

void container_skia::draw_text(litehtml::uint_ptr hdc, const char* text, litehtml::uint_ptr hFont,
                               litehtml::web_color color, const litehtml::position& pos,
                               litehtml::text_overflow overflow, litehtml::direction dir,
                               litehtml::writing_mode mode) {
    if (!m_canvas) return;
    font_info* fi = (font_info*)hFont;
    if (!fi || fi->fonts.empty()) return;

    litehtml::position actual_pos = pos;
    // litehtml collapses a run that must be followed by an ellipsis to ~0 width.
    const bool force_ellipsis = overflow == litehtml::text_overflow_ellipsis && pos.width < 1.0f;
    if (overflow == litehtml::text_overflow_ellipsis) {
        if (!m_clip.clips.empty()) {
            // Clip right edge is the real available width for this line.
            litehtml::pixel_t clip_right = m_clip.clips.back().first.right();
            if (pos.width < 1.0f) {
                // litehtml collapses a force-ellipsis run to width 0.1. Recover the
                // real width from the clip box so the visible prefix is kept and
                // only the ellipsis is appended (instead of drawing "..." alone).
                actual_pos.width = std::max<litehtml::pixel_t>(clip_right - pos.x, 0.0f);
            } else {
                actual_pos.width = std::min(pos.width, clip_right - pos.x);
            }
        }
    }

    // background-clip: text support for PNG rendering
    // When text is transparent and there's a pending text-clip gradient,
    // use saveLayer + SrcIn compositing to fill text shapes with the gradient
    if (!m_tagging && color.alpha == 0 && !m_paint.pendingTextClips.empty()) {
        const pending_text_clip* best_tc = nullptr;
        float max_overlap_area = 0;
        for (const auto& tc : m_paint.pendingTextClips) {
            const auto& clip = tc.layer.clip_box;
            if (actual_pos.x < clip.right() && actual_pos.right() > clip.x &&
                actual_pos.y < clip.bottom() && actual_pos.bottom() > clip.y) {
                float ix = std::max((float)actual_pos.x, (float)clip.x);
                float iy = std::max((float)actual_pos.y, (float)clip.y);
                float ir = std::min((float)actual_pos.right(), (float)clip.right());
                float ib = std::min((float)actual_pos.bottom(), (float)clip.bottom());
                float area = (ir - ix) * (ib - iy);
                if (area > max_overlap_area) {
                    max_overlap_area = area;
                    best_tc = &tc;
                }
            }
        }
        if (best_tc) {
            const auto& tc = *best_tc;
            const auto& clip = tc.layer.clip_box;
            flush();

            SkRect bounds = SkRect::MakeXYWH((float)clip.x, (float)clip.y, (float)clip.width,
                                             (float)clip.height);
            float outset_amount = std::max(50.0f, (float)fi->desc.size * 0.5f);
            bounds.outset(outset_amount, outset_amount);
            m_canvas->saveLayer(bounds, nullptr);

            // Draw text as opaque white mask
            litehtml::web_color maskColor;
            maskColor.red = 255;
            maskColor.green = 255;
            maskColor.blue = 255;
            maskColor.alpha = 255;
            satoru::TextRenderer::drawText(
                &m_context, m_canvas, text, fi, maskColor, actual_pos, overflow, dir, mode, false,
                get_current_opacity(), m_paint.textShadows, m_paint.textDraws, m_usedGlyphs,
                m_usedGlyphDraws, m_resourceManager ? &m_text.usedCodepoints : nullptr, m_text.batcher, force_ellipsis);
            flush();

            // Draw gradient with SrcIn blend mode (only visible through text mask)
            const auto& gradient = tc.gradient;
            SkPoint pts[2] = {SkPoint::Make((float)gradient.start.x, (float)gradient.start.y),
                              SkPoint::Make((float)gradient.end.x, (float)gradient.end.y)};
            std::vector<SkColor4f> colors;
            std::vector<float> positions;
            for (const auto& stop : gradient.color_points) {
                colors.push_back({stop.color.red / 255.0f, stop.color.green / 255.0f,
                                  stop.color.blue / 255.0f, stop.color.alpha / 255.0f});
                positions.push_back(stop.offset);
            }
            SkGradient sk_grad(
                SkGradient::Colors(SkSpan(colors), SkSpan(positions), SkTileMode::kClamp),
                SkGradient::Interpolation());
            SkPaint gradPaint;
            gradPaint.setShader(SkShaders::LinearGradient(pts, sk_grad));
            gradPaint.setBlendMode(SkBlendMode::kSrcIn);
            gradPaint.setAntiAlias(true);
            m_canvas->drawRect(bounds, gradPaint);

            m_canvas->restore();

            // Resource manager pass for font subsetting
            if (fi && m_resourceManager) {
                satoru::TextRenderer::drawText(&m_context, nullptr, text, fi, maskColor, actual_pos,
                                               overflow, dir, mode, false, get_current_opacity(),
                                               m_paint.textShadows, m_paint.textDraws, m_usedGlyphs,
                                               m_usedGlyphDraws, &fi->used_codepoints, nullptr, force_ellipsis);
            }
            return;
        }
    }

    satoru::TextRenderer::drawText(&m_context, m_canvas, text, fi, color, actual_pos, overflow, dir,
                                   mode, m_tagging, get_current_opacity(), m_paint.textShadows,
                                   m_paint.textDraws, m_usedGlyphs, m_usedGlyphDraws,
                                   m_resourceManager ? &m_text.usedCodepoints : nullptr, m_text.batcher, force_ellipsis);
    std::string s(text);
    if (!s.empty() && s.find_first_not_of(" \n\r\t") != std::string::npos) {
        if (!m_clip.clips.empty()) {
            SkRect cb = m_canvas->getLocalClipBounds();
        }
    }
    if (fi && m_resourceManager) {
        satoru::TextRenderer::drawText(&m_context, nullptr, text, fi, color, actual_pos, overflow,
                                       dir, mode, m_tagging, get_current_opacity(),
                                       m_paint.textShadows, m_paint.textDraws, m_usedGlyphs,
                                       m_usedGlyphDraws, &fi->used_codepoints, nullptr, force_ellipsis);
    }
}

// ── §3 paint → container_skia_paint.cpp に移動 (P4: 関数移動のみ) ──

// ── §4 misc document_container I/F (viewport/media/language/css/color/...) ──
litehtml::pixel_t container_skia::pt_to_px(float pt) const { return pt * 96.0f / 72.0f; }
litehtml::pixel_t container_skia::get_default_font_size() const { return 16.0f; }
const char* container_skia::get_default_font_name() const { return "sans-serif"; }

int container_skia::get_bidi_level(const char* text, int base_level) {
    if (m_text.lastBaseLevel != base_level) {
        m_text.lastBidiLevel = base_level;
        m_text.lastBaseLevel = base_level;
    }

    if (!text || !*text) return base_level;
    const unsigned char c0 = static_cast<unsigned char>(text[0]);
    if (c0 < 0x80) {
        bool is_ascii_neutral = c0 <= 0x2F || (c0 >= 0x3A && c0 <= 0x40) ||
                                (c0 >= 0x5B && c0 <= 0x60) || (c0 >= 0x7B && c0 <= 0x7F);
        if (!is_ascii_neutral) {
            int level = (base_level == 1) ? 2 : 0;
            m_text.lastBidiLevel = level;
            return level;
        }
    } else {
        const unsigned char c1 = static_cast<unsigned char>(text[1]);
        if ((c0 >= 0xE4 && c0 <= 0xE9) ||                // CJK Unified Ideographs (U+4E00-U+9FFF)
            (c0 == 0xE3 && c1 >= 0x81 && c1 <= 0x83) ||  // Hiragana/Katakana
            (c0 == 0xE3 && c1 >= 0x90) ||                // CJK Extension A (U+3400-U+3FFF)
            (c0 >= 0xEA && c0 <= 0xED)) {                // Hangul
            int level = (base_level == 1) ? 2 : 0;
            m_text.lastBidiLevel = level;
            return level;
        }
    }
    return m_context.getUnicodeService().getBidiLevel(text, base_level, &m_text.lastBidiLevel);
}

// paint: draw_list_marker → container_skia_paint.cpp に移動 (P4: 関数移動のみ)。

void container_skia::load_image(const char* src, const char* baseurl, bool redraw_on_ready) {
    if (m_resourceManager && src && *src)
        m_resourceManager->request(src, src, ResourceType::Image, redraw_on_ready);
}
void container_skia::get_image_size(const char* src, const char* baseurl, litehtml::size& sz) {
    int w, h;
    if (m_context.get_image_size(src, w, h)) {
        sz.width = w;
        sz.height = h;
    } else {
        sz.width = 0;
        sz.height = 0;
    }
}
bool container_skia::get_image_ratio_only(const char* src, const char* baseurl) {
    return src && m_context.get_image_ratio_only(src);
}
void container_skia::get_viewport(litehtml::position& viewport) const {
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = m_width;
    viewport.height = m_height;
}
void container_skia::transform_text(litehtml::string& text, litehtml::text_transform tt) {
    if (text.empty() || tt == litehtml::text_transform_none) return;
    std::string out;
    out.reserve(text.size());
    const auto* p = (const utf8proc_uint8_t*)text.data();
    utf8proc_ssize_t remaining = (utf8proc_ssize_t)text.size();
    bool word_start = true;
    while (remaining > 0) {
        utf8proc_int32_t cp;
        utf8proc_ssize_t n = utf8proc_iterate(p, remaining, &cp);
        if (n < 0) {  // invalid byte: copy as-is
            out.push_back((char)*p);
            p++;
            remaining--;
            word_start = false;
            continue;
        }
        utf8proc_int32_t mapped = cp;
        switch (tt) {
            case litehtml::text_transform_uppercase:
                mapped = utf8proc_toupper(cp);
                break;
            case litehtml::text_transform_lowercase:
                mapped = utf8proc_tolower(cp);
                break;
            case litehtml::text_transform_capitalize:
                if (word_start && utf8proc_category(cp) >= UTF8PROC_CATEGORY_LU &&
                    utf8proc_category(cp) <= UTF8PROC_CATEGORY_LO) {
                    mapped = utf8proc_totitle(cp);
                }
                break;
            default:
                break;
        }
        // Words are runs of letters/numbers/marks (an apostrophe stays inside a word).
        auto cat = utf8proc_category(cp);
        bool in_word = (cat >= UTF8PROC_CATEGORY_LU && cat <= UTF8PROC_CATEGORY_LO) ||
                       (cat >= UTF8PROC_CATEGORY_MN && cat <= UTF8PROC_CATEGORY_ME) ||
                       (cat >= UTF8PROC_CATEGORY_ND && cat <= UTF8PROC_CATEGORY_NO) ||
                       cp == '\'' || cp == 0x2019 || cp == '_';
        word_start = !in_word;
        utf8proc_uint8_t buf[4];
        utf8proc_ssize_t len = utf8proc_encode_char(mapped, buf);
        if (len <= 0) {
            out.append((const char*)p, (size_t)n);
        } else {
            out.append((const char*)buf, (size_t)len);
        }
        p += n;
        remaining -= n;
    }
    text = std::move(out);
}
// Called for a <link> whose stylesheet is fetched later (see import_css). Remember its media
// attribute so the sheet is applied only where the media query matches.
void container_skia::link(const std::shared_ptr<litehtml::document>& /*doc*/,
                          const litehtml::element::ptr& el) {
    if (!el || !m_resourceManager) return;
    const char* rel = el->get_attr("rel");
    const char* href = el->get_attr("href");
    const char* media = el->get_attr("media");
    bool isSheet = false;
    if (rel) {
        std::istringstream relTokens(rel);
        std::string tok;
        while (relTokens >> tok) {
            if (tok == "stylesheet") isSheet = true;
            else if (tok == "alternate") { isSheet = false; break; }
        }
    }
    if (isSheet && href && media && href[0] && media[0]) {
        m_resourceManager->setCssMedia(href, media);
    }
}

void container_skia::import_css(litehtml::string& text, const litehtml::string& url,
                                litehtml::string& baseurl) {
    if (!url.empty() && m_resourceManager) {
        if (looks_like_font_url(url)) {
            m_resourceManager->request(url, "", ResourceType::Font);
        } else {
            m_resourceManager->request(url, url, ResourceType::Css);
        }
    } else {
        m_context.fontManager.scanFontFaces(text);
    }
}

void container_skia::get_media_features(litehtml::media_features& features) const {
    features.type = m_media_type;
    features.width = m_width;
    features.height = m_height;
    features.device_width = m_width;
    features.device_height = m_height;
    features.color = 8;
    features.monochrome = 0;
    features.color_index = 256;
    features.resolution = 96;
}
void container_skia::get_language(litehtml::string& language, litehtml::string& culture) const {
    language = "en";
    culture = "en-US";
}

litehtml::string container_skia::resolve_color(const litehtml::string& color) const {
    if (color.empty()) return "";

    std::string name = color;
    if (name.size() > 4 && name.substr(0, 4) == "var(") {
        size_t pos = name.find('(');
        size_t end = name.find_last_of(')');
        if (pos != std::string::npos && end != std::string::npos && end > pos + 1) {
            name = name.substr(pos + 1, end - pos - 1);
            // Handle fallback if present (comma)
            size_t comma = name.find(',');
            if (comma != std::string::npos) {
                name = name.substr(0, comma);
            }
            // Trim whitespace
            name.erase(0, name.find_first_not_of(" \t\r\n"));
            size_t last = name.find_last_not_of(" \t\r\n");
            if (last != std::string::npos) {
                name.erase(last + 1);
            }
        }
    }

    if (name.substr(0, 2) == "--") {
        if (m_doc && m_doc->root()) {
            litehtml::css_token_vector tokens;
            if (m_doc->root()->get_custom_property(litehtml::_id(name), tokens)) {
                return litehtml::get_repr(tokens);
            }
        }
    }
    return "";
}

void container_skia::split_text(const char* text, const std::function<void(const char*)>& on_word,
                                const std::function<void(const char*)>& on_space) {
    auto profile_start = std::chrono::high_resolution_clock::time_point{};
    if (m_context.layoutProfile.enabled) {
        profile_start = std::chrono::high_resolution_clock::now();
        m_context.layoutProfile.container_split_text_count++;
    }
    satoru::TextLayout::splitText(&m_context, text, on_word, on_space);
    if (m_context.layoutProfile.enabled) {
        auto profile_end = std::chrono::high_resolution_clock::now();
        m_context.layoutProfile.container_split_text_ms +=
            std::chrono::duration<double, std::milli>(profile_end - profile_start).count();
    }
}

SkBlendMode container_skia::to_skia_blend_mode(litehtml::blend_mode bm) {
    // P2a: 実体は container_skia_helpers.cpp の無状態版へ一本化 (完全一致のため委譲)。
    // 公開シグネチャ・返却値不変。
    return ::to_skia_blend_mode(bm);
}

litehtml::element::ptr container_skia::create_element(
    const char* tag_name, const litehtml::string_map& attributes,
    const std::shared_ptr<litehtml::document>& doc) {
    std::string tag = tag_name;
    if (tag == "table") {
        return std::make_shared<litehtml::el_table>(doc);
    }
    if (tag == "tr") {
        return std::make_shared<litehtml::el_tr>(doc);
    }
    if (tag == "td" || tag == "th") {
        return std::make_shared<litehtml::el_td>(doc);
    }
    if (tag == "svg") {
        return std::make_shared<litehtml::el_svg>(doc);
    }
    if (tag == "pre" || tag == "div") {
        // <pre class="mermaid"> / <div class="mermaid">: a diagram that the engine draws itself
        auto cls = attributes.find("class");
        if (cls != attributes.end()) {
            std::string v = " " + cls->second + " ";
            for (char& ch : v)
                if (ch == '\t' || ch == '\n' || ch == '\r') ch = ' ';
            if (v.find(" mermaid ") != std::string::npos) return std::make_shared<litehtml::el_mermaid>(doc);
        }
    }
    return nullptr;
}

void container_skia::collect_used_font_characters(const font_request& req,
                                                  std::vector<char32_t>& out) const {
    auto it = m_text.createdFonts.find(req);
    if (it == m_text.createdFonts.end()) return;
    for (auto fi : it->second) {
        out.insert(out.end(), fi->used_codepoints.begin(), fi->used_codepoints.end());
    }
}

void container_skia::collect_measured_font_characters(const font_request& req,
                                                      std::vector<char32_t>& out) const {
    auto it = m_text.measuredCodepoints.find(req);
    if (it == m_text.measuredCodepoints.end()) return;
    out.insert(out.end(), it->second.begin(), it->second.end());
}

const std::set<char32_t>* container_skia::get_measured_font_codepoints(
    const font_request& req) const {
    auto it = m_text.measuredCodepoints.find(req);
    if (it == m_text.measuredCodepoints.end() || it->second.empty()) return nullptr;
    return &it->second;
}
