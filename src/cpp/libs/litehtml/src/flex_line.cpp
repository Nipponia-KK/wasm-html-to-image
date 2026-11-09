#include "flex_line.h"

#include "flex_item.h"
#include "types.h"

void litehtml::flex_line::distribute_free_space(pixel_t container_main_size) {
    // 1 Determine the used flex factor. Sum the outer hypothetical main sizes of all items on the
    // line. If the sum is less than the flex container’s inner main size, use the flex grow factor
    // for the rest of this algorithm; otherwise, use the flex shrink factor.

    pixel_t sum_hypothetical_main_size = 0;
    for (const auto& item : items) {
        sum_hypothetical_main_size += item->hypothetical_main_size;
    }
    if (!items.empty()) {
        sum_hypothetical_main_size += (pixel_t)(items.size() - 1) * main_gap;
    }

    if (sum_hypothetical_main_size < container_main_size) {
        distribute_free_space_grow(container_main_size);
    } else {
        distribute_free_space_shrink(container_main_size);
    }
}

void litehtml::flex_line::distribute_free_space_grow(pixel_t container_main_size) {
    pixel_t initial_free_space = container_main_size;
    if (!items.empty()) {
        initial_free_space -= (pixel_t)(items.size() - 1) * main_gap;
    }

    bool all_inflexible = true;

    for (auto& item : items) {
        // 2. Size inflexible items. Freeze, setting its target main size to its hypothetical main
        // size any item that has a flex factor of zero if using the flex grow factor: any item that
        // has a flex base size greater than its hypothetical main size

        // 3. Calculate initial free space. Sum the outer sizes of all items on the line, and
        // subtract this from the flex container’s inner main size. For frozen items, use their
        // outer target main size; for other items, use their outer flex base size.

        if (item->grow == 0 || item->flex_base_size > item->hypothetical_main_size) {
            item->frozen = true;
            item->clamp_state = flex_clamp_state_inflexible;
            initial_free_space -= item->hypothetical_main_size;

        } else {
            initial_free_space -= item->flex_base_size;
            all_inflexible = false;
        }
    }

    // 4. Loop:

    // 4.a Check for flexible items. If all the flex items on the line are frozen, free space has
    // been distributed; exit this loop.

    if (all_inflexible) return;

    while (true) {
        // 4.b Calculate the remaining free space as for initial free space, above. If the sum of
        // the unfrozen flex items’ flex factors is less than one, multiply the initial free space
        // by this sum. If the magnitude of this value is less than the magnitude of the remaining
        // free space, use this as the remaining free space.

        int sum_flex_grow_factor = 0;
        pixel_t remaining_free_space = container_main_size;
        if (!items.empty()) {
            remaining_free_space -= (pixel_t)(items.size() - 1) * main_gap;
        }

        for (auto& item : items) {
            if (item->frozen) {
                remaining_free_space -= item->main_size;
            } else {
                remaining_free_space -= item->flex_base_size;
                sum_flex_grow_factor += item->grow;
            }
        }

        if (sum_flex_grow_factor < 1000) {
            pixel_t adjusted_free_space =
                initial_free_space * (pixel_t)sum_flex_grow_factor / (pixel_t)1000;
            if (adjusted_free_space < remaining_free_space) {
                remaining_free_space = adjusted_free_space;
            }
        }

        // 4.c Distribute free space proportional to the flex factors.

        // If the remaining free space is zero
        // Do nothing.

        if (remaining_free_space != 0) {
            for (auto& item : items) {
                if (!item->frozen) {
                    // If using the flex grow factor
                    // Find the ratio of the item’s flex grow factor to the sum of the flex grow
                    // factors of all unfrozen items on the line. Set the item’s target main size to
                    // its flex base size plus a fraction of the remaining free space proportional
                    // to the ratio.

                    item->main_size = item->flex_base_size + remaining_free_space *
                                                                 (pixel_t)item->grow /
                                                                 (pixel_t)sum_flex_grow_factor;
                }
            }
        }

        if (fix_min_max_violations()) break;
    }
}

