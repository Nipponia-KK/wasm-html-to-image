#include <functional>
#include "render_item.h"

#include <typeinfo>

#include "css_parser.h"
#include "document.h"
#include "document_container.h"
#include "types.h"

namespace {
bool subtree_paint_bounds(const std::shared_ptr<litehtml::render_item>& ri, litehtml::pixel_t ox,
                          litehtml::pixel_t oy, bool is_root, litehtml::pixel_t& left,
                          litehtml::pixel_t& top, litehtml::pixel_t& right,
                          litehtml::pixel_t& bottom, int depth);
// Page-band cull used by draw_children: true only when the subtree provably paints nothing
// into `clip` (drawing coordinates). Defined next to subtree_paint_bounds below.
bool subtree_outside_clip(const std::shared_ptr<litehtml::render_item>& ri, litehtml::pixel_t ox,
                          litehtml::pixel_t oy, const litehtml::position* clip);
}  // namespace


static bool is_orthogonal(litehtml::writing_mode wm1, litehtml::writing_mode wm2) {
    return (wm1 == litehtml::writing_mode_horizontal_tb) !=
           (wm2 == litehtml::writing_mode_horizontal_tb);
}

litehtml::render_item::render_item(std::shared_ptr<element> _src_el)
    : m_element(std::move(_src_el)), m_skip(false), m_force_ellipsis(false) {
    document::ptr doc = src_el()->get_document();
    auto fm = css().get_font_metrics();

    m_margins.left = doc->to_pixels(src_el()->css().get_margins().left, fm, 0);
    m_margins.right = doc->to_pixels(src_el()->css().get_margins().right, fm, 0);
    m_margins.top = doc->to_pixels(src_el()->css().get_margins().top, fm, 0);
    m_margins.bottom = doc->to_pixels(src_el()->css().get_margins().bottom, fm, 0);
    m_css_margins = m_margins;

    m_padding.left = doc->to_pixels(src_el()->css().get_padding().left, fm, 0);
    m_padding.right = doc->to_pixels(src_el()->css().get_padding().right, fm, 0);
    m_padding.top = doc->to_pixels(src_el()->css().get_padding().top, fm, 0);
    m_padding.bottom = doc->to_pixels(src_el()->css().get_padding().bottom, fm, 0);

    m_borders.left = doc->to_pixels(src_el()->css().get_borders().left.width, fm, 0);
    m_borders.right = doc->to_pixels(src_el()->css().get_borders().right.width, fm, 0);
    m_borders.top = doc->to_pixels(src_el()->css().get_borders().top.width, fm, 0);
    m_borders.bottom = doc->to_pixels(src_el()->css().get_borders().bottom.width, fm, 0);
}

litehtml::pixel_t litehtml::render_item::measure(
    const containing_block_context& containing_block_size, formatting_context* fmt_ctx) {
    if (m_cached_parent_width != (pixel_t)containing_block_size.inline_size()) {
        calc_outlines(containing_block_size.inline_size());
        m_cached_parent_width = containing_block_size.inline_size();
        m_is_measured = false;
    }

    if (m_is_measured && m_cached_cb_context == containing_block_size) {
        // Same value a fresh measure returns: the inline size, which is the height in vertical writing.
        return css().get_writing_mode() == writing_mode_horizontal_tb
                   ? m_pos.width + content_offset_width()
                   : m_pos.height + content_offset_height();
    }

    // Intrinsic-size queries are answered from a per-pass memo. The subtree is not touched, so the
    // measured state stays consistent with m_cached_cb_context; m_is_measured is cleared because
    // m_pos now holds another context's size and a real measure must run before it is trusted.
    //
    // Versioning of the memo (layout_epoch / fontVersion / cssVersion):
    //  - layout_epoch is bumped by reset_measure_budget() for every layout pass and by
    //    invalidate_measure_memos() when a container query restyles mid-pass. That is the only
    //    invalidation a per-pass row needs.
    //  - fontVersion and cssVersion are deliberately not in the key: a font or stylesheet change
    //    rebuilds the render tree in the resource loop, so the render items (and these rows) are
    //    recreated. A memo made to outlive a pass would have to key on both of them (and on
    //    layout_epoch) instead.
    // A row is reused only for the exact (element, cb) pair. Widening it to other cb inline sizes
    // is sound only for width-independent queries: min-content (content is laid out at 1px,
    // percentage inline sizes are cyclic) and the explicit 100000 "unbounded" probe (no line can
    // be capped by it). A definite-width measure resolves percentages and descendant sizes
    // against the cb inline size, so its rows must stay per-pass per-cb.
    document* memo_doc = src_el()->get_document().get();
    const bool memoizable = css().get_writing_mode() == writing_mode_horizontal_tb &&
                            (containing_block_size.size_mode & containing_block_context::size_mode_measure) &&
                            (src_el()->is_block_formatting_context() || !fmt_ctx);
    if (memoizable) {
        if (m_memo_epoch != memo_doc->layout_epoch()) {
            m_memo_epoch = memo_doc->layout_epoch();
            m_measure_memo.clear();
        }
        for (const auto& e : m_measure_memo) {
            if (e.cb == containing_block_size) {
                m_pos.width = e.w;
                m_pos.height = e.h;
                m_is_measured = false;
                return e.ret;
            }
        }
    }

    // Safety net for pathological nesting (deep flex/table trees re-measure every child several
    // times per level, i.e. exponentially). Once a layout pass has used its measure budget, an
    // element that already has a measured state keeps it instead of being measured again, which
    // bounds the work at the cost of an approximate layout.
    if (m_measured_once && src_el()->get_document()->measure_budget_exhausted()) {
        return css().get_writing_mode() == writing_mode_horizontal_tb
                   ? m_pos.width + content_offset_width()
                   : m_pos.height + content_offset_height();
    }

    m_cached_cb_context = containing_block_size;
    m_self_size = calculate_containing_block_context(containing_block_size);
    m_is_measured = true;
    m_measured_once = true;

    pixel_t measured_size = 0;
    if (src_el()->is_block_formatting_context() || !fmt_ctx) {
        formatting_context fmt(css().get_writing_mode());
        measured_size = _measure(containing_block_size, &fmt);
    } else {
        measured_size = _measure(containing_block_size, fmt_ctx);
    }

    // Update self_size with actual measured dimensions
    m_self_size.width = m_pos.width;
    m_self_size.height = m_pos.height;
    m_self_size.render_width = m_pos.width;
    m_self_size.render_height = m_pos.height;

    // Container Queries Support: Detect size change and trigger restyle
    if (css().get_container_type() != container_type_none) {
        pixel_t current_w = m_pos.width - m_padding.width() - m_borders.width();
        pixel_t current_h = m_pos.height - m_padding.height() - m_borders.height();

        if (src_el()->css_w().m_last_container_size.width != current_w ||
            src_el()->css_w().m_last_container_size.height != current_h) {
            src_el()->css_w().m_last_container_size.width = current_w;
            src_el()->css_w().m_last_container_size.height = current_h;

            // Trigger style re-computation for all descendants based on new container size
            src_el()->compute_styles(true);
            src_el()->get_document()->invalidate_measure_memos();

            // Re-measure after restyle to apply changes
            if (src_el()->is_block_formatting_context() || !fmt_ctx) {
                formatting_context fmt(css().get_writing_mode());
                measured_size = _measure(containing_block_size, &fmt);
            } else {
                measured_size = _measure(containing_block_size, fmt_ctx);
            }
        }
    }

    if (memoizable) {
        if (m_measure_memo.size() >= 8) m_measure_memo.erase(m_measure_memo.begin());
        m_measure_memo.push_back({containing_block_size, measured_size, m_pos.width, m_pos.height});
    }

    return measured_size;
}

void litehtml::render_item::place(pixel_t x, pixel_t y,
                                  const containing_block_context& containing_block_size,
                                  formatting_context* fmt_ctx) {
    pixel_t content_left = content_offset_left();
    pixel_t content_top = content_offset_top();

    m_pos.x = x + content_left;
    m_pos.y = y + content_top;

    // Same safety net as in measure(): children are positioned relative to their parent, so once
    // the layout budget is gone an already placed subtree is left as it is instead of being laid
    // out again (the re-layout chain is what blows up on deeply nested flex/table trees).
    if (m_placed_once && src_el()->get_document()->measure_budget_over()) {
        return;
    }
    m_placed_once = true;

    m_cached_cb_context.width = containing_block_size.width;
    m_cached_cb_context.height = containing_block_size.height;

    if (m_cached_cb_context != containing_block_size) {
        m_cached_cb_context = containing_block_size;
        m_self_size = calculate_containing_block_context(containing_block_size);
        m_is_measured = false;
    }

    if (src_el()->is_block_formatting_context() || !fmt_ctx) {
        formatting_context fmt(css().get_writing_mode());
        _place(x, y, containing_block_size, &fmt);
        fmt.apply_relative_shift(containing_block_size);
    } else {
        // The formatting context tracks logical offsets from the block-start edge. In vertical-rl
        // that edge is the right one, so convert the physical x of the content box.
        pixel_t push_x = x + content_left;
        if (css().get_writing_mode() == writing_mode_vertical_rl ||
            css().get_writing_mode() == writing_mode_sideways_rl) {
            push_x = (pixel_t)containing_block_size.width - (x + width()) + content_offset_right();
        }
        fmt_ctx->push_position(push_x, y + content_top);
        _place(x, y, containing_block_size, fmt_ctx);
        fmt_ctx->pop_position(push_x, y + content_top);
    }

    // A single-line form control (input, button, select) centres its text in the box: with a height
    // taller than the line the content sits in the middle, not at the top.
    const char* tag = src_el()->get_tagName();
    if ((tag[0] == 'i' || tag[0] == 'b' || tag[0] == 's') && css().get_writing_mode() == writing_mode_horizontal_tb &&
        (!strcmp(tag, "input") || !strcmp(tag, "button") || !strcmp(tag, "select")) && !m_children.empty()) {
        satoru::WritingModeContext wm = get_wm_context();
        pixel_t content_end = 0;
        // An inline wrapper (the placeholder's span) has no box of its own: its end is its text's.
        std::function<pixel_t(const std::shared_ptr<render_item>&)> end_of =
            [&](const std::shared_ptr<render_item>& el) {
                pixel_t end = el->block_end_pos(wm);
                if (el->src_el()->css().get_display() == display_inline) {
                    for (const auto& child : el->m_children) {
                        if (child->src_el()->css().get_display() != display_none) end = std::max(end, end_of(child));
                    }
                }
                return end;
            };
        for (const auto& el : m_children) {
            if (el->src_el()->css().get_display() != display_none) {
                content_end = std::max(content_end, end_of(el));
            }
        }
        const pixel_t shift = (m_pos.height - content_end) / 2;
        m_control_shift = shift > 0.5f ? shift : 0;
        if (shift > 0.5f) {
            for (auto& el : m_children) {
                el->block_shift(shift);
            }
        }
    }
}

litehtml::pixel_t litehtml::render_item::render(
    pixel_t x, pixel_t y, const containing_block_context& containing_block_size,
    formatting_context* fmt_ctx, bool second_pass) {
    if (containing_block_size.size_mode & containing_block_context::size_mode_measure) {
        return measure(containing_block_size, fmt_ctx);
    }

    pixel_t ret = measure(containing_block_size, fmt_ctx);
    place(x, y, containing_block_size, fmt_ctx);

    return ret;
}

void litehtml::render_item::calc_outlines(pixel_t parent_width) {
    m_padding.left = m_element->css().get_padding().left.calc_percent(parent_width);
    m_padding.right = m_element->css().get_padding().right.calc_percent(parent_width);

    m_borders.left = m_element->css().get_borders().left.width.calc_percent(parent_width);
    m_borders.right = m_element->css().get_borders().right.width.calc_percent(parent_width);

    m_margins.left = m_element->css().get_margins().left.calc_percent(parent_width);
    m_margins.right = m_element->css().get_margins().right.calc_percent(parent_width);

    m_margins.top = m_element->css().get_margins().top.calc_percent(parent_width);
    m_margins.bottom = m_element->css().get_margins().bottom.calc_percent(parent_width);
    m_css_margins = m_margins;
    m_collapsed_out_margin = 0;

    m_padding.top = m_element->css().get_padding().top.calc_percent(parent_width);
    m_padding.bottom = m_element->css().get_padding().bottom.calc_percent(parent_width);
}

