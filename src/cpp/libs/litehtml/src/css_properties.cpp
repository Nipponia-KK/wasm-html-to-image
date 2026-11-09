#include "css_properties.h"
#include "css_parser.h"

#include <algorithm>
#include <cmath>

#include "css_tokenizer.h"
#include "document.h"
#include "document_container.h"
#include "html.h"
#include "html_tag.h"
#include "types.h"

#define offset(member) ((uint_ptr) & this->member - (uint_ptr)this)
// #define offset(func)  [](const css_properties& css) { return css.func; }

// No external depth tracking needed now

namespace litehtml
{
namespace
{
// True when the inline (text flow) axis runs backwards and the line's items must be mirrored
// along it. Only sideways-lr does so: it reads bottom-to-top, while sideways-rl and the
// vertical-* modes read top-to-bottom. A vertical writing mode with direction:rtl only aligns
// the line to the inline end for the LTR runs (CJK/Latin) this engine lays out; it does NOT
// reverse the character order, so it is deliberately not mirrored (see Chromium, case 65).
// Kept here so the layout code consumes a plain flag and `direction` never leaks into
// logical_geometry.h. Takes the *original* writing mode, before sideways-rl/lr are folded into
// vertical-rl/lr below. `direction` is part of the predicate's shape for that reason.
bool is_inline_reverse(writing_mode mode, direction direction_unused)
{
    (void) direction_unused;
    return mode == writing_mode_sideways_lr;
}

// ── OpenType feature resolution (font-variant-* / font-feature-settings) ──
uint32_t ot_tag(char a, char b, char c, char d)
{
    return ((uint32_t)(uint8_t)a << 24) | ((uint32_t)(uint8_t)b << 16) |
           ((uint32_t)(uint8_t)c << 8) | (uint32_t)(uint8_t)d;
}

void add_ot_feature(std::vector<font_feature_setting>& out, uint32_t tag, uint32_t value)
{
    out.push_back({tag, value});
}

// group: 0=ligatures 1=numeric 2=east-asian 3=caps 4=position
void append_font_variant_features(std::vector<font_feature_setting>& out, const string& value,
                                  int group)
{
    size_t pos = 0;
    while (pos < value.size()) {
        size_t end = value.find(' ', pos);
        string kw = value.substr(pos, end == string::npos ? string::npos : end - pos);
        pos = end == string::npos ? value.size() : end + 1;
        if (kw.empty() || kw == "normal") continue;

        switch (group) {
            case 0:
                if (kw == "none") {
                    add_ot_feature(out, ot_tag('l', 'i', 'g', 'a'), 0);
                    add_ot_feature(out, ot_tag('c', 'l', 'i', 'g'), 0);
                    add_ot_feature(out, ot_tag('d', 'l', 'i', 'g'), 0);
                    add_ot_feature(out, ot_tag('h', 'l', 'i', 'g'), 0);
                    add_ot_feature(out, ot_tag('c', 'a', 'l', 't'), 0);
                } else if (kw == "common-ligatures") {
                    add_ot_feature(out, ot_tag('l', 'i', 'g', 'a'), 1);
                    add_ot_feature(out, ot_tag('c', 'l', 'i', 'g'), 1);
                } else if (kw == "no-common-ligatures") {
                    add_ot_feature(out, ot_tag('l', 'i', 'g', 'a'), 0);
                    add_ot_feature(out, ot_tag('c', 'l', 'i', 'g'), 0);
                } else if (kw == "discretionary-ligatures") {
                    add_ot_feature(out, ot_tag('d', 'l', 'i', 'g'), 1);
                } else if (kw == "no-discretionary-ligatures") {
                    add_ot_feature(out, ot_tag('d', 'l', 'i', 'g'), 0);
                } else if (kw == "historical-ligatures") {
                    add_ot_feature(out, ot_tag('h', 'l', 'i', 'g'), 1);
                } else if (kw == "no-historical-ligatures") {
                    add_ot_feature(out, ot_tag('h', 'l', 'i', 'g'), 0);
                } else if (kw == "contextual") {
                    add_ot_feature(out, ot_tag('c', 'a', 'l', 't'), 1);
                } else if (kw == "no-contextual") {
                    add_ot_feature(out, ot_tag('c', 'a', 'l', 't'), 0);
                }
                break;
            case 1:
                if (kw == "ordinal") add_ot_feature(out, ot_tag('o', 'r', 'd', 'n'), 1);
                else if (kw == "slashed-zero") add_ot_feature(out, ot_tag('z', 'e', 'r', 'o'), 1);
                else if (kw == "lining-nums") add_ot_feature(out, ot_tag('l', 'n', 'u', 'm'), 1);
                else if (kw == "oldstyle-nums") add_ot_feature(out, ot_tag('o', 'n', 'u', 'm'), 1);
                else if (kw == "proportional-nums") add_ot_feature(out, ot_tag('p', 'n', 'u', 'm'), 1);
                else if (kw == "tabular-nums") add_ot_feature(out, ot_tag('t', 'n', 'u', 'm'), 1);
                else if (kw == "diagonal-fractions") add_ot_feature(out, ot_tag('f', 'r', 'a', 'c'), 1);
                else if (kw == "stacked-fractions") add_ot_feature(out, ot_tag('a', 'f', 'r', 'c'), 1);
                break;
            case 2:
                if (kw == "jis78") add_ot_feature(out, ot_tag('j', 'p', '7', '8'), 1);
                else if (kw == "jis83") add_ot_feature(out, ot_tag('j', 'p', '8', '3'), 1);
                else if (kw == "jis90") add_ot_feature(out, ot_tag('j', 'p', '9', '0'), 1);
                else if (kw == "jis04") add_ot_feature(out, ot_tag('j', 'p', '0', '4'), 1);
                else if (kw == "simplified") add_ot_feature(out, ot_tag('s', 'm', 'p', 'l'), 1);
                else if (kw == "traditional") add_ot_feature(out, ot_tag('t', 'r', 'a', 'd'), 1);
                else if (kw == "full-width") add_ot_feature(out, ot_tag('f', 'w', 'i', 'd'), 1);
                else if (kw == "proportional-width") add_ot_feature(out, ot_tag('p', 'w', 'i', 'd'), 1);
                else if (kw == "ruby") add_ot_feature(out, ot_tag('r', 'u', 'b', 'y'), 1);
                break;
            case 3:
                if (kw == "small-caps") add_ot_feature(out, ot_tag('s', 'm', 'c', 'p'), 1);
                else if (kw == "all-small-caps") {
                    add_ot_feature(out, ot_tag('s', 'm', 'c', 'p'), 1);
                    add_ot_feature(out, ot_tag('c', '2', 's', 'c'), 1);
                } else if (kw == "petite-caps") add_ot_feature(out, ot_tag('p', 'c', 'a', 'p'), 1);
                else if (kw == "all-petite-caps") {
                    add_ot_feature(out, ot_tag('p', 'c', 'a', 'p'), 1);
                    add_ot_feature(out, ot_tag('c', '2', 'p', 'c'), 1);
                } else if (kw == "unicase") add_ot_feature(out, ot_tag('u', 'n', 'i', 'c'), 1);
                else if (kw == "titling-caps") add_ot_feature(out, ot_tag('t', 'i', 't', 'l'), 1);
                break;
            case 4:
                if (kw == "sub") add_ot_feature(out, ot_tag('s', 'u', 'b', 's'), 1);
                else if (kw == "super") add_ot_feature(out, ot_tag('s', 'u', 'p', 's'), 1);
                break;
            default:
                break;
        }
    }
}

void append_font_feature_settings(std::vector<font_feature_setting>& out,
                                  const css_token_vector& tokens)
{
    size_t i = 0;
    while (i < tokens.size()) {
        if (tokens[i].type != STRING) {
            ++i;
            continue;
        }
        const string& tag = tokens[i].str;
        uint32_t value = 1;
        size_t j = i + 1;
        if (j < tokens.size() && tokens[j].type != COMMA) {
            if (tokens[j].type == IDENT) {
                string id = lowcase(tokens[j].ident());
                if (id == "off") value = 0;
            } else if (tokens[j].type == NUMBER) {
                value = (uint32_t)(int)tokens[j].n.number;
            }
            ++j;
        }
        if (tag.size() == 4) {
            add_ot_feature(out, ot_tag(tag[0], tag[1], tag[2], tag[3]), value);
        }
        while (j < tokens.size() && tokens[j].type != STRING) ++j;
        i = j;
    }
}

// font-variation-settings: a list of <string> <number> pairs. The tag is a
// 4 character axis name (e.g. "wght"), the value the position on that axis.
void append_font_variation_settings(std::vector<font_variation_setting>& out,
                                    const css_token_vector& tokens)
{
    size_t i = 0;
    while (i < tokens.size()) {
        if (tokens[i].type != STRING || tokens[i].str.size() != 4) {
            ++i;
            continue;
        }
        const string& tag = tokens[i].str;
        float value = 0;
        size_t j = i + 1;
        if (j < tokens.size() && tokens[j].type == NUMBER) {
            value = tokens[j].n.number;
            ++j;
        }
        out.push_back({ot_tag(tag[0], tag[1], tag[2], tag[3]), value});
        while (j < tokens.size() && tokens[j].type != STRING) ++j;
        i = j;
    }
}
}  // namespace
}  // namespace litehtml