void litehtml::flex_line::distribute_free_space_shrink(pixel_t container_main_size) {
    pixel_t initial_free_space = container_main_size;
    if (!items.empty()) {
        initial_free_space -= (pixel_t)(items.size() - 1) * main_gap;
    }

    bool all_inflexible = true;

    for (auto& item : items) {
        // 2. Size inflexible items. Freeze, setting its target main size to its hypothetical main
        // size any item that has a flex factor of zero if using the flex shrink factor: any item
        // that has a flex base size smaller than its hypothetical main size

        // 3. Calculate initial free space. Sum the outer sizes of all items on the line, and
        // subtract this from the flex container’s inner main size. For frozen items, use their
        // outer target main size; for other items, use their outer flex base size.

        if (item->shrink == 0 || item->flex_base_size < item->hypothetical_main_size) {
            item->frozen = true;
            item->clamp_state = flex_clamp_state_inflexible;
            initial_free_space -= item->hypothetical_main_size;
        } else {
            initial_free_space -= item->flex_base_size;
            all_inflexible = false;
        }
    }

    // 4. Loop:

    // 4.a Check for flexible items. If all the flex items on the line are frozen, free space has
    // been distributed; exit this loop.

    if (all_inflexible) return;

    while (true) {
        // 4.b Calculate the remaining free space as for initial free space, above. If the sum of
        // the unfrozen flex items’ flex factors is less than one, multiply the initial free space
        // by this sum. If the magnitude of this value is less than the magnitude of the remaining
        // free space, use this as the remaining free space.

        int sum_flex_shrink_factor = 0;
        pixel_t sum_scaled_flex_shrink_factor = 0;
        pixel_t remaining_free_space = container_main_size;
        if (!items.empty()) {
            remaining_free_space -= (pixel_t)(items.size() - 1) * main_gap;
        }

        for (auto& item : items) {
            if (item->frozen) {
                remaining_free_space -= item->main_size;
            } else {
                remaining_free_space -= item->flex_base_size;
                sum_flex_shrink_factor += item->shrink;
                sum_scaled_flex_shrink_factor += item->scaled_flex_shrink_factor;
            }
        }

        if (sum_flex_shrink_factor < 1000) {
            pixel_t adjusted_free_space =
                initial_free_space * (pixel_t)sum_flex_shrink_factor / (pixel_t)1000;
            if (adjusted_free_space > remaining_free_space) {
                remaining_free_space = adjusted_free_space;
            }
        }

        // 4.c Distribute free space proportional to the flex factors.

        // If the remaining free space is zero
        // Do nothing.

        if (remaining_free_space != 0) {
            for (auto& item : items) {
                if (!item->frozen) {
                    // If using the flex shrink factor
                    // For every unfrozen item on the line, multiply its flex shrink factor by its
                    // inner flex base size, and note this as its scaled flex shrink factor. Find
                    // the ratio of the item’s scaled flex shrink factor to the sum of the scaled
                    // flex shrink factors of all unfrozen items on the line. Set the item’s target
                    // main size to its flex base size minus a fraction of the absolute value of the
                    // remaining free space proportional to the ratio.

                    item->main_size = item->flex_base_size + remaining_free_space *
                                                                 item->scaled_flex_shrink_factor /
                                                                 sum_scaled_flex_shrink_factor;
                }
            }
        }

        if (fix_min_max_violations()) break;
    }
}