litehtml::pixel_t litehtml::render_item::calc_auto_margins(pixel_t parent_width) {
    auto wm = get_wm_context();
    if (src_el()->is_block_box() && src_el()->css().get_position() != element_position_absolute &&
        src_el()->css().get_float() == float_none) {
        pixel_t old_margin_start = margin_inline_start();
        if (wm.inline_start(src_el()->css().get_margins()).is_predefined() &&
            wm.inline_end(src_el()->css().get_margins()).is_predefined()) {
            pixel_t el_inline_size = inline_size() - margin_inline_start() - margin_inline_end();
            if (el_inline_size <= parent_width) {
                margin_inline_start((parent_width - el_inline_size) / 2);
                margin_inline_end((parent_width - el_inline_size) - margin_inline_start());
            } else {
                margin_inline_start(0);
                margin_inline_end(0);
            }
        } else if (wm.inline_start(src_el()->css().get_margins()).is_predefined() &&
                   !wm.inline_end(src_el()->css().get_margins()).is_predefined()) {
            pixel_t el_inline_size = inline_size() - margin_inline_start();
            margin_inline_start(parent_width - el_inline_size);
            if (margin_inline_start() < 0) margin_inline_start(0);
        } else if (!wm.inline_start(src_el()->css().get_margins()).is_predefined() &&
                   wm.inline_end(src_el()->css().get_margins()).is_predefined()) {
            pixel_t el_inline_size = inline_size() - margin_inline_end();
            margin_inline_end(parent_width - el_inline_size);
            if (margin_inline_end() < 0) margin_inline_end(0);
        } else {
            direction dir = direction_ltr;
            if (src_el()->parent()) {
                dir = src_el()->parent()->get_direction();
            }
            if (dir == direction_rtl) {
                pixel_t el_inline_size = inline_size() - margin_inline_start();
                margin_inline_start(parent_width - el_inline_size);
                if (margin_inline_start() < 0) margin_inline_start(0);
            }
        }
        return margin_inline_start() - old_margin_start;
    }
    return 0;
}

std::pair<litehtml::pixel_t, litehtml::pixel_t> litehtml::render_item::relative_offset(
    const containing_block_context& containing_block_size) const {
    pixel_t inline_off = 0;
    pixel_t block_off = 0;
    if (src_el()->css().get_position() == element_position_relative) {
        css_offsets offsets = src_el()->css().get_offsets();
        if (!offsets.left.is_predefined()) {
            inline_off = offsets.left.calc_percent(containing_block_size.inline_size());
        } else if (!offsets.right.is_predefined()) {
            inline_off = -offsets.right.calc_percent(containing_block_size.inline_size());
        }
        if (!offsets.top.is_predefined()) {
            block_off = offsets.top.calc_percent(containing_block_size.block_size());
        } else if (!offsets.bottom.is_predefined()) {
            block_off = -offsets.bottom.calc_percent(containing_block_size.block_size());
        }
    }
    return {inline_off, block_off};
}

void litehtml::render_item::apply_relative_shift(
    const containing_block_context& containing_block_size) {
    if (src_el()->css().get_position() != element_position_relative) return;
    // Already shifted at this very position (the line box was finished again without moving
    // the item): shifting again would pile the offset up once per call.
    if (m_relative_shifted && m_pos == m_relative_shifted_pos) return;
    auto [inline_off, block_off] = relative_offset(containing_block_size);
    if (inline_off != 0) inline_shift(inline_off);
    if (block_off != 0) block_shift(block_off);
    m_relative_shifted_pos = m_pos;
    m_relative_shifted = true;
}

std::tuple<std::shared_ptr<litehtml::render_item>, std::shared_ptr<litehtml::render_item>,
           std::shared_ptr<litehtml::render_item> >
litehtml::render_item::split_inlines() {
    std::tuple<std::shared_ptr<litehtml::render_item>, std::shared_ptr<litehtml::render_item>,
               std::shared_ptr<litehtml::render_item> >
        ret;
    for (const auto& child : m_children) {
        // An absolutely positioned box is out of flow: it never splits the line it sits in
        // (horizontal writing only; vertical writing keeps the block-level treatment).
        const auto child_pos = child->src_el()->css().get_position();
        const bool out_of_flow = src_el()->css().get_writing_mode() == writing_mode_horizontal_tb &&
                                 (child_pos == element_position_absolute || child_pos == element_position_fixed);
        if (child->src_el()->is_block_box() && child->src_el()->css().get_float() == float_none &&
            !out_of_flow) {
            std::get<0>(ret) = clone();
            std::get<1>(ret) = child;
            std::get<2>(ret) = clone();

            std::get<1>(ret)->parent(std::get<0>(ret));
            std::get<2>(ret)->parent(std::get<0>(ret));

            bool found = false;
            for (const auto& ch : m_children) {
                if (ch == child) {
                    found = true;
                    continue;
                }
                if (!found) {
                    std::get<0>(ret)->add_child(ch);
                } else {
                    std::get<2>(ret)->add_child(ch);
                }
            }
            break;
        }
        if (!child->children().empty() && !child->src_el()->is_inline_box()) {
            auto child_split = child->split_inlines();
            if (std::get<0>(child_split)) {
                std::get<0>(ret) = clone();
                std::get<1>(ret) = std::get<1>(child_split);
                std::get<2>(ret) = clone();

                std::get<2>(ret)->parent(std::get<0>(ret));

                bool found = false;
                for (const auto& ch : m_children) {
                    if (ch == child) {
                        found = true;
                        continue;
                    }
                    if (!found) {
                        std::get<0>(ret)->add_child(ch);
                    } else {
                        std::get<2>(ret)->add_child(ch);
                    }
                }
                std::get<0>(ret)->add_child(std::get<0>(child_split));
                std::get<2>(ret)->add_child(std::get<2>(child_split));
                break;
            }
        }
    }
    return ret;
}

bool litehtml::render_item::fetch_positioned() {
    bool ret = false;

    m_positioned.clear();

    litehtml::element_position el_pos;

    for (auto& el : m_children) {
        el_pos = el->src_el()->css().get_position();
        if (el->src_el()->is_positioned()) {
            add_positioned(el);
        }
        if (!ret && (el_pos == element_position_absolute || el_pos == element_position_fixed)) {
            ret = true;
        }
        if (el->fetch_positioned()) {
            ret = true;
        }
    }
    return ret;
}

void litehtml::render_item::sort_positioned() {
    for (auto& el : m_positioned) {
        el->sort_positioned();
    }

    if (!m_positioned.empty()) {
        std::stable_sort(
            m_positioned.begin(), m_positioned.end(),
            [](const std::shared_ptr<render_item>& Left,
               const std::shared_ptr<render_item>& Right) {
                return (Left->src_el()->css().get_z_index() < Right->src_el()->css().get_z_index());
            });
    }
}