void litehtml::css_properties::compute(const element* el, const document::ptr& doc) {
    m_color = el->get_property<web_color>(_color_, true, web_color::black, offset(m_color));
    m_text_fill_color = el->get_property<web_color>(
        __webkit_text_fill_color_, true, web_color(0, 0, 0, 1), offset(m_text_fill_color));

    // -webkit-text-stroke is inherited. Width defaults to 0 (no stroke); color
    // defaults to currentColor. Keep the raw value here (currentColor keyword is
    // preserved) so inheritance propagates the keyword and each element resolves
    // it against its own `color` later in compute_font.
    m_text_stroke_width = el->get_property<css_length>(
        __webkit_text_stroke_width_, true, css_length(), offset(m_text_stroke_width));
    m_text_stroke_color = el->get_property<web_color>(
        __webkit_text_stroke_color_, true, web_color::current_color,
        offset(m_text_stroke_color));

    m_el_position = (element_position)el->get_property<int>(
        _position_, false, element_position_static, offset(m_el_position));
    m_direction =
        (direction)el->get_property<int>(_direction_, true, direction_ltr, offset(m_direction));
    m_unicode_bidi = (unicode_bidi)el->get_property<int>(
        _unicode_bidi_, true, unicode_bidi_normal, offset(m_unicode_bidi));
    m_writing_mode = (writing_mode)el->get_property<int>(
        _writing_mode_, true, writing_mode_horizontal_tb, offset(m_writing_mode));
    m_text_orientation = (text_orientation)el->get_property<int>(
        _text_orientation_, true, text_orientation_mixed, offset(m_text_orientation));
    // Retain whether the inline axis is mirrored before sideways-* is folded away.
    m_inline_reverse = is_inline_reverse(m_writing_mode, m_direction);
    // sideways-rl/lr are vertical flows whose glyphs are all rotated sideways. Fold them into the
    // vertical modes the layout code understands so the two cannot drift apart.
    if (m_writing_mode == writing_mode_sideways_rl || m_writing_mode == writing_mode_sideways_lr) {
        m_writing_mode = m_writing_mode == writing_mode_sideways_rl ? writing_mode_vertical_rl
                                                                    : writing_mode_vertical_lr;
        m_text_orientation = text_orientation_sideways;
    }
    m_text_combine_upright = (text_combine_upright)el->get_property<int>(
        _text_combine_upright_, true, text_combine_upright_none, offset(m_text_combine_upright));

    m_display =
        (style_display)el->get_property<int>(_display_, false, display_inline, offset(m_display));
    // display:flow-root is a block box that always establishes a new block formatting context.
    m_flow_root = (m_display == display_flow_root);
    if (m_flow_root) m_display = display_block;
    m_visibility = (visibility)el->get_property<int>(_visibility_, true, visibility_visible,
                                                     offset(m_visibility));
    m_float = (element_float)el->get_property<int>(_float_, false, float_none, offset(m_float));
    m_clear = (element_clear)el->get_property<int>(_clear_, false, clear_none, offset(m_clear));
    m_appearance = (appearance)el->get_property<int>(_appearance_, false, appearance_none,
                                                     offset(m_appearance));
    m_box_sizing = (box_sizing)el->get_property<int>(_box_sizing_, false, box_sizing_content_box,
                                                     offset(m_box_sizing));
    m_overflow =
        (overflow)el->get_property<int>(_overflow_, false, overflow_visible, offset(m_overflow));
    m_overflow_x =
        (overflow)el->get_property<int>(_overflow_x_, false, m_overflow, offset(m_overflow_x));
    m_overflow_y =
        (overflow)el->get_property<int>(_overflow_y_, false, m_overflow, offset(m_overflow_y));
    // A clipping axis makes the other axis clip too (visible computes to auto), and layout only
    // looks at m_overflow, so promote it when only overflow-x/-y were given.
    if (m_overflow == overflow_visible)
        m_overflow = m_overflow_x != overflow_visible ? m_overflow_x : m_overflow_y;
    m_text_overflow = (text_overflow)el->get_property<int>(
        _text_overflow_, false, text_overflow_clip, offset(m_text_overflow));
    m_text_align = (text_align)el->get_property<int>(_text_align_, true, text_align_start,
                                                     offset(m_text_align));
    m_vertical_align = (vertical_align)el->get_property<int>(_vertical_align_, false, va_baseline,
                                                             offset(m_vertical_align));
    m_text_transform = (text_transform)el->get_property<int>(
        _text_transform_, true, text_transform_none, offset(m_text_transform));
    m_white_space = (white_space)el->get_property<int>(_white_space_, true, white_space_normal,
                                                       offset(m_white_space));
    m_text_wrap =
        (text_wrap)el->get_property<int>(_text_wrap_, true, text_wrap_wrap, offset(m_text_wrap));
    // white-space-collapse / text-wrap-mode are the CSS Text 4 longhands behind
    // white-space. The shorthand parser writes them too, so a longhand that comes
    // later in the cascade wins for its own axis. Derive the longhands from the
    // shorthand first (this also covers `inherit`), then let explicitly cascaded
    // longhands override their axis and fold the pair back onto the legacy enums
    // consumed by layout.
    switch (m_white_space)
    {
      case white_space_pre:
      case white_space_pre_wrap:
        m_white_space_collapse = white_space_collapse_preserve;
        break;
      case white_space_pre_line:
        m_white_space_collapse = white_space_collapse_preserve_breaks;
        break;
      case white_space_break_spaces:
        m_white_space_collapse = white_space_collapse_break_spaces;
        break;
      default:
        m_white_space_collapse = white_space_collapse_collapse;
        break;
    }
    m_text_wrap_mode = (m_white_space == white_space_nowrap || m_white_space == white_space_pre)
                           ? text_wrap_mode_nowrap
                           : text_wrap_mode_wrap;
    m_text_wrap_style = (m_text_wrap == text_wrap_balance) ? text_wrap_style_balance
                        : (m_text_wrap == text_wrap_pretty) ? text_wrap_style_pretty
                                                            : text_wrap_style_auto;
    m_white_space_collapse = (white_space_collapse)el->get_property<int>(
        _white_space_collapse_, true, m_white_space_collapse, offset(m_white_space_collapse));
    m_text_wrap_mode = (text_wrap_mode)el->get_property<int>(
        _text_wrap_mode_, true, m_text_wrap_mode, offset(m_text_wrap_mode));
    m_text_wrap_style = (text_wrap_style)el->get_property<int>(
        _text_wrap_style_, true, m_text_wrap_style, offset(m_text_wrap_style));
    switch (m_white_space_collapse)
    {
      case white_space_collapse_preserve:
        m_white_space = (m_text_wrap_mode == text_wrap_mode_nowrap) ? white_space_pre
                                                                   : white_space_pre_wrap;
        break;
      case white_space_collapse_preserve_breaks:
        m_white_space = white_space_pre_line;
        break;
      case white_space_collapse_preserve_spaces:
      case white_space_collapse_break_spaces:
        m_white_space = white_space_pre_wrap;
        break;
      default:
        m_white_space = (m_text_wrap_mode == text_wrap_mode_nowrap) ? white_space_nowrap
                                                                   : white_space_normal;
        break;
    }
    if (m_text_wrap_style == text_wrap_style_balance)
      m_text_wrap = text_wrap_balance;
    else if (m_text_wrap_style == text_wrap_style_pretty)
      m_text_wrap = text_wrap_pretty;
    else
      m_text_wrap = (m_text_wrap_mode == text_wrap_mode_nowrap) ? text_wrap_nowrap : text_wrap_wrap;
    m_word_break = (word_break)el->get_property<int>(_word_break_, true, word_break_normal,
                                                     offset(m_word_break));
    // CSS Fragmentation. Not inherited. Legacy page-break-* are remapped onto the
    // break-* properties at parse time (style::add_property), so only the modern
    // names are read here.
    m_break_before =
        (break_flow)el->get_property<int>(_break_before_, false, break_flow_auto,
                                          offset(m_break_before));
    m_break_after =
        (break_flow)el->get_property<int>(_break_after_, false, break_flow_auto,
                                          offset(m_break_after));
    m_break_inside =
        (break_inside)el->get_property<int>(_break_inside_, false, break_inside_auto,
                                            offset(m_break_inside));
    // orphans / widows are inherited integers (CSS Fragmentation), default 2.
    m_orphans = el->get_property<int>(_orphans_, true, 2, offset(m_orphans));
    if (m_orphans < 1) m_orphans = 1;
    m_widows = el->get_property<int>(_widows_, true, 2, offset(m_widows));
    if (m_widows < 1) m_widows = 1;
    m_overflow_wrap = (overflow_wrap)el->get_property<int>(
        _overflow_wrap_, true, overflow_wrap_normal, offset(m_overflow_wrap));
    if (m_overflow_wrap == overflow_wrap_normal) {
        m_overflow_wrap = (overflow_wrap)el->get_property<int>(
            _word_wrap_, true, overflow_wrap_normal, offset(m_overflow_wrap));
    }
    m_isolation =
        (isolation)el->get_property<int>(_isolation_, false, isolation_auto, offset(m_isolation));
    m_mix_blend_mode = (blend_mode)el->get_property<int>(_mix_blend_mode_, false, blend_mode_normal,
                                                         offset(m_mix_blend_mode));
    m_background_blend_mode = (blend_mode)el->get_property<int>(
        _background_blend_mode_, false, blend_mode_normal, offset(m_background_blend_mode));
    m_caption_side = (caption_side)el->get_property<int>(_caption_side_, true, caption_side_top,
                                                         offset(m_caption_side));
    m_object_fit = (object_fit)el->get_property<int>(_object_fit_, false, object_fit_fill,
                                                     offset(m_object_fit));
    m_image_rendering = (image_rendering)el->get_property<int>(
        _image_rendering_, true, image_rendering_auto, offset(m_image_rendering));
    m_table_layout = (table_layout)el->get_property<int>(_table_layout_, true, table_layout_auto,
                                                         offset(m_table_layout));

    m_container_type = (container_type)el->get_property<int>(
        _container_type_, false, container_type_none, offset(m_container_type));
    m_container_name =
        el->get_property<string>(_container_name_, false, "", offset(m_container_name));

    m_box_shadow =
        el->get_property<shadow_vector>(_box_shadow_, false, shadow_vector(), offset(m_box_shadow));
    m_text_shadow = el->get_property<shadow_vector>(_text_shadow_, true, shadow_vector(),
                                                    offset(m_text_shadow));
    m_opacity = el->get_property<float>(_opacity_, false, 1.0f, offset(m_opacity));
    m_aspect_ratio = el->get_property<aspect_ratio>(_aspect_ratio_, false, aspect_ratio(),
                                                    offset(m_aspect_ratio));

    m_transform = el->get_property<css_token_vector>(_transform_, false, css_token_vector(),
                                                     offset(m_transform));
    m_rotate =
        el->get_property<css_token_vector>(_rotate_, false, css_token_vector(), offset(m_rotate));
    m_scale =
        el->get_property<css_token_vector>(_scale_, false, css_token_vector(), offset(m_scale));
    m_translate = el->get_property<css_token_vector>(_translate_, false, css_token_vector(),
                                                     offset(m_translate));
    m_transform_origin = el->get_property<css_token_vector>(
        _transform_origin_, false, css_token_vector(), offset(m_transform_origin));
    // CSS Motion Path. Not inherited; resolved at paint time by the container.
    m_offset_path = el->get_property<css_token_vector>(_offset_path_, false,
                                                      css_token_vector(), offset(m_offset_path));
    m_offset_distance = el->get_property<css_token_vector>(
        _offset_distance_, false, css_token_vector(), offset(m_offset_distance));
    m_offset_rotate = el->get_property<css_token_vector>(_offset_rotate_, false,
                                                         css_token_vector(), offset(m_offset_rotate));
    m_offset_anchor = el->get_property<css_token_vector>(_offset_anchor_, false,
                                                         css_token_vector(), offset(m_offset_anchor));
    m_offset_position = el->get_property<css_token_vector>(
        _offset_position_, false, css_token_vector(), offset(m_offset_position));
    m_filter =
        el->get_property<css_token_vector>(_filter_, false, css_token_vector(), offset(m_filter));
    m_backdrop_filter = el->get_property<css_token_vector>(
        _backdrop_filter_, false, css_token_vector(), offset(m_backdrop_filter));
    if (m_backdrop_filter.empty()) {
        m_backdrop_filter = el->get_property<css_token_vector>(
            __webkit_backdrop_filter_, false, css_token_vector(), offset(m_backdrop_filter));
    }
    m_object_position = el->get_property<css_token_vector>(
        _object_position_, false, css_token_vector(), offset(m_object_position));
    m_mask = el->get_property<css_token_vector>(_mask_, false, css_token_vector(), offset(m_mask));
    if (m_mask.empty()) {
        m_mask = el->get_property<css_token_vector>(__webkit_mask_, false, css_token_vector(),
                                                    offset(m_mask));
    }
    const bool mask_from_shorthand = !m_mask.empty();
    // mask-image longhand (same image-layer tokens as the shorthand)
    if (m_mask.empty()) {
        m_mask = el->get_property<css_token_vector>(_mask_image_, false, css_token_vector(),
                                                    offset(m_mask));
    }
    if (m_mask.empty()) {
        m_mask = el->get_property<css_token_vector>(__webkit_mask_image_, false,
                                                    css_token_vector(), offset(m_mask));
    }
    if (!m_mask.empty() && !mask_from_shorthand) {
        // Fold mask-size / mask-repeat / mask-position into shorthand-equivalent layer tokens:
        // <image> <position> [/ <size>] <repeat>  (list entries cycle across layers).
        auto get_list = [&](string_id id, string_id webkit_id) {
            css_token_vector v = el->get_property<css_token_vector>(id, false, css_token_vector(), offset(m_mask));
            if (v.empty()) v = el->get_property<css_token_vector>(webkit_id, false, css_token_vector(), offset(m_mask));
            return parse_comma_separated_list(v);
        };
        auto images = parse_comma_separated_list(m_mask);
        auto sizes = get_list(_mask_size_, __webkit_mask_size_);
        auto repeats = get_list(_mask_repeat_, __webkit_mask_repeat_);
        auto positions = get_list(_mask_position_, __webkit_mask_position_);
        if (!sizes.empty() || !repeats.empty() || !positions.empty()) {
            css_token_vector merged;
            for (size_t i = 0; i < images.size(); i++) {
                if (i > 0) merged.push_back(css_token(css_token_type(',')));
                merged.insert(merged.end(), images[i].begin(), images[i].end());
                const css_token_vector* pos = positions.empty() ? nullptr : &positions[i % positions.size()];
                const css_token_vector* size = sizes.empty() ? nullptr : &sizes[i % sizes.size()];
                if (pos) merged.insert(merged.end(), pos->begin(), pos->end());
                if (size) {
                    merged.push_back(css_token(css_token_type('/')));
                    merged.insert(merged.end(), size->begin(), size->end());
                }
                if (!repeats.empty()) {
                    const auto& rep = repeats[i % repeats.size()];
                    merged.insert(merged.end(), rep.begin(), rep.end());
                }
            }
            m_mask = merged;
        }
    }
    // `url(/a.svg)` without quotes is a URL token; the mask code only knows the function form
    // `url("/a.svg")`, so normalise it (MDN writes its icon masks the unquoted way).
    if (!m_mask.empty()) {
        css_token_vector normalized;
        normalized.reserve(m_mask.size());
        for (const auto& tok : m_mask) {
            if (tok.type == URL) {
                css_token fn(CV_FUNCTION, string("url"));
                fn.value.push_back(css_token(STRING, tok.str));
                fn.repr = tok.repr;
                normalized.push_back(fn);
            } else {
                normalized.push_back(tok);
            }
        }
        m_mask = std::move(normalized);
    }
    // Mask images are requested while styles are computed, like background images: a request
    // made only when the mask is painted comes after the resource-discovery rounds, so the
    // image is never fetched and the masked box stays fully hidden (MDN's icon buttons).
    for (const auto& layer : parse_comma_separated_list(m_mask)) {
        for (const auto& tok : layer) {
            if (tok.type == CV_FUNCTION && lowcase(tok.name) == "url" && !tok.value.empty() &&
                !tok.value.front().str.empty()) {
                doc->container()->load_image(tok.value.front().str.c_str(), "", true);
            }
        }
    }
    m_clip = el->get_property<css_token_vector>(_clip_, false, css_token_vector(), offset(m_clip));

    // https://www.w3.org/TR/CSS22/visuren.html#dis-pos-flo
    if (m_display == display_none) {
        // 1. If 'display' has the value 'none', then 'position' and 'float' do not apply. In this
        // case, the element
        //    generates no box.
        m_float = float_none;
    } else {
        // 2. Otherwise, if 'position' has the value 'absolute' or 'fixed', the box is absolutely
        // positioned,
        //    the computed value of 'float' is 'none', and display is set according to the table
        //    below. The position of the box will be determined by the 'top', 'right', 'bottom' and
        //    'left' properties and the box's containing block.
        if (m_el_position == element_position_absolute || m_el_position == element_position_fixed) {
            m_float = float_none;

            if (m_display == display_inline_table) {
                m_display = display_table;
            } else if (m_display == display_inline_flex) {
                m_display = display_flex;
            } else if (m_display == display_inline_grid) {
                m_display = display_grid;
            } else if (m_display == display_inline || m_display == display_table_row_group ||
                       m_display == display_table_column ||
                       m_display == display_table_column_group ||
                       m_display == display_table_header_group ||
                       m_display == display_table_footer_group || m_display == display_table_row ||
                       m_display == display_table_cell || m_display == display_table_caption ||
                       m_display == display_inline_block) {
                m_display = display_block;
            } else if (m_display == display_webkit_inline_box) {
                m_display = display_webkit_box;
            }
        } else if (m_float != float_none) {
            // 3. Otherwise, if 'float' has a value other than 'none', the box is floated and
            // 'display' is set
            //    according to the table below.
            if (m_display == display_inline_table) {
                m_display = display_table;
            } else if (m_display == display_inline_flex) {
                m_display = display_flex;
            } else if (m_display == display_inline_grid) {
                m_display = display_grid;
            } else if (m_display == display_inline || m_display == display_table_row_group ||
                       m_display == display_table_column ||
                       m_display == display_table_column_group ||
                       m_display == display_table_header_group ||
                       m_display == display_table_footer_group || m_display == display_table_row ||
                       m_display == display_table_cell || m_display == display_table_caption ||
                       m_display == display_inline_block) {
                m_display = display_block;
            } else if (m_display == display_webkit_inline_box) {
                m_display = display_webkit_box;
            }
        } else if (el->is_root()) {
            // 4. Otherwise, if the element is the root element, 'display' is set according to the
            // table below,
            //    except that it is undefined in CSS 2.2 whether a specified value of 'list-item'
            //    becomes a computed value of 'block' or 'list-item'.
            if (m_display == display_inline_table) {
                m_display = display_table;
            } else if (m_display == display_inline || m_display == display_table_row_group ||
                       m_display == display_table_column ||
                       m_display == display_table_column_group ||
                       m_display == display_table_header_group ||
                       m_display == display_table_footer_group || m_display == display_table_row ||
                       m_display == display_table_cell || m_display == display_table_caption ||
                       m_display == display_inline_block || m_display == display_list_item) {
                m_display = display_block;
            } else if (m_display == display_webkit_inline_box) {
                m_display = display_webkit_box;
            }
        } else if (el->is_replaced() && m_display == display_inline) {
            m_display = display_inline_block;
        }
    }
    // 5. Otherwise, the remaining 'display' property values apply as specified.

    compute_font(el, doc);
    {
        css_length def_tab;
        def_tab.set_value(8, css_units_none);
        m_tab_size = el->get_property<css_length>(_tab_size_, true, def_tab, offset(m_tab_size));
        m_tab_size_number = 8;
        m_tab_size_px = -1;
        if (!m_tab_size.is_predefined()) {
            if (m_tab_size.units() == css_units_none) m_tab_size_number = m_tab_size.val();
            else m_tab_size_px = doc->to_pixels(m_tab_size, m_font_metrics, 0);
        }
    }
    pixel_t font_size = get_font_size();

    const css_length _auto = css_length::predef_value(0);
    const css_length none = _auto, normal = _auto;

    m_css_width = el->get_property<css_length>(_width_, false, _auto, offset(m_css_width));
    m_css_height = el->get_property<css_length>(_height_, false, _auto, offset(m_css_height));

    m_css_min_width =
        el->get_property<css_length>(_min_width_, false, _auto, offset(m_css_min_width));
    m_css_min_height =
        el->get_property<css_length>(_min_height_, false, _auto, offset(m_css_min_height));

    m_css_max_width =
        el->get_property<css_length>(_max_width_, false, none, offset(m_css_max_width));
    m_css_max_height =
        el->get_property<css_length>(_max_height_, false, none, offset(m_css_max_height));

    // Logical size properties map onto the physical ones according to the writing mode.
    {
        const bool vertical = m_writing_mode != writing_mode_horizontal_tb;
        struct logical_size_prop {
            string_id inline_id, block_id;
            css_length* inline_target;
            css_length* block_target;
            bool none_default;
        };
        const logical_size_prop props[] = {
            {_inline_size_, _block_size_, vertical ? &m_css_height : &m_css_width,
             vertical ? &m_css_width : &m_css_height, false},
            {_min_inline_size_, _min_block_size_, vertical ? &m_css_min_height : &m_css_min_width,
             vertical ? &m_css_min_width : &m_css_min_height, false},
            {_max_inline_size_, _max_block_size_, vertical ? &m_css_max_height : &m_css_max_width,
             vertical ? &m_css_max_width : &m_css_max_height, true},
        };
        for (const auto& p : props) {
            css_length is = el->get_property<css_length>(p.inline_id, false, _auto, 0);
            if (!is.is_predefined()) *p.inline_target = is;
            css_length bs = el->get_property<css_length>(p.block_id, false, _auto, 0);
            if (!bs.is_predefined()) *p.block_target = bs;
        }
    }

    // The line height is computed before the box lengths below: the lh unit
    // (and through the root, rlh) resolves against it, so it is also stored
    // into the font metrics every later cvt_units for this element reads.
    m_line_height.css_value =
        el->get_property<css_length>(_line_height_, true, normal, offset(m_line_height.css_value));
    if (m_line_height.css_value.is_predefined()) {
        m_line_height.computed_value = (pixel_t)(m_font_metrics.height);
    } else if (m_line_height.css_value.units() == css_units_lh) {
        // lh in the line-height property itself refers to the PARENT's line
        // height (the element's own is what is being defined).
        pixel_t base = el->parent() ? el->parent()->css().line_height().computed_value : 0;
        if (base <= 0) base = m_font_metrics.height;
        m_line_height.computed_value = (pixel_t)(m_line_height.css_value.val() * base);
        m_line_height.css_value = (float)m_line_height.computed_value;
    } else if (m_line_height.css_value.is_calc()) {
        // A calc() length carries css_units_none, so without this branch it
        // fell into the unitless-number case below and computed to 0.
        m_line_height.computed_value =
            doc->to_pixels(m_line_height.css_value, m_font_metrics, m_font_metrics.font_size);
        m_line_height.css_value = (float)m_line_height.computed_value;
    } else if (m_line_height.css_value.units() == css_units_none) {
        m_line_height.computed_value = (pixel_t)(m_line_height.css_value.val() * font_size);
    } else {
        m_line_height.computed_value =
            doc->to_pixels(m_line_height.css_value, m_font_metrics, m_font_metrics.font_size);
        m_line_height.css_value = (float)m_line_height.computed_value;
    }
    m_font_metrics.line_height = m_line_height.computed_value;

    doc->cvt_units(m_css_width, m_font_metrics, 0);
    doc->cvt_units(m_css_height, m_font_metrics, 0);

    doc->cvt_units(m_css_min_width, m_font_metrics, 0);
    doc->cvt_units(m_css_min_height, m_font_metrics, 0);

    doc->cvt_units(m_css_max_width, m_font_metrics, 0);
    doc->cvt_units(m_css_max_height, m_font_metrics, 0);

    string_id _margin_is =
        (m_direction == direction_ltr) ? _margin_inline_start_ : _margin_inline_end_;
    string_id _margin_ie =
        (m_direction == direction_ltr) ? _margin_inline_end_ : _margin_inline_start_;
    string_id _padding_is =
        (m_direction == direction_ltr) ? _padding_inline_start_ : _padding_inline_end_;
    string_id _padding_ie =
        (m_direction == direction_ltr) ? _padding_inline_end_ : _padding_inline_start_;
    string_id _inset_is =
        (m_direction == direction_ltr) ? _inset_inline_start_ : _inset_inline_end_;
    string_id _inset_ie =
        (m_direction == direction_ltr) ? _inset_inline_end_ : _inset_inline_start_;

    if (m_writing_mode == writing_mode_vertical_rl || m_writing_mode == writing_mode_vertical_lr) {
        m_css_margins.top = get_logical_property<css_length>(
            el, _margin_is, _margin_top_, _margin_inline_, 0, false, offset(m_css_margins.top));
        m_css_margins.bottom =
            get_logical_property<css_length>(el, _margin_ie, _margin_bottom_, _margin_inline_, 0,
                                             false, offset(m_css_margins.bottom));
        if (m_writing_mode == writing_mode_vertical_rl) {
            m_css_margins.right = get_logical_property<css_length>(
                el, _margin_block_start_, _margin_right_, _margin_block_, 0, false,
                offset(m_css_margins.right));
            m_css_margins.left = get_logical_property<css_length>(
                el, _margin_block_end_, _margin_left_, _margin_block_, 0, false,
                offset(m_css_margins.left));
        } else {
            m_css_margins.left = get_logical_property<css_length>(
                el, _margin_block_start_, _margin_left_, _margin_block_, 0, false,
                offset(m_css_margins.left));
            m_css_margins.right = get_logical_property<css_length>(
                el, _margin_block_end_, _margin_right_, _margin_block_, 0, false,
                offset(m_css_margins.right));
        }

        m_css_padding.top = get_logical_property<css_length>(
            el, _padding_is, _padding_top_, _padding_block_, 0, false, offset(m_css_padding.top));
        m_css_padding.bottom =
            get_logical_property<css_length>(el, _padding_ie, _padding_bottom_, _padding_block_, 0,
                                             false, offset(m_css_padding.bottom));
        if (m_writing_mode == writing_mode_vertical_rl) {
            m_css_padding.right = get_logical_property<css_length>(
                el, _padding_block_start_, _padding_right_, _padding_block_, 0, false,
                offset(m_css_padding.right));
            m_css_padding.left = get_logical_property<css_length>(
                el, _padding_block_end_, _padding_left_, _padding_block_, 0, false,
                offset(m_css_padding.left));
        } else {
            m_css_padding.left = get_logical_property<css_length>(
                el, _padding_block_start_, _padding_left_, _padding_block_, 0, false,
                offset(m_css_padding.left));
            m_css_padding.right = get_logical_property<css_length>(
                el, _padding_block_end_, _padding_right_, _padding_block_, 0, false,
                offset(m_css_padding.right));
        }
    } else {
        m_css_margins.left = get_logical_property<css_length>(
            el, _margin_is, _margin_left_, _margin_inline_, 0, false, offset(m_css_margins.left));
        m_css_margins.right = get_logical_property<css_length>(
            el, _margin_ie, _margin_right_, _margin_inline_, 0, false, offset(m_css_margins.right));
        m_css_margins.top =
            get_logical_property<css_length>(el, _margin_block_start_, _margin_top_, _margin_block_,
                                             0, false, offset(m_css_margins.top));
        m_css_margins.bottom = get_logical_property<css_length>(
            el, _margin_block_end_, _margin_bottom_, _margin_block_, 0, false,
            offset(m_css_margins.bottom));

        m_css_padding.left =
            get_logical_property<css_length>(el, _padding_is, _padding_left_, _padding_inline_, 0,
                                             false, offset(m_css_padding.left));
        m_css_padding.right =
            get_logical_property<css_length>(el, _padding_ie, _padding_right_, _padding_inline_, 0,
                                             false, offset(m_css_padding.right));
        m_css_padding.top =
            get_logical_property<css_length>(el, _padding_block_start_, _padding_top_,
                                             _padding_block_, 0, false, offset(m_css_padding.top));
        m_css_padding.bottom = get_logical_property<css_length>(
            el, _padding_block_end_, _padding_bottom_, _padding_block_, 0, false,
            offset(m_css_padding.bottom));
    }

    doc->cvt_units(m_css_margins.left, m_font_metrics, font_size);
    doc->cvt_units(m_css_margins.right, m_font_metrics, font_size);
    doc->cvt_units(m_css_margins.top, m_font_metrics, font_size);
    doc->cvt_units(m_css_margins.bottom, m_font_metrics, font_size);

    doc->cvt_units(m_css_padding.left, m_font_metrics, font_size);
    doc->cvt_units(m_css_padding.right, m_font_metrics, font_size);
    doc->cvt_units(m_css_padding.top, m_font_metrics, font_size);
    doc->cvt_units(m_css_padding.bottom, m_font_metrics, font_size);

    string_id _border_is_c =
        (m_direction == direction_ltr) ? _border_inline_start_color_ : _border_inline_end_color_;
    string_id _border_ie_c =
        (m_direction == direction_ltr) ? _border_inline_end_color_ : _border_inline_start_color_;
    string_id _border_is_s =
        (m_direction == direction_ltr) ? _border_inline_start_style_ : _border_inline_end_style_;
    string_id _border_ie_s =
        (m_direction == direction_ltr) ? _border_inline_end_style_ : _border_inline_start_style_;
    string_id _border_is_w =
        (m_direction == direction_ltr) ? _border_inline_start_width_ : _border_inline_end_width_;
    string_id _border_ie_w =
        (m_direction == direction_ltr) ? _border_inline_end_width_ : _border_inline_start_width_;

    if (m_writing_mode == writing_mode_vertical_rl || m_writing_mode == writing_mode_vertical_lr) {
        m_css_borders.top.color = get_logical_property<web_color>(
            el, _border_is_c, _border_top_color_, _border_inline_color_, m_color, false,
            offset(m_css_borders.top.color));
        m_css_borders.bottom.color = get_logical_property<web_color>(
            el, _border_ie_c, _border_bottom_color_, _border_inline_color_, m_color, false,
            offset(m_css_borders.bottom.color));
        m_css_borders.top.style = (border_style)get_logical_property<int>(
            el, _border_is_s, _border_top_style_, _border_inline_style_, border_style_none, false,
            offset(m_css_borders.top.style));
        m_css_borders.bottom.style = (border_style)get_logical_property<int>(
            el, _border_ie_s, _border_bottom_style_, _border_inline_style_, border_style_none,
            false, offset(m_css_borders.bottom.style));
        m_css_borders.top.width = get_logical_property<css_length>(
            el, _border_is_w, _border_top_width_, _border_inline_width_, border_width_medium_value,
            false, offset(m_css_borders.top.width));
        m_css_borders.bottom.width = get_logical_property<css_length>(
            el, _border_ie_w, _border_bottom_width_, _border_inline_width_,
            border_width_medium_value, false, offset(m_css_borders.bottom.width));

        if (m_writing_mode == writing_mode_vertical_rl) {
            m_css_borders.right.color = get_logical_property<web_color>(
                el, _border_block_start_color_, _border_right_color_, _border_block_color_, m_color,
                false, offset(m_css_borders.right.color));
            m_css_borders.left.color = get_logical_property<web_color>(
                el, _border_block_end_color_, _border_left_color_, _border_block_color_, m_color,
                false, offset(m_css_borders.left.color));
            m_css_borders.right.style = (border_style)get_logical_property<int>(
                el, _border_block_start_style_, _border_right_style_, _border_block_style_,
                border_style_none, false, offset(m_css_borders.right.style));
            m_css_borders.left.style = (border_style)get_logical_property<int>(
                el, _border_block_end_style_, _border_left_style_, _border_block_style_,
                border_style_none, false, offset(m_css_borders.left.style));
            m_css_borders.right.width = get_logical_property<css_length>(
                el, _border_block_start_width_, _border_right_width_, _border_block_width_,
                border_width_medium_value, false, offset(m_css_borders.right.width));
            m_css_borders.left.width = get_logical_property<css_length>(
                el, _border_block_end_width_, _border_left_width_, _border_block_width_,
                border_width_medium_value, false, offset(m_css_borders.left.width));
        } else {
            m_css_borders.left.color = get_logical_property<web_color>(
                el, _border_block_start_color_, _border_left_color_, _border_block_color_, m_color,
                false, offset(m_css_borders.left.color));
            m_css_borders.right.color = get_logical_property<web_color>(
                el, _border_block_end_color_, _border_right_color_, _border_block_color_, m_color,
                false, offset(m_css_borders.right.color));
            m_css_borders.left.style = (border_style)get_logical_property<int>(
                el, _border_block_start_style_, _border_left_style_, _border_block_style_,
                border_style_none, false, offset(m_css_borders.left.style));
            m_css_borders.right.style = (border_style)get_logical_property<int>(
                el, _border_block_end_style_, _border_right_style_, _border_block_style_,
                border_style_none, false, offset(m_css_borders.right.style));
            m_css_borders.left.width = get_logical_property<css_length>(
                el, _border_block_start_width_, _border_left_width_, _border_block_width_,
                border_width_medium_value, false, offset(m_css_borders.left.width));
            m_css_borders.right.width = get_logical_property<css_length>(
                el, _border_block_end_width_, _border_right_width_, _border_block_width_,
                border_width_medium_value, false, offset(m_css_borders.right.width));
        }
    } else {
        m_css_borders.left.color = get_logical_property<web_color>(
            el, _border_is_c, _border_left_color_, _border_inline_color_, m_color, false,
            offset(m_css_borders.left.color));
        m_css_borders.right.color = get_logical_property<web_color>(
            el, _border_ie_c, _border_right_color_, _border_inline_color_, m_color, false,
            offset(m_css_borders.right.color));
        m_css_borders.top.color = get_logical_property<web_color>(
            el, _border_block_start_color_, _border_top_color_, _border_block_color_, m_color,
            false, offset(m_css_borders.top.color));
        m_css_borders.bottom.color = get_logical_property<web_color>(
            el, _border_block_end_color_, _border_bottom_color_, _border_block_color_, m_color,
            false, offset(m_css_borders.bottom.color));

        m_css_borders.left.style = (border_style)get_logical_property<int>(
            el, _border_is_s, _border_left_style_, _border_inline_style_, border_style_none, false,
            offset(m_css_borders.left.style));
        m_css_borders.right.style = (border_style)get_logical_property<int>(
            el, _border_ie_s, _border_right_style_, _border_inline_style_, border_style_none, false,
            offset(m_css_borders.right.style));
        m_css_borders.top.style = (border_style)get_logical_property<int>(
            el, _border_block_start_style_, _border_top_style_, _border_block_style_,
            border_style_none, false, offset(m_css_borders.top.style));
        m_css_borders.bottom.style = (border_style)get_logical_property<int>(
            el, _border_block_end_style_, _border_bottom_style_, _border_block_style_,
            border_style_none, false, offset(m_css_borders.bottom.style));

        m_css_borders.left.width = get_logical_property<css_length>(
            el, _border_is_w, _border_left_width_, _border_inline_width_, border_width_medium_value,
            false, offset(m_css_borders.left.width));
        m_css_borders.right.width = get_logical_property<css_length>(
            el, _border_ie_w, _border_right_width_, _border_inline_width_,
            border_width_medium_value, false, offset(m_css_borders.right.width));
        m_css_borders.top.width = get_logical_property<css_length>(
            el, _border_block_start_width_, _border_top_width_, _border_block_width_,
            border_width_medium_value, false, offset(m_css_borders.top.width));
        m_css_borders.bottom.width = get_logical_property<css_length>(
            el, _border_block_end_width_, _border_bottom_width_, _border_block_width_,
            border_width_medium_value, false, offset(m_css_borders.bottom.width));
    }

    if (m_css_borders.left.color.is_current_color) m_css_borders.left.color = m_color;
    if (m_css_borders.right.color.is_current_color) m_css_borders.right.color = m_color;
    if (m_css_borders.top.color.is_current_color) m_css_borders.top.color = m_color;
    if (m_css_borders.bottom.color.is_current_color) m_css_borders.bottom.color = m_color;

    if (m_css_borders.left.style == border_style_none ||
        m_css_borders.left.style == border_style_hidden)
        m_css_borders.left.width = 0;
    if (m_css_borders.right.style == border_style_none ||
        m_css_borders.right.style == border_style_hidden)
        m_css_borders.right.width = 0;
    if (m_css_borders.top.style == border_style_none ||
        m_css_borders.top.style == border_style_hidden)
        m_css_borders.top.width = 0;
    if (m_css_borders.bottom.style == border_style_none ||
        m_css_borders.bottom.style == border_style_hidden)
        m_css_borders.bottom.width = 0;

    snap_border_width(m_css_borders.left.width, doc);
    snap_border_width(m_css_borders.right.width, doc);
    snap_border_width(m_css_borders.top.width, doc);
    snap_border_width(m_css_borders.bottom.width, doc);

    m_css_borders.radius.top_left_x = el->get_property<css_length>(
        _border_top_left_radius_x_, false, 0, offset(m_css_borders.radius.top_left_x));
    m_css_borders.radius.top_left_y = el->get_property<css_length>(
        _border_top_left_radius_y_, false, 0, offset(m_css_borders.radius.top_left_y));

    m_css_borders.radius.top_right_x = el->get_property<css_length>(
        _border_top_right_radius_x_, false, 0, offset(m_css_borders.radius.top_right_x));
    m_css_borders.radius.top_right_y = el->get_property<css_length>(
        _border_top_right_radius_y_, false, 0, offset(m_css_borders.radius.top_right_y));

    m_css_borders.radius.bottom_left_x = el->get_property<css_length>(
        _border_bottom_left_radius_x_, false, 0, offset(m_css_borders.radius.bottom_left_x));
    m_css_borders.radius.bottom_left_y = el->get_property<css_length>(
        _border_bottom_left_radius_y_, false, 0, offset(m_css_borders.radius.bottom_left_y));

    m_css_borders.radius.bottom_right_x = el->get_property<css_length>(
        _border_bottom_right_radius_x_, false, 0, offset(m_css_borders.radius.bottom_right_x));
    m_css_borders.radius.bottom_right_y = el->get_property<css_length>(
        _border_bottom_right_radius_y_, false, 0, offset(m_css_borders.radius.bottom_right_y));

    {
        // corner-shape longhands: "<keyword index>:<superellipse parameter>" per corner.
        static const string_id ids[4] = {_corner_top_left_shape_, _corner_top_right_shape_,
                                         _corner_bottom_right_shape_, _corner_bottom_left_shape_};
        for (int i = 0; i < 4; i++) {
            string v = el->get_property<string>(ids[i], false, "", offset(m_css_borders.radius.shape[i]));
            int kw = 0;
            float param = 0;
            if (!v.empty()) {
                kw = atoi(v.c_str());
                size_t colon = v.find(':');
                if (colon != string::npos) param = (float)atof(v.c_str() + colon + 1);
            }
            m_css_borders.radius.shape[i] = kw;
            m_css_borders.radius.shape_param[i] = param;
        }
    }
    doc->cvt_units(m_css_borders.radius.top_left_x, m_font_metrics, 0);
    doc->cvt_units(m_css_borders.radius.top_left_y, m_font_metrics, 0);
    doc->cvt_units(m_css_borders.radius.top_right_x, m_font_metrics, 0);
    doc->cvt_units(m_css_borders.radius.top_right_y, m_font_metrics, 0);
    doc->cvt_units(m_css_borders.radius.bottom_left_x, m_font_metrics, 0);
    doc->cvt_units(m_css_borders.radius.bottom_left_y, m_font_metrics, 0);
    doc->cvt_units(m_css_borders.radius.bottom_right_x, m_font_metrics, 0);
    doc->cvt_units(m_css_borders.radius.bottom_right_y, m_font_metrics, 0);

    m_border_collapse = (border_collapse)el->get_property<int>(
        _border_collapse_, true, border_collapse_separate, offset(m_border_collapse));

    m_css_border_spacing_x = el->get_property<css_length>(__litehtml_border_spacing_x_, true, 0,
                                                          offset(m_css_border_spacing_x));
    m_css_border_spacing_y = el->get_property<css_length>(__litehtml_border_spacing_y_, true, 0,
                                                          offset(m_css_border_spacing_y));

    doc->cvt_units(m_css_border_spacing_x, m_font_metrics, 0);
    doc->cvt_units(m_css_border_spacing_y, m_font_metrics, 0);

    m_outline.top.width = m_outline.bottom.width = m_outline.left.width = m_outline.right.width =
        el->get_property<css_length>(_outline_width_, false, border_width_medium_value,
                                     offset(m_outline.top.width));
    m_outline.top.style = m_outline.bottom.style = m_outline.left.style = m_outline.right.style =
        (border_style)el->get_property<int>(_outline_style_, false, border_style_none,
                                            offset(m_outline.top.style));
    m_outline.top.color = m_outline.bottom.color = m_outline.left.color = m_outline.right.color =
        get_color_property(el, _outline_color_, false, m_color, offset(m_outline.top.color));
    m_outline_offset =
        el->get_property<css_length>(_outline_offset_, false, 0, offset(m_outline_offset));

    doc->cvt_units(m_outline.top.width, m_font_metrics, 0);
    doc->cvt_units(m_outline.bottom.width, m_font_metrics, 0);
    doc->cvt_units(m_outline.left.width, m_font_metrics, 0);
    doc->cvt_units(m_outline.right.width, m_font_metrics, 0);
    doc->cvt_units(m_outline_offset, m_font_metrics, 0);

    if (m_outline.top.style == border_style_none) m_outline.top.width = 0;
    if (m_outline.bottom.style == border_style_none) m_outline.bottom.width = 0;
    if (m_outline.left.style == border_style_none) m_outline.left.width = 0;
    if (m_outline.right.style == border_style_none) m_outline.right.width = 0;

    snap_border_width(m_outline.top.width, doc);
    snap_border_width(m_outline.bottom.width, doc);
    snap_border_width(m_outline.left.width, doc);
    snap_border_width(m_outline.right.width, doc);

    if (m_writing_mode == writing_mode_vertical_rl || m_writing_mode == writing_mode_vertical_lr) {
        m_css_offsets.top = get_logical_property<css_length>(
            el, _inset_is, _top_, _inset_block_, _auto, false, offset(m_css_offsets.top));
        m_css_offsets.bottom = get_logical_property<css_length>(
            el, _inset_ie, _bottom_, _inset_block_, _auto, false, offset(m_css_offsets.bottom));
        if (m_writing_mode == writing_mode_vertical_rl) {
            m_css_offsets.right =
                get_logical_property<css_length>(el, _inset_block_start_, _right_, _inset_block_,
                                                 _auto, false, offset(m_css_offsets.right));
            m_css_offsets.left =
                get_logical_property<css_length>(el, _inset_block_end_, _left_, _inset_block_,
                                                 _auto, false, offset(m_css_offsets.left));
        } else {
            m_css_offsets.left =
                get_logical_property<css_length>(el, _inset_block_start_, _left_, _inset_block_,
                                                 _auto, false, offset(m_css_offsets.left));
            m_css_offsets.right =
                get_logical_property<css_length>(el, _inset_block_end_, _right_, _inset_block_,
                                                 _auto, false, offset(m_css_offsets.right));
        }
    } else {
        m_css_offsets.left = get_logical_property<css_length>(
            el, _inset_is, _left_, _inset_inline_, _auto, false, offset(m_css_offsets.left));
        m_css_offsets.right = get_logical_property<css_length>(
            el, _inset_ie, _right_, _inset_inline_, _auto, false, offset(m_css_offsets.right));
        m_css_offsets.top = get_logical_property<css_length>(
            el, _inset_block_start_, _top_, _inset_block_, _auto, false, offset(m_css_offsets.top));
        m_css_offsets.bottom =
            get_logical_property<css_length>(el, _inset_block_end_, _bottom_, _inset_block_, _auto,
                                             false, offset(m_css_offsets.bottom));
    }

    doc->cvt_units(m_css_offsets.left, m_font_metrics, 0);
    doc->cvt_units(m_css_offsets.right, m_font_metrics, 0);
    doc->cvt_units(m_css_offsets.top, m_font_metrics, 0);
    doc->cvt_units(m_css_offsets.bottom, m_font_metrics, 0);

    m_z_index = el->get_property<css_length>(_z_index_, false, _auto, offset(m_z_index));
    m_content = el->get_property<string>(_content_, false, "", offset(m_content));
    m_cursor = el->get_property<string>(_cursor_, true, "auto", offset(m_cursor));

    m_css_text_indent =
        el->get_property<css_length>(_text_indent_, true, 0, offset(m_css_text_indent));
    doc->cvt_units(m_css_text_indent, m_font_metrics, 0);


    m_list_style_type = (list_style_type)el->get_property<int>(
        _list_style_type_, true, list_style_type_disc, offset(m_list_style_type));
    m_list_style_position = (list_style_position)el->get_property<int>(
        _list_style_position_, true, list_style_position_outside, offset(m_list_style_position));

    m_list_style_image =
        el->get_property<string>(_list_style_image_, true, "", offset(m_list_style_image));
    if (!m_list_style_image.empty()) {
        m_list_style_image_baseurl = el->get_property<string>(_list_style_image_baseurl_, true, "",
                                                              offset(m_list_style_image_baseurl));
        doc->container()->load_image(m_list_style_image.c_str(), m_list_style_image_baseurl.c_str(),
                                     true);
    }

    m_order = el->get_property<int>(_order_, false, 0, offset(m_order));
    m_line_clamp = el->get_property<int>(_line_clamp_, false, 0, offset(m_line_clamp));
    if (m_line_clamp == 0) {
        m_line_clamp = el->get_property<int>(__webkit_line_clamp_, false, 0, offset(m_line_clamp));
    }
    // max-lines / block-ellipsis (CSS Overflow 4). block-ellipsis is stored as the final text:
    // auto -> U+2026, no-ellipsis -> "", <string> -> the string.
    m_max_lines = el->get_property<int>(_max_lines_, false, 0, offset(m_max_lines));
    // The ellipsis of a clamped last line is clipped by the element's overflow clip, so a
    // max-lines block container is treated as overflow:hidden (its box is N lines tall anyway).
    if (m_max_lines > 0 && m_overflow == overflow_visible && m_display != display_inline &&
        m_display != display_none) {
        m_overflow = m_overflow_x = m_overflow_y = overflow_hidden;
    }
    m_column_count = el->get_property<int>(_column_count_, false, 0, offset(m_column_count));
    m_column_width = el->get_property<css_length>(_column_width_, false,
                                                 css_length::predef_value(0),
                                                 offset(m_column_width));
    doc->cvt_units(m_column_width, m_font_metrics, 0);
    m_column_gap = el->get_property<css_length>(_column_gap_, false, 0, offset(m_column_gap));
    doc->cvt_units(m_column_gap, m_font_metrics, 0);

    m_letter_spacing =
        el->get_property<css_length>(_letter_spacing_, true, normal, offset(m_letter_spacing));
    m_word_spacing =
        el->get_property<css_length>(_word_spacing_, true, normal, offset(m_word_spacing));
    doc->cvt_units(m_letter_spacing, m_font_metrics, font_size);
    doc->cvt_units(m_word_spacing, m_font_metrics, font_size);

    m_column_rule.width = el->get_property<css_length>(
        _column_rule_width_, false, border_width_medium_value, offset(m_column_rule.width));
    m_column_rule.style = (border_style)el->get_property<int>(
        _column_rule_style_, false, border_style_none, offset(m_column_rule.style));
    m_column_rule.color =
        get_color_property(el, _column_rule_color_, false, m_color, offset(m_column_rule.color));

    if (m_column_rule.style == border_style_none || m_column_rule.style == border_style_hidden)
        m_column_rule.width = 0;

    snap_border_width(m_column_rule.width, doc);

    m_webkit_box_orient = (box_orient)el->get_property<int>(
        __webkit_box_orient_, false, box_orient_horizontal, offset(m_webkit_box_orient));

    compute_background(el, doc);
    compute_border_image(el, doc);
    compute_flex(el, doc);
    compute_grid(el, doc);

    if ((m_column_count > 0 || !m_column_width.is_predefined()) &&
        m_display != display_none) {
        // Multicol is emulated with an equal-width (fr) grid: content is split
        // across N column boxes in init()/layout, gaps and rules come from
        // column-gap/column-rule. Balancing, fragmentation and spanning are
        // out of scope — each column simply flows its share of the content.
        const bool gap_unspecified =
            el->get_property_value(_column_gap_).is<invalid>();
        if (gap_unspecified || m_column_gap.is_predefined()) {
            // The initial/normal column gap is 1em (unlike grid's 0).
            m_column_gap.set_value((float)get_font_size(), css_units_px);
        }
        m_display = display_grid;
        m_grid_template_columns.clear();
        if (m_column_count > 0 && m_column_width.is_predefined()) {
            for (int i = 0; i < m_column_count; i++) {
                m_grid_template_columns.push_back(css_length(1, css_units_fr));
            }
        } else {
            // Width-driven (or width+count): the track count depends on the
            // available inline size, so it is resolved in render_item_grid::
            // calculate_grid_layout. Keep a single fr placeholder here.
            m_grid_template_columns.push_back(css_length(1, css_units_fr));
        }
    }
}