bool litehtml::flex_line::fix_min_max_violations() {
    // 4.d  Fix min/max violations. Clamp each non-frozen item’s target main size by its used min
    // and max main sizes and floor its content-box size at zero. If the item’s target main size was
    // made smaller by this, it’s a max violation. If the item’s target main size was made larger
    // by this, it’s a min violation.

    pixel_t total_violation = 0;

    for (auto& item : items) {
        if (!item->frozen) {
            if (item->main_size < item->min_main_size) {
                total_violation += item->min_main_size - item->main_size;
                item->main_size = item->min_main_size;
                item->clamp_state = flex_clamp_state_min_violation;
            } else if (!item->max_main_size.is_default() && item->main_size > item->max_main_size) {
                total_violation += item->max_main_size - item->main_size;
                item->main_size = item->max_main_size;
                item->clamp_state = flex_clamp_state_max_violation;
            }
        }
    }

    // 4.e Freeze over-flexed items. The total violation is the sum of the adjustments from the
    // previous step (clamped size - unclamped size). If the total violation is:
    // Zero: Freeze all items.
    // Positive: Freeze all the items with min violations.
    // Negative: Freeze all the items with max violations.

    if (total_violation == 0) {
        return true;
    }

    bool all_frozen = true;

    flex_clamp_state state_to_freeze =
        total_violation > 0 ? flex_clamp_state_min_violation : flex_clamp_state_max_violation;

    for (auto& item : items) {
        if (!item->frozen) {
            if (item->clamp_state == state_to_freeze) {
                item->frozen = true;
            } else {
                all_frozen = false;
                item->clamp_state = flex_clamp_state_unclamped;
            }
        }
    }

    return all_frozen;
}

bool litehtml::flex_line::distribute_main_auto_margins(pixel_t free_main_size) {
    if (free_main_size > 0 && (num_auto_margin_main_start || num_auto_margin_main_end)) {
        pixel_t add = free_main_size / (pixel_t)(num_auto_margin_main_start + num_auto_margin_main_end);
        for (auto& item : items) {
            if (!item->auto_margin_main_start.is_default()) {
                item->auto_margin_main_start = add;
                main_size += add;
                free_main_size -= add;
            }
            if (!item->auto_margin_main_end.is_default()) {
                item->auto_margin_main_end = add;
                main_size += add;
                free_main_size -= add;
            }
        }

        pixel_t distribute_step = 1;
        while (free_main_size > 0) {
            for (auto& item : items) {
                if (!item->auto_margin_main_start.is_default()) {
                    item->auto_margin_main_start = item->auto_margin_main_start + distribute_step;
                    main_size += distribute_step;
                    free_main_size -= distribute_step;
                    if (free_main_size < distribute_step) break;
                }
                if (!item->auto_margin_main_end.is_default()) {
                    item->auto_margin_main_end = item->auto_margin_main_end + distribute_step;
                    main_size += distribute_step;
                    free_main_size -= distribute_step;
                    if (free_main_size < distribute_step) break;
                }
            }
        }
        return true;
    }
    return false;
}