void litehtml::render_item::render_positioned(render_type rt) {
    position view_port;
    src_el()->get_document()->container()->get_viewport(view_port);

    element_position el_position;
    bool process;
    for (auto& el : m_positioned) {
        el_position = el->src_el()->css().get_position();

        process = false;
        if (el->src_el()->css().get_display() != display_none) {
            if (el_position == element_position_absolute) {
                if (rt != render_fixed_only) {
                    process = true;
                }
            } else if (el_position == element_position_fixed) {
                if (rt != render_no_fixed) {
                    process = true;
                }
            }
        }

        if (process) {
            containing_block_context containing_block_size;
            pixel_t cb_origin_x = 0;
            if (el_position == element_position_fixed ||
                (is_root() && !src_el()->is_positioned())) {
                containing_block_size.height = view_port.height;
                containing_block_size.width = view_port.width;
                // scrollbar-gutter: stable reserves the same strip of the
                // initial containing block that document::render subtracts
                // for in-flow layout. Positioned boxes whose containing
                // block is the ICB (and fixed boxes, whose containing block
                // is the viewport) resolve percentages and auto sizes
                // against the narrowed width too; without this they lay out
                // one gutter wider than the page around them.
                if (auto doc = src_el()->get_document()) {
                    const int gutter = doc->root()->css().get_scrollbar_gutter();
                    if (gutter == scrollbar_gutter_stable ||
                        gutter == scrollbar_gutter_stable_both_edges) {
                        containing_block_size.width.value -=
                            15 * (gutter == scrollbar_gutter_stable_both_edges ? 2 : 1);
                        // stable both-edges insets the scrollport on the inline-start
                        // side too: boxes against this containing block start one
                        // gutter in from the page edge.
                        if (gutter == scrollbar_gutter_stable_both_edges) {
                            cb_origin_x = 15;
                        }
                        if (containing_block_size.width.value < 0) {
                            containing_block_size.width.value = 0;
                        }
                    }
                }
            } else {
                containing_block_size.height = m_pos.height + m_padding.height();
                containing_block_size.width = m_pos.width + m_padding.width();
            }
            // an absolutely positioned box's percentage height resolves against this (definite) block
            containing_block_size.height.type = containing_block_context::cbc_value_type_absolute;

            css_length css_left = el->src_el()->css().get_offsets().left;
            css_length css_right = el->src_el()->css().get_offsets().right;
            css_length css_top = el->src_el()->css().get_offsets().top;
            css_length css_bottom = el->src_el()->css().get_offsets().bottom;

            bool need_render = false;

            css_length el_width = el->src_el()->css().get_width();
            css_length el_height = el->src_el()->css().get_height();

            auto fix_height_min_max = [&](pixel_t height) {
                auto max_height = el->css().get_max_height();
                auto min_height = el->css().get_min_height();
                if (!max_height.is_predefined()) {
                    pixel_t max_height_value =
                        max_height.calc_percent(containing_block_size.height);
                    if (height > max_height_value) {
                        height = max_height_value;
                    }
                }
                if (!min_height.is_predefined()) {
                    pixel_t min_height_value =
                        min_height.calc_percent(containing_block_size.height);
                    if (height < min_height_value) {
                        height = min_height_value;
                    }
                }
                height += el->content_offset_height();
                return height;
            };

            auto fix_width_min_max = [&](pixel_t width) {
                auto max_width = el->css().get_max_width();
                auto min_width = el->css().get_min_width();
                if (!max_width.is_predefined()) {
                    pixel_t max_width_value = max_width.calc_percent(containing_block_size.width);
                    if (width > max_width_value) {
                        width = max_width_value;
                    }
                }
                if (!min_width.is_predefined()) {
                    pixel_t min_height_value = min_width.calc_percent(containing_block_size.width);
                    if (width < min_height_value) {
                        width = min_height_value;
                    }
                }
                width += el->content_offset_width();
                return width;
            };

            // A replaced box takes its size from its own content (10.3.8/10.6.5), which the in-flow
            // pass never measured for an absolutely positioned one: measure it here, against the width
            // left between the insets when both are given (a ratio-only <svg> fills that width). Only
            // the size is kept; the position is solved below.
            if (el->src_el()->is_replaced()) {
                pixel_t avail = containing_block_size.width;
                if (!css_left.is_predefined() && !css_right.is_predefined()) {
                    avail -= css_left.calc_percent(containing_block_size.width) +
                             css_right.calc_percent(containing_block_size.width);
                }
                const pixel_t keep_x = el->m_pos.x;
                const pixel_t keep_y = el->m_pos.y;
                // (new_width() would add the old width as a "diff": this containing block has no
                // render_width, so set both fields.)
                containing_block_context natural_cb = containing_block_size;
                natural_cb.width = std::max<pixel_t>(0, avail);
                natural_cb.render_width = natural_cb.width;
                natural_cb.size_mode = containing_block_context::size_mode_content;
                el->measure(natural_cb, nullptr);
                el->m_pos.x = keep_x;
                el->m_pos.y = keep_y;
            }

            pixel_t bottom = 0;
            pixel_t top = 0;
            pixel_t height = 0;
            auto [el_static_offset_x, el_static_offset_y] = element_static_offset(el);
            pixel_t el_static_x = el->m_pos.x + el_static_offset_x;
            pixel_t el_static_y = el->m_pos.y + el_static_offset_y;
            // Calculate vertical position
            // https://www.w3.org/TR/CSS22/visudet.html#abs-non-replaced-height
            // 10.6.4 Absolutely positioned, non-replaced elements
            if (css_top.is_predefined() && !css_bottom.is_predefined() &&
                el_height.is_predefined()) {
                // 1. 'top' and 'height' are 'auto' and 'bottom' is not 'auto', then the height is
                // based on the content per 10.6.7, set 'auto' values for 'margin-top' and
                // 'margin-bottom' to 0, and solve for 'top'
                if (el->css().get_margins().top.is_predefined()) el->m_margins.top = 0;
                if (el->css().get_margins().bottom.is_predefined()) el->m_margins.bottom = 0;
                height = el->height();
                bottom = css_bottom.calc_percent(containing_block_size.height);
                top = containing_block_size.height - height - bottom;
            } else if (css_top.is_predefined() && css_bottom.is_predefined() &&
                       !el_height.is_predefined()) {
                // 2. 'top' and 'bottom' are 'auto' and 'height' is not 'auto', then set 'top' to
                // the static position, set 'auto' values for 'margin-top' and 'margin-bottom' to 0,
                // and solve for 'bottom'
                if (el->css().get_margins().top.is_predefined()) el->m_margins.top = 0;
                if (el->css().get_margins().bottom.is_predefined()) el->m_margins.bottom = 0;
                top = el_static_y - el->content_offset_top();
                height =
                    fix_height_min_max(el->get_predefined_height(containing_block_size.height));
            } else if (!css_top.is_predefined() && css_bottom.is_predefined() &&
                       el_height.is_predefined()) {
                // 3. 'height' and 'bottom' are 'auto' and 'top' is not 'auto', then the height is
                // based on the content per 10.6.7, set 'auto' values for 'margin-top' and
                // 'margin-bottom' to 0, and solve for 'bottom'
                if (el->css().get_margins().top.is_predefined()) el->m_margins.top = 0;
                if (el->css().get_margins().bottom.is_predefined()) el->m_margins.bottom = 0;
                height = el->height();
                top = css_top.calc_percent(containing_block_size.height);
            } else if (css_top.is_predefined() && !css_bottom.is_predefined() &&
                       !el_height.is_predefined()) {
                // 4. 'top' is 'auto', 'height' and 'bottom' are not 'auto', then set 'auto' values
                // for 'margin-top' and 'margin-bottom' to 0, and solve for 'top'
                if (el->css().get_margins().top.is_predefined()) el->m_margins.top = 0;
                if (el->css().get_margins().bottom.is_predefined()) el->m_margins.bottom = 0;
                height =
                    fix_height_min_max(el->get_predefined_height(containing_block_size.height));
                bottom = css_bottom.calc_percent(containing_block_size.height);
                top = containing_block_size.height - height - bottom;
            } else if (!css_top.is_predefined() && !css_bottom.is_predefined() &&
                       el_height.is_predefined()) {
                // 5. 'height' is 'auto', 'top' and 'bottom' are not 'auto', then 'auto' values for
                // 'margin-top' and 'margin-bottom' are set to 0 and solve for 'height'
                if (el->css().get_margins().top.is_predefined()) el->m_margins.top = 0;
                if (el->css().get_margins().bottom.is_predefined()) el->m_margins.bottom = 0;
                bottom = css_bottom.calc_percent(containing_block_size.height);
                top = css_top.calc_percent(containing_block_size.height);
                if (el->src_el()->is_replaced()) {
                    height = el->height() - el->content_offset_height();
                    pixel_t reminded = (containing_block_size.height - top - bottom) - height -
                                       el->content_offset_height();
                    // Auto margins centre the box vertically even when it overflows (a negative
                    // remainder); only the horizontal axis floors them at 0.
                    if (reminded != 0) {
                        int divider = 0;
                        if (el->css().get_margins().top.is_predefined()) divider++;
                        if (el->css().get_margins().bottom.is_predefined()) divider++;
                        if (divider != 0) {
                            if (el->css().get_margins().top.is_predefined())
                                el->m_margins.top = reminded / divider;
                            if (el->css().get_margins().bottom.is_predefined())
                                el->m_margins.bottom = reminded / divider;
                        }
                    }
                    height += el->content_offset_height();
                } else if (!el->css().get_aspect_ratio().is_auto() && !el_width.is_predefined() &&
                           el->css().get_aspect_ratio().width > 0) {
                    // A definite width and a preferred aspect ratio fix the height; the insets then
                    // only position the box (auto margins centre it) instead of stretching it.
                    const aspect_ratio ar = el->css().get_aspect_ratio();
                    height = el->get_predefined_width(containing_block_size.width) * ar.height / ar.width +
                             el->content_offset_height();
                    pixel_t remained = containing_block_size.height - height - top - bottom;
                    const auto& m = el->css().get_margins();
                    if (m.top.is_predefined() && m.bottom.is_predefined()) {
                        el->m_margins.top = el->m_margins.bottom = remained / 2;
                    } else if (m.top.is_predefined()) {
                        el->m_margins.top = remained;
                    } else if (m.bottom.is_predefined()) {
                        el->m_margins.bottom = remained;
                    }
                    if (m.top.is_predefined()) height += el->m_margins.top;
                    if (m.bottom.is_predefined()) height += el->m_margins.bottom;
                } else {
                    height = containing_block_size.height - top - bottom;
                }
                if (!el->css().get_max_height().is_predefined()) {
                    pixel_t max_height =
                        el->css().get_max_height().calc_percent(containing_block_size.height);
                    if (height - el->content_offset_height() > max_height) {
                        pixel_t reminded = height - el->content_offset_height() - max_height;
                        height = max_height;
                        int divider = 0;
                        if (el->css().get_margins().top.is_predefined()) divider++;
                        if (el->css().get_margins().bottom.is_predefined()) divider++;
                        if (divider != 0) {
                            if (el->css().get_margins().top.is_predefined())
                                el->m_margins.top = reminded / divider;
                            if (el->css().get_margins().bottom.is_predefined())
                                el->m_margins.bottom = reminded / divider;
                        }
                        height += el->content_offset_height();
                    }
                }
            } else if (!css_top.is_predefined() && css_bottom.is_predefined() &&
                       !el_height.is_predefined()) {
                // 6. 'bottom' is 'auto', 'top' and 'height' are not 'auto', then set 'auto' values
                // for 'margin-top' and 'margin-bottom' to 0 and solve for 'bottom'
                if (el->css().get_margins().top.is_predefined()) el->m_margins.top = 0;
                if (el->css().get_margins().bottom.is_predefined()) el->m_margins.bottom = 0;
                height =
                    fix_height_min_max(el->get_predefined_height(containing_block_size.height));
                top = css_top.calc_percent(containing_block_size.height);
            } else if (css_top.is_predefined() && css_bottom.is_predefined() &&
                       el_height.is_predefined()) {
                // If all three of 'top', 'height', and 'bottom' are auto, set 'top' to the static
                // position and apply rule number three.
                if (el->css().get_margins().top.is_predefined()) el->m_margins.top = 0;
                if (el->css().get_margins().bottom.is_predefined()) el->m_margins.bottom = 0;
                height = el->height();
                top = el_static_y - el->content_offset_top();
            } else {
                // If none of the three are 'auto':
                height =
                    fix_height_min_max(el->get_predefined_height(containing_block_size.height));
                top = css_top.calc_percent(containing_block_size.height);
                bottom = css_bottom.calc_percent(containing_block_size.height);
                pixel_t remained = containing_block_size.height - height - top - bottom;

                if (el->css().get_margins().top.is_predefined() &&
                    el->css().get_margins().bottom.is_predefined()) {
                    // If both 'margin-top' and 'margin-bottom' are 'auto', solve the equation under
                    // the extra constraint that the two margins get equal values.
                    el->m_margins.top = el->m_margins.bottom = remained / 2;
                    height += el->m_margins.top + el->m_margins.bottom;
                } else {
                    // If one of 'margin-top' or 'margin-bottom' is 'auto', solve the equation for
                    // that value.
                    if (el->css().get_margins().top.is_predefined()) {
                        el->m_margins.top = remained;
                        height += el->m_margins.top;
                    }
                    if (el->css().get_margins().bottom.is_predefined()) {
                        el->m_margins.bottom = remained;
                        height += el->m_margins.bottom;
                    }
                }
            }
            el->m_pos.y = top + el->content_offset_top();
            if (el->m_pos.height != height - el->content_offset_height()) {
                el->m_pos.height = height - el->content_offset_height();
                need_render = true;
            }

            // The shrink-to-fit width (10.3.7): min(max(preferred minimum,
            // available), preferred), where the insets are the definite offsets
            // the width must leave room for. Taking the preferred width alone
            // overflows a containing block narrower than the content (an abspos
            // box inside a 6px-wide abspos parent came out at its full 300px
            // preferred width -- all fuzz seed 43).
            auto shrink_to_fit = [&](pixel_t insets) -> pixel_t
            {
                position pos = el->m_pos;
                pixel_t pref = el->measure(
                    containing_block_size.new_width(containing_block_size.width,
                                                    containing_block_context::size_mode_content),
                    nullptr);
                el->m_pos = pos;
                pixel_t pref_min = el->measure(
                    containing_block_size.new_width(containing_block_size.width,
                                                    containing_block_context::size_mode_content |
                                                    containing_block_context::size_mode_min_content),
                    nullptr);
                el->m_pos = pos;
                pixel_t avail = containing_block_size.width - insets;
                return std::min(std::max(pref_min, avail), pref);
            };

            // Calculate horizontal position
            pixel_t right = 0;
            pixel_t left = 0;
            pixel_t width = 0;
            // https://www.w3.org/TR/CSS22/visudet.html#abs-non-replaced-width
            // 10.3.7 Absolutely positioned, non-replaced elements
            if (css_left.is_predefined() && !css_right.is_predefined() &&
                el_width.is_predefined()) {
                // 1. 'left' and 'width' are 'auto' and 'right' is not 'auto', then the width is
                // shrink-to-fit. Then solve for 'left'
                if (el->css().get_margins().left.is_predefined()) el->m_margins.left = 0;
                if (el->css().get_margins().right.is_predefined()) el->m_margins.right = 0;
                right = css_right.calc_percent(containing_block_size.width);
                width = shrink_to_fit(right);
                left = containing_block_size.width - width - right;
            } else if (css_left.is_predefined() && css_right.is_predefined() &&
                       !el_width.is_predefined()) {
                // 2. 'left' and 'right' are 'auto' and 'width' is not 'auto', then if the
                // 'direction' property of the element establishing the static-position containing
                // block is 'ltr' set 'left' to the static position, otherwise set 'right' to the
                // static position. Then solve for 'left' (if 'direction is 'rtl') or 'right' (if
                // 'direction' is 'ltr').
                if (el->css().get_margins().left.is_predefined()) el->m_margins.left = 0;
                if (el->css().get_margins().right.is_predefined()) el->m_margins.right = 0;
                left = el_static_x - el->content_offset_left();
                width = fix_width_min_max(el->get_predefined_width(containing_block_size.width));
            } else if (!css_left.is_predefined() && css_right.is_predefined() &&
                       el_width.is_predefined()) {
                // 3. 'width' and 'right' are 'auto' and 'left' is not 'auto', then the width is
                // shrink-to-fit . Then solve for 'right'
                if (el->css().get_margins().left.is_predefined()) el->m_margins.left = 0;
                if (el->css().get_margins().right.is_predefined()) el->m_margins.right = 0;
                left = css_left.calc_percent(containing_block_size.width);
                width = shrink_to_fit(left);
            } else if (css_left.is_predefined() && !css_right.is_predefined() &&
                       !el_width.is_predefined()) {
                // 4. 'left' is 'auto', 'width' and 'right' are not 'auto', then solve for 'left'
                if (el->css().get_margins().left.is_predefined()) el->m_margins.left = 0;
                if (el->css().get_margins().right.is_predefined()) el->m_margins.right = 0;
                right = css_right.calc_percent(containing_block_size.width);
                width = fix_width_min_max(el->get_predefined_width(containing_block_size.width));
                left = containing_block_size.width - right - width;
            } else if (!css_left.is_predefined() && !css_right.is_predefined() &&
                       el_width.is_predefined()) {
                // 5. 'width' is 'auto', 'left' and 'right' are not 'auto', then solve for 'width'
                if (el->css().get_margins().left.is_predefined()) el->m_margins.left = 0;
                if (el->css().get_margins().right.is_predefined()) el->m_margins.right = 0;
                left = css_left.calc_percent(containing_block_size.width);
                right = css_right.calc_percent(containing_block_size.width);
                if (el->src_el()->is_replaced()) {
                    width = el->width() - el->content_offset_width();
                    pixel_t reminded = (containing_block_size.width - left - right) - width -
                                       el->content_offset_width();
                    if (reminded != 0) {
                        int divider = 0;
                        if (el->css().get_margins().left.is_predefined()) divider++;
                        if (el->css().get_margins().right.is_predefined()) divider++;
                        if (divider != 0) {
                            if (el->css().get_margins().left.is_predefined())
                                el->m_margins.left = reminded / divider;
                            if (el->css().get_margins().right.is_predefined())
                                el->m_margins.right = reminded / divider;
                        }
                    }
                    width += el->content_offset_width();
                } else {
                    width = containing_block_size.width - left - right;
                }
                if (!el->css().get_max_width().is_predefined()) {
                    pixel_t max_width =
                        el->css().get_max_width().calc_percent(containing_block_size.width);
                    if (width - el->content_offset_width() > max_width) {
                        pixel_t reminded = width - el->content_offset_width() - max_width;
                        width = max_width;
                        int divider = 0;
                        if (el->css().get_margins().left.is_predefined()) divider++;
                        if (el->css().get_margins().right.is_predefined()) divider++;
                        if (divider != 0) {
                            if (el->css().get_margins().left.is_predefined())
                                el->m_margins.left = reminded / divider;
                            if (el->css().get_margins().right.is_predefined())
                                el->m_margins.right = reminded / divider;
                        }
                        width += el->content_offset_width();
                    }
                }
            } else if (!css_left.is_predefined() && css_right.is_predefined() &&
                       !el_width.is_predefined()) {
                // 6. 'right' is 'auto', 'left' and 'width' are not 'auto', then solve for 'right'
                if (el->css().get_margins().left.is_predefined()) el->m_margins.left = 0;
                if (el->css().get_margins().right.is_predefined()) el->m_margins.right = 0;
                left = css_left.calc_percent(containing_block_size.width);
                width = fix_width_min_max(el->get_predefined_width(containing_block_size.width));
            } else if (css_left.is_predefined() && css_right.is_predefined() &&
                       el_width.is_predefined()) {
                // If all three of 'left', 'width', and 'right' are 'auto': First set any 'auto'
                // values for 'margin-left' and 'margin-right' to 0. Then, if the 'direction'
                // property of the element establishing the static-position containing block is
                // 'ltr' set 'left' to the static position and apply rule number three below;
                // otherwise, set 'right' to the static position and apply rule number one below.
                if (el->css().get_margins().left.is_predefined()) el->m_margins.left = 0;
                if (el->css().get_margins().right.is_predefined()) el->m_margins.right = 0;
                left = el_static_x - el->content_offset_left();
                width = shrink_to_fit(left);
            } else {
                // If none of the three is 'auto':
                width = fix_width_min_max(el->get_predefined_width(containing_block_size.width));
                left = css_left.calc_percent(containing_block_size.width);
                right = css_right.calc_percent(containing_block_size.width);
                pixel_t remained = containing_block_size.width - width - left - right;

                if (el->css().get_margins().left.is_predefined() &&
                    el->css().get_margins().right.is_predefined()) {
                    // If both 'margin-left' and 'margin-right' are 'auto', solve the equation under
                    // the extra constraint that the two margins get equal values, unless this would
                    // make them negative, in which case when direction of the containing block is
                    // 'ltr' ('rtl'), set 'margin-left'
                    // ('margin-right') to zero and solve for 'margin-right' ('margin-left').
                    el->m_margins.left = el->m_margins.right = remained / 2;
                    if (el->m_margins.left < 0) {
                        el->m_margins.left = 0;
                        el->m_margins.right = remained;
                    }
                    width += el->m_margins.left + el->m_margins.right;
                } else {
                    // If one of 'margin-left' or 'margin-right' is 'auto', solve the equation
                    // for that value. If the values are over-constrained, ignore the value for
                    // 'left' (in case the 'direction' property of the containing block is 'rtl') or
                    // 'right' (in case 'direction' is 'ltr') and solve for that value.
                    if (el->css().get_margins().left.is_predefined()) {
                        el->m_margins.left = remained;
                        width += el->m_margins.left;
                    }
                    if (el->css().get_margins().right.is_predefined()) {
                        el->m_margins.right = remained;
                        width += el->m_margins.right;
                    }
                }
            }
            el->m_pos.x = left + el->content_offset_left() + cb_origin_x;
            // (a zero-width box matches by accident when nothing was laid out inside it: measure it)
            // A percentage width needs the render too: the box may already carry the solved
            // width from the static-position place in its parent's block layout, but its
            // children were laid out there against the measured (shrink) width, not against
            // the solved one -- the equal own width must not suppress the re-render.
            // A box with children needs the render too when its subtree was laid out by an
            // earlier provisional pass (a measure for a flex/grid parent's intrinsic size, or
            // a shrink-to-fit trial): the box's own solved size can already match while the
            // children still carry the trial geometry (a percentage-width child resolved
            // against the trial width, an auto-width child shrink-wrapped instead of filling).
            if (el->m_pos.width != width - el->content_offset_width() || el->m_pos.width == 0 ||
                el_width.is_predefined() || el_width.units() == css_units_percentage ||
                !el->children().empty()) {
                el->m_pos.width = width - el->content_offset_width();
                need_render = true;
            }

            if (need_render) {
                // measure() recomputes the margins from the CSS, which turns the `auto` margins solved
                // above (centering an over-constrained box) back into 0, so put them back afterwards.
                const auto solved_margins = el->m_margins;
                const auto& css_margins = el->css().get_margins();
                auto restore_auto_margins = [&]() {
                    if (css_margins.left.is_predefined()) el->m_margins.left = solved_margins.left;
                    if (css_margins.right.is_predefined()) el->m_margins.right = solved_margins.right;
                    if (css_margins.top.is_predefined()) el->m_margins.top = solved_margins.top;
                    if (css_margins.bottom.is_predefined()) el->m_margins.bottom = solved_margins.bottom;
                };
                if (el->css().get_width().is_predefined() || el->css().get_height().is_predefined()) {
                    // CSS 2.1 10.3.7/10.6.4 solve an auto width/height from the containing block
                    // remainder as a border-box size. size_mode_exact_* carries the value the CSS
                    // property would have, and calculate_containing_block_context() only strips
                    // padding/border from it for border-box elements. A content-box element must
                    // therefore get the remainder minus its own padding/border, or the used box
                    // overflows the containing block by exactly that amount.
                    // The solved sizes above are margin boxes; a border-box size excludes the margins.
                    auto exact_size = [&](pixel_t margin_box_size, pixel_t content_offset, pixel_t margins) {
                        if (el->css().get_box_sizing() == box_sizing_content_box) {
                            return std::max<pixel_t>(0, margin_box_size - content_offset);
                        }
                        return std::max<pixel_t>(0, margin_box_size - margins);
                    };
                    containing_block_context measure_cb = containing_block_size;
                    if (el->css().get_width().is_predefined()) {
                        if (!css_left.is_predefined() && !css_right.is_predefined()) {
                            measure_cb.width = exact_size(width, el->content_offset_width(),
                                                          el->m_margins.left + el->m_margins.right);
                            measure_cb.size_mode |= (measure_cb.mode == writing_mode_horizontal_tb
                                                         ? containing_block_context::size_mode_exact_width
                                                         : containing_block_context::size_mode_exact_height);
                        } else if (measure_cb.mode == writing_mode_horizontal_tb && !el->src_el()->is_replaced()) {
                            // shrink-to-fit width already solved above: lay the box out at exactly that
                            // width, so percentage widths inside resolve against it
                            measure_cb.width = exact_size(width, el->content_offset_width(),
                                                          el->m_margins.left + el->m_margins.right);
                            measure_cb.render_width = measure_cb.width;
                            measure_cb.size_mode |= containing_block_context::size_mode_exact_width;
                        } else {
                            measure_cb.size_mode |= containing_block_context::size_mode_content;
                        }
                    }
                    if (el->css().get_height().is_predefined()) {
                        if (!css_top.is_predefined() && !css_bottom.is_predefined()) {
                            measure_cb.height = exact_size(height, el->content_offset_height(),
                                                           el->m_margins.top + el->m_margins.bottom);
                            measure_cb.size_mode |= (measure_cb.mode == writing_mode_horizontal_tb
                                                         ? containing_block_context::size_mode_exact_height
                                                         : containing_block_context::size_mode_exact_width);
                        } else if (measure_cb.mode != writing_mode_horizontal_tb ||
                                   el->css().get_width().is_predefined()) {
                            // An auto height is solved from the content anyway. The content flag
                            // is a shrink-to-fit request for the inline size too, so it must not
                            // be set when the width is definite: it would turn a percentage or
                            // calc() width (`width: calc(100% - 4px)`) into auto.
                            measure_cb.size_mode |= containing_block_context::size_mode_content;
                        }
                    }
                    if (el->css().get_height().units() == css_units_percentage && !el->css().get_height().is_predefined() &&
                        measure_cb.mode == writing_mode_horizontal_tb) {
                        // a percentage height resolves against the containing block's padding box
                        measure_cb.height = containing_block_context::typed_pixel(
                            containing_block_size.height, containing_block_context::cbc_value_type_absolute);
                    }
                    el->measure(measure_cb, nullptr);
                    restore_auto_margins();
                    el->place(left + cb_origin_x, top, measure_cb, nullptr);
                } else {
                    el->measure(containing_block_size, nullptr);
                    restore_auto_margins();
                    el->place(left + cb_origin_x, top, containing_block_size, nullptr);
                }
            }

            if (el_position != element_position_fixed) {
                el->m_pos.x -= el_static_offset_x;
                el->m_pos.y -= el_static_offset_y;
            }

            if (el_position == element_position_fixed) {
                position fixed_pos = el->pos();
                el->get_redraw_box(fixed_pos);
                src_el()->get_document()->add_fixed_box(fixed_pos);
            }
        }

        el->render_positioned();
    }

    if (!m_positioned.empty()) {
        std::stable_sort(
            m_positioned.begin(), m_positioned.end(),
            [](const std::shared_ptr<render_item>& Left,
               const std::shared_ptr<render_item>& Right) {
                return (Left->src_el()->css().get_z_index() < Right->src_el()->css().get_z_index());
            });
    }
}