// used for all color properties except `color` (color:currentcolor is converted to color:inherit
// during parsing)
litehtml::web_color litehtml::css_properties::get_color_property(const element* el, string_id name,
                                                                 bool inherited,
                                                                 web_color default_value,
                                                                 uint_ptr member_offset) const {
    web_color color = el->get_property<web_color>(name, inherited, default_value, member_offset);
    if (color.is_current_color) color = m_color;
    return color;
}

static const litehtml::pixel_t font_size_table[8][7] = {
    {9, 9, 9, 9, 11, 14, 18},    {9, 9, 9, 10, 12, 15, 20},  {9, 9, 9, 11, 13, 17, 22},
    {9, 9, 10, 12, 14, 18, 24},  {9, 9, 10, 13, 16, 20, 26}, {9, 9, 11, 14, 17, 21, 28},
    {9, 10, 12, 15, 17, 23, 30}, {9, 10, 13, 16, 18, 24, 32}};

void litehtml::css_properties::compute_font(const element* el, const document::ptr& doc) {
    // initialize font size
    css_length sz = el->get_property<css_length>(
        _font_size_, true, css_length::predef_value(font_size_medium), offset(m_font_size));

    pixel_t parent_sz = 0;
    pixel_t doc_font_size = doc->container()->get_default_font_size();
    element::ptr el_parent = el->parent();
    if (el_parent) {
        parent_sz = el_parent->css().get_font_size();
    } else {
        parent_sz = doc_font_size;
    }

    pixel_t font_size = parent_sz;

    if (sz.is_predefined()) {
        int idx_in_table = round_f(doc_font_size - 9);
        if (idx_in_table >= 0 && idx_in_table <= 7) {
            if (sz.predef() >= font_size_xx_small && sz.predef() <= font_size_xx_large) {
                font_size = font_size_table[idx_in_table][sz.predef()];
            } else if (sz.predef() == font_size_smaller) {
                font_size = parent_sz / (pixel_t)1.2;
            } else if (sz.predef() == font_size_larger) {
                font_size = parent_sz * (pixel_t)1.2;
            } else {
                font_size = parent_sz;
            }
        } else {
            switch (sz.predef()) {
                case font_size_xx_small:
                    font_size = doc_font_size * 3 / 5;
                    break;
                case font_size_x_small:
                    font_size = doc_font_size * 3 / 4;
                    break;
                case font_size_small:
                    font_size = doc_font_size * 8 / 9;
                    break;
                case font_size_large:
                    font_size = doc_font_size * 6 / 5;
                    break;
                case font_size_x_large:
                    font_size = doc_font_size * 3 / 2;
                    break;
                case font_size_xx_large:
                    font_size = doc_font_size * 2;
                    break;
                case font_size_smaller:
                    font_size = parent_sz / (pixel_t)1.2;
                    break;
                case font_size_larger:
                    font_size = parent_sz * (pixel_t)1.2;
                    break;
                default:
                    font_size = parent_sz;
                    break;
            }
        }
    } else {
        if (sz.units() == css_units_percentage) {
            font_size = sz.calc_percent(parent_sz);
        } else {
            font_metrics fm;
            fm.x_height = fm.font_size = parent_sz;
            font_size = doc->to_pixels(sz, fm, 0);
        }
    }

    font_size *= el->text_fit_scale();  // text-fit adjustment (1 unless the element was fitted)
    m_font_size = (float)font_size;

    // initialize font
    m_font_family = el->get_property<string>(
        _font_family_, true, doc->container()->get_default_font_name(), offset(m_font_family));
    // Chromium quirk: a default-sized (medium) element whose font-family is exactly `monospace`
    // is 13px, not 16px; `monospace, monospace` or any other size opts out. Only a size that
    // still comes from the keyword scale qualifies: an ancestor's `font-size:16px` does not.
    const property_value& own_size = el->get_property_value(_font_size_);
    const bool declared_size = own_size.is<css_length>();
    if (declared_size) {
        m_font_size_keyword = sz.is_predefined();
    } else {
        m_font_size_keyword = el_parent ? el_parent->css().font_size_is_keyword() : true;
    }
    const bool size_is_default = declared_size ? (sz.is_predefined() && sz.predef() == font_size_medium)
                                               : m_font_size_keyword;
    // The keyword size is also re-derived when it was inherited from a monospace parent (13px): a
    // child with a non-monospace family goes back to the default size (`<pre><span style="font-family:
    // sans-serif">` is 16px), as long as nothing set the size explicitly in between.
    const pixel_t mono_size = doc_font_size * 13 / 16;
    if (size_is_default && (parent_sz == doc_font_size || parent_sz == mono_size)) {
        string fam = m_font_family;
        trim(fam);
        lcase(fam);
        if (fam == "monospace") {
            font_size = mono_size;
            m_font_size = (float)font_size;
        } else if (parent_sz == mono_size && !declared_size && el->text_fit_scale() == 1) {
            font_size = doc_font_size;
            m_font_size = (float)font_size;
        }
    }
    m_font_weight = el->get_property<css_length>(
        _font_weight_, true, css_length::predef_value(font_weight_normal), offset(m_font_weight));
    m_font_style = (font_style)el->get_property<int>(_font_style_, true, font_style_normal,
                                                     offset(m_font_style));
    m_font_variant_ligatures = el->get_property<string>(
        _font_variant_ligatures_, true, "normal", offset(m_font_variant_ligatures));
    m_font_variant_numeric = el->get_property<string>(
        _font_variant_numeric_, true, "normal", offset(m_font_variant_numeric));
    m_font_variant_east_asian = el->get_property<string>(
        _font_variant_east_asian_, true, "normal", offset(m_font_variant_east_asian));
    m_font_variant_caps = el->get_property<string>(_font_variant_caps_, true, "normal",
                                                   offset(m_font_variant_caps));
    m_font_variant_position = el->get_property<string>(
        _font_variant_position_, true, "normal", offset(m_font_variant_position));
    m_font_feature_settings = el->get_property<css_token_vector>(
        _font_feature_settings_, true, css_token_vector(), offset(m_font_feature_settings));
    m_font_kerning =
        el->get_property<string>(_font_kerning_, true, "auto", offset(m_font_kerning));
    m_font_variation_settings = el->get_property<css_token_vector>(
        _font_variation_settings_, true, css_token_vector(), offset(m_font_variation_settings));
    m_font_stretch =
        el->get_property<float>(_font_stretch_, true, 100.0f, offset(m_font_stretch));
    bool propagate_decoration =
        !is_one_of(m_display, display_inline_block, display_inline_table, display_inline_flex, display_inline_grid) &&
        m_float == float_none &&
        !is_one_of(m_el_position, element_position_absolute, element_position_fixed);

    m_text_decoration_line =
        el->get_property<int>(_text_decoration_line_, propagate_decoration,
                              text_decoration_line_none, offset(m_text_decoration_line));

    // Merge parent text decoration with child text decoration
    if (propagate_decoration && el->parent()) {
        m_text_decoration_line |= el->parent()->css().get_text_decoration_line();
    }

    if (m_text_decoration_line) {
        m_text_decoration_thickness =
            el->get_property<css_length>(_text_decoration_thickness_, propagate_decoration,
                                         css_length::predef_value(text_decoration_thickness_auto),
                                         offset(m_text_decoration_thickness));
        m_text_underline_offset = el->get_property<css_length>(
            _text_underline_offset_, propagate_decoration, css_length::predef_value(0),
            offset(m_text_underline_offset));

        // A percentage of text-decoration-thickness / text-underline-offset
        // refers to the font size of the element (CSS Text Decoration 3).
        // cvt_units() leaves percentages untouched because their reference is
        // property specific, so the raw percentage number would otherwise be
        // used as a pixel value.
        doc->cvt_units(m_text_decoration_thickness, m_font_metrics, font_size, font_size);
        doc->cvt_units(m_text_underline_offset, m_font_metrics, font_size, font_size);

        m_text_decoration_style = (text_decoration_style)el->get_property<int>(
            _text_decoration_style_, propagate_decoration, text_decoration_style_solid,
            offset(m_text_decoration_style));
        m_text_decoration_color =
            get_color_property(el, _text_decoration_color_, propagate_decoration,
                               web_color::current_color, offset(m_text_decoration_color));
        m_text_decoration_skip_ink = el->get_property<int>(
            _text_decoration_skip_ink_, propagate_decoration, text_decoration_skip_ink_auto,
            offset(m_text_decoration_skip_ink));
        m_text_underline_position = el->get_property<int>(
            _text_underline_position_, propagate_decoration, text_underline_position_auto,
            offset(m_text_underline_position));
    } else {
        m_text_decoration_thickness = css_length::predef_value(text_decoration_thickness_auto);
        m_text_underline_offset = css_length::predef_value(0);
        m_text_decoration_color = web_color::current_color;
        m_text_decoration_skip_ink = text_decoration_skip_ink_auto;
        m_text_underline_position = text_underline_position_auto;
    }

    m_text_fit = el->get_property<int>(_text_fit_, false, 0, offset(m_text_fit));
    m_scrollbar_gutter = el->get_property<int>(_scrollbar_gutter_, false, scrollbar_gutter_auto, offset(m_scrollbar_gutter));
    m_line_break = (line_break)el->get_property<int>(_line_break_, true, line_break_auto, offset(m_line_break));
    m_text_align_last = (text_align_last)el->get_property<int>(_text_align_last_, true, text_align_last_auto,
                                                               offset(m_text_align_last));
    m_text_justify = (text_justify)el->get_property<int>(_text_justify_, true, text_justify_auto,
                                                         offset(m_text_justify));
    {
        // hyphens (standard) wins over -webkit-hyphens; otherwise inherited, default manual.
        int h = el->get_property<int>(_hyphens_, false, -1, offset(m_hyphens));
        if (h < 0) h = el->get_property<int>(__webkit_hyphens_, false, -1, offset(m_hyphens));
        if (h < 0) {
            element::ptr par = el->parent();
            h = par ? (int)par->css().get_hyphens() : (int)hyphens_manual;
        }
        m_hyphens = (hyphens)h;
        m_hyphenate_character = el->get_property<string>(_hyphenate_character_, true, "", offset(m_hyphenate_character));
        m_font_optical_sizing = el->get_property<int>(_font_optical_sizing_, true, 0, offset(m_font_optical_sizing));
        const int fs_weight = el->get_property<int>(_font_synthesis_weight_, true, 0, offset(m_font_synthesis_weight));
        const int fs_style = el->get_property<int>(_font_synthesis_style_, true, 0, offset(m_font_synthesis_style));
        const int fs_caps = el->get_property<int>(_font_synthesis_small_caps_, true, 0, offset(m_font_synthesis_caps));
        const int fs_pos = el->get_property<int>(_font_synthesis_position_, true, 0, offset(m_font_synthesis_pos));
        m_font_synthesis_weight = fs_weight;
        m_font_synthesis_style = fs_style;
        m_font_synthesis_caps = fs_caps;
        m_font_synthesis_pos = fs_pos;
        m_font_synthesis = (fs_weight ? 0 : 1) | (fs_style ? 0 : 2) | (fs_caps ? 0 : 4) | (fs_pos ? 0 : 8);
    }
    m_stroke_linejoin = el->get_property<int>(_stroke_linejoin_, true, -1, offset(m_stroke_linejoin));
    m_box_decoration_break = (box_decoration_break)el->get_property<int>(
        _box_decoration_break_, false, box_decoration_break_slice, offset(m_box_decoration_break));
    if (m_box_decoration_break == box_decoration_break_slice) {
        m_box_decoration_break = (box_decoration_break)el->get_property<int>(
            __webkit_box_decoration_break_, false, box_decoration_break_slice,
            offset(m_box_decoration_break));
    }
    // block-ellipsis is part of the font description (it is the text a clamped line ends with).
    m_block_ellipsis = el->get_property<string>(_block_ellipsis_, true, "\xE2\x80\xA6",
                                                offset(m_block_ellipsis));

    // text-emphasis
    m_text_emphasis_style =
        el->get_property<string>(_text_emphasis_style_, true, "", offset(m_text_emphasis_style));
    m_text_emphasis_position =
        el->get_property<int>(_text_emphasis_position_, true, text_emphasis_position_over,
                              offset(m_text_emphasis_position));
    m_text_emphasis_color = get_color_property(
        el, _text_emphasis_color_, true, web_color::current_color, offset(m_text_emphasis_color));

    if (el->parent()) {
        if (m_text_emphasis_style.empty() || m_text_emphasis_style == "initial" ||
            m_text_emphasis_style == "unset") {
            m_text_emphasis_style = el->parent()->css().get_text_emphasis_style();
        }
        if (m_text_emphasis_color == web_color::current_color) {
            m_text_emphasis_color = el->parent()->css().get_text_emphasis_color();
        }
        m_text_emphasis_position |= el->parent()->css().get_text_emphasis_position();
    }

    // -webkit-text-stroke-width: resolve to px, clamp negatives (calc can be < 0)
    // and quantize to 1/64px so the font cache key stays stable. em/ex need the
    // element's own font size, which m_font_metrics does not carry yet at this
    // point, so supply a metrics copy with font_size filled in.
    if (!m_text_stroke_width.is_predefined()) {
        font_metrics stroke_metrics = m_font_metrics;
        stroke_metrics.font_size = font_size;
        stroke_metrics.x_height = font_size;
        doc->cvt_units(m_text_stroke_width, stroke_metrics, font_size);
    }
    pixel_t text_stroke_width =
        m_text_stroke_width.is_predefined() ? 0.0f : m_text_stroke_width.val();
    if (text_stroke_width < 0.0f) text_stroke_width = 0.0f;
    text_stroke_width = std::round(text_stroke_width * 64.0f) / 64.0f;

    // currentColor resolves against this element's own `color`.
    web_color text_stroke_color = m_text_stroke_color;
    if (text_stroke_color.is_current_color) text_stroke_color = m_color;

    if (m_font_weight.is_predefined()) {
        switch (m_font_weight.predef()) {
            case font_weight_bold:
                m_font_weight = 700;
                break;
            case font_weight_bolder: {
                const int inherited = (int)el->parent()->css().m_font_weight.val();
                if (inherited < 400)
                    m_font_weight = 400;
                else if (inherited >= 400 && inherited < 600)
                    m_font_weight = 700;
                else
                    m_font_weight = 900;
            } break;
            case font_weight_lighter: {
                const int inherited = (int)el->parent()->css().m_font_weight.val();
                if (inherited < 600)
                    m_font_weight = 100;
                else if (inherited >= 600 && inherited < 800)
                    m_font_weight = 400;
                else
                    m_font_weight = 700;
            } break;
            default:
                m_font_weight = 400;
                break;
        }
    }

    // The font description below needs the spacing, but compute() resolves it only after
    // compute_font() has run, so resolve it here first.
    m_letter_spacing =
        el->get_property<css_length>(_letter_spacing_, true, css_length::predef_value(0), offset(m_letter_spacing));
    m_word_spacing =
        el->get_property<css_length>(_word_spacing_, true, css_length::predef_value(0), offset(m_word_spacing));
    // m_font_metrics is not up to date yet, so resolve em units against the font size directly
    for (css_length* spacing : {&m_letter_spacing, &m_word_spacing}) {
        if (!spacing->is_predefined() && spacing->units() == css_units_em) {
            spacing->set_value(spacing->val() * font_size, css_units_px);
        }
    }
    doc->cvt_units(m_letter_spacing, m_font_metrics, font_size);
    doc->cvt_units(m_word_spacing, m_font_metrics, font_size);

    font_description descr;
    descr.family = m_font_family;
    descr.size = std::round(font_size);
    descr.style = m_font_style;
    descr.weight = (int)m_font_weight.val();
    descr.decoration_line = m_text_decoration_line;
    descr.decoration_thickness = m_text_decoration_thickness;
    descr.underline_offset = m_text_underline_offset;
    descr.decoration_style = m_text_decoration_style;
    descr.decoration_color = m_text_decoration_color;
    descr.decoration_skip_ink = m_text_decoration_skip_ink;
    descr.underline_position = m_text_underline_position;
    descr.emphasis_style = m_text_emphasis_style;
    descr.emphasis_color = m_text_emphasis_color;
    descr.emphasis_position = m_text_emphasis_position;
    descr.orientation = m_text_orientation;
    descr.text_combine_upright_ = m_text_combine_upright;
    descr.letter_spacing = m_letter_spacing.is_predefined() ? 0 : m_letter_spacing.val();
    descr.word_spacing = m_word_spacing.is_predefined() ? 0 : m_word_spacing.val();
    descr.text_shadow = m_text_shadow;
    descr.text_stroke_width = text_stroke_width;
    descr.text_stroke_color = text_stroke_color;
    descr.block_ellipsis = m_block_ellipsis;
    descr.stroke_linejoin = m_stroke_linejoin;
    descr.synthesis = m_font_synthesis;
    descr.optical_sizing_auto = m_font_optical_sizing == 0;

    // font-kerning maps onto the `kern` OpenType feature. `auto` leaves the
    // kerning of the font itself alone, `normal` turns it on and `none` turns
    // it off. It is applied before font-variant-* / font-feature-settings so
    // that font-feature-settings can still override it.
    if (m_font_kerning == "normal")
        add_ot_feature(descr.features, ot_tag('k', 'e', 'r', 'n'), 1);
    else if (m_font_kerning == "none")
        add_ot_feature(descr.features, ot_tag('k', 'e', 'r', 'n'), 0);

    append_font_variant_features(descr.features, m_font_variant_ligatures, 0);
    append_font_variant_features(descr.features, m_font_variant_numeric, 1);
    append_font_variant_features(descr.features, m_font_variant_east_asian, 2);
    append_font_variant_features(descr.features, m_font_variant_caps, 3);
    append_font_variant_features(descr.features, m_font_variant_position, 4);
    append_font_feature_settings(descr.features, m_font_feature_settings);

    descr.stretch = (int)std::lround(m_font_stretch);
    append_font_variation_settings(descr.variations, m_font_variation_settings);

    m_font = doc->get_font(descr, &m_font_metrics);

    // The font is created at a whole-pixel size (descr.size is rounded
    // above), but em lengths and percentage line-heights resolve against
    // the element's computed font size, which can be fractional (0.8rem is
    // 12.8px, not 13px). Keep the metrics' font_size at the computed value;
    // the ascent/descent and other glyph metrics stay the created font's.
    m_font_metrics.font_size = get_font_size();
}

