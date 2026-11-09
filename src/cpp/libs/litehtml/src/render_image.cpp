#include "render_image.h"

#include <algorithm>
#include <cstdio>
#include <iostream>

#include "document.h"
#include "types.h"

litehtml::pixel_t litehtml::render_item_image::_measure(
    const containing_block_context& containing_block_size, formatting_context* /*fmt_ctx*/) {
    containing_block_context self_size = m_self_size;

    litehtml::size sz;
    src_el()->get_content_size(sz, containing_block_size.width);

    m_pos.width = 0;
    m_pos.height = 0;

    bool is_content_size =
        (containing_block_size.size_mode & containing_block_context::size_mode_content) != 0;
    bool is_min_content =
        (containing_block_size.size_mode & containing_block_context::size_mode_min_content) != 0;

    // A percentage inline size is cyclic while the containing block is being sized from content
    // (shrink-to-fit / min-content): it contributes 0 to the min-content size instead of falling
    // back to the intrinsic size. This is what lets a flex item shrink below its image's natural
    // width (Chromium: the min-content contribution of a percentage-width replaced element is 0).
    // A percentage max-width does NOT do this: the element's min-content stays its (clamped)
    // intrinsic size, so e.g. an image with width:max-content, max-height and max-width:100%
    // keeps its transferred size as a flex item's automatic minimum instead of shrinking to 0.
    if (is_min_content && css().get_width().units() == css_units_percentage) {
        m_pos.width = 0;
        m_pos.height = 0;
        src_el()->css_w().line_height_w().computed_value = height();
        return content_offset_width();
    }

    // A wrapping column flex container has no definite cross size when it measures an item's
    // content size: the line's cross size depends on the items themselves, so the transferred size
    // suggestion does not apply (CSS Flexbox 9.2). The measure nonetheless hands over the
    // container's inline size as an exact (already stretched) width, but the item is placed at the
    // line's cross size later. Deriving the block size from that provisional width makes a
    // replaced item's main size depend on a width it never has: a 40x30 image in a 27px-wide
    // wrapping column is 30px tall (and keeps its 40px width) until the line stretch resolves.
    // A nowrap column stretches to the container's definite width, where the ratio transfer is
    // right (40x30 -> 100x75). (Blink: FlexItem::ComputeFlexBaseSizeForChild / LayoutReplaced)
    const bool provisional_flex_width =
        (containing_block_size.size_mode & containing_block_context::size_mode_measure) &&
        !(containing_block_size.size_mode &
          (containing_block_context::size_mode_content |
           containing_block_context::size_mode_min_content)) &&
        css().get_width().is_predefined() && css().get_height().is_predefined() &&
        sz.width > 0 && sz.height > 0 && [&]() {
            auto p = parent();
            if (!p) return false;
            const auto disp = p->css().get_display();
            if (disp != display_flex && disp != display_inline_flex) return false;
            // Only a horizontal writing mode puts a column container's cross axis on the width; in a
            // vertical one the exact inline size is the main size and the ratio transfer is right.
            if (p->css().get_writing_mode() != writing_mode_horizontal_tb) return false;
            const auto dir = p->css().get_flex_direction();
            if (dir != flex_direction_column && dir != flex_direction_column_reverse) return false;
            return p->css().get_flex_wrap() != flex_wrap_nowrap;
        }();

    // An exact inline size handed down by a consumer (flex layout re-measuring the item at
    // the main size it assigned) is the used size even when the width property is a keyword:
    // the intrinsic branches below would report the natural size instead and the item would
    // outgrow its flex size (a width:max-content image with max-width:50% came back at its
    // full 512 in a 384 main size). The provisional-flex path above is the exception: the
    // width it hands over is a trial size the transfer must not use.
    const bool exact_inline_size = self_size.mode == writing_mode_horizontal_tb
        ? (containing_block_size.size_mode & containing_block_context::size_mode_exact_width) != 0
        : (containing_block_size.size_mode & containing_block_context::size_mode_exact_height) != 0;
    const bool width_is_auto =
        provisional_flex_width ||
        (self_size.width.type == containing_block_context::cbc_value_type_auto && !exact_inline_size);
    const bool height_is_auto =
        self_size.height.type == containing_block_context::cbc_value_type_auto;

    if (!width_is_auto) {
        m_pos.width = self_size.render_width;
    }
    if (!height_is_auto) {
        m_pos.height = self_size.render_height;
    }

    // The used ratio for a replaced element: an explicit aspect-ratio replaces the intrinsic
    // ratio, while `aspect-ratio: auto <ratio>` only falls back to the specified ratio when the
    // image has no intrinsic ratio. A ratio of 0 means unknown, so only the intrinsic ratio can
    // be used for the arithmetic below.
    float used_ratio = 0;  // height / width
    const aspect_ratio ar = css().get_aspect_ratio();
    // An SVG with no dimensions and no viewBox has no intrinsic ratio:
    // its reported size is the default object size, and an auto axis takes
    // its own default instead of a size transferred through a ratio. An
    // explicit aspect-ratio property still transfers (handled above via
    // has_intrinsic_ratio being false, which lets `auto <ratio>` fall back
    // to the specified ratio).
    const bool no_ratio = src_el()->is_no_ratio_replaced();
    const bool has_intrinsic_ratio = sz.width > 0 && sz.height > 0 && !no_ratio;
    if (!ar.is_auto() && !(ar.use_intrinsic && has_intrinsic_ratio)) {
        if (ar.width > 0 && ar.height > 0) used_ratio = ar.height / ar.width;
    } else if (has_intrinsic_ratio) {
        used_ratio = (float)sz.height / (float)sz.width;
    }

    if (width_is_auto && height_is_auto && src_el()->is_ratio_only_replaced()) {
        // An aspect ratio but no natural size (CSS 2.1 10.3.2): the width comes from the block-level
        // equation, i.e. the containing block's width. Its min-content contribution is cyclic and
        // counts as 0 (as for a percentage width), except for an absolutely positioned box, which is
        // sized against its own containing block. The max-content (shrink-to-fit) size keeps the
        // fill: the document root is measured in that mode, so it cannot tell a float from a block.
        const bool positioned = css().get_position() == element_position_absolute ||
                                css().get_position() == element_position_fixed;
        // A percentage width is cyclic while the box it belongs to is sized from its content (CSS
        // treats it as auto during intrinsic sizing), so such a box shrink-to-fits like an auto one.
        const auto cyclic_percent_width = [](const css_length& len) {
            return !len.is_predefined() &&
                   (len.units() == css_units_percentage || (len.is_calc() && len.calc_percent() != 0));
        };
        // A ratio-only box inside a float / inline-block / absolutely positioned / fit-content ancestor
        // (through auto-width blocks) has no width of its own to contribute: it is 0 wide, as in Chromium.
        const render_item* child_under = this;
        auto in_shrink_to_fit = [&]() {
            for (auto p = parent(); p; child_under = p.get(), p = p->parent()) {
                const auto& pc = p->css();
                const auto disp = pc.get_display();
                if (p == parent() && (disp == display_grid || disp == display_inline_grid)) return true;  // a grid sizes its tracks from the item's contribution
                if ((disp == display_flex || disp == display_inline_flex) && p != parent()) {
                    // A column item with an auto inline size that is not stretched is fit-content: the
                    // box this one fills is its own content, so a percentage width inside it is cyclic
                    // (Blink treats it as auto while that item is sized from its content).
                    const auto dir = pc.get_flex_direction();
                    const bool column = dir == flex_direction_column || dir == flex_direction_column_reverse;
                    const css_length& item_width = child_under->css().get_width();
                    if (column && item_width.is_predefined() && item_width.predef() == 0) {
                        const auto align = pc.get_flex_align_items();
                        const bool stretched = align == flex_align_items_stretch || align == flex_align_items_normal;
                        if (!stretched) return true;
                        // A stretched item takes the container's cross size, so the container's own
                        // width decides whether the percentage is indefinite.
                        if (!pc.get_width().is_predefined()) {
                            if (cyclic_percent_width(pc.get_width())) continue;
                            return false;
                        }
                        continue;
                    }
                }
                // (a flex / grid item is blockified: its float computes to none)
                const auto* pp = p->parent().get();
                const bool floated = pc.get_float() != float_none &&
                    !(pp && (pp->css().get_display() == display_flex || pp->css().get_display() == display_inline_flex ||
                             pp->css().get_display() == display_grid || pp->css().get_display() == display_inline_grid));
                if (floated || disp == display_inline_block || disp == display_inline_flex ||
                    disp == display_inline_grid || disp == display_inline_table)
                    // a definite width is a width to fill; a cyclic percentage one is auto
                    return pc.get_width().is_predefined() || cyclic_percent_width(pc.get_width());
                if (pc.get_position() == element_position_absolute || pc.get_position() == element_position_fixed)
                    return pc.get_width().is_predefined() || cyclic_percent_width(pc.get_width());
                if (pc.get_width().is_predefined() && pc.get_width().predef() > 0) return true;  // fit-content / max-content / min-content
                if (!pc.get_width().is_predefined()) {
                    // The ancestor is auto-width in this query (see above): keep walking to whatever
                    // gives it its size (a fit-content / float box).
                    if (cyclic_percent_width(pc.get_width())) continue;
                    return false;
                }
                if (disp != display_block && disp != display_inline && disp != display_flex) return false;  // grid / table: not handled here
            }
            return false;
        };
        // The 0 is only right while the inline size this box would fill is still open. The main size
        // of a block-level flex container is definite before its items are measured, so an item's
        // auto inline size resolves against it (Blink: LayoutReplaced::ComputeReplacedSize uses the
        // available inline size there); an inline-flex is shrink-to-fit, where the box's own inline
        // size would feed back into the container's, so it stays cyclic. 0 / the max-content trial
        // width means the container itself is being sized from this item.
        const bool flex_main_definite = [&]() -> bool {
            const auto* p = parent().get();
            if (!p || p->css().get_display() != display_flex ||
                p->css().get_writing_mode() != writing_mode_horizontal_tb) return false;
            const auto dir = p->css().get_flex_direction();
            if (dir != flex_direction_row && dir != flex_direction_row_reverse) return false;
            const pixel_t avail = (pixel_t)containing_block_size.render_inline_size();
            return avail > 0 && avail < 50000;
        }();
        // A percentage width in this branch did not resolve (the branch requires an auto used
        // width), so it is cyclic: the container's main size is indefinite for this item even
        // when a trial width is on offer, and the definite-main-size fill does not apply -- the
        // item contributes its default object size to the container's intrinsic size instead.
        // An auto width keeps the fill: at the container's resolved main size it is what lets
        // flex sizing and the cross-size transfer produce the item's final size.
        if ((is_min_content && !positioned) ||
            (is_content_size && !positioned && in_shrink_to_fit() &&
             (!flex_main_definite || cyclic_percent_width(css().get_width())))) {
            // (a percentage width is cyclic here: it counts as auto, and a box with no natural width then
            // takes the 300px default object width for the max-content size)
            const bool percent_width = css().get_width().units() == css_units_percentage && !css().get_width().is_predefined();
            m_pos.width = (percent_width && !is_min_content) ? 300 : 0;
        } else {
            const pixel_t avail = containing_block_size.render_width.type == containing_block_context::cbc_value_type_auto
                                      ? (pixel_t)containing_block_size.width
                                      : (pixel_t)containing_block_size.render_width;
            // (a max-content grid track measures its items against a huge width: a ratio-only box has no
            // intrinsic width to contribute there, it does not stretch to fill it)
            const bool cyclic_percent = css().get_width().units() == css_units_percentage && !css().get_width().is_predefined();
            m_pos.width = avail >= 50000 ? (cyclic_percent ? 300 : 0) : std::max<pixel_t>(0, avail - content_offset_width());
        }
        m_pos.height = used_ratio > 0 ? m_pos.width * used_ratio : sz.height;
    } else if (width_is_auto && height_is_auto) {
        // Both axes auto: the intrinsic inline size, with the used ratio (or the intrinsic ratio
        // when there is no explicit aspect-ratio) deriving the block size.
        m_pos.width = sz.width;
        m_pos.height = used_ratio > 0 ? m_pos.width * used_ratio : sz.height;
    } else if (width_is_auto) {
        if (used_ratio > 0) {
            m_pos.width = m_pos.height / used_ratio;
        } else if (no_ratio) {
            m_pos.width = sz.width;
        } else if (sz.height != 0) {
            m_pos.width = m_pos.height * sz.width / sz.height;
        }
    } else if (height_is_auto) {
        if (used_ratio > 0) {
            m_pos.height = m_pos.width * used_ratio;
        } else if (no_ratio) {
            m_pos.height = sz.height;
        } else if (sz.width != 0) {
            m_pos.height = m_pos.width * sz.height / sz.width;
        }
    }

    // Constraints. The tentative size is clamped per axis, and an auto axis is
    // re-derived from the other axis's clamped size -- but a definite axis is
    // never re-derived (CSS 2.1 10.4 / CSS Sizing constraint resolution):
    // width:400px with max-height:128px stays 400 wide with the height clamped
    // alone, while an auto width follows a clamped height (the MDN contributor
    // shape: max-height alone transfers to 202.89). Sequence: clamp the width,
    // re-derive an auto height, clamp the height, then re-derive and re-clamp
    // an auto width. On a same-axis conflict the min wins, as in CSS.
    //
    // A percentage resolves against the containing block's size. While that size
    // is indefinite (an intrinsic-size query passes 0), a block-flow query treats
    // a percentage max-width as 0 -- a replaced element with a percentage
    // max-width is compressible there. But when flex code measures the item
    // itself (size_mode_measure survives only on that direct query; a container
    // strips it from the contexts it hands to its children), the flex container's
    // inline size is definite, so the percentage is not cyclic: treat it as none
    // instead of letting the item's automatic minimum collapse to 0.
    // An exact-size context hands this box its own used size as the containing
    // block size (flex layout re-measures the item at the size it assigned): a
    // percentage limit resolved against it would apply the percentage to the box
    // itself -- max-width:50% on a 202.89 flex item clamped it again to 101.45.
    // The assigned size already went through the consumer's own clamping, so a
    // percentage limit is not re-derived there (calculate_containing_block_context
    // does the same for a block's min/max-width; absolute limits still apply).
    // A percentage floor against an indefinite containing block resolves to 0
    // and never binds, so the min pair needs no intrinsic-query guard.
    auto percent_based = [](const css_length& len) {
        return len.units() == css_units_percentage || (len.is_calc() && len.calc_percent() != 0);
    };
    const bool exact_phys_width = self_size.mode == writing_mode_horizontal_tb
        ? (containing_block_size.size_mode & containing_block_context::size_mode_exact_width) != 0
        : (containing_block_size.size_mode & containing_block_context::size_mode_exact_height) != 0;
    const bool exact_phys_height = self_size.mode == writing_mode_horizontal_tb
        ? (containing_block_size.size_mode & containing_block_context::size_mode_exact_height) != 0
        : (containing_block_size.size_mode & containing_block_context::size_mode_exact_width) != 0;
    auto resolve_limit = [&](const css_length& len, pixel_t basis, bool exact_axis,
                             bool is_max) -> pixel_t {
        const pixel_t none = is_max ? pixel_t(1e9) : pixel_t(0);
        if (len.is_predefined()) return none;
        if (percent_based(len) && exact_axis) return none;
        if (is_max && len.units() == css_units_percentage && basis <= 0 &&
            (containing_block_size.size_mode & containing_block_context::size_mode_measure))
            return none;
        // A max-content query hands a descendant an indefinite (zero) basis once the
        // direct measure bit has been stripped by the containers in between: the
        // percentage is cyclic there and behaves as none (CSS Sizing: a percentage
        // against an indefinite containing block does not resolve), so the element
        // contributes its definite/intrinsic size. Resolving it as 0 instead let a
        // percentage max-width erase e.g. a width:40px ratio-only img measured as a
        // flex item's content (its wrapper collapsed to the img's padding).
        if (is_max && percent_based(len) && basis <= 0 && is_content_size && !is_min_content)
            return none;
        return src_el()->get_document()->to_pixels(len, css().get_font_metrics(), basis);
    };
    const pixel_t max_w = resolve_limit(css().get_max_width(), containing_block_size.width, exact_phys_width, true);
    const pixel_t min_w = resolve_limit(css().get_min_width(), containing_block_size.width, exact_phys_width, false);
    const pixel_t max_h = resolve_limit(css().get_max_height(), containing_block_size.height, exact_phys_height, true);
    const pixel_t min_h = resolve_limit(css().get_min_height(), containing_block_size.height, exact_phys_height, false);
    auto clamp_width = [&]() {
        if (m_pos.width > max_w) m_pos.width = max_w;
        if (m_pos.width < min_w) m_pos.width = min_w;
    };
    auto clamp_height = [&]() {
        if (m_pos.height > max_h) m_pos.height = max_h;
        if (m_pos.height < min_h) m_pos.height = min_h;
    };
    auto derive_height = [&]() {
        if (used_ratio > 0) {
            m_pos.height = m_pos.width * used_ratio;
        } else if (no_ratio) {
            m_pos.height = sz.height;
        } else if (sz.width != 0) {
            m_pos.height = m_pos.width * sz.height / sz.width;
        }
    };
    auto derive_width = [&]() {
        if (used_ratio > 0) {
            m_pos.width = m_pos.height / used_ratio;
        } else if (no_ratio) {
            m_pos.width = sz.width;
        } else if (sz.height != 0) {
            m_pos.width = m_pos.height * sz.width / sz.height;
        }
    };
    clamp_width();
    if (height_is_auto) derive_height();
    clamp_height();
    if (width_is_auto) {
        derive_width();
        clamp_width();
    }

    src_el()->css_w().line_height_w().computed_value = height();

    return m_pos.width + content_offset_width();
}

void litehtml::render_item_image::_place(pixel_t x, pixel_t y,
                                         const containing_block_context& /*containing_block_size*/,
                                         formatting_context* /*fmt_ctx*/) {
    m_pos.move_to(x, y);
    m_pos.x += content_offset_left();
    m_pos.y += content_offset_top();
}

litehtml::pixel_t litehtml::render_item_image::calc_max_height(pixel_t image_height,
                                                               pixel_t containing_block_height) {
    document::ptr doc = src_el()->get_document();
    return doc->to_pixels(css().get_max_height(), css().get_font_metrics(),
                          containing_block_height == 0 ? image_height : containing_block_height);
}