void litehtml::flex_line::init(pixel_t container_main_size, bool fit_container,
                               bool is_row_direction,
                               const litehtml::containing_block_context& self_size,
                               litehtml::formatting_context* fmt_ctx) {
    if (!fit_container) {
        distribute_free_space(container_main_size);
    }

    cross_size = 0;
    main_size = 0;
    first_baseline.set(0, baseline::baseline_type_none);
    last_baseline.set(0, baseline::baseline_type_none);

    satoru::WritingModeContext wm(self_size.mode, self_size.width, self_size.height);

    if (is_row_direction) {
        def_value<pixel_t> first_baseline_top = 0;
        def_value<pixel_t> first_baseline_bottom = 0;
        def_value<pixel_t> last_baseline_top = 0;
        def_value<pixel_t> last_baseline_bottom = 0;
        pixel_t non_baseline_height = 0;

        // Calculate maximum cross size. The line's cross size is bounded by the
        // container's used cross size: the definite size property and the max
        // property each bound it (so they combine with min, not max), and the
        // used (render) size -- e.g. one assigned by an outer flex container --
        // bounds it directly when it is definite.
        def_value<pixel_t> max_cross_size(0);
        if (self_size.mode == writing_mode_horizontal_tb) {
            if (self_size.height.type != containing_block_context::cbc_value_type_auto) {
                max_cross_size = self_size.height;
            }
            if (self_size.max_height.type != containing_block_context::cbc_value_type_none) {
                if (max_cross_size.is_default()) {
                    max_cross_size = self_size.max_height;
                } else {
                    max_cross_size = std::min((pixel_t)max_cross_size, (pixel_t)self_size.max_height);
                }
            }
            if (self_size.render_height.type != containing_block_context::cbc_value_type_auto) {
                if (max_cross_size.is_default()) {
                    max_cross_size = self_size.render_height;
                } else {
                    max_cross_size = std::min((pixel_t)max_cross_size, (pixel_t)self_size.render_height);
                }
            }
        } else {
            if (self_size.width.type != containing_block_context::cbc_value_type_auto) {
                max_cross_size = self_size.width;
            }
            if (self_size.max_width.type != containing_block_context::cbc_value_type_none) {
                if (max_cross_size.is_default()) {
                    max_cross_size = self_size.max_width;
                } else {
                    max_cross_size = std::min((pixel_t)max_cross_size, (pixel_t)self_size.max_width);
                }
            }
            if (self_size.render_width.type != containing_block_context::cbc_value_type_auto) {
                if (max_cross_size.is_default()) {
                    max_cross_size = self_size.render_width;
                } else {
                    max_cross_size = std::min((pixel_t)max_cross_size, (pixel_t)self_size.render_width);
                }
            }
        }

        /// Find line cross_size
        /// Find line first/last baseline
        for (auto& item : items) {
            pixel_t inline_size = item->main_size - item->el->content_offset_inline(wm);
            if (item->el->css().get_box_sizing() == box_sizing_border_box) {
                inline_size = item->main_size - (wm.inline_start(item->el->get_margins()) +
                                                 wm.inline_end(item->el->get_margins()));
            }

            containing_block_context child_cb = self_size;
            child_cb.mode = wm.mode();
            child_cb.inline_size() = inline_size;
            child_cb.render_inline_size() = inline_size;
            child_cb.size_mode = (wm.is_vertical() ? containing_block_context::size_mode_exact_height
                                                   : containing_block_context::size_mode_exact_width) |
                                 containing_block_context::size_mode_measure;

                        if (wm.get_block_size(item->el->css()).is_predefined()) {
                child_cb.size_mode |= containing_block_context::size_mode_content;
            }

            item->el->measure(child_cb, fmt_ctx);
            if ((item->align & 0xFF) == flex_align_items_baseline && !(self_size.size_mode & containing_block_context::size_mode_measure)) {
                // a measure pass leaves the descendants unplaced, and a baseline is read from their positions
                containing_block_context place_cb = child_cb;
                place_cb.size_mode &= ~containing_block_context::size_mode_measure;
                item->el->place(0, 0, place_cb, fmt_ctx);
            }

            if ((item->align & 0xFF) == flex_align_items_baseline) {
                if (item->align & flex_align_items_last) {
                    last_baseline.type(reverse_cross ? baseline::baseline_type_top
                                                     : baseline::baseline_type_bottom);

                    pixel_t top = -item->el->get_last_baseline();
                    pixel_t bottom = top + item->get_el_cross_size();

                    if (last_baseline_top.is_default())
                        last_baseline_top = top;
                    else
                        last_baseline_top = std::min((pixel_t)last_baseline_top, top);

                    if (last_baseline_bottom.is_default())
                        last_baseline_bottom = bottom;
                    else
                        last_baseline_bottom = std::max((pixel_t)last_baseline_bottom, bottom);
                } else {
                    first_baseline.type(reverse_cross ? baseline::baseline_type_bottom
                                                      : baseline::baseline_type_top);
                    pixel_t top = -item->el->get_first_baseline();
                    pixel_t bottom = top + item->get_el_cross_size();

                    if (first_baseline_top.is_default())
                        first_baseline_top = top;
                    else
                        first_baseline_top = std::min((pixel_t)first_baseline_top, top);

                    if (first_baseline_bottom.is_default())
                        first_baseline_bottom = bottom;
                    else
                        first_baseline_bottom = std::max((pixel_t)first_baseline_bottom, bottom);
                }
            } else {
                non_baseline_height = std::max(non_baseline_height, item->get_el_cross_size());
            }
            if (item != items.front()) main_size += main_gap;
            main_size += item->get_el_main_size();
        }

        cross_size = std::max(first_baseline_bottom - first_baseline_top,
                              last_baseline_bottom - last_baseline_top);
        cross_size = std::max(cross_size, non_baseline_height);
        if (!multiline && !max_cross_size.is_default() && cross_size > max_cross_size) {
            cross_size = max_cross_size;
        }

        first_baseline.calc(first_baseline_top, first_baseline_bottom);
        last_baseline.calc(last_baseline_top, last_baseline_bottom);
    } else {
        // Calculate maximum cross size (inline axis in column direction). As in
        // the row branch, the bound is the container's used cross size: the
        // definite size and max properties combine with min, and a definite
        // used (render) inline size bounds the line directly -- a column flex
        // item whose width was assigned by an outer flex container (with a
        // larger max-width of its own) must not let its items overflow it.
        def_value<pixel_t> max_cross_size(0);
        if (self_size.mode == writing_mode_horizontal_tb) {
            if (self_size.width.type != containing_block_context::cbc_value_type_auto) {
                max_cross_size = self_size.width;
            }
            if (self_size.max_width.type != containing_block_context::cbc_value_type_none) {
                if (max_cross_size.is_default()) {
                    max_cross_size = self_size.max_width;
                } else {
                    max_cross_size = std::min((pixel_t)max_cross_size, (pixel_t)self_size.max_width);
                }
            }
            if (self_size.render_width.type != containing_block_context::cbc_value_type_auto) {
                if (max_cross_size.is_default()) {
                    max_cross_size = self_size.render_width;
                } else {
                    max_cross_size = std::min((pixel_t)max_cross_size, (pixel_t)self_size.render_width);
                }
            }
        } else {
            if (self_size.height.type != containing_block_context::cbc_value_type_auto) {
                max_cross_size = self_size.height;
            }
            if (self_size.max_height.type != containing_block_context::cbc_value_type_none) {
                if (max_cross_size.is_default()) {
                    max_cross_size = self_size.max_height;
                } else {
                    max_cross_size = std::min((pixel_t)max_cross_size, (pixel_t)self_size.max_height);
                }
            }
            if (self_size.render_height.type != containing_block_context::cbc_value_type_auto) {
                if (max_cross_size.is_default()) {
                    max_cross_size = self_size.render_height;
                } else {
                    max_cross_size = std::min((pixel_t)max_cross_size, (pixel_t)self_size.render_height);
                }
            }
        }

        for (auto& item : items) {
            pixel_t block_size = item->main_size - item->el->content_offset_block(wm);
            if (item->el->css().get_box_sizing() == box_sizing_border_box) {
                block_size = item->main_size - (wm.block_start(item->el->get_margins()) +
                                                wm.block_end(item->el->get_margins()));
            }

            containing_block_context child_cb = self_size;
            child_cb.mode = wm.mode();
            child_cb.block_size() = block_size;
            child_cb.render_block_size() = block_size;
            child_cb.size_mode = (wm.is_vertical() ? containing_block_context::size_mode_exact_width
                                                   : containing_block_context::size_mode_exact_height) |
                                 containing_block_context::size_mode_measure;
            // The pinned block size is the item's resolved main size. It is a definite size for
            // percentage resolution inside the item only when the container's block size is
            // definite: a content-derived container height (auto / fit-content property, an
            // auto cb height, or a cyclic one) keeps percentage heights inside the item cyclic
            // even though the size arrives as a plain number.
            if (!wm.is_vertical() &&
                (self_size.height.type == containing_block_context::cbc_value_type_auto ||
                 self_size.height_cyclic ||
                 (item->el->parent() && item->el->parent()->css().get_height().is_predefined()))) {
                child_cb.size_mode |= containing_block_context::size_mode_cyclic_height;
            }

                        if (wm.get_inline_size(item->el->css()).is_predefined()) {
                child_cb.size_mode |= containing_block_context::size_mode_content;
            }

            item->el->measure(child_cb, fmt_ctx);

            // A fit-content inline size resolves against the container's content width
            // (fit-content = min(max-content, max(min-content, available))). The content
            // measure above answers the raw max-content width, so a wrapper whose content
            // resolves narrower (420 max-content in a 379.4 container) would size its
            // line from a width the item never gets and push every following line
            // sideways. Resolve it here and re-measure at the resolved width, which
            // also gives the main size of the box the item actually ends up with.
            const css_length& item_inline_prop = wm.get_inline_size(item->el->css());
            const int inline_predef = item_inline_prop.is_predefined() ? item_inline_prop.predef() : -1;
            // min-content sizes the item from its smallest content in any line
            // breaking; an auto inline size in a multi-line column container is
            // sized by its line, whose cross size is the item's fit-content
            // size; fit-content already resolves the same way.
            const bool resolve_inline = (inline_predef == 1 || inline_predef == 3 ||
                                         (inline_predef == 0 && multiline)) &&
                self_size.render_inline_size().type != containing_block_context::cbc_value_type_auto &&
                self_size.render_inline_size().value > 0;
            if (resolve_inline)
            {
                const pixel_t available = self_size.render_inline_size().value -
                    item->el->content_offset_inline(wm) + item->el->box_sizing_inline(wm);
                pixel_t resolved = item->get_el_cross_size() - item->el->content_offset_inline(wm) +
                    item->el->box_sizing_inline(wm);
                if (inline_predef == 3)
                {
                    containing_block_context min_cb = child_cb;
                    min_cb.size_mode |= containing_block_context::size_mode_min_content;
                    item->el->measure(min_cb, fmt_ctx);
                    resolved = item->get_el_cross_size() - item->el->content_offset_inline(wm) +
                        item->el->box_sizing_inline(wm);
                    containing_block_context pin_cb = child_cb;
                    pin_cb.inline_size() = resolved - item->el->box_sizing_inline(wm);
                    pin_cb.render_inline_size() = pin_cb.inline_size();
                    pin_cb.size_mode |= wm.is_vertical() ? containing_block_context::size_mode_exact_height
                                                         : containing_block_context::size_mode_exact_width;
                    pin_cb.size_mode &= ~(containing_block_context::size_mode_content |
                                          containing_block_context::size_mode_min_content);
                    item->el->measure(pin_cb, fmt_ctx);
                }
                else if (resolved > available)
                {
                    containing_block_context min_cb = child_cb;
                    min_cb.size_mode |= containing_block_context::size_mode_min_content;
                    item->el->measure(min_cb, fmt_ctx);
                    const pixel_t min_size = item->get_el_cross_size() - item->el->content_offset_inline(wm) +
                        item->el->box_sizing_inline(wm);
                    // An auto item whose min-content already exceeds the available
                    // width cannot shrink to fit at any width: it keeps its
                    // max-content size (a 130 max-content item with a 124
                    // min-content in a 120 column stays 130, not 124).
                    if (inline_predef == 0 && min_size > available)
                    {
                        item->el->measure(child_cb, fmt_ctx);
                    }
                    else
                    {
                        resolved = std::max(min_size, available);
                        containing_block_context fit_cb = child_cb;
                        fit_cb.inline_size() = resolved - item->el->box_sizing_inline(wm);
                        fit_cb.render_inline_size() = fit_cb.inline_size();
                        fit_cb.size_mode |= wm.is_vertical() ? containing_block_context::size_mode_exact_height
                                                             : containing_block_context::size_mode_exact_width;
                        fit_cb.size_mode &= ~(containing_block_context::size_mode_content |
                                              containing_block_context::size_mode_min_content);
                        item->el->measure(fit_cb, fmt_ctx);
                    }
                }
            }

            if (item != items.front()) main_size += main_gap;
            main_size += item->get_el_main_size();
            cross_size = std::max(cross_size, item->get_el_cross_size());
        }
        if (!multiline && !max_cross_size.is_default() && cross_size > max_cross_size) {
            cross_size = max_cross_size;
        }
    }
}