void litehtml::css_properties::compute_border_image(const element* el, const document::ptr& doc) {
    m_border_image.source = el->get_property<image>(_border_image_source_, false, image(),
                                                    offset(m_border_image.source));
    if (m_border_image.source.type == image::type_url && !m_border_image.source.url.empty()) {
        m_border_image.baseurl = el->get_property<string>(_id("border-image-source-baseurl"), false,
                                                          "", offset(m_border_image.baseurl));
        doc->container()->load_image(m_border_image.source.url.c_str(),
                                     m_border_image.baseurl.c_str(), true);
    } else if (m_border_image.source.type == image::type_gradient) {
        for (auto& item : m_border_image.source.m_gradient.m_colors) {
            if (item.length) doc->cvt_units(*item.length, m_font_metrics, 0);
        }
    }

    m_border_image.slice[0] = el->get_property<css_length>(_border_image_slice_top_, false,
                                                           css_length(100, css_units_percentage),
                                                           offset(m_border_image.slice[0]));
    m_border_image.slice[1] = el->get_property<css_length>(_border_image_slice_right_, false,
                                                           css_length(100, css_units_percentage),
                                                           offset(m_border_image.slice[1]));
    m_border_image.slice[2] = el->get_property<css_length>(_border_image_slice_bottom_, false,
                                                           css_length(100, css_units_percentage),
                                                           offset(m_border_image.slice[2]));
    m_border_image.slice[3] = el->get_property<css_length>(_border_image_slice_left_, false,
                                                           css_length(100, css_units_percentage),
                                                           offset(m_border_image.slice[3]));
    m_border_image.slice_fill = el->get_property<int>(_id("border-image-slice-fill"), false, 0,
                                                      offset(m_border_image.slice_fill)) != 0;

    m_border_image.width[0] =
        el->get_property<css_length>(_border_image_width_top_, false, css_length(1, css_units_none),
                                     offset(m_border_image.width[0]));
    m_border_image.width[1] = el->get_property<css_length>(_border_image_width_right_, false,
                                                           css_length(1, css_units_none),
                                                           offset(m_border_image.width[1]));
    m_border_image.width[2] = el->get_property<css_length>(_border_image_width_bottom_, false,
                                                           css_length(1, css_units_none),
                                                           offset(m_border_image.width[2]));
    m_border_image.width[3] = el->get_property<css_length>(_border_image_width_left_, false,
                                                           css_length(1, css_units_none),
                                                           offset(m_border_image.width[3]));

    m_border_image.outset[0] = el->get_property<css_length>(
        _border_image_outset_top_, false, css_length(0), offset(m_border_image.outset[0]));
    m_border_image.outset[1] = el->get_property<css_length>(
        _border_image_outset_right_, false, css_length(0), offset(m_border_image.outset[1]));
    m_border_image.outset[2] = el->get_property<css_length>(
        _border_image_outset_bottom_, false, css_length(0), offset(m_border_image.outset[2]));
    m_border_image.outset[3] = el->get_property<css_length>(
        _border_image_outset_left_, false, css_length(0), offset(m_border_image.outset[3]));

    int repeat =
        el->get_property<int>(_border_image_repeat_, false,
                              border_image_repeat_stretch | (border_image_repeat_stretch << 8),
                              offset(m_border_image.repeat_h));
    m_border_image.repeat_h = (border_image_repeat)(repeat & 0xFF);
    m_border_image.repeat_v = (border_image_repeat)((repeat >> 8) & 0xFF);

    pixel_t font_size = get_font_size();
    for (int i = 0; i < 4; i++) {
        if (m_border_image.width[i].units() != css_units_none)
            doc->cvt_units(m_border_image.width[i], m_font_metrics, font_size);
        if (m_border_image.outset[i].units() != css_units_none)
            doc->cvt_units(m_border_image.outset[i], m_font_metrics, font_size);
    }
}