void litehtml::render_item::add_positioned(const std::shared_ptr<litehtml::render_item>& el) {
    // an absolutely positioned box resolves against the nearest real containing block; a box that is only
    // stacked (opacity, isolation, z-index...) keeps the nearest stacking ancestor
    const auto child_pos = el->src_el()->css().get_position();
    const bool child_is_abs = child_pos == element_position_absolute || child_pos == element_position_fixed;
    if ((child_is_abs ? src_el()->is_abs_containing_block() : src_el()->is_positioned()) || is_root()) {
        m_positioned.push_back(el);
    } else {
        auto el_parent = parent();
        if (el_parent) {
            el_parent->add_positioned(el);
        }
    }
}

void litehtml::render_item::get_redraw_box(litehtml::position& pos, pixel_t x /*= 0*/,
                                           pixel_t y /*= 0*/) {
    if (is_visible()) {
        pixel_t p_left = std::min(pos.left(), x + m_pos.left() - m_padding.left - m_borders.left);
        pixel_t p_right =
            std::max(pos.right(), x + m_pos.right() + m_padding.left + m_borders.left);
        pixel_t p_top = std::min(pos.top(), y + m_pos.top() - m_padding.top - m_borders.top);
        pixel_t p_bottom =
            std::max(pos.bottom(), y + m_pos.bottom() + m_padding.bottom + m_borders.bottom);

        pos.x = p_left;
        pos.y = p_top;
        pos.width = p_right - p_left;
        pos.height = p_bottom - p_top;

        if (src_el()->css().get_overflow() == overflow_visible) {
            for (auto& el : m_children) {
                if (el->src_el()->css().get_position() != element_position_fixed) {
                    el->get_redraw_box(pos, x + m_pos.x, y + m_pos.y);
                }
            }
        }
    }
}