litehtml::pixel_t litehtml::flex_line::calculate_items_position(
    pixel_t container_main_size, flex_justify_content justify_content, bool is_row_direction,
    const containing_block_context& self_size, formatting_context* fmt_ctx) {
    /// Distribute main axis free space for auto-margins
    pixel_t free_main_size = container_main_size - main_size;
    distribute_main_auto_margins(free_main_size);
    free_main_size = container_main_size - main_size;

    /// Fix justify-content property
    switch (justify_content) {
        case flex_justify_content_left:
        case flex_justify_content_right:
            if (!is_row_direction) {
                justify_content = flex_justify_content_start;
            }
            break;
        case flex_justify_content_space_between:
            // If the leftover free-space is negative or there is only a single flex item on the
            // line, this value is identical to flex-start.
            if (items.size() == 1 || free_main_size < 0)
                justify_content = flex_justify_content_flex_start;
            break;
        case flex_justify_content_space_around:
        case flex_justify_content_space_evenly:
            // With a single flex item the fallback is center. With negative free space the
            // distribution falls back to packing at the container's start edge (css-align-3),
            // so the line overflows only toward the end side. The item list is pre-reversed
            // for reverse directions in this engine, so packing at the start edge is
            // flex-start normally and flex-end when the main axis is reversed (Chrome:
            // row-reverse + space-around + an oversized item packs it at the left edge,
            // where flex-start would pack it at the right; fuzz replaced seed 41).
            if (free_main_size < 0)
                justify_content = reverse_main ? flex_justify_content_flex_end
                                               : flex_justify_content_flex_start;
            else if (items.size() == 1)
                justify_content = flex_justify_content_center;
            break;
        default:
            break;
    }

    /// Distribute free main size using justify-content property
    pixel_t main_pos = 0;
    pixel_t add_before_item = 0;
    pixel_t add_after_item = 0;
    pixel_t item_remainder = 0;

    /// find initial main position and spaces between items
    switch (justify_content) {
        case flex_justify_content_right:
            main_pos = free_main_size;
            break;
        case flex_justify_content_left:
        case flex_justify_content_start:
            main_pos = 0;
            break;
        case flex_justify_content_end:
            main_pos = free_main_size;
            break;
        case flex_justify_content_flex_end:
            if (!reverse_main) {
                main_pos = free_main_size;
            }
            break;
        case flex_justify_content_center:
            main_pos = free_main_size / 2;
            break;
        case flex_justify_content_space_between:
            add_after_item = free_main_size / (pixel_t)(items.size() - 1);
            item_remainder = free_main_size - (add_after_item * (pixel_t)(items.size() - 1));
            break;
        case flex_justify_content_space_around:
            add_after_item = add_before_item = free_main_size / (pixel_t)(items.size() * 2);
            item_remainder = free_main_size - (add_after_item * (pixel_t)items.size() * 2);
            break;
        case flex_justify_content_space_evenly:
            add_before_item = free_main_size / (pixel_t)(items.size() + 1);
            item_remainder = free_main_size - add_before_item * (pixel_t)(items.size() + 1);
            break;
        default:
            if (reverse_main) {
                main_pos = free_main_size;
            }
            break;
    }

    /// Place all items in main and cross positions
    pixel_t height = 0;

    pixel_t distribute_step = 1;
    bool first = true;
    for (auto& item : items) {
        if (!first) main_pos += main_gap;
        first = false;

        main_pos += add_before_item;
        if (add_before_item > 0 && item_remainder > 0) {
            main_pos += distribute_step;
            item_remainder -= distribute_step;
        }

        item->place(*this, main_pos, self_size, fmt_ctx);
        main_pos += item->get_el_main_size();

        main_pos += add_after_item;
        if (add_after_item > 0 && item_remainder > 0) {
            main_pos += distribute_step;
            item_remainder -= distribute_step;
        }
        height = std::max(height, item->cross_pos + item->get_el_cross_size());
    }
    return height;
}