void litehtml::css_properties::compute_background(const element* el, const document::ptr& doc) {
    m_bg.m_color = get_color_property(el, _background_color_, false, web_color::transparent,
                                      offset(m_bg.m_color));

    const css_size auto_auto(css_length::predef_value(background_size_auto),
                             css_length::predef_value(background_size_auto));
    m_bg.m_position_x = el->get_property<length_vector>(_background_position_x_, false,
                                                        {css_length(0, css_units_percentage)},
                                                        offset(m_bg.m_position_x));
    m_bg.m_position_y = el->get_property<length_vector>(_background_position_y_, false,
                                                        {css_length(0, css_units_percentage)},
                                                        offset(m_bg.m_position_y));
    m_bg.m_size =
        el->get_property<size_vector>(_background_size_, false, {auto_auto}, offset(m_bg.m_size));

    for (auto& x : m_bg.m_position_x) doc->cvt_units(x, m_font_metrics, 0);
    for (auto& y : m_bg.m_position_y) doc->cvt_units(y, m_font_metrics, 0);
    for (auto& size : m_bg.m_size) {
        doc->cvt_units(size.width, m_font_metrics, 0);
        doc->cvt_units(size.height, m_font_metrics, 0);
    }

    m_bg.m_attachment = el->get_property<int_vector>(
        _background_attachment_, false, {background_attachment_scroll}, offset(m_bg.m_attachment));
    m_bg.m_repeat = el->get_property<int_vector>(_background_repeat_, false,
                                                 {background_repeat_repeat}, offset(m_bg.m_repeat));
    m_bg.m_clip = el->get_property<int_vector>(_background_clip_, false, {background_box_border},
                                               offset(m_bg.m_clip));
    m_bg.m_origin = el->get_property<int_vector>(_background_origin_, false,
                                                 {background_box_padding}, offset(m_bg.m_origin));

    m_bg.m_image =
        el->get_property<vector<image>>(_background_image_, false, {{}}, offset(m_bg.m_image));
    m_bg.m_baseurl =
        el->get_property<string>(_background_image_baseurl_, false, "", offset(m_bg.m_baseurl));

    for (auto& image : m_bg.m_image) {
        switch (image.type) {
            case image::type_none:
                break;
            case image::type_url:
                if (!image.url.empty()) {
                    doc->container()->load_image(image.url.c_str(), m_bg.m_baseurl.c_str(), true);
                }
                break;
            case image::type_gradient:
                for (auto& item : image.m_gradient.m_colors) {
                    if (item.length) doc->cvt_units(*item.length, m_font_metrics, 0);
                }
                break;
        }
    }
}