void litehtml::render_item::calc_document_size(litehtml::size& sz, pixel_t x /*= 0*/,
                                               pixel_t y /*= 0*/) {
    // Fixed-position subtrees are laid out relative to the viewport and must not
    // contribute to the document size (Chromium parity). Skip the whole subtree,
    // not just the fixed box itself, so viewport-based child offsets (e.g. a
    // bottom-anchored child with top ~3000px) cannot leak into the height.
    if (src_el()->css().get_position() == element_position_fixed) {
        return;
    }
    if (css().get_display() != display_inline && css().get_display() != display_table_row) {
        if (is_visible()) {
            if (src_el()->css().get_position() != element_position_fixed) {
                sz.width = std::max(sz.width, x + right());
                sz.height = std::max(sz.height, y + bottom());

                // Account for box-shadow
                const auto& shadows = css().get_box_shadow();
                if (!shadows.empty()) {
                    for (const auto& s : shadows) {
                        // A transparent shadow, or one entirely behind the box, paints nothing
                        // outside it (Tailwind's `0 0 #0000` defaults) and must not grow the page.
                        const bool paints_outside = s.color.alpha != 0 &&
                            (s.x.val() != 0 || s.y.val() != 0 || s.blur.val() != 0 || s.spread.val() > 0);
                        if (!s.inset && paints_outside) {
                            pixel_t offset_x = src_el()->get_document()->to_pixels(
                                s.x, css().get_font_metrics(), 0);
                            pixel_t offset_y = src_el()->get_document()->to_pixels(
                                s.y, css().get_font_metrics(), 0);
                            pixel_t blur = src_el()->get_document()->to_pixels(
                                s.blur, css().get_font_metrics(), 0);
                            pixel_t spread = src_el()->get_document()->to_pixels(
                                s.spread, css().get_font_metrics(), 0);

                            sz.width =
                                std::max(sz.width, x + right() + offset_x + spread + blur + 10);
                            sz.height =
                                std::max(sz.height, y + bottom() + offset_y + spread + blur + 10);
                        }
                    }
                }
            }

            if (is_one_of(src_el()->css().get_overflow(), overflow_scroll, overflow_auto)) {
                size child_size;
                for (const auto& el : m_children) {
                    el->calc_document_size(child_size, 0, 0);
                }
                if (!m_scroll_view) {
                    m_scroll_view = std::make_shared<scroll_view>(m_pos, child_size);
                } else {
                    m_scroll_view->set(m_pos, child_size);
                }
            } else {
                // All children of tables and blocks with style other than "overflow: visible" are
                // inside element. We can skip calculating the size of children
                if (src_el()->css().get_overflow() == overflow_visible &&
                    src_el()->css().get_display() != display_table) {
                    for (const auto& el : m_children) {
                        el->calc_document_size(sz, x + m_pos.x, y + m_pos.y);
                    }
                } else {
                    size child_size;
                    for (const auto& el : m_children) {
                        el->calc_document_size(child_size, 0, 0);
                    }
                }
            }
        }
    } else {
        position::vector boxes;
        get_inline_boxes(boxes);
        for (const auto& box : boxes) {
            sz.width = std::max(sz.width, x + box.x + box.width);
            sz.height = std::max(sz.height, y + box.y + box.height);
        }
    }
}

void litehtml::render_item::draw_stacking_context(uint_ptr hdc, pixel_t x, pixel_t y,
                                                  const position* clip, bool with_positioned) {
    if (!is_visible()) return;

    std::map<int, bool> z_indexes;
    if (with_positioned) {
        // The boxes this stacking context paints in its z-index passes: the stacked descendants not inside
        // another stacking context. (m_positioned follows containing blocks, which is not the same set.)
        std::function<void(const render_item&)> collect = [&](const render_item& ri) {
            for (const auto& child : ri.m_children) {
                if (child->src_el()->is_positioned()) {
                    int z_index = 0;
                    if (child->src_el()->css().get_position() != element_position_static) {
                        z_index = child->src_el()->css().get_z_index();
                    }
                    z_indexes[z_index] = true;
                } else {
                    collect(*child);
                }
            }
        };
        collect(*this);

        for (const auto& idx : z_indexes) {
            if (idx.first < 0) {
                draw_children(hdc, x, y, clip, draw_positioned, idx.first);
            }
        }
    }
    draw_children(hdc, x, y, clip, draw_block, 0);
    draw_children(hdc, x, y, clip, draw_floats, 0);
    draw_children(hdc, x, y, clip, draw_inlines, 0);
    if (with_positioned) {
        for (auto& z_index : z_indexes) {
            if (z_index.first == 0) {
                draw_children(hdc, x, y, clip, draw_positioned, z_index.first);
            }
        }

        for (auto& z_index : z_indexes) {
            if (z_index.first > 0) {
                draw_children(hdc, x, y, clip, draw_positioned, z_index.first);
            }
        }
    }
}