void litehtml::css_properties::compute_flex(const element* el, const document::ptr& doc) {
    if (m_display == display_flex || m_display == display_inline_flex) {
        m_flex_direction = (flex_direction)el->get_property<int>(
            _flex_direction_, false, flex_direction_row, offset(m_flex_direction));
        m_flex_wrap = (flex_wrap)el->get_property<int>(_flex_wrap_, false, flex_wrap_nowrap,
                                                       offset(m_flex_wrap));

        m_flex_justify_content = (flex_justify_content)el->get_property<int>(
            _justify_content_, false, flex_justify_content_flex_start,
            offset(m_flex_justify_content));
        m_flex_align_items = (flex_align_items)el->get_property<int>(
            _align_items_, false, flex_align_items_normal, offset(m_flex_align_items));
        m_flex_align_content = (flex_align_content)el->get_property<int>(
            _align_content_, false, flex_align_content_stretch, offset(m_flex_align_content));

        m_row_gap = el->get_property<css_length>(_row_gap_, false, 0, offset(m_row_gap));
        m_column_gap = el->get_property<css_length>(_column_gap_, false, 0, offset(m_column_gap));
        doc->cvt_units(m_row_gap, m_font_metrics, 0);
        doc->cvt_units(m_column_gap, m_font_metrics, 0);
    }
    m_flex_align_self = (flex_align_items)el->get_property<int>(
        _align_self_, false, flex_align_items_auto, offset(m_flex_align_self));
    // A display:contents parent generates no box: the element takes part in the formatting
    // context of the nearest ancestor that does generate one, so flex/grid item properties
    // (flex-basis defaults, blockification) are decided by that ancestor, not the contents one.
    auto parent = el->parent();
    while (parent && parent->css().m_display == display_contents) {
        parent = parent->parent();
    }
    if (parent && (parent->css().m_display == display_flex ||
                   parent->css().m_display == display_inline_flex)) {
        m_flex_grow = el->get_property<float>(_flex_grow_, false, 0, offset(m_flex_grow));
        m_flex_shrink = el->get_property<float>(_flex_shrink_, false, 1, offset(m_flex_shrink));
        m_flex_basis = el->get_property<css_length>(
            _flex_basis_, false, css_length::predef_value(flex_basis_auto), offset(m_flex_basis));
        if (!m_flex_basis.is_predefined() && m_flex_basis.units() == css_units_none &&
            m_flex_basis.val() != 0) {
            // flex-basis property must contain units
            m_flex_basis.predef(flex_basis_auto);
        }
        doc->cvt_units(m_flex_basis, m_font_metrics, 0);
        if (m_display == display_inline || m_display == display_inline_block) {
            m_display = display_block;
        } else if (m_display == display_inline_table) {
            m_display = display_table;
        } else if (m_display == display_inline_flex) {
            m_display = display_flex;
        }
    }
    // Grid items are blockified too (CSS Display 3 "blockification").
    if (parent && (parent->css().m_display == display_grid ||
                   parent->css().m_display == display_inline_grid)) {
        if (m_display == display_inline || m_display == display_inline_block) {
            m_display = display_block;
        } else if (m_display == display_inline_table) {
            m_display = display_table;
        } else if (m_display == display_inline_flex) {
            m_display = display_flex;
        } else if (m_display == display_inline_grid) {
            m_display = display_grid;
        }
    }
}