void litehtml::render_item::draw_children(uint_ptr hdc, pixel_t x, pixel_t y, const position* clip,
                                          draw_flag flag, int zindex) {
    position pos = m_pos;
    pos.x += x - get_scroll_left();
    pos.y += y - get_scroll_top();

    document::ptr doc = src_el()->get_document();

    bool overflow_clipped = false;
    if (src_el()->css().get_overflow() != overflow_visible) {
        // TODO: Process overflow for inline elements
        if (src_el()->css().get_display() != display_inline) {
            position clip_box = m_pos;
            clip_box += m_padding;
            clip_box.x += x;
            clip_box.y += y;
            position border_box = clip_box;
            border_box += m_borders;

            border_radiuses bdr_radius = src_el()->css().get_borders().radius.calc_percents(
                border_box.width, border_box.height);

            bdr_radius -= m_borders;

            doc->container()->set_clip(clip_box, bdr_radius);
            overflow_clipped = true;
        }
    }

    for (const auto& el : m_children) {
        if (el->is_visible()) {
            // Page-band cull: when the caller draws one visible slice of the document (PDF
            // page bands, cropped raster/SVG exports) the `clip` is that slice in drawing
            // coordinates, so a child whose whole subtree paints outside it is skipped
            // instead of walked. nullptr (full-document draw) never culls. Positioned /
            // fixed boxes, box-decoration-break: clone, transforms, tables, grid and
            // list-item subtrees are rejected by the bounds helper and keep drawing, so
            // fragmentation extras (fixed, repeated <thead>, clone fragments) are safe.
            if (subtree_outside_clip(el, pos.x, pos.y, clip)) continue;
            bool is_positioned = el->src_el()->is_positioned();
            int el_z_index = 0;
            if (el->src_el()->css().get_position() != element_position_static) {
                el_z_index = el->src_el()->css().get_z_index();
            }

            bool is_active = false;
            bool force_stacking = false;
            bool should_recurse = true;

            switch (flag) {
                case draw_positioned:
                    if (is_positioned && el_z_index == zindex) {
                        is_active = true;
                        force_stacking = true;
                        should_recurse = false;
                    }
                    break;
                case draw_block:
                    if (!is_positioned && !el->src_el()->is_inline() &&
                        el->src_el()->css().get_float() == float_none) {
                        is_active = true;
                    }
                    break;
                case draw_floats:
                    if (!is_positioned && el->src_el()->css().get_float() != float_none) {
                        is_active = true;
                        force_stacking = true;
                        should_recurse = false;
                    }
                    break;
                case draw_inlines:
                    if (!is_positioned && el->src_el()->is_inline() &&
                        el->src_el()->css().get_float() == float_none) {
                        is_active = true;
                        if (el->src_el()->is_inline_box()) {
                            force_stacking = true;
                            should_recurse = false;
                        }
                    }
                    break;
                default:
                    break;
            }

            if (is_active) {
                float opacity = el->src_el()->css().get_opacity();
                const auto& transform = el->src_el()->css().get_transform();
                const auto& rotate = el->src_el()->css().get_rotate();
                const auto& scale = el->src_el()->css().get_scale();
                const auto& translate = el->src_el()->css().get_translate();
                const auto& filter = el->src_el()->css().get_filter();
                const auto& backdrop_filter = el->src_el()->css().get_backdrop_filter();
                const auto& mask = el->src_el()->css().get_mask();
                const auto& offset_path = el->src_el()->css().get_offset_path();
                const auto& offset_distance = el->src_el()->css().get_offset_distance();
                const auto& offset_rotate = el->src_el()->css().get_offset_rotate();
                const auto& offset_anchor = el->src_el()->css().get_offset_anchor();
                const auto& offset_position = el->src_el()->css().get_offset_position();
                css_token_vector clip_path;
                el->src_el()->get_custom_property(_clip_path_, clip_path);
                if (!clip_path.empty()) {
                    // clip-rule: evenodd is forwarded to the container as a leading keyword.
                    css_token_vector clip_rule;
                    if (el->src_el()->get_custom_property(_clip_rule_, clip_rule) &&
                        !clip_rule.empty() && clip_rule[0].type == IDENT &&
                        lowcase(clip_rule[0].ident()) == "evenodd") {
                        clip_path.insert(clip_path.begin(), css_token(IDENT, string("evenodd")));
                    }
                }

                blend_mode mix_blend = el->src_el()->css().get_mix_blend_mode();
                bool has_offset = !offset_path.empty();
                bool has_props = (opacity < 1.0f) || (mix_blend != blend_mode_normal) || !transform.empty() || !rotate.empty() ||
                                 !scale.empty() || !translate.empty() || !filter.empty() ||
                                 !backdrop_filter.empty() || !clip_path.empty() || !mask.empty() ||
                                 has_offset;

                // An opacity layer without filters / masks / clip-paths / offset paths / blend modes
                // is pushed inside the element's own transform (the result is identical) with a
                // bound of what the subtree can paint, instead of a full-canvas layer.
                bool bounded_layer = false;
                position layer_bounds;
                if (opacity < 1.0f && mix_blend == blend_mode_normal && !has_offset &&
                    filter.empty() && backdrop_filter.empty() && mask.empty() && clip_path.empty() &&
                    el->src_el()->css().get_position() != element_position_fixed) {
                    pixel_t l = 1e9f, t = 1e9f, r = -1e9f, b = -1e9f;
                    if (subtree_paint_bounds(el, pos.x, pos.y, true, l, t, r, b, 0)) {
                        layer_bounds = position(l, t, r - l, b - t);
                        bounded_layer = true;
                    }
                }

                if (has_props) {
                    if (!bounded_layer && (opacity < 1.0f || mix_blend != blend_mode_normal))
                        doc->container()->push_layer(hdc, opacity, mix_blend);
                    if (!backdrop_filter.empty()) doc->container()->push_backdrop_filter(hdc, el);

                    if (has_offset) {
                        // Reference box = containing block (the box that is currently drawing);
                        // element box = the drawn border box in absolute output coordinates.
                        position offset_ref_box = pos;
                        position offset_box = el->pos();
                        if (el->src_el()->css().get_position() != element_position_fixed) {
                            offset_box.x += pos.x;
                            offset_box.y += pos.y;
                        }
                        doc->container()->push_offset_path(
                            hdc, offset_path, offset_position, offset_distance, offset_rotate,
                            offset_anchor, el->src_el()->css().get_transform_origin(),
                            offset_ref_box, offset_box);
                    }

                    css_token_vector merged_transform;
                    if (!translate.empty()) {
                        css_token tok;
                        tok.type = CV_FUNCTION;
                        tok.name = "translate";
                        tok.value = translate;
                        merged_transform.push_back(tok);
                    }
                    if (!rotate.empty()) {
                        css_token tok;
                        tok.type = CV_FUNCTION;
                        tok.name = "rotate";
                        tok.value = rotate;
                        merged_transform.push_back(tok);
                    }
                    if (!scale.empty()) {
                        css_token tok;
                        tok.type = CV_FUNCTION;
                        tok.name = "scale";
                        tok.value = scale;
                        merged_transform.push_back(tok);
                    }
                    if (!transform.empty()) {
                        merged_transform.insert(merged_transform.end(), transform.begin(), transform.end());
                    }

                    if (el->src_el()->css().get_position() == element_position_fixed) {
                        if (!merged_transform.empty())
                            doc->container()->push_transform(
                                hdc, merged_transform, el->src_el()->css().get_transform_origin(),
                                el->pos());
                        if (!filter.empty()) doc->container()->push_filter(hdc, filter);
                        if (!clip_path.empty())
                            doc->container()->push_clip_path(hdc, clip_path, el->pos());
                        if (!mask.empty())
                            doc->container()->push_mask(hdc, mask, el->pos());
                    } else {
                        position el_pos = el->pos();
                        el_pos.x += pos.x;
                        el_pos.y += pos.y;

                        if (!merged_transform.empty())
                            doc->container()->push_transform(
                                hdc, merged_transform, el->src_el()->css().get_transform_origin(), el_pos);
                        if (bounded_layer)
                            doc->container()->push_layer(hdc, opacity, mix_blend, layer_bounds);
                        if (!filter.empty()) doc->container()->push_filter(hdc, filter);
                        if (!clip_path.empty())
                            doc->container()->push_clip_path(hdc, clip_path, el_pos);
                        if (!mask.empty())
                            doc->container()->push_mask(hdc, mask, el_pos);
                    }
                }

                // `clip: rect()` clips an absolutely positioned box and everything in it (it is not limited to
                // its children: a visually hidden `overflow: hidden; clip: rect(0, 0, 0, 0)` link paints nothing).
                bool box_clipped = false;
                {
                    const auto el_pos_kind = el->src_el()->css().get_position();
                    const auto& bclip = el->src_el()->css().get_clip();
                    if ((el_pos_kind == element_position_absolute || el_pos_kind == element_position_fixed) &&
                        bclip.size() == 1 && bclip[0].type == CV_FUNCTION && lowcase(bclip[0].name) == "rect") {
                        // rect(<top>, <right>, <bottom>, <left>) or the legacy space-separated rect(<top> <right> <bottom> <left>)
                        std::vector<css_token> cvals;
                        auto cargs = parse_comma_separated_list(bclip[0].value);
                        for (const auto& group : cargs) {
                            for (const auto& tok : group) {
                                if (tok.type != WHITESPACE) cvals.push_back(tok);
                            }
                        }
                        if (cvals.size() == 4) {
                            position border_box = el->pos();
                            border_box += el->get_paddings();
                            border_box += el->get_borders();
                            const auto& fm = el->css().get_font_metrics();
                            // an `auto` edge is the corresponding edge of the border box
                            auto edge = [&](const css_token& tok, pixel_t auto_value) -> pixel_t {
                                if (tok.type == IDENT && lowcase(tok.ident()) == "auto") return auto_value;
                                css_length len;
                                len.from_token(tok, f_length);
                                return doc->to_pixels(len, fm, 0);
                            };
                            const pixel_t ct = edge(cvals[0], 0), cr = edge(cvals[1], border_box.width);
                            const pixel_t cb = edge(cvals[2], border_box.height), cl = edge(cvals[3], 0);
                            const bool is_fixed = el_pos_kind == element_position_fixed && flag == draw_positioned;
                            position rect;
                            rect.x = (is_fixed ? 0 : pos.x) + border_box.x + cl;
                            rect.y = (is_fixed ? 0 : pos.y) + border_box.y + ct;
                            rect.width = std::max<pixel_t>(0, cr - cl);
                            rect.height = std::max<pixel_t>(0, cb - ct);
                            doc->container()->set_clip(rect, border_radiuses());
                            box_clipped = true;
                        }
                    }
                }

                if (flag == draw_positioned &&
                    el->src_el()->css().get_position() == element_position_fixed) {
                    el->src_el()->draw(hdc, 0, 0, clip, el);
                    el->draw_stacking_context(hdc, 0, 0, clip, true);
                } else {
                    el->src_el()->draw(hdc, pos.x, pos.y, clip, el);
                    if (force_stacking) {
                        el->draw_stacking_context(hdc, pos.x, pos.y, clip,
                                                  (flag == draw_positioned));
                    }
                }
                if (box_clipped) doc->container()->del_clip();

                if (has_props) {
                    if (!mask.empty()) doc->container()->pop_mask(hdc);
                    if (!clip_path.empty()) doc->container()->pop_clip_path(hdc);
                    if (!filter.empty()) doc->container()->pop_filter(hdc);
                    if (bounded_layer) doc->container()->pop_layer(hdc);
                    if (!rotate.empty() || !scale.empty() || !translate.empty() || !transform.empty()) doc->container()->pop_transform(hdc);
                    if (has_offset) doc->container()->pop_offset_path(hdc);
                    if (!backdrop_filter.empty()) doc->container()->pop_backdrop_filter(hdc);
                    if (!bounded_layer && (opacity < 1.0f || mix_blend != blend_mode_normal)) doc->container()->pop_layer(hdc);
                }
            }

            if (should_recurse) {
                // Recurse for elements that allow it in this pass
                if (flag == draw_positioned) {
                    if (!is_positioned) {
                        el->draw_children(hdc, pos.x, pos.y, clip, flag, zindex);
                    }
                } else {
                    if (el->src_el()->css().get_float() == float_none &&
                        !el->src_el()->is_inline_box() && !is_positioned) {
                        el->draw_children(hdc, pos.x, pos.y, clip, flag, zindex);
                    }
                }
            }
        }
    }

    if (overflow_clipped) {
        doc->container()->del_clip();
    }

    if (!src_el()->css().get_outline().top.width.is_predefined() &&
        src_el()->css().get_outline().top.width.val() > 0) {
        position outline_box = m_pos;
        outline_box += m_padding;
        outline_box += m_borders;
        outline_box.x += x;
        outline_box.y += y;

        pixel_t offset = doc->to_pixels(src_el()->css().get_outline_offset(),
                                        src_el()->css().get_font_metrics(), 0);
        outline_box.x -= offset;
        outline_box.y -= offset;
        outline_box.width += offset * 2;
        outline_box.height += offset * 2;

        doc->container()->draw_borders(hdc, src_el()->css().get_outline(), outline_box, false);
    }
}

std::shared_ptr<litehtml::element> litehtml::render_item::get_child_by_point(
    pixel_t x, pixel_t y, pixel_t client_x, pixel_t client_y, draw_flag flag, int zindex,
    const std::function<bool(const std::shared_ptr<render_item>&)>& check) {
    if (src_el()->css().get_overflow() > overflow_visible) {
        if (!m_pos.is_point_inside(x, y)) {
            return nullptr;
        }
    }

    element::ptr ret = nullptr;

    position el_pos = m_pos;
    el_pos.x = x - el_pos.x + get_scroll_left();
    el_pos.y = y - el_pos.y + get_scroll_top();

    for (auto i = m_children.crbegin(); i != m_children.crend() && !ret; std::advance(i, 1)) {
        const auto& el = *i;

        if (el->is_visible() && el->src_el()->css().get_display() != display_inline_text) {
            bool process = true;
            switch (flag) {
                case draw_positioned:
                    if (el->src_el()->is_positioned() &&
                        el->src_el()->css().get_z_index() == zindex) {
                        if (el->src_el()->css().get_position() == element_position_fixed) {
                            ret = el->get_element_by_point(client_x, client_y, client_x, client_y,
                                                           check);
                            if (!ret && el->is_point_inside(client_x, client_y)) {
                                if (!check || check(el)) {
                                    ret = el->src_el();
                                }
                            }
                        } else {
                            ret = el->get_element_by_point(el_pos.x, el_pos.y, client_x, client_y,
                                                           check);
                            if (!ret && el->is_point_inside(el_pos.x, el_pos.y)) {
                                if (!check || check(el)) {
                                    ret = el->src_el();
                                }
                            }
                        }
                        process = false;
                    }
                    break;
                case draw_block:
                    if (!el->src_el()->is_inline() &&
                        el->src_el()->css().get_float() == float_none &&
                        !el->src_el()->is_positioned()) {
                        ret = el->get_child_by_point(el_pos.x, el_pos.y, client_x, client_y, flag,
                                                     zindex, check);

                        if (!ret && el->is_point_inside(el_pos.x, el_pos.y)) {
                            if (!check || check(el)) {
                                ret = el->src_el();
                            }
                        }
                        process = false;
                    }
                    break;
                case draw_floats:
                    if (el->src_el()->css().get_float() != float_none &&
                        !el->src_el()->is_positioned()) {
                        ret =
                            el->get_element_by_point(el_pos.x, el_pos.y, client_x, client_y, check);

                        if (!ret && el->is_point_inside(el_pos.x, el_pos.y)) {
                            ret = el->src_el();
                        }
                        process = false;
                    }
                    break;
                case draw_inlines:
                    if (el->src_el()->is_inline() &&
                        el->src_el()->css().get_float() == float_none &&
                        !el->src_el()->is_positioned()) {
                        if (el->src_el()->css().get_display() == display_inline_block ||
                            el->src_el()->css().get_display() == display_inline_table ||
                            el->src_el()->css().get_display() == display_inline_flex ||
                            el->src_el()->css().get_display() == display_inline_grid) {
                            ret = el->get_element_by_point(el_pos.x, el_pos.y, client_x, client_y,
                                                           check);
                            process = false;
                        }
                        if (!ret && el->is_point_inside(el_pos.x, el_pos.y)) {
                            if (!check || check(el)) {
                                ret = el->src_el();
                            }
                        }
                    }
                    break;
                default:
                    break;
            }

            if (process && !el->src_el()->is_positioned()) {
                if (flag == draw_positioned) {
                    element::ptr child = el->get_child_by_point(el_pos.x, el_pos.y, client_x,
                                                                client_y, flag, zindex, check);
                    if (child) {
                        if (!check) {
                            ret = child;
                        } else {
                            child->run_on_renderers(
                                [&check, &ret,
                                 &child](const std::shared_ptr<render_item>& ri) -> bool {
                                    if (check(ri)) {
                                        ret = child;
                                        return false;
                                    }
                                    return true;
                                });
                        }
                    }
                } else {
                    if (el->src_el()->css().get_float() == float_none &&
                        el->src_el()->css().get_display() != display_inline_block &&
                        el->src_el()->css().get_display() != display_inline_flex &&
                        el->src_el()->css().get_display() != display_inline_grid) {
                        element::ptr child = el->get_child_by_point(el_pos.x, el_pos.y, client_x,
                                                                    client_y, flag, zindex, check);
                        if (child) {
                            if (!check) {
                                ret = child;
                            } else {
                                child->run_on_renderers(
                                    [&check, &ret,
                                     &child](const std::shared_ptr<render_item>& ri) -> bool {
                                        if (check(ri)) {
                                            ret = child;
                                            return false;
                                        }
                                        return true;
                                    });
                            }
                        }
                    }
                }
            }
        }
    }

    return ret;
}

std::shared_ptr<litehtml::element> litehtml::render_item::get_element_by_point(
    pixel_t x, pixel_t y, pixel_t client_x, pixel_t client_y,
    const std::function<bool(const std::shared_ptr<render_item>&)>& check) {
    if (!is_visible()) return nullptr;

    element::ptr ret;

    std::map<int, bool> z_indexes;

    for (const auto& i : m_positioned) {
        z_indexes[i->src_el()->css().get_z_index()];
    }

    for (auto iter = z_indexes.rbegin(); iter != z_indexes.rend(); ++iter) {
        if (iter->first > 0) {
            ret = get_child_by_point(x, y, client_x, client_y, draw_positioned, iter->first, check);
            if (ret) return ret;
        }
    }

    for (const auto& z_index : z_indexes) {
        if (z_index.first == 0) {
            ret =
                get_child_by_point(x, y, client_x, client_y, draw_positioned, z_index.first, check);
            if (ret) return ret;
        }
    }

    ret = get_child_by_point(x, y, client_x, client_y, draw_inlines, 0, check);
    if (ret) return ret;

    ret = get_child_by_point(x, y, client_x, client_y, draw_floats, 0, check);
    if (ret) return ret;

    ret = get_child_by_point(x, y, client_x, client_y, draw_block, 0, check);
    if (ret) return ret;

    for (auto iter = z_indexes.rbegin(); iter != z_indexes.rend(); ++iter) {
        if (iter->first < 0) {
            ret = get_child_by_point(x, y, client_x, client_y, draw_positioned, iter->first, check);
            if (ret) return ret;
        }
    }

    if (src_el()->css().get_position() == element_position_fixed) {
        if (is_point_inside(client_x, client_y)) {
            if (!check || check(this->shared_from_this())) {
                ret = src_el();
            }
        }
    } else {
        if (is_point_inside(x, y)) {
            if (!check || check(this->shared_from_this())) {
                ret = src_el();
            }
        }
    }

    return ret;
}

bool litehtml::render_item::is_point_inside(pixel_t x, pixel_t y) const {
    if (src_el()->css().get_display() != display_inline &&
        src_el()->css().get_display() != display_table_row) {
        position pos = m_pos;
        pos += m_padding;
        pos += m_borders;
        return pos.is_point_inside(x, y);
    } else {
        position::vector boxes;
        get_inline_boxes(boxes);
        for (auto& box : boxes) {
            if (box.is_point_inside(x, y)) {
                return true;
            }
        }
    }
    return false;
}

void litehtml::render_item::get_rendering_boxes(position::vector& redraw_boxes) const {
    if (src_el()->css().get_display() == display_inline ||
        src_el()->css().get_display() == display_table_row) {
        get_inline_boxes(redraw_boxes);
        for (auto& box : redraw_boxes) {
            scroll_box(box);
        }
    } else {
        position pos = m_pos;
        pos += m_padding;
        pos += m_borders;
        redraw_boxes.push_back(pos);
    }

    if (src_el()->css().get_position() != element_position_fixed) {
        auto cur_el = parent();
        // pixel_t	 add_x	= 0;
        // pixel_t	 add_y	= 0;

        position placement = get_placement();
        pixel_t add_x = placement.x - m_pos.x;
        pixel_t add_y = placement.y - m_pos.y;

        for (auto& box : redraw_boxes) {
            box.x += add_x;
            box.y += add_y;
        }
    } else {
        position view_port;
        src_el()->get_document()->container()->get_viewport(view_port);
        for (auto& box : redraw_boxes) {
            box.x += view_port.left();
            box.y += view_port.top();
        }
    }
}

void litehtml::render_item::dump(litehtml::dumper& cout) {
    cout.begin_node(src_el()->dump_get_name() + "{" + string(typeid(*this).name()) + "}");

    auto attrs = src_el()->dump_get_attrs();
    if (!attrs.empty()) {
        cout.begin_attrs_group("attributes");
        for (const auto& attr : attrs) {
            cout.add_attr(std::get<0>(attr), std::get<1>(attr));
        }
        cout.end_attrs_group();
    }

    if (!m_children.empty()) {
        cout.begin_attrs_group("children");
        for (const auto& el : m_children) {
            el->dump(cout);
        }
        cout.end_attrs_group();
    }

    cout.end_node();
}

namespace {

// Area a box paints outside its border box: shadows, plus a margin for outlines / glyph
// overhang / strokes. Shared by subtree_paint_bounds and the page-band cull.
litehtml::pixel_t paint_growth(const litehtml::css_properties& css) {
    using namespace litehtml;
    auto shadow_extent = [](const shadow_vector& shadows) {
        pixel_t e = 0;
        for (const auto& s : shadows) {
            if (s.inset) continue;
            pixel_t reach = std::abs(s.x.val()) + std::abs(s.y.val()) + s.blur.val() * 1.5f +
                            std::max<pixel_t>(0, s.spread.val());
            e = std::max(e, reach);
        }
        return e;
    };
    return 24 + css.get_font_metrics().height * 0.5f +
           std::max(shadow_extent(css.get_box_shadow()), shadow_extent(css.get_text_shadow()));
}

// Conservative area the subtree of `ri` paints to (absolute coordinates of the drawing pass),
// or false when that cannot be bounded safely (transforms / filters / masks / clip-paths /
// offset paths / blend modes below the root, positioned or table / grid / list-item boxes).
// Used to give an opacity layer a bound instead of the whole canvas.
bool subtree_paint_bounds(const std::shared_ptr<litehtml::render_item>& ri, litehtml::pixel_t ox,
                          litehtml::pixel_t oy, bool is_root, litehtml::pixel_t& left,
                          litehtml::pixel_t& top, litehtml::pixel_t& right,
                          litehtml::pixel_t& bottom, int depth) {
    using namespace litehtml;
    if (depth > 64) return false;
    const auto& css = ri->src_el()->css();
    if (!is_root) {
        if (!css.get_transform().empty() || !css.get_rotate().empty() || !css.get_scale().empty() ||
            !css.get_translate().empty() || !css.get_offset_path().empty() ||
            css.get_mix_blend_mode() != blend_mode_normal)
            return false;
    }
    if (!css.get_filter().empty() || !css.get_backdrop_filter().empty() || !css.get_mask().empty())
        return false;
    {
        css_token_vector clip_path;
        if (ri->src_el()->get_custom_property(_clip_path_, clip_path) && !clip_path.empty())
            return false;
    }
    const element_position positioning = css.get_position();
    if (positioning == element_position_absolute || positioning == element_position_fixed)
        return false;
    switch (css.get_display()) {
        case display_table: case display_inline_table: case display_table_caption:
        case display_table_cell: case display_table_column: case display_table_column_group:
        case display_table_footer_group: case display_table_header_group: case display_table_row:
        case display_table_row_group: case display_grid: case display_inline_grid:
        case display_list_item:
            return false;
        default:
            break;
    }

    position box = ri->pos();
    box.x += ox;
    box.y += oy;
    const pixel_t content_x = box.x, content_y = box.y;
    box += ri->get_paddings();
    box += ri->get_borders();

    // Painted outside the box: shadows, plus a margin for outlines / glyph overhang / strokes.
    const pixel_t grow = paint_growth(css);

    left = std::min(left, box.x - grow);
    top = std::min(top, box.y - grow);
    right = std::max(right, box.x + box.width + grow);
    bottom = std::max(bottom, box.y + box.height + grow);

    // Inline boxes lay their text out relative to the enclosing block, not to themselves.
    const bool inline_box = css.get_display() == display_inline;
    for (const auto& child : ri->children()) {
        if (!subtree_paint_bounds(child, inline_box ? ox : content_x, inline_box ? oy : content_y,
                                  false, left, top, right, bottom, depth + 1))
            return false;
    }
    return true;
}

// Page-band cull (draw_children): the child itself is cheap to reject, and only a box that
// cannot reach the band pays for the recursive subtree bounds. Both tests over-approximate
// the painted area (shadows / glyph overhang included), so a false "outside" is the only
// thing that could drop paint -- and the bounds helper refuses the cases that cannot be
// bounded (positioned / fixed / transforms / filters / masks / tables / grid / list-item).
bool subtree_outside_clip(const std::shared_ptr<litehtml::render_item>& ri, litehtml::pixel_t ox,
                          litehtml::pixel_t oy, const litehtml::position* clip) {
    using namespace litehtml;
    if (!clip) return false;
    const auto& css = ri->src_el()->css();
    // Fixed / positioned content is drawn against its own containing block (the viewport for
    // fixed) and is re-painted by the page fragmentation passes: never cull it.
    if (css.get_position() == element_position_fixed) return false;
    // box-decoration-break: clone re-paints a complete box per page fragment.
    if (css.get_box_decoration_break() == box_decoration_break_clone) return false;

    position box = ri->pos();
    box.x += ox;
    box.y += oy;
    box += ri->get_paddings();
    box += ri->get_borders();
    const pixel_t grow = paint_growth(css);
    box.x -= grow;
    box.y -= grow;
    box.width += grow * 2;
    box.height += grow * 2;
    if (box.does_intersect(clip)) return false;

    pixel_t left = 1e9f, top = 1e9f, right = -1e9f, bottom = -1e9f;
    if (!subtree_paint_bounds(ri, ox, oy, false, left, top, right, bottom, 0)) return false;
    return !position(left, top, right - left, bottom - top).does_intersect(clip);
}

}  // namespace

litehtml::position litehtml::render_item::get_placement() const {
    litehtml::position pos = m_pos;
    if (css().get_position() == element_position_fixed) {
        position view_port;
        src_el()->get_document()->container()->get_viewport(view_port);
        pos.x += view_port.left();
        pos.y += view_port.top();
        return pos;
    }
    auto cur_el = parent();
    while (cur_el) {
        auto p = cur_el->calc_placement();
        pos.x += p.x;
        pos.y += p.y;

        if (cur_el->css().get_position() == element_position_fixed) {
            position view_port;
            src_el()->get_document()->container()->get_viewport(view_port);
            pos.x += view_port.left();
            pos.y += view_port.top();
            return pos;
        }

        cur_el = cur_el->parent();
    }
    return pos;
}

std::shared_ptr<litehtml::render_item> litehtml::render_item::init() {
    src_el()->add_render(shared_from_this());

    for (auto& el : children()) {
        el = el->init();
    }

    return shared_from_this();
}

// A percentage height resolves against the containing block's height only when that height is
// definite; against an auto-height block it behaves as auto (min-height: 0, max-height: none).
// Without this `height: 100%` chains resolved against the 3000px initial viewport height.
static bool percent_height_is_indefinite(const litehtml::css_length& len,
                                         const litehtml::containing_block_context& cb_context) {
    if (len.is_predefined()) return false;
    const bool is_percent = len.units() == litehtml::css_units_percentage ||
                            (len.is_calc() && len.calc_percent() != 0);
    return is_percent && (cb_context.height.type == litehtml::containing_block_context::cbc_value_type_auto ||
                          cb_context.height_cyclic);
}

// A percentage inline-size is cyclic when the containing block's inline-size is itself being
// derived from its content (shrink-to-fit / intrinsic measurement, e.g. a column flex item's
// cross size measured in flex_line::init). Resolving it against the available width would make
// the measured size equal to the available width - a width:100% child would stop the parent from
// ever shrinking below it. CSS treats such a percentage as auto during intrinsic sizing, so it
// contributes the element's own content size instead.
static bool percent_width_is_indefinite(const litehtml::css_length& len,
                                        const litehtml::containing_block_context& cb_context) {
    if (len.is_predefined()) return false;
    const bool is_percent = len.units() == litehtml::css_units_percentage ||
                            (len.is_calc() && len.calc_percent() != 0);
    if (!is_percent) return false;
    const bool content_sized =
        (cb_context.size_mode & (litehtml::containing_block_context::size_mode_content |
                                 litehtml::containing_block_context::size_mode_min_content)) != 0;
    return content_sized &&
           cb_context.width.type == litehtml::containing_block_context::cbc_value_type_auto;
}

void litehtml::render_item::calc_cb_length(const css_length& len, pixel_t percent_base,
                                           containing_block_context::typed_pixel& out_value) const {
    if (!len.is_predefined()) {
        if (len.units() == litehtml::css_units_percentage || len.is_calc()) {
            out_value.value = len.calc_percent(percent_base);
            out_value.type = litehtml::containing_block_context::cbc_value_type_percentage;
        } else {
            out_value.value = src_el()->get_document()->to_pixels(len, css().get_font_metrics(), 0);
            out_value.type = containing_block_context::cbc_value_type_absolute;
        }
    }
}