void litehtml::css_properties::compute_grid(const element* el, const document::ptr& doc) {
    if (m_display == display_grid || m_display == display_inline_grid) {
        m_grid_template_columns = el->get_property<length_vector>(
            _grid_template_columns_, false, length_vector(), offset(m_grid_template_columns));
        m_grid_template_rows = el->get_property<length_vector>(
            _grid_template_rows_, false, length_vector(), offset(m_grid_template_rows));
        m_grid_auto_columns = el->get_property<length_vector>(
            _grid_auto_columns_, false, length_vector(), offset(m_grid_auto_columns));
        m_grid_auto_rows = el->get_property<length_vector>(
            _grid_auto_rows_, false, length_vector(), offset(m_grid_auto_rows));
        m_grid_auto_flow = (grid_auto_flow)el->get_property<int>(
            _grid_auto_flow_, false, grid_auto_flow_row, offset(m_grid_auto_flow));

        m_grid_columns_tokens = el->get_property<css_token_vector>(
            _satoru_grid_columns_tokens_, false, css_token_vector(), offset(m_grid_columns_tokens));
        m_grid_rows_tokens = el->get_property<css_token_vector>(
            _satoru_grid_rows_tokens_, false, css_token_vector(), offset(m_grid_rows_tokens));
        m_grid_template_areas = el->get_property<css_token_vector>(
            _grid_template_areas_, false, css_token_vector(), offset(m_grid_template_areas));

        for (auto& x : m_grid_template_columns) doc->cvt_units(x, m_font_metrics, 0);
        for (auto& y : m_grid_template_rows) doc->cvt_units(y, m_font_metrics, 0);
        for (auto& x : m_grid_auto_columns) doc->cvt_units(x, m_font_metrics, 0);
        for (auto& y : m_grid_auto_rows) doc->cvt_units(y, m_font_metrics, 0);

        m_row_gap = el->get_property<css_length>(_row_gap_, false, 0, offset(m_row_gap));
        m_column_gap = el->get_property<css_length>(_column_gap_, false, 0, offset(m_column_gap));
        doc->cvt_units(m_row_gap, m_font_metrics, 0);
        doc->cvt_units(m_column_gap, m_font_metrics, 0);

        m_column_rule.width = el->get_property<css_length>(
            _column_rule_width_, false, border_width_medium_value, offset(m_column_rule.width));
        m_column_rule.style = (border_style)el->get_property<int>(
            _column_rule_style_, false, border_style_none, offset(m_column_rule.style));
        m_column_rule.color = get_color_property(el, _column_rule_color_, false, m_color,
                                                 offset(m_column_rule.color));

        if (m_column_rule.style == border_style_none || m_column_rule.style == border_style_hidden)
            m_column_rule.width = 0;

        snap_border_width(m_column_rule.width, doc);

        // justify-content's initial value is normal (its auto tracks stretch);
        // flex_start here would conflate "not set" with an explicit start.
        m_flex_justify_content = (flex_justify_content)el->get_property<int>(
            _justify_content_, false, flex_justify_content_normal,
            offset(m_flex_justify_content));
        m_flex_align_items = (flex_align_items)el->get_property<int>(
            _align_items_, false, flex_align_items_normal, offset(m_flex_align_items));
        m_flex_align_content = (flex_align_content)el->get_property<int>(
            _align_content_, false, flex_align_content_stretch, offset(m_flex_align_content));
    }

    // Grid item properties
    m_grid_column_start = el->get_property<css_token_vector>(
        _grid_column_start_, false, css_token_vector(), offset(m_grid_column_start));
    m_grid_column_end = el->get_property<css_token_vector>(
        _grid_column_end_, false, css_token_vector(), offset(m_grid_column_end));
    m_grid_row_start = el->get_property<css_token_vector>(
        _grid_row_start_, false, css_token_vector(), offset(m_grid_row_start));
    m_grid_row_end = el->get_property<css_token_vector>(_grid_row_end_, false, css_token_vector(),
                                                        offset(m_grid_row_end));

    m_align_self = (flex_align_items)el->get_property<int>(
        _align_self_, false, flex_align_items_auto, offset(m_align_self));
    m_justify_self = (flex_justify_content)el->get_property<int>(
        _justify_self_, false, flex_justify_content_auto, offset(m_justify_self));
    m_justify_items = (flex_justify_content)el->get_property<int>(
        _justify_items_, false, flex_justify_content_normal, offset(m_justify_items));
}

// https://www.w3.org/TR/css-values-4/#snap-a-length-as-a-border-width
void litehtml::css_properties::snap_border_width(css_length& width,
                                                 const std::shared_ptr<document>& doc) {
    if (width.is_predefined() || width.units() == css_units_percentage) {
        return;
    }

    pixel_t px = doc->to_pixels(width, m_font_metrics, 0);

    if (px > 0 && px < 1) {
        px = 1;
    } else {
        px = std::floor(px);
    }

    width.set_value(px, css_units_px);
}

template <class T>
T litehtml::css_properties::get_logical_property(const element* el, string_id logical_side,
                                                 string_id physical_side, string_id logical_all,
                                                 T default_value, bool inherited,
                                                 uint_ptr member_offset) const {
    const property_value& v_logical = el->get_property_value(logical_side);
    const property_value& v_logical_all = el->get_property_value(logical_all);
    const property_value& v_physical = el->get_property_value(physical_side);

    if (v_logical.is<invalid>() && v_logical_all.is<invalid>() && v_physical.is<invalid>()) {
        if (inherited) {
            if (auto p = el->parent())
                return *(const T*)((litehtml::byte*)&p->css() + member_offset);
        }
        return default_value;
    }

    const property_value* best = nullptr;

    if (!v_physical.is<invalid>()) best = &v_physical;

    if (!v_logical_all.is<invalid>()) {
        if (!best || v_logical_all.m_priority >= best->m_priority) best = &v_logical_all;
    }

    if (!v_logical.is<invalid>()) {
        if (!best || v_logical.m_priority >= best->m_priority) best = &v_logical;
    }

    if (best->is<inherit>() || (best->is<unset>() && inherited)) {
        if (auto p = el->parent())
            return *(const T*)((litehtml::byte*)&p->css() + member_offset);
        else
            return default_value;
    }

    // CSS-wide keywords `initial` / non-inherited `unset` mean "use the initial value".
    if (best->is<initial>() || best->is<unset>()) {
        return default_value;
    }

    // Guard: if the value is an unresolved css_token_vector (e.g. from var()),
    // we cannot extract the expected type. Return default_value instead of crashing.
    if (best->is<css_token_vector>()) {
        return default_value;
    }

    return best->get<T>();
}

template litehtml::css_length litehtml::css_properties::get_logical_property<litehtml::css_length>(
    const element* el, string_id logical_side, string_id physical_side, string_id logical_all,
    litehtml::css_length default_value, bool inherited, uint_ptr member_offset) const;
template litehtml::web_color litehtml::css_properties::get_logical_property<litehtml::web_color>(
    const element* el, string_id logical_side, string_id physical_side, string_id logical_all,
    litehtml::web_color default_value, bool inherited, uint_ptr member_offset) const;
template int litehtml::css_properties::get_logical_property<int>(
    const element* el, string_id logical_side, string_id physical_side, string_id logical_all,
    int default_value, bool inherited, uint_ptr member_offset) const;

std::vector<std::tuple<litehtml::string, litehtml::string>>
litehtml::css_properties::dump_get_attrs() const {
    std::vector<std::tuple<string, string>> ret;

    ret.emplace_back("display", index_value(m_display, style_display_strings));
    ret.emplace_back("direction", index_value(m_direction, direction_strings));
    ret.emplace_back("unicode_bidi", index_value(m_unicode_bidi, unicode_bidi_strings));
    ret.emplace_back("writing_mode", index_value(m_writing_mode, writing_mode_strings));
    ret.emplace_back("text_orientation", index_value(m_text_orientation, text_orientation_strings));
    ret.emplace_back("text_combine_upright",
                     index_value(m_text_combine_upright, text_combine_upright_strings));
    ret.emplace_back("el_position", index_value(m_el_position, element_position_strings));
    ret.emplace_back("text_align", index_value(m_text_align, text_align_strings));
    ret.emplace_back("font_size", m_font_size.to_string());
    ret.emplace_back("overflow", index_value(m_overflow, overflow_strings));
    ret.emplace_back("text_overflow", index_value(m_text_overflow, text_overflow_strings));
    ret.emplace_back("white_space", index_value(m_white_space, white_space_strings));
    ret.emplace_back("visibility", index_value(m_visibility, visibility_strings));
    ret.emplace_back("scrollbar-gutter", index_value(m_scrollbar_gutter, scrollbar_gutter_strings));
    ret.emplace_back("appearance", index_value(m_appearance, appearance_strings));
    ret.emplace_back("box_sizing", index_value(m_box_sizing, box_sizing_strings));
    ret.emplace_back("z_index", m_z_index.to_string());
    ret.emplace_back("vertical_align", index_value(m_vertical_align, vertical_align_strings));
    ret.emplace_back("float", index_value(m_float, element_float_strings));
    ret.emplace_back("clear", index_value(m_clear, element_clear_strings));
    ret.emplace_back("margins", m_css_margins.to_string());
    ret.emplace_back("padding", m_css_padding.to_string());
    ret.emplace_back("borders", m_css_borders.to_string());
    ret.emplace_back("width", m_css_width.to_string());
    ret.emplace_back("height", m_css_height.to_string());
    ret.emplace_back("min_width", m_css_min_width.to_string());
    ret.emplace_back("min_height", m_css_min_width.to_string());
    ret.emplace_back("max_width", m_css_max_width.to_string());
    ret.emplace_back("max_height", m_css_max_width.to_string());
    ret.emplace_back("offsets", m_css_offsets.to_string());
    ret.emplace_back("text_indent", m_css_text_indent.to_string());
    ret.emplace_back("line_height", std::to_string(m_line_height.computed_value));
    ret.emplace_back("list_style_type", index_value(m_list_style_type, list_style_type_strings));
    ret.emplace_back("list_style_position",
                     index_value(m_list_style_position, list_style_position_strings));
    ret.emplace_back("border_spacing_x", m_css_border_spacing_x.to_string());
    ret.emplace_back("border_spacing_y", m_css_border_spacing_y.to_string());
    ret.emplace_back("line_clamp", std::to_string(m_line_clamp));
    ret.emplace_back("max_lines", std::to_string(m_max_lines));
    ret.emplace_back("webkit_box_orient", index_value(m_webkit_box_orient, box_orient_strings));
    ret.emplace_back("word_break", index_value(m_word_break, word_break_strings));
    ret.emplace_back("break_before", index_value(m_break_before, break_flow_strings));
    ret.emplace_back("break_after", index_value(m_break_after, break_flow_strings));
    ret.emplace_back("break_inside", index_value(m_break_inside, break_inside_strings));
    ret.emplace_back("orphans", std::to_string(m_orphans));
    ret.emplace_back("widows", std::to_string(m_widows));
    ret.emplace_back("overflow_wrap", index_value(m_overflow_wrap, overflow_wrap_strings));
    ret.emplace_back("text_wrap", index_value(m_text_wrap, text_wrap_strings));
    ret.emplace_back("white_space_collapse",
                     index_value(m_white_space_collapse, white_space_collapse_strings));
    ret.emplace_back("text_wrap_mode", index_value(m_text_wrap_mode, text_wrap_mode_strings));
    ret.emplace_back("text_wrap_style", index_value(m_text_wrap_style, text_wrap_style_strings));
    ret.emplace_back("opacity", std::to_string(m_opacity));
    ret.emplace_back("object_fit", index_value(m_object_fit, object_fit_strings));
    ret.emplace_back("image_rendering", index_value(m_image_rendering, image_rendering_strings));
    ret.emplace_back("mix_blend_mode", index_value(m_mix_blend_mode, blend_mode_strings));
    ret.emplace_back("background_blend_mode",
                     index_value(m_background_blend_mode, blend_mode_strings));

    return ret;
}

void litehtml::css_properties::set_grid_column_start(const char* val) {
    m_grid_column_start = tokenize(val);
}
void litehtml::css_properties::set_grid_column_end(const char* val) {
    m_grid_column_end = tokenize(val);
}
void litehtml::css_properties::set_grid_row_start(const char* val) {
    m_grid_row_start = tokenize(val);
}
void litehtml::css_properties::set_grid_row_end(const char* val) { m_grid_row_end = tokenize(val); }