litehtml::containing_block_context litehtml::render_item::calculate_containing_block_context(
    const containing_block_context& cb_context) {
    containing_block_context ret;

    ret.mode = css().get_writing_mode();
    ret.size_mode = cb_context.size_mode & (containing_block_context::size_mode_content |
                                            containing_block_context::size_mode_min_content |
                                            containing_block_context::size_mode_exact_width |
                                            containing_block_context::size_mode_exact_height);
    ret.context_idx = cb_context.context_idx + 1;

    // Initial logical sizes mapped from parent's physical dimensions
    pixel_t phys_width = cb_context.width.value;
    pixel_t phys_height = cb_context.height.value;

    if (ret.mode == writing_mode_horizontal_tb) {
        ret.width.value = ret.max_width.value = phys_width - content_offset_width();
        if (src_el()->css().get_position() != element_position_absolute &&
            src_el()->css().get_position() != element_position_fixed) {
            ret.height.value = phys_height - content_offset_height();
        }
        if (cb_context.size_mode & containing_block_context::size_mode_content) {
            ret.width.type = containing_block_context::cbc_value_type_auto;
        }
    } else {
        // Vertical: child's logical inline-size (ret.height) maps to parent's physical height
        // child's logical block-size (ret.width) maps to parent's physical width
        ret.height.value = ret.max_height.value = phys_height - content_offset_height();
        if (src_el()->css().get_position() != element_position_absolute &&
            src_el()->css().get_position() != element_position_fixed) {
            ret.width.value = phys_width - content_offset_width();
        }
        if (cb_context.size_mode & containing_block_context::size_mode_content) {
            ret.width.type = containing_block_context::cbc_value_type_auto;
        }
    }

    auto par = parent();
    bool is_flex_child = par && (par->css().get_display() == display_flex ||
                                 par->css().get_display() == display_inline_flex);

    // Calculate width
    if (src_el()->css().get_display() != display_table_cell) {
        if (cb_context.size_mode & containing_block_context::size_mode_exact_width) {
            ret.width.value = cb_context.width;
            ret.width.type = containing_block_context::cbc_value_type_absolute;
        } else {
            auto* width = &css().get_width();
            if (is_flex_child) {
                // In Flexbox, if flex-basis is set, width/height might be overridden
                if (!css().get_flex_basis().is_predefined() && css().get_flex_basis().val() >= 0) {
                    auto flex_dir = par->css().get_flex_direction();
                    bool is_main_axis_width = false;
                    if (par->css().get_writing_mode() == writing_mode_horizontal_tb) {
                        is_main_axis_width = (flex_dir == flex_direction_row || flex_dir == flex_direction_row_reverse);
                    } else {
                        is_main_axis_width = (flex_dir == flex_direction_column || flex_dir == flex_direction_column_reverse);
                    }
                    if (is_main_axis_width) {
                        ret.width.type = containing_block_context::cbc_value_type_auto;
                        // An intrinsic-size query keeps the available width: zeroing it measures the
                        // item's content at min-content width (a percentage flex-basis then makes the
                        // whole container collapse to its longest word).
                        if (!(cb_context.size_mode & containing_block_context::size_mode_content)) {
                            ret.width.value = 0;
                        }
                        width = nullptr;
                    }
                }
            }
            if (cb_context.size_mode & containing_block_context::size_mode_ignore_width) width = nullptr;
            if (width && !percent_width_is_indefinite(*width, cb_context)) {
                // Mapping physical width to parent's physical width
                calc_cb_length(*width, cb_context.width, ret.width);
            }
        }
        if (ret.width.type != containing_block_context::cbc_value_type_auto &&
            (src_el()->css().get_display() == display_table || src_el()->is_root())) {
            ret.width.value -= m_padding.width() + m_borders.width();
        }
    }

    // Calculate height
    {
        if (cb_context.size_mode & containing_block_context::size_mode_exact_height) {
            ret.height.value = cb_context.height;
            ret.height.type = containing_block_context::cbc_value_type_absolute;
            ret.height_cyclic = (cb_context.size_mode & containing_block_context::size_mode_cyclic_height) != 0;
        } else {
            auto* height = &css().get_height();
            if (is_flex_child) {
                if (!css().get_flex_basis().is_predefined() && css().get_flex_basis().val() >= 0) {
                    auto flex_dir = par->css().get_flex_direction();
                    bool is_main_axis_height = false;
                    if (par->css().get_writing_mode() == writing_mode_horizontal_tb) {
                        is_main_axis_height = (flex_dir == flex_direction_column || flex_dir == flex_direction_column_reverse);
                    } else {
                        is_main_axis_height = (flex_dir == flex_direction_row || flex_dir == flex_direction_row_reverse);
                    }
                    if (is_main_axis_height) {
                        ret.height.type = containing_block_context::cbc_value_type_auto;
                        ret.height.value = 0;
                        height = nullptr;
                    }
                }
            }
            if (height && !percent_height_is_indefinite(*height, cb_context)) {
                // Mapping physical height to parent's physical height
                calc_cb_length(*height, cb_context.height, ret.height);
            }
        }
        if (ret.height.type != containing_block_context::cbc_value_type_auto &&
            (src_el()->css().get_display() == display_table || src_el()->is_root())) {
            ret.height.value -= m_padding.height() + m_borders.height();
        }
    }

    // Calculate aspect ratio
    if (!css().get_aspect_ratio().is_auto()) {
        aspect_ratio ar = css().get_aspect_ratio();
        if (ret.width.type == containing_block_context::cbc_value_type_auto &&
            ret.height.type == containing_block_context::cbc_value_type_absolute) {
            ret.width.value = ret.height.value * ar.width / ar.height;
            ret.width.type = containing_block_context::cbc_value_type_absolute;
        } else if (ret.height.type == containing_block_context::cbc_value_type_auto &&
                   ret.width.type == containing_block_context::cbc_value_type_absolute) {
            ret.height.value = ret.width.value * ar.height / ar.width;
            ret.height.type = containing_block_context::cbc_value_type_absolute;
        }
    }

    ret.render_width = ret.width;
    ret.render_height = ret.height;

    // Min/Max widths (physical width = inline-size in horizontal-tb)
    // An exact-inline-size context hands this box its own used inline size as the
    // containing block width (a flex/grid/line consumer re-measuring the box at the
    // size it assigned). A percentage min/max-width resolved against it would apply
    // the percentage to the box itself: a shrink-to-fit box with max-width:50% is
    // measured at the clamped 488, re-measured exactly there, and clamped again to
    // 244. The consumer's size already went through its own min/max clamping, so a
    // percentage limit is not re-derived in that case (absolute limits still are).
    const bool exact_inline = ret.mode == writing_mode_horizontal_tb
        ? (cb_context.size_mode & containing_block_context::size_mode_exact_width) != 0
        : (cb_context.size_mode & containing_block_context::size_mode_exact_height) != 0;
    auto percent_based = [](const css_length& len) {
        return len.units() == css_units_percentage || (len.is_calc() && len.calc_percent() != 0);
    };
    if (exact_inline && percent_based(src_el()->css().get_min_width()))
        ret.min_width.type = containing_block_context::cbc_value_type_none;
    else
        calc_cb_length(src_el()->css().get_min_width(), cb_context.width, ret.min_width);
    if (exact_inline && percent_based(src_el()->css().get_max_width()))
        ret.max_width.type = containing_block_context::cbc_value_type_none;
    else
        calc_cb_length(src_el()->css().get_max_width(), cb_context.width, ret.max_width);

    // Min/Max heights (physical height = block-size in horizontal-tb)
    if (!percent_height_is_indefinite(src_el()->css().get_min_height(), cb_context))
        calc_cb_length(src_el()->css().get_min_height(), cb_context.height, ret.min_height);
    if (!percent_height_is_indefinite(src_el()->css().get_max_height(), cb_context))
        calc_cb_length(src_el()->css().get_max_height(), cb_context.height, ret.max_height);

    // Fix box sizing
    // Physical width adjustment (inline-size)
    if (ret.width.type != containing_block_context::cbc_value_type_auto) {
        ret.render_width = std::max(0.0f, (pixel_t)ret.width - box_sizing_width());
    }
    if (ret.min_width.type != containing_block_context::cbc_value_type_none) {
        ret.min_width.value = std::max(0.0f, (pixel_t)ret.min_width.value - box_sizing_width());
    }
    if (ret.max_width.type != containing_block_context::cbc_value_type_none) {
        ret.max_width.value = std::max(0.0f, (pixel_t)ret.max_width.value - box_sizing_width());
    }

    // Physical height adjustment (block-size)
    if (ret.height.type != containing_block_context::cbc_value_type_auto) {
        ret.render_height = std::max(0.0f, (pixel_t)ret.height - box_sizing_height());
    }
    if (ret.min_height.type != containing_block_context::cbc_value_type_none) {
        ret.min_height.value = std::max(0.0f, (pixel_t)ret.min_height.value - box_sizing_height());
    }
    if (ret.max_height.type != containing_block_context::cbc_value_type_none) {
        ret.max_height.value = std::max(0.0f, (pixel_t)ret.max_height.value - box_sizing_height());
    }

    // For children, the containing block is the content box.
    // Sync width/height with render_width/render_height to ensure consistent layout.
    ret.width = ret.render_width;
    ret.height = ret.render_height;

    // A vertical box with a definite height (inline size) gives its children that size to fill,
    // so the shrink-to-fit mode inherited from the document root must not reach them.
    if (ret.mode != writing_mode_horizontal_tb &&
        ret.height.type == containing_block_context::cbc_value_type_absolute) {
        ret.size_mode &= ~(containing_block_context::size_mode_content |
                           containing_block_context::size_mode_min_content);
    }
    // The same holds for a horizontal box whose width is definite, e.g. a flex item measured at its
    // stretched/shrunk main size: the shrink-to-fit flag it was measured with (its own height is
    // auto) must not make its children's percentage widths indefinite.
    if (ret.mode == writing_mode_horizontal_tb &&
        (cb_context.size_mode & containing_block_context::size_mode_exact_width) &&
        ret.width.type == containing_block_context::cbc_value_type_absolute) {
        ret.size_mode &= ~(containing_block_context::size_mode_content |
                           containing_block_context::size_mode_min_content);
    }

    return ret;
}

std::tuple<litehtml::pixel_t, litehtml::pixel_t> litehtml::render_item::element_static_offset(
    const std::shared_ptr<litehtml::render_item>& el) {
    pixel_t offset_x = 0;
    pixel_t offset_y = 0;
    auto cur_el = el->parent();
    auto this_el = el->css().get_position() != element_position_fixed
                       ? shared_from_this()
                       : src_el()->get_document()->root_render();
    while (cur_el && cur_el != this_el) {
        offset_x += cur_el->m_pos.x;
        offset_y += cur_el->m_pos.y;
        cur_el = cur_el->parent();
    }

    if (el->css().get_position() == element_position_fixed ||
        (is_root() && !src_el()->is_positioned())) {
        offset_x += this_el->m_pos.x;
        offset_y += this_el->m_pos.y;
    } else {
        offset_x += m_padding.left;
        offset_y += m_padding.top;
    }

    return {offset_x, offset_y};
}

litehtml::pixel_t litehtml::render_item::get_predefined_width(pixel_t parent_width) const {
    pixel_t ret = css().get_width().calc_percent(parent_width);
    if (css().get_box_sizing() == box_sizing_border_box) {
        ret -= m_padding.width() + m_borders.width();
        if (ret < 0) ret = 0;
    }
    return ret;
}

litehtml::pixel_t litehtml::render_item::get_predefined_height(pixel_t parent_height) const {
    pixel_t ret = css().get_height().calc_percent(parent_height);
    if (css().get_box_sizing() == box_sizing_border_box) {
        ret -= m_padding.height() + m_borders.height();
        if (ret < 0) ret = 0;
    }
    return ret;
}
