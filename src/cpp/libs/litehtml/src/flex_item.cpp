#include <cmath>
#include <cstdio>
#include "flex_item.h"
#include "flex_line.h"
#include "types.h"
#include <iostream>

void litehtml::flex_item::init(const litehtml::containing_block_context &self_size_const,
							   litehtml::formatting_context *fmt_ctx, flex_align_items align_items)
{
	containing_block_context self_size = self_size_const;
	self_size.width = self_size.render_width;
	self_size.height = self_size.render_height;

	m_container_wm = satoru::WritingModeContext(self_size.mode, (pixel_t) self_size.width, (pixel_t) self_size.height);

	grow = (int) std::nearbyint(el->css().get_flex_grow() * 1000.0);
	if(grow < 0) grow = 0;

	shrink = (int) std::nearbyint(el->css().get_flex_shrink() * 1000.0);
	if(shrink < 0) shrink = 1000;

	el->calc_outlines(self_size.render_inline_size());
	order = el->css().get_order();

	if (el->css().get_flex_align_self() == flex_align_items_auto)
	{
		align = align_items;
	} else
	{
		align = el->css().get_flex_align_self();
	}

	direction_specific_init(self_size, fmt_ctx);

	if (flex_base_size < min_main_size)
	{
		hypothetical_main_size = min_main_size;
	} else if (!max_main_size.is_default() && flex_base_size > max_main_size)
	{
		hypothetical_main_size = max_main_size;
	} else
	{
		hypothetical_main_size = flex_base_size;
	}
	// An automatic minimum (the min-content size from the measure above, taken when the
	// min-width property is automatic) is itself clamped by the max main size: a min-content
	// size is resolved under the box's own min/max constraints, so a replaced item with
	// width:max-content and max-width:50% in a 768 container has a 384 floor, not its 512
	// intrinsic width -- the percentage in its min-content measure is treated as none there
	// (its basis is indefinite in that query), which would otherwise let the floor override
	// the cap here. An explicit min-width still wins over max-width, as in CSS.
	{
		const css_length& min_main_prop = m_container_wm.is_vertical() ? el->css().get_min_height() : el->css().get_min_width();
		if (min_main_prop.is_predefined() && !max_main_size.is_default() &&
			hypothetical_main_size > max_main_size)
		{
			hypothetical_main_size = max_main_size;
			main_size = hypothetical_main_size;
		}
	}

	main_size = hypothetical_main_size;
	frozen = false;
}

void litehtml::flex_item::place(flex_line &ln, pixel_t main_pos,
								const containing_block_context &self_size_const,
								formatting_context *fmt_ctx)
{
	containing_block_context self_size = self_size_const;
	self_size.width = self_size.render_width;
	self_size.height = self_size.render_height;

	layout_item(ln, self_size, fmt_ctx);
	apply_main_auto_margins();
	set_main_position(main_pos);

	if(!apply_cross_auto_margins(ln.cross_size))
	{
		switch (align & 0xFF)
		{
			case flex_align_items_baseline:
				align_baseline(ln, self_size, fmt_ctx);
				break;
			case flex_align_items_flex_end:
				if(ln.reverse_cross)
				{
					set_cross_position(ln.cross_start);
				} else
				{
					set_cross_position(ln.cross_start + ln.cross_size - get_el_cross_size());
				}
				break;
			case flex_align_items_end:
				set_cross_position(ln.cross_start + ln.cross_size - get_el_cross_size());
				break;
			case flex_align_items_center:
				set_cross_position(ln.cross_start + ln.cross_size / 2 - get_el_cross_size() / 2);
				break;
			case flex_align_items_flex_start:
				if(ln.reverse_cross)
				{
					set_cross_position(ln.cross_start + ln.cross_size - get_el_cross_size());
				} else
				{
					set_cross_position(ln.cross_start);
				}
				break;
			case flex_align_items_start:
				set_cross_position(ln.cross_start);
				break;
			default:
				align_stretch(ln, self_size, fmt_ctx);
				break;
		}
	} else {
		set_cross_position(ln.cross_start);
	}
}

litehtml::pixel_t litehtml::flex_item::get_last_baseline(baseline::_baseline_type type) const
{
	if(type == baseline::baseline_type_top)
	{
		return el->get_last_baseline();
	} else if(type == baseline::baseline_type_bottom)
	{
		return el->height() - el->get_last_baseline();
	}
	return 0;
}

litehtml::pixel_t litehtml::flex_item::get_first_baseline(litehtml::baseline::_baseline_type type) const
{
	if(type == baseline::baseline_type_top)
	{
		return el->get_first_baseline();
	} else if(type == baseline::baseline_type_bottom)
	{
		return el->height() - el->get_first_baseline();
	}
	return 0;
}


////////////////////////////////////////////////////////////////////////////////////

void litehtml::flex_item_row_direction::direction_specific_init(const litehtml::containing_block_context &self_size_const,
																litehtml::formatting_context *fmt_ctx)
{
	containing_block_context self_size = self_size_const;
	self_size.width = self_size.render_width;
	self_size.height = self_size.render_height;

    // Map logical margins to auto_margin flags
    const auto& margins = el->css().get_margins();
    if(m_container_wm.inline_start(margins).is_predefined()) auto_margin_main_start = 0;
    if(m_container_wm.inline_end(margins).is_predefined()) auto_margin_main_end = 0;
    if(m_container_wm.block_start(margins).is_predefined()) auto_margin_cross_start = true;
    if(m_container_wm.block_end(margins).is_predefined()) auto_margin_cross_end = true;

	def_value<pixel_t> content_size(0);
	pixel_t measured_min_content = -1;
	// A stretched item's cross size is definite -- the container's cross size -- and
	// its main size is measured at that cross size: a column-flex item wraps its
	// lines there, so measuring it with an indefinite cross size reports the
	// unwrapped size (a column container that is 464 wide once wrapped at the
	// container's 60px height measured 444 -- all fuzz seed 36). The item measures
	// below pin the cross size exact under this condition.
	// (Only outside an intrinsic-size query: while an ancestor is being sized from
	// its content, a fit-content descendant's contribution is resolved against the
	// query's own available size, and pinning the cross size would wrap the item's
	// lines at a height the query does not use -- the nested-flex intrinsic family.)
	// Single-line only: in a wrapping container an item stretches to its LINE's
	// cross size (the tallest item on the line), not the container's, so the
	// container's cross size is not the item's definite cross size there.
	const bool stretch_cross_definite = !m_container_wm.is_vertical() &&
		!el->src_el()->is_replaced() &&
		el->parent() && el->parent()->css().get_flex_wrap() == flex_wrap_nowrap &&
		!(self_size.size_mode & containing_block_context::size_mode_content) &&
		self_size.render_height.type != containing_block_context::cbc_value_type_auto &&
		((align & 0xFF) == flex_align_items_stretch || (align & 0xFF) == flex_align_items_normal) &&
		el->css().get_height().is_predefined() && el->css().get_height().predef() == 0;
    
    // No helpers for min/max properties in WritingModeContext, so manual branching
    const css_length& min_size_prop = m_container_wm.is_vertical() ? el->css().get_min_height() : el->css().get_min_width();
    const css_length& max_size_prop = m_container_wm.is_vertical() ? el->css().get_max_height() : el->css().get_max_width();
    const css_length& size_prop = m_container_wm.get_inline_size(el->css()); // Main size prop

    pixel_t render_offset = el->render_offset_width(); 
    if(m_container_wm.is_vertical()) render_offset = el->render_offset_height();

	pixel_t content_offset = el->content_offset_inline(m_container_wm);
	pixel_t parent_size = self_size.render_inline_size();

	if (min_size_prop.is_predefined())
	{
		if (el->css().get_overflow() == overflow_visible ||
			el->src_el()->css().get_display() == display_webkit_box)
		{
			formatting_context fmt_ctx_copy;
			if(fmt_ctx) fmt_ctx_copy = *fmt_ctx;
            
            containing_block_context::typed_pixel tp_content_offset(content_offset, containing_block_context::cbc_value_type_auto);
            containing_block_context measure_cb = m_container_wm.update_logical(self_size, tp_content_offset, std::nullopt);
            // The automatic minimum size is the item's min-content size, not its max-content size:
            // measure with size_mode_min_content so cyclic percentage widths (e.g. an image with
            // width:100% inside an auto-width wrapper) contribute 0 instead of their intrinsic
            // width. Otherwise flex-shrink could never bring the item down to the container.
            measure_cb.size_mode = containing_block_context::size_mode_content |
                                   containing_block_context::size_mode_min_content |
                                   containing_block_context::size_mode_measure;
            if (stretch_cross_definite) measure_cb.size_mode |= containing_block_context::size_mode_exact_height;
			el->measure(measure_cb, fmt_ctx ? &fmt_ctx_copy : nullptr);

			min_main_size = get_el_main_size();
			// Automatic minimum size (CSS Flexbox 4.5) = min(specified size, content size): an item with a
			// definite width may shrink down to its content, not only to that width.
			if (!m_container_wm.is_vertical() && !size_prop.is_predefined() && size_prop.units() != css_units_percentage)
			{
				formatting_context fmt_ctx_copy2;
				if(fmt_ctx) fmt_ctx_copy2 = *fmt_ctx;
				measure_cb.size_mode |= containing_block_context::size_mode_ignore_width;
				if (stretch_cross_definite) measure_cb.size_mode |= containing_block_context::size_mode_exact_height;
			el->measure(measure_cb, fmt_ctx ? &fmt_ctx_copy2 : nullptr);
				// The specified size is the width property even when flex-basis overrides it for the flex base size.
				min_main_size = std::min(get_el_main_size(), size_prop.calc_percent(parent_size) + render_offset - el->box_sizing_inline(m_container_wm));
			}
			else if (!m_container_wm.is_vertical() && size_prop.units() == css_units_percentage && parent_size > 0 &&
					 !(self_size.size_mode & (containing_block_context::size_mode_content | containing_block_context::size_mode_min_content)))
			{
				// A percentage width against a definite container is a specified size too.
				// The content size suggestion it is clamped by (Flexbox 4.5) is the item's
				// min-content size -- which for a REPLACED item is its intrinsic width: the
				// min-content measure above treats the percentage as cyclic and reports ~0
				// for an image, so measure a replaced item at its max-content (identical to
				// its min-content) instead. An image with width:30% and a 300px intrinsic
				// width in a 496px container then floors at min(148.8, 300) = 148.8 and does
				// not shrink at all, while width:90% floors at min(446.4, 300) = 300. A
				// non-replaced item (a text block, say) keeps the min-content floor and
				// shrinks freely below its percentage.
				if (el->src_el()->is_replaced())
				{
					formatting_context fmt_ctx_copy3;
					if(fmt_ctx) fmt_ctx_copy3 = *fmt_ctx;
					measure_cb.size_mode = containing_block_context::size_mode_content |
										   containing_block_context::size_mode_measure;
					if (stretch_cross_definite) measure_cb.size_mode |= containing_block_context::size_mode_exact_height;
			el->measure(measure_cb, fmt_ctx ? &fmt_ctx_copy3 : nullptr);
					min_main_size = std::min(get_el_main_size(), size_prop.calc_percent(parent_size) + render_offset - el->box_sizing_inline(m_container_wm));
				} else
				{
					min_main_size = std::min(min_main_size, size_prop.calc_percent(parent_size) + render_offset - el->box_sizing_inline(m_container_wm));
				}
			}
			content_size = min_main_size;
		} else
		{
			min_main_size = render_offset;
			content_size = min_main_size;
			// The automatic minimum size of a scroll container is 0, but its
			// min-content contribution is still its min-content size (the same
			// item as a grid item contributes its content, not zero): measure
			// it for the contribution below.
			formatting_context fmt_ctx_copy;
			if(fmt_ctx) fmt_ctx_copy = *fmt_ctx;
			containing_block_context::typed_pixel tp_content_offset(content_offset, containing_block_context::cbc_value_type_auto);
			containing_block_context measure_cb = m_container_wm.update_logical(self_size, tp_content_offset, std::nullopt);
			measure_cb.size_mode = containing_block_context::size_mode_content |
								   containing_block_context::size_mode_min_content |
								   containing_block_context::size_mode_measure;
			if (stretch_cross_definite) measure_cb.size_mode |= containing_block_context::size_mode_exact_height;
			el->measure(measure_cb, fmt_ctx ? &fmt_ctx_copy : nullptr);
			measured_min_content = get_el_main_size();
		}
	} else
	{
		min_main_size = min_size_prop.calc_percent(parent_size) + render_offset - el->box_sizing_inline(m_container_wm);
		// A definite min-width is only the floor of the item's min-content contribution, not the
		// contribution itself: the contribution is the item's min-content size clamped by that
		// floor (CSS Sizing / Flexbox 9.9.1), so measure it as for an automatic minimum. Without
		// this a `min-width:100px` item whose content needs 198 contributes 100, and a
		// shrink-to-fit ancestor comes out too narrow even though the item can never be that
		// narrow in a min-content sense. A definite width still counts in full below, so only
		// an auto / percentage width needs the measure.
		if (!m_container_wm.is_vertical() &&
			(size_prop.is_predefined() || size_prop.units() == css_units_percentage || size_prop.is_calc()))
		{
			formatting_context fmt_ctx_copy;
			if(fmt_ctx) fmt_ctx_copy = *fmt_ctx;
			containing_block_context::typed_pixel tp_content_offset(content_offset, containing_block_context::cbc_value_type_auto);
			containing_block_context measure_cb = m_container_wm.update_logical(self_size, tp_content_offset, std::nullopt);
			measure_cb.size_mode = containing_block_context::size_mode_content |
								   containing_block_context::size_mode_min_content |
								   containing_block_context::size_mode_measure;
			if (stretch_cross_definite) measure_cb.size_mode |= containing_block_context::size_mode_exact_height;
			el->measure(measure_cb, fmt_ctx ? &fmt_ctx_copy : nullptr);
			measured_min_content = get_el_main_size();
		}
	}
	// min-content contribution: a definite width counts in full, otherwise the item's min-content size
	min_content_contribution = min_main_size;
	if (measured_min_content >= 0)
		min_content_contribution = std::max(min_content_contribution, measured_min_content);
	if (!m_container_wm.is_vertical() && !size_prop.is_predefined() && size_prop.units() != css_units_percentage)
		min_content_contribution = std::max(min_main_size, size_prop.calc_percent(parent_size) + render_offset - el->box_sizing_inline(m_container_wm));
	// An inflexible item (flex-shrink: 0) with a definite flex-basis can never be
	// narrower than its flex base size in any layout, so the base size is its
	// min-content contribution — not its width property (width:20px with
	// flex:0 0 40px contributes 40) and not its content size.
	if (!m_container_wm.is_vertical() && shrink == 0 && !el->css().get_flex_basis().is_predefined() &&
		el->css().get_flex_basis().units() == css_units_px && el->css().get_flex_basis().val() > 0 &&
		el->parent() && el->parent()->css().get_flex_wrap() == flex_wrap_nowrap)
	{
		// The item is always exactly its base size, so that is the contribution (a
		// definite min-width still floors it, a definite max-width still caps it).
		// Only in a nowrap container: a wrapping container's intrinsic sizes keep the
		// item's own min-content size, because an item alone on a line is floored by
		// its automatic minimum (its width), not by its basis.
		const pixel_t basis_size = el->css().get_flex_basis().calc_percent(parent_size) + render_offset - el->box_sizing_inline(m_container_wm);
		min_content_contribution = min_size_prop.is_predefined() ? basis_size : std::max(basis_size, min_main_size);
		if (!max_main_size.is_default())
			min_content_contribution = std::min(min_content_contribution, (pixel_t) max_main_size);
	}
	// A shrinkable item's min-content contribution is clamped by a definite flex-basis:
	// its width (or content) may be wider than the basis, but as a contribution it counts
	// at most the base size — otherwise a parent measuring this container's min-content
	// floors the container's automatic minimum above the size the item is laid out at
	// (width:50px with flex:0 1 40px contributes 40).
	if (!m_container_wm.is_vertical() && shrink > 0 && min_size_prop.is_predefined() &&
		!el->css().get_flex_basis().is_predefined() && el->css().get_flex_basis().units() == css_units_px &&
		el->css().get_flex_basis().val() > 0 &&
		el->parent() && el->parent()->css().get_flex_wrap() == flex_wrap_nowrap)
	{
		min_content_contribution = std::min(min_content_contribution,
			el->css().get_flex_basis().calc_percent(parent_size) + render_offset - el->box_sizing_inline(m_container_wm));
		if (!max_main_size.is_default())
			min_content_contribution = std::min(min_content_contribution, (pixel_t) max_main_size);
	}
	if (!max_size_prop.is_predefined())
	{
		max_main_size = max_size_prop.calc_percent(parent_size) + render_offset - el->box_sizing_inline(m_container_wm);
	}

	bool flex_basis_predefined = el->css().get_flex_basis().is_predefined();
	int predef = flex_basis_auto;
	if(flex_basis_predefined) predef = el->css().get_flex_basis().predef();
	else if(el->css().get_flex_basis().val() < 0) flex_basis_predefined = true;
	// The basis is rewritten to "content" below only because this is an intrinsic-size query; the
	// item's own inline size still contributes to that query (see the content case).
	bool flex_basis_from_query = false;

	// Fix: If we are measuring the container (intrinsic sizes), we should treat
	// flex-basis as content for growable items or if basis is 0/percentage.
	// CAUTION: Using flex_basis_content instead of flex_basis_max_content so it can wrap appropriately
	// in normal flow.
	// A percentage only falls back to content when the container's inline size is indefinite
	// (an intrinsic-size query); measuring the height at a definite width resolves it normally,
	// otherwise the items take their max-content width and wrap one per line.
	if (!flex_basis_predefined && (self_size.size_mode & containing_block_context::size_mode_measure)) {
		const bool indefinite = (self_size.size_mode & containing_block_context::size_mode_content) ||
								parent_size <= 0;
		// Growable items and a zero basis measure as content only for an intrinsic-size query; at a
		// definite inline size the basis is what the final layout uses, or the line breaking of a
		// wrapping container would differ from the layout and its height would be overestimated.
		// An item that can shrink contributes its content size to an intrinsic size, not its flex-basis:
		// the container may end up narrower than the basis and the item shrinks with it (Blink and the
		// spec's flex fraction both give a shrinkable `flex: 0 1 100px` empty item a contribution of 0).
		const bool shrinkable_in_query = (self_size.size_mode & containing_block_context::size_mode_content) && shrink > 0;
		// ...but a definite width narrower than the basis is that contribution (`width:20px; flex:0 1 100px`).
		const bool width_below_basis = shrinkable_in_query && size_prop.units() == css_units_px && !size_prop.is_predefined() &&
			el->css().get_flex_basis().units() == css_units_px && size_prop.val() < el->css().get_flex_basis().val();
		if (indefinite && width_below_basis)
		{
			flex_basis_predefined = true;
			predef = flex_basis_auto;
		}
		else if (indefinite && (self_size.size_mode & containing_block_context::size_mode_content) &&
				 !size_prop.is_predefined() && size_prop.units() == css_units_px &&
				 el->css().get_flex_basis().units() == css_units_px)
		{
			// A definite width and basis: the contribution is worked out below from the item's own width
			// (CSS Flexbox 9.9.1) instead of measuring its content.
		}
		else if (indefinite && (grow > 0 || shrinkable_in_query || el->css().get_flex_basis().units() == css_units_percentage ||
			el->css().get_flex_basis().val() == 0)) {
			flex_basis_predefined = true;
			predef = flex_basis_content;
			flex_basis_from_query = true;
		}
	}

	if (flex_basis_predefined)
	{
		if(predef == flex_basis_auto && size_prop.is_predefined()) predef = flex_basis_content;
		// A percentage width (`width:100%`) is cyclic while the container is being sized from its
		// content (an intrinsic-size query with an indefinite inline size): it behaves as auto, so the
		// item contributes its content size instead of the available width. Otherwise an empty
		// `width:100%` child makes its wrapper as wide as the space it is measured in, and the items
		// of a wrapping flex container each fill a line.
		if (predef == flex_basis_auto && !m_container_wm.is_vertical() &&
			(size_prop.units() == css_units_percentage || (size_prop.is_calc() && size_prop.calc_percent() != 0)) &&
			(self_size.size_mode & (containing_block_context::size_mode_content | containing_block_context::size_mode_min_content)) &&
			self_size.render_inline_size().type == containing_block_context::cbc_value_type_auto)
		{
			predef = flex_basis_content;
		}

		switch (predef)
		{
			case flex_basis_auto:
				flex_base_size = size_prop.calc_percent(parent_size) + render_offset - el->box_sizing_inline(m_container_wm);
				break;
			case flex_basis_fit_content:
			case flex_basis_content:
				{
					formatting_context fmt_ctx_copy;
					if(fmt_ctx) fmt_ctx_copy = *fmt_ctx;
                    
                    pixel_t logical_is_val = m_container_wm.to_logical(self_size.width, self_size.height).inline_size + content_offset;
                    // flex-basis: content is the item's max-content size, which does not depend on the
                    // inline size on offer: a wrapping row container inside the item would otherwise be
                    // capped by the outer container's width (see render_flex.cpp), wrap its items where
                    // the max-content size keeps them on one line, and report a taller item. The content
                    // measure below answers min(max-content, space on offer), so while the content fits
                    // that space it already is the max-content size; only a measure that comes back at
                    // the space on offer was capped and has to be repeated unbounded. fit-content is
                    // that same query capped by the space on offer, so it keeps the available size,
                    // and so does an explicit fit-content / max-content / min-content width.
                    const bool width_is_intrinsic_keyword = size_prop.is_predefined() && size_prop.predef() > 0;
                    const bool reload_unbounded = predef == flex_basis_content && !width_is_intrinsic_keyword &&
                        !el->src_el()->is_replaced() && logical_is_val < 100000;
                    containing_block_context::typed_pixel tp_logical_is(logical_is_val, containing_block_context::cbc_value_type_auto);
                    
                    containing_block_context measure_cb = m_container_wm.update_logical(self_size, tp_logical_is, std::nullopt);
                    measure_cb.size_mode = containing_block_context::size_mode_measure | containing_block_context::size_mode_content;
                    
					if (stretch_cross_definite) measure_cb.size_mode |= containing_block_context::size_mode_exact_height;
			el->measure(measure_cb, fmt_ctx ? &fmt_ctx_copy : nullptr);
					flex_base_size = get_el_main_size();
					// The measure can also come back capped by the item's own max main size
					// (a percentage max-width resolves against the space on offer, margins
					// included, inside the item's containing-block calculation): the flex base
					// size is the unclamped max-content size -- the clamp belongs to the
					// hypothetical main size -- so that case needs the unbounded reload too.
					const bool hit_max_main = !max_main_size.is_default() &&
						flex_base_size >= (pixel_t) max_main_size;
					if (reload_unbounded && (flex_base_size >= logical_is_val || hit_max_main))
					{
						// (no inline size on offer, the same "unbounded" query render_grid.cpp uses)
						containing_block_context::typed_pixel tp_unbounded(100000, containing_block_context::cbc_value_type_auto);
						containing_block_context unbounded_cb = m_container_wm.update_logical(self_size, tp_unbounded, std::nullopt);
						unbounded_cb.size_mode = containing_block_context::size_mode_measure | containing_block_context::size_mode_content;
						if (stretch_cross_definite) unbounded_cb.size_mode |= containing_block_context::size_mode_exact_height;
					el->measure(unbounded_cb, fmt_ctx ? &fmt_ctx_copy : nullptr);
						flex_base_size = get_el_main_size();
					}
					// A zero/percentage basis only became a content measure for this intrinsic-size query;
					// the item's flex-basis still overrode its own definite width in that measure (see
					// calculate_containing_block_context), so an empty box would contribute nothing. Its
					// outer max-content size is the specified inline size (CSS Flexbox 9.9.1: Blink gives
					// `width:90px; flex:1 1 0%` a contribution of 90, padding included).
					if (flex_basis_from_query && !m_container_wm.is_vertical() && !size_prop.is_predefined() &&
						size_prop.units() == css_units_px)
					{
						flex_base_size = size_prop.val() + render_offset - el->box_sizing_inline(m_container_wm);
					}
				}
				break;
			case flex_basis_min_content:
				if(content_size.is_default())
				{
					formatting_context fmt_ctx_copy;
					if(fmt_ctx) fmt_ctx_copy = *fmt_ctx;
                    
                    containing_block_context::typed_pixel tp_content_offset(content_offset, containing_block_context::cbc_value_type_auto);
                    containing_block_context measure_cb = m_container_wm.update_logical(self_size, tp_content_offset, std::nullopt);
                    measure_cb.size_mode = containing_block_context::size_mode_content | containing_block_context::size_mode_measure;
                    
					if (stretch_cross_definite) measure_cb.size_mode |= containing_block_context::size_mode_exact_height;
			el->measure(measure_cb, fmt_ctx ? &fmt_ctx_copy : nullptr);
					content_size = get_el_main_size();
				}
				flex_base_size = content_size;
				break;
			case flex_basis_max_content:
				{
					formatting_context fmt_ctx_copy;
					if(fmt_ctx) fmt_ctx_copy = *fmt_ctx;
                    
                    containing_block_context::typed_pixel tp_zero(0, containing_block_context::cbc_value_type_auto);
                    containing_block_context measure_cb = m_container_wm.update_logical(self_size, tp_zero, std::nullopt);
                    measure_cb.size_mode = containing_block_context::size_mode_measure | containing_block_context::size_mode_content;
                    
					if (stretch_cross_definite) measure_cb.size_mode |= containing_block_context::size_mode_exact_height;
			el->measure(measure_cb, fmt_ctx ? &fmt_ctx_copy : nullptr);
					flex_base_size = get_el_main_size();
				}
				break;
			default:
				flex_base_size = 0;
				break;
		}
	} else
	{
		flex_base_size = el->css().get_flex_basis().calc_percent(parent_size) + render_offset - el->box_sizing_inline(m_container_wm);
		// The max-content size of a wrapping container sums its items' max-content contributions, which
		// come from their own width rather than from their flex-basis.
		if (!m_container_wm.is_vertical() && el->parent() &&
			(self_size.size_mode & containing_block_context::size_mode_measure) &&
			(self_size.size_mode & containing_block_context::size_mode_content) &&
			!size_prop.is_predefined() && size_prop.units() == css_units_px)
		{
			const pixel_t own = size_prop.val() + render_offset - el->box_sizing_inline(m_container_wm);
			// CSS Flexbox 9.9.1 (Blink's max-content contribution, wrapping or not): the item's own size, kept
			// within its flex base size from above when it cannot grow and from below when it cannot shrink.
			pixel_t contribution = own;
			if (!(grow > 0)) contribution = std::min(contribution, flex_base_size);
			if (!(shrink > 0)) contribution = std::max(contribution, flex_base_size);
			flex_base_size = contribution;
		}
	}

	// A replaced item with an intrinsic ratio that is stretched to a definite container height transfers
	// that height to its main size (CSS Flexbox 9.2): the flex base size follows the stretched cross size.
	if (!m_container_wm.is_vertical() && el->src_el()->is_replaced() && el->css().get_flex_basis().is_predefined() && el->css().get_flex_basis().predef() == flex_basis_auto &&
		self_size.render_height.type != containing_block_context::cbc_value_type_auto &&
		size_prop.is_predefined() && size_prop.predef() == 0 &&
		el->css().get_height().is_predefined() && el->css().get_height().predef() == 0 &&
		((align & 0xFF) == flex_align_items_stretch || (align & 0xFF) == flex_align_items_normal) &&
		el->parent() && el->parent()->css().get_flex_wrap() == flex_wrap_nowrap)
	{
		litehtml::size sz;
		el->src_el()->get_content_size(sz, 0);
		if (sz.width > 0 && sz.height > 0)
		{
			const pixel_t cross = std::max<pixel_t>(0, (pixel_t)self_size.render_height - el->content_offset_block(m_container_wm));
			flex_base_size = cross * sz.width / sz.height + render_offset;
			// The transferred size is the item's min-content contribution in this container
			// too: at the definite stretched cross size the ratio leaves it no narrower
			// size, so an intrinsic query on the container (a min-content grid track) sees
			// the transferred width, not the size the cross-less min-content measure
			// reports (0 for a ratio-only item).
			min_content_contribution = std::max(min_content_contribution, flex_base_size);
			// The automatic minimum is then the transferred size too, in both directions: an image
			// squeezed to a container shorter than itself gets narrower, not stuck at its natural
			// width, and one stretched to a TALLER container keeps the transferred width as its
			// floor, so flex-shrink cannot pull it back to its intrinsic width (Chrome keeps the
			// transferred size and lets the line overflow). The min-content measure behind
			// min_main_size runs without the stretched cross size, so it cannot see the transfer.
			if (min_size_prop.is_predefined() && min_main_size > flex_base_size)
			{
				min_main_size = flex_base_size;
				min_content_contribution = std::min(min_content_contribution, flex_base_size);
			} else if (min_size_prop.is_predefined() && !el->src_el()->is_ratio_only_replaced())
			{
				// Raise the shrink floor to the transferred size. Ratio-only items are
				// excluded: render_flex re-transfers them against its own cross size
				// afterwards, and a floor raised from THIS pass's height would clamp
				// that second transfer up to a size the item never gets.
				min_main_size = flex_base_size;
			}
		}
	}

	scaled_flex_shrink_factor = (flex_base_size - render_offset) * shrink;
}

void litehtml::flex_item_row_direction::apply_main_auto_margins()
{
    if(!auto_margin_main_start.is_default()) m_container_wm.set_inline_start(el->get_margins(), auto_margin_main_start);
    if(!auto_margin_main_end.is_default()) m_container_wm.set_inline_end(el->get_margins(), auto_margin_main_end);
}

bool litehtml::flex_item_row_direction::apply_cross_auto_margins(pixel_t cross_size)
{
	if(auto_margin_cross_end || auto_margin_cross_start)
	{
		int margins_num = 0;
		if(auto_margin_cross_end) margins_num++;
		if(auto_margin_cross_start) margins_num++;
		pixel_t margin = (cross_size - get_el_cross_size()) / margins_num;

        if(auto_margin_cross_start) m_container_wm.set_block_start(el->get_margins(), margin);
        if(auto_margin_cross_end) m_container_wm.set_block_end(el->get_margins(), margin);
        
		return true;
	}
	return false;
}

void litehtml::flex_item_row_direction::set_main_position(pixel_t pos)
{
    main_pos = pos;
}

void litehtml::flex_item_row_direction::set_cross_position(pixel_t pos)
{
    cross_pos = pos;
}

void litehtml::flex_item_row_direction::layout_item(litehtml::flex_line &ln,
													   const litehtml::containing_block_context &self_size_const,
													   litehtml::formatting_context *fmt_ctx)
{
	containing_block_context self_size = self_size_const;
	self_size.width = self_size.render_width;
	self_size.height = self_size.render_height;

	containing_block_context child_cb = self_size;
	child_cb.mode = m_container_wm.mode();
    // Main Axis = Inline
    child_cb.inline_size() = main_size - el->content_offset_inline(m_container_wm) + el->box_sizing_inline(m_container_wm);
    child_cb.render_inline_size() = child_cb.inline_size();

	bool stretch = ((align & 0xFF) == flex_align_items_stretch || (align & 0xFF) == flex_align_items_normal) &&
                   !auto_margin_cross_start && !auto_margin_cross_end;
	const css_length& cross_prop = m_container_wm.get_block_size(el->css());

	if (stretch && (cross_prop.is_predefined() && cross_prop.predef() == 0))
	{
        // Cross Axis = Block
        child_cb.block_size() = ln.cross_size - el->content_offset_block(m_container_wm) + el->box_sizing_block(m_container_wm);
        child_cb.render_block_size() = child_cb.block_size();
		child_cb.size_mode = containing_block_context::size_mode_exact_width | containing_block_context::size_mode_exact_height;
		// The stretched size is not definite for percentage children while the container height is auto.
		if (self_size_const.height.type == containing_block_context::cbc_value_type_auto && !m_container_wm.is_vertical())
			child_cb.size_mode |= containing_block_context::size_mode_cyclic_height;
	} else {
        child_cb.block_size().type = containing_block_context::cbc_value_type_auto;
        child_cb.render_block_size().type = containing_block_context::cbc_value_type_auto;
        child_cb.size_mode &= ~(containing_block_context::size_mode_exact_width | containing_block_context::size_mode_exact_height);
        
        if (m_container_wm.is_vertical())
        {
            child_cb.size_mode |= containing_block_context::size_mode_exact_height;
        } else
        {
            child_cb.size_mode |= containing_block_context::size_mode_exact_width;
        }
		if (cross_prop.is_predefined())
		{
			child_cb.size_mode |= containing_block_context::size_mode_content;
		}
		else if (!m_container_wm.is_vertical() && cross_prop.units() == css_units_percentage &&
				 self_size.render_block_size().type != containing_block_context::cbc_value_type_auto &&
				 !(self_size.height_cyclic && el->parent() && el->parent()->css().get_height().is_predefined()))
		{
			// A percentage height against a definite container height is a definite cross size.
			// It stays indefinite when the container's height is cyclic AND content-derived
			// (an auto height the container got from flexing in an auto-height ancestor):
			// a container with a definite height of its own (height:80px) resolves the
			// percentage even when the cyclic flag was inherited from further up.
			// The block size carries the box-sizing offset (as in align_stretch), so it is used as is.
			pixel_t content = cross_prop.calc_percent(self_size.render_block_size().value);
			child_cb.block_size() = content;
			child_cb.render_block_size() = content;
			child_cb.size_mode |= containing_block_context::size_mode_exact_height;
		}
	}

	el->measure(child_cb, fmt_ctx);
	if (!(self_size.size_mode & containing_block_context::size_mode_measure))
	{
		// The inline size is the flex main size by now. Placing with the shrink-to-fit flag still set
		// would skip the text-align of the item's lines (they only align inside an exact width).
		containing_block_context place_cb = child_cb;
		if (!m_container_wm.is_vertical() && el->css().get_writing_mode() == writing_mode_horizontal_tb)
			place_cb.size_mode &= ~containing_block_context::size_mode_content;
		el->place(0, 0, place_cb, fmt_ctx);
	}
}

void litehtml::flex_item_row_direction::align_stretch(flex_line &ln, const containing_block_context &self_size_const,
													  formatting_context *fmt_ctx)
{
	containing_block_context self_size = self_size_const;
	self_size.width = self_size.render_width;
	self_size.height = self_size.render_height;

	set_cross_position(ln.cross_start);
    const css_length& cross_prop = m_container_wm.get_block_size(el->css());
	if ((cross_prop.is_predefined() && cross_prop.predef() == 0))
	{
        pixel_t new_block_size = ln.cross_size - el->content_offset_block(m_container_wm) + el->box_sizing_block(m_container_wm);
        
        // Use current physical dimensions converted to logical inline size
        satoru::logical_size current_content_size = m_container_wm.to_logical(el->pos().width, el->pos().height);
        pixel_t new_inline_size = current_content_size.inline_size + el->box_sizing_inline(m_container_wm);

        containing_block_context::typed_pixel tp_is(new_inline_size, containing_block_context::cbc_value_type_absolute);
        // A stretched item is a definite size for its percentage children only when the container's own height is definite.
        containing_block_context::typed_pixel tp_bs(new_block_size, containing_block_context::cbc_value_type_absolute);

        containing_block_context cb = m_container_wm.update_logical(self_size, tp_is, tp_bs);
        cb.size_mode = containing_block_context::size_mode_exact_width | containing_block_context::size_mode_exact_height;
        if (self_size_const.height.type == containing_block_context::cbc_value_type_auto)
            cb.size_mode |= containing_block_context::size_mode_cyclic_height;

		if (self_size.size_mode & containing_block_context::size_mode_measure)
		{
			el->measure(cb, fmt_ctx);
		} else
		{
			el->measure(cb, fmt_ctx);
			el->place(el->left(), el->top(), cb, fmt_ctx);
		}
		apply_main_auto_margins();
	}
}

void litehtml::flex_item_row_direction::align_baseline(litehtml::flex_line &ln,
													   const containing_block_context &/*self_size*/,
													   formatting_context */*fmt_ctx*/)
{
	if (align & flex_align_items_last)
	{
		set_cross_position(ln.cross_start + ln.last_baseline.get_offset_from_top(ln.cross_size) - el->get_last_baseline());
	} else
	{
		set_cross_position(ln.cross_start + ln.first_baseline.get_offset_from_top(ln.cross_size) - el->get_first_baseline());
	}
}

litehtml::pixel_t litehtml::flex_item_row_direction::get_el_main_size()
{
	return el->inline_size(m_container_wm);
}

litehtml::pixel_t litehtml::flex_item_row_direction::get_el_cross_size()
{
	return el->block_size(m_container_wm);
}

////////////////////////////////////////////////////////////////////////////////////

void litehtml::flex_item_column_direction::direction_specific_init(const litehtml::containing_block_context &self_size_const,
																   litehtml::formatting_context *fmt_ctx)
{
	containing_block_context self_size = self_size_const;
	self_size.width = self_size.render_width;
	self_size.height = self_size.render_height;

	// The container height is indefinite for percentage resolution not only when it is
	// auto-typed: a height that was itself resolved from content in an auto-height
	// ancestor arrives as a plain number but stays cyclic (a column flex item in an
	// auto-height column container). The container's own height property staying auto
	// is the discriminator -- a container with a definite height of its own resolves
	// percentages against it even under a cyclic ancestor.
	const bool container_height_indefinite =
		self_size.render_height.type == containing_block_context::cbc_value_type_auto ||
		(self_size.height_cyclic && el->parent() && el->parent()->css().get_height().is_predefined());
    // Map logical margins to auto_margin flags (Main = Block, Cross = Inline)
    const auto& margins = el->css().get_margins();
    if(m_container_wm.block_start(margins).is_predefined()) auto_margin_main_start = 0;
    if(m_container_wm.block_end(margins).is_predefined()) auto_margin_main_end = 0;
    if(m_container_wm.inline_start(margins).is_predefined()) auto_margin_cross_start = true;
    if(m_container_wm.inline_end(margins).is_predefined()) auto_margin_cross_end = true;
	
	// A specified width (e.g. `width:100%` on an svg/img) is just as definite: its block size depends on it.
	const css_length& cross_size = m_container_wm.get_inline_size(el->css());
	// `fit-content` / `max-content` / `min-content` are not a stretch: the item's used inline size comes
	// from its content, so it can end up wider than the container (a 90px child in an 80px column).
	// Measuring the block size at the container width freezes it too small for that box.
	const bool intrinsic_cross = cross_size.is_predefined() && cross_size.predef() > 0;
	// An indefinite inline size in an intrinsic-size query (an auto-width container measured from its
	// content) is the trial width, not a cross size to stretch to: the item contributes its own content
	// width to the line instead. In a normal layout the parent has already resolved that width, so it
	// is the used cross size. The exception is a container whose own width comes from its content
	// (`width:fit-content` / max-content / min-content): there the trial width is the only width
	// there is, and stretching to it hands the item a width the container may never be that wide at.
	const bool container_width_from_content = [&]()
	{
		auto par = el->parent();
		if (!par) return false;
		const css_length& par_width = par->src_el()->css().get_width();
		return par_width.is_predefined() && par_width.predef() > 0;
	}();
	const bool definite_inline = self_size.render_inline_size().type != containing_block_context::cbc_value_type_auto ||
		!(self_size.size_mode & containing_block_context::size_mode_content) || !container_width_from_content;
	// A stretched item's block size is measured at its stretched cross size (the container's width),
	// not as shrink-to-fit content: its percentage-based descendants need that definite width.
	const bool stretch_cross = ((align & 0xFF) == flex_align_items_stretch || (align & 0xFF) == flex_align_items_normal) &&
		!auto_margin_cross_start && !auto_margin_cross_end && !m_container_wm.is_vertical() &&
		!intrinsic_cross && definite_inline &&
		cross_size.is_predefined() &&
		self_size.render_width.value > 0;
	const bool specified_cross = !m_container_wm.is_vertical() && !cross_size.is_predefined() &&
		!cross_size.is_calc() && definite_inline && self_size.render_width.value > 0;
	// A percentage cross size also resolves when the content width is 0 (a border-box flex container
	// with padding wider than its width): it is definite, and only 0, which still transfers to the
	// main size (a 300x90 image in a 0-wide column is 0 tall, not 90).
	const bool percent_cross_definite = !m_container_wm.is_vertical() && !cross_size.is_predefined() &&
		!cross_size.is_calc() && definite_inline;
	// A multi-line column container also breaks lines in the inline axis: each line's cross size is
	// the largest hypothetical (fit-content) inline size of its items (CSS Flexbox 9.4.8), so an item
	// whose content does not fit the container's content width is laid out wider than the container.
	// The base block size has to be measured at that width: measured at the container width, its
	// height is frozen too small for the box the item ends up with (e.g. a ratio-sized svg inside an
	// auto-width wrapper). Only raise the width, never lower it, so ordinary stretching is unchanged.
	const bool multiline_cross = el->parent() && el->parent()->css().get_flex_wrap() != flex_wrap_nowrap;
	def_value<pixel_t> fit_inline_size(0);
	def_value<pixel_t> intrinsic_inline_size(0);
	// An intrinsic keyword (`width:fit-content`, max-content, min-content) sizes the item from its
	// content, which can be wider than the container. Its block size then has to be measured at that
	// inline size: at the container's width the content is laid out narrower and the height is frozen
	// too small (a ratio-sized svg inside a fit-content wrapper). The content measure answers that
	// width; measure the block size at it.
	auto intrinsic_inline = [&]() -> pixel_t
	{
		if (intrinsic_inline_size.is_default())
		{
			formatting_context fmt_ctx_copy;
			if (fmt_ctx) fmt_ctx_copy = *fmt_ctx;
			containing_block_context fit_cb = self_size;
			fit_cb.width.type = containing_block_context::cbc_value_type_auto;
			fit_cb.render_width.type = containing_block_context::cbc_value_type_auto;
			fit_cb.height.type = containing_block_context::cbc_value_type_auto;
			fit_cb.render_height.type = containing_block_context::cbc_value_type_auto;
			fit_cb.size_mode &= ~(containing_block_context::size_mode_exact_width |
								  containing_block_context::size_mode_exact_height);
			fit_cb.size_mode |= containing_block_context::size_mode_measure |
								containing_block_context::size_mode_content;
			if (cross_size.predef() == 3)
			{
				fit_cb.size_mode |= containing_block_context::size_mode_min_content;
			}
			el->measure(fit_cb, fmt_ctx ? &fmt_ctx_copy : nullptr);
			pixel_t measured = std::max<pixel_t>(0, el->inline_size(m_container_wm) -
														el->content_offset_inline(m_container_wm) +
														el->box_sizing_inline(m_container_wm));
			// fit-content = min(max-content, max(min-content, available)), so a 300px max-content
			// inside an 80px column is 80 wide, not 300.
			const pixel_t available = self_size.render_width.value - el->content_offset_width() + el->box_sizing_width();
			if (cross_size.predef() == 1 && measured > available)
			{
				fit_cb.size_mode |= containing_block_context::size_mode_min_content;
				el->measure(fit_cb, fmt_ctx ? &fmt_ctx_copy : nullptr);
				const pixel_t min_measured = std::max<pixel_t>(0, el->inline_size(m_container_wm) -
																	  el->content_offset_inline(m_container_wm) +
																	  el->box_sizing_inline(m_container_wm));
				measured = std::max(min_measured, available);
			}
			intrinsic_inline_size = measured;
		}
		return intrinsic_inline_size;
	};
	auto stretched_inline_size = [&]() -> pixel_t
	{
		pixel_t width = self_size.render_width.value - el->content_offset_width() + el->box_sizing_width();
		// A definite min-width can make the item wider than the container (a column does not shrink an
		// item below it), so its block size has to be measured at that width, not at the container's.
		const css_length& min_inline = el->css().get_min_width();
		if (!min_inline.is_predefined() && definite_inline)
		{
			width = std::max(width, min_inline.calc_percent(self_size.render_inline_size().value) -
									   el->box_sizing_width());
		}
		if (!multiline_cross || !cross_size.is_predefined() || width <= 0) return width;
		if (fit_inline_size.is_default())
		{
			formatting_context fmt_ctx_copy;
			if (fmt_ctx) fmt_ctx_copy = *fmt_ctx;
			// Width auto with the content mode is the fit-content measure (available width, floored by
			// the item's min-content width and clamped by its max-width).
			containing_block_context fit_cb = self_size;
			fit_cb.height.type = containing_block_context::cbc_value_type_auto;
			fit_cb.size_mode &= ~(containing_block_context::size_mode_exact_width |
								  containing_block_context::size_mode_exact_height);
			fit_cb.size_mode |= containing_block_context::size_mode_measure |
								containing_block_context::size_mode_content;
			el->measure(fit_cb, fmt_ctx ? &fmt_ctx_copy : nullptr);
			fit_inline_size = std::max<pixel_t>(0, el->inline_size(m_container_wm) -
												   el->content_offset_inline(m_container_wm) +
												   el->box_sizing_inline(m_container_wm));
		}
		return std::max(width, (pixel_t) fit_inline_size);
	};
	// The hypothetical cross size of a stretched item in a multi-line column container: the
	// fit-content width its line is sized from, before align-content stretches the line.
	auto hypothetical_inline_size = [&]() -> pixel_t
	{
		if (fit_inline_size.is_default())
		{
			formatting_context fmt_ctx_copy;
			if (fmt_ctx) fmt_ctx_copy = *fmt_ctx;
			containing_block_context fit_cb = self_size;
			fit_cb.height.type = containing_block_context::cbc_value_type_auto;
			fit_cb.size_mode &= ~(containing_block_context::size_mode_exact_width |
								  containing_block_context::size_mode_exact_height);
			fit_cb.size_mode |= containing_block_context::size_mode_measure |
								containing_block_context::size_mode_content;
			el->measure(fit_cb, fmt_ctx ? &fmt_ctx_copy : nullptr);
			fit_inline_size = std::max<pixel_t>(0, el->inline_size(m_container_wm) -
												   el->content_offset_inline(m_container_wm) +
												   el->box_sizing_inline(m_container_wm));
		}
		return fit_inline_size;
	};
	// An auto inline size in a column that does not stretch is fit-content: min(max-content,
	// max(min-content, available)). The content measures below answer with max-content (a ratio-only
	// svg with a percentage width contributes the 300px default object width, so a 72px column's item
	// is 300 wide there), so the block size has to be measured at the fitted width: the percentage svg
	// then lays out at 30% of 72 and the item is 4.31 tall, not 60.
	const bool fit_content_cross = !m_container_wm.is_vertical() && cross_size.is_predefined() &&
		cross_size.predef() == 0 && !stretch_cross && definite_inline && self_size.render_width.value > 0;
	auto clamp_fit_content_width = [&](const containing_block_context& measured_cb, formatting_context* ctx)
	{
		if (!fit_content_cross) return;
		const pixel_t available = self_size.render_width.value - el->content_offset_width() + el->box_sizing_width();
		auto current_inline = [&]()
		{
			return el->inline_size(m_container_wm) - el->content_offset_inline(m_container_wm) +
				   el->box_sizing_inline(m_container_wm);
		};
		if (current_inline() <= available)
		{
			// The content measure laid the item's inline content out at its max-content width,
			// so the main size it reported belongs to that wider layout. When the item's
			// resolved inline size is smaller -- its own max-width caps it below the available
			// width, e.g. a max-width:768 paragraph in an align-items:flex-start column of
			// 1000 -- re-measure at the resolved inline size, so the main size matches the
			// box the item ends up with (the paragraph is 3 lines tall at 768, not the single
			// line of its max-content measure).
			if ((measured_cb.size_mode & containing_block_context::size_mode_content) &&
				!(measured_cb.size_mode & containing_block_context::size_mode_min_content))
			{
				const pixel_t resolved = current_inline();
				if (resolved > 0)
				{
					containing_block_context fit_cb = measured_cb;
					fit_cb.width = resolved;
					fit_cb.render_width = resolved;
					fit_cb.size_mode |= containing_block_context::size_mode_exact_width;
					fit_cb.size_mode &= ~(containing_block_context::size_mode_content |
										  containing_block_context::size_mode_min_content);
					el->measure(fit_cb, ctx);
				}
			}
			return;
		}
		containing_block_context min_cb = measured_cb;
		min_cb.size_mode |= containing_block_context::size_mode_min_content;
		el->measure(min_cb, ctx);
		const pixel_t fit = std::max((pixel_t) current_inline(), available);
		containing_block_context fit_cb = measured_cb;
		fit_cb.width = fit;
		fit_cb.render_width = fit;
		fit_cb.size_mode |= containing_block_context::size_mode_exact_width;
		fit_cb.size_mode &= ~(containing_block_context::size_mode_content |
							  containing_block_context::size_mode_min_content);
		el->measure(fit_cb, ctx);
	};
	auto apply_stretch_width = [&](containing_block_context& cb)
	{
		if (specified_cross)
		{
			cb.width = cross_size.calc_percent(self_size.render_width.value);
			cb.width.type = containing_block_context::cbc_value_type_absolute;
			cb.render_width = cb.width;
			cb.size_mode |= containing_block_context::size_mode_exact_width;
			cb.size_mode &= ~(containing_block_context::size_mode_content | containing_block_context::size_mode_min_content);
			return;
		}
		if (!stretch_cross && !intrinsic_cross) return;
		// In a multi-line column container the item's base size is measured at its hypothetical
		// (fit-content) cross size: the lines are sized from the items and only stretched
		// afterwards, so the stretched width is not yet known while the base size is computed.
		cb.width = intrinsic_cross ? intrinsic_inline()
				   : (multiline_cross && !el->src_el()->is_replaced() ? hypothetical_inline_size()
																	  : stretched_inline_size());
		cb.width.type = containing_block_context::cbc_value_type_absolute;
		cb.render_width = cb.width;
		cb.size_mode |= containing_block_context::size_mode_exact_width;
		cb.size_mode &= ~(containing_block_context::size_mode_content | containing_block_context::size_mode_min_content);
	};

    // Manual branching for min/max
    const css_length& min_size_prop = m_container_wm.is_vertical() ? el->css().get_min_width() : el->css().get_min_height();
    const css_length& max_size_prop = m_container_wm.is_vertical() ? el->css().get_max_width() : el->css().get_max_height();
    const css_length& size_prop = m_container_wm.get_block_size(el->css()); // Main size
    
    pixel_t render_offset = el->render_offset_height();
    if(m_container_wm.is_vertical()) render_offset = el->render_offset_width();

    pixel_t content_offset = el->content_offset_block(m_container_wm);
	pixel_t parent_size = self_size.render_block_size();

	if (min_size_prop.is_predefined())
	{
		if (el->css().get_overflow() == overflow_visible ||
			el->src_el()->css().get_display() == display_webkit_box)
		{
			formatting_context fmt_ctx_copy;
			if(fmt_ctx) fmt_ctx_copy = *fmt_ctx;
			int mode = containing_block_context::size_mode_measure;
			bool stretch = (align & 0xFF) == flex_align_items_stretch || (align & 0xFF) == flex_align_items_normal;
			mode |= containing_block_context::size_mode_content;
			
            // Reset both? Original logic: 
            // child_cb.width.type = auto; child_cb.height.type = auto;
            // child_cb.size_mode &= ~exact; size_mode |= mode;
            containing_block_context measure_cb = m_container_wm.update_logical(self_size, std::nullopt, std::nullopt); 
            measure_cb.width.type = containing_block_context::cbc_value_type_auto;
            measure_cb.height.type = containing_block_context::cbc_value_type_auto;
            measure_cb.size_mode &= ~(containing_block_context::size_mode_exact_width | containing_block_context::size_mode_exact_height);
            measure_cb.size_mode |= mode;
            apply_stretch_width(measure_cb);

			el->measure(measure_cb, fmt_ctx ? &fmt_ctx_copy : nullptr);
			clamp_fit_content_width(measure_cb, fmt_ctx ? &fmt_ctx_copy : nullptr);
			min_main_size = get_el_main_size();
			// Automatic minimum size: the smaller of the content size and a definite specified
			// size. The measured size already honours the item's own height, so take the content
			// size from the extent of its in-flow children (horizontal writing mode only).
			if (!m_container_wm.is_vertical() && !size_prop.is_predefined() &&
				size_prop.units() != css_units_percentage)
			{
				pixel_t content_extent = 0;
				for (const auto& child : el->children())
				{
					auto position = child->src_el()->css().get_position();
					if (position == element_position_absolute || position == element_position_fixed) continue;
					content_extent = std::max(content_extent, child->top() + child->height() + child->get_margins().bottom);
				}
				pixel_t content_main = content_extent + el->content_offset_block(m_container_wm);
				if (content_main < min_main_size) min_main_size = content_main;
			}
			else if (!m_container_wm.is_vertical() && size_prop.units() == css_units_percentage &&
					 !container_height_indefinite)
			{
				// A percentage against a definite container height is a specified size suggestion too.
				pixel_t specified = size_prop.calc_percent(parent_size) + render_offset - el->box_sizing_block(m_container_wm);
				if (specified < min_main_size) min_main_size = specified;
			}
		} else
		{
			min_main_size = render_offset;
		}
	} else
	{
		min_main_size = min_size_prop.calc_percent(parent_size) + render_offset - el->box_sizing_block(m_container_wm);
	}
	if (!max_size_prop.is_predefined())
	{
		max_main_size = max_size_prop.calc_percent(parent_size) + render_offset - el->box_sizing_block(m_container_wm);
	}

	bool flex_basis_predefined = el->css().get_flex_basis().is_predefined();
	int predef = flex_basis_auto;
	if(flex_basis_predefined) predef = el->css().get_flex_basis().predef();
	else if(el->css().get_flex_basis().val() < 0) flex_basis_predefined = true;

	// Fix: If we are measuring the container (intrinsic sizes), we should treat
	// flex-basis as content for growable items or if basis is 0/percentage.
	// CAUTION: Using flex_basis_content instead of flex_basis_max_content so it can wrap appropriately
	// in normal flow.
	if (!flex_basis_predefined && (self_size.size_mode & containing_block_context::size_mode_measure)) {
		if (grow > 0 || el->css().get_flex_basis().units() == css_units_percentage || el->css().get_flex_basis().val() == 0) {
			flex_basis_predefined = true;
			predef = flex_basis_content;
		}
	}

	// A percentage flex-basis (`flex: 1 1` is `1 1 0%`) against an indefinite container height
	// behaves as content.
	if (!flex_basis_predefined && !m_container_wm.is_vertical() &&
		el->css().get_flex_basis().units() == css_units_percentage &&
		container_height_indefinite)
	{
		flex_basis_predefined = true;
		predef = flex_basis_fit_content;
	}

	if (flex_basis_predefined)
	{
		if(predef == flex_basis_auto && size_prop.is_predefined()) predef = flex_basis_fit_content;
		// A percentage block size against an indefinite container height behaves as auto.
		if(predef == flex_basis_auto && !m_container_wm.is_vertical() &&
		   size_prop.units() == css_units_percentage &&
		   container_height_indefinite)
		{
			predef = flex_basis_fit_content;
		}
		switch (predef)
		{
			case flex_basis_auto:
				flex_base_size = size_prop.calc_percent(parent_size) + render_offset - el->box_sizing_block(m_container_wm);
				break;
			case flex_basis_content:
			case flex_basis_max_content:
			case flex_basis_fit_content:
				{
					containing_block_context measure_size = self_size;
                    measure_size.width.type = containing_block_context::cbc_value_type_auto;
                    measure_size.height.type = containing_block_context::cbc_value_type_auto;
                    measure_size.size_mode &= ~(containing_block_context::size_mode_exact_width | containing_block_context::size_mode_exact_height);
					measure_size.size_mode |= containing_block_context::size_mode_measure;
					bool stretch = (align & 0xFF) == flex_align_items_stretch || (align & 0xFF) == flex_align_items_normal;
					measure_size.size_mode |= containing_block_context::size_mode_content;
					apply_stretch_width(measure_size);
					formatting_context fmt_ctx_copy;
					if(fmt_ctx) fmt_ctx_copy = *fmt_ctx;
					el->measure(measure_size, fmt_ctx ? &fmt_ctx_copy : nullptr);
					clamp_fit_content_width(measure_size, fmt_ctx ? &fmt_ctx_copy : nullptr);
					flex_base_size = get_el_main_size();
				}
				break;
			case flex_basis_min_content:
				flex_base_size = min_main_size;
				break;
			default:
				flex_base_size = 0;
		}
	} else
	{
		flex_base_size = el->css().get_flex_basis().calc_percent(parent_size) + render_offset - el->box_sizing_block(m_container_wm);
	}

	scaled_flex_shrink_factor = (flex_base_size - render_offset) * shrink;
	// A replaced item with an intrinsic ratio and an auto block size takes its main size from its
	// cross size (the transferred size suggestion, CSS Flexbox 9.2): a `width:30%` svg in a 600px
	// column is 180 wide and 36 tall, whatever trial width the content measure ran at.
	// A stretched cross size is only definite up front in a single-line container: a wrapping column
	// breaks its lines in the inline axis, so the line cross size a multi-line item stretches to is
	// resolved after the items are sized (Blink's flexible box computes those line cross sizes from
	// the items themselves, and a stretched item therefore has nothing to transfer from while its
	// flex base size is being computed). Such an item keeps its intrinsic main size and is only
	// widened by the stretch (a 120x30 image in a 600px `wrap` column is 600x30, not 600x150).
	const bool stretch_transfers = stretch_cross && !multiline_cross;
	if (!m_container_wm.is_vertical() && size_prop.is_predefined() && el->src_el()->is_replaced())
	{
		pixel_t used_cross = 0;
		if (cross_size.is_predefined())
		{
			if (stretch_transfers) used_cross = stretched_inline_size();
		} else if (percent_cross_definite)
		{
			used_cross = cross_size.calc_percent(self_size.render_inline_size().value);
		}
		litehtml::size sz;
		el->src_el()->get_content_size(sz, 0);
		if ((stretch_transfers || percent_cross_definite) && sz.width > 0 && sz.height > 0)
		{
			const pixel_t transferred = used_cross * sz.height / sz.width + el->render_offset_height();
			flex_base_size = transferred;
			if (min_size_prop.is_predefined() && min_main_size > transferred)
			{
				min_main_size = transferred;
			}
		}
	}
}

void litehtml::flex_item_column_direction::apply_main_auto_margins()
{
    if(!auto_margin_main_start.is_default()) m_container_wm.set_block_start(el->get_margins(), auto_margin_main_start);
    if(!auto_margin_main_end.is_default()) m_container_wm.set_block_end(el->get_margins(), auto_margin_main_end);
}

bool litehtml::flex_item_column_direction::apply_cross_auto_margins(pixel_t cross_size)
{
	if(auto_margin_cross_end || auto_margin_cross_start)
	{
		int margins_num = 0;
		if(auto_margin_cross_end) margins_num++;
		if(auto_margin_cross_start) margins_num++;
		pixel_t margin = (cross_size - get_el_cross_size()) / margins_num;

        if(auto_margin_cross_start) m_container_wm.set_inline_start(el->get_margins(), margin);
        if(auto_margin_cross_end) m_container_wm.set_inline_end(el->get_margins(), margin);

		return true;
	}
	return false;
}

void litehtml::flex_item_column_direction::set_main_position(pixel_t pos)
{
    main_pos = pos;
}

void litehtml::flex_item_column_direction::set_cross_position(pixel_t pos)
{
    cross_pos = pos;
}

void litehtml::flex_item_column_direction::layout_item(litehtml::flex_line &ln,
													   const litehtml::containing_block_context &self_size_const,
													   litehtml::formatting_context *fmt_ctx)
{
	containing_block_context self_size = self_size_const;
	self_size.width = self_size.render_width;
	self_size.height = self_size.render_height;

	containing_block_context child_cb = self_size;
	child_cb.mode = m_container_wm.mode();
    // Main Axis = Block
    child_cb.block_size() = main_size - el->content_offset_block(m_container_wm) + el->box_sizing_block(m_container_wm);
    child_cb.render_block_size() = child_cb.block_size();

	bool stretch = ((align & 0xFF) == flex_align_items_stretch || (align & 0xFF) == flex_align_items_normal) &&
                   !auto_margin_cross_start && !auto_margin_cross_end;
	const css_length& cross_prop = m_container_wm.get_inline_size(el->css());

	if (stretch && (cross_prop.is_predefined() && cross_prop.predef() == 0))
	{
        // Cross Axis = Inline
        child_cb.inline_size() = ln.cross_size - el->content_offset_inline(m_container_wm) + el->box_sizing_inline(m_container_wm);
        child_cb.render_inline_size() = child_cb.inline_size();
		child_cb.size_mode = containing_block_context::size_mode_exact_width | containing_block_context::size_mode_exact_height;
		if (!m_container_wm.is_vertical() &&
			(self_size_const.height.type == containing_block_context::cbc_value_type_auto || self_size_const.height_cyclic) &&
			el->css().get_height().is_predefined())
			child_cb.size_mode |= containing_block_context::size_mode_cyclic_height;
	} else {
        child_cb.width.type = containing_block_context::cbc_value_type_auto;
        child_cb.height.type = containing_block_context::cbc_value_type_auto;
        child_cb.size_mode &= ~(containing_block_context::size_mode_exact_width | containing_block_context::size_mode_exact_height);

		if (m_container_wm.is_vertical())
		{
            child_cb.width = main_size - el->content_offset_width() + el->box_sizing_width();
            child_cb.render_width = child_cb.width;
			child_cb.size_mode |= containing_block_context::size_mode_exact_width;
		} else
		{
            child_cb.height = main_size - el->content_offset_height() + el->box_sizing_height();
            child_cb.render_height = child_cb.height;
			child_cb.size_mode |= containing_block_context::size_mode_exact_height;
			// The main size of an item in an auto-height column container is not definite for percentage children.
			if ((self_size_const.height.type == containing_block_context::cbc_value_type_auto || self_size_const.height_cyclic) &&
				el->css().get_height().is_predefined())
				child_cb.size_mode |= containing_block_context::size_mode_cyclic_height;
		}
		if (cross_prop.is_predefined())
		{
			child_cb.size_mode |= containing_block_context::size_mode_content;
		}
	}

	el->measure(child_cb, fmt_ctx);
	// A fit-content item is min(max-content, max(min-content, available)): the content measure above
	// answers with max-content, so a 300px max-content inside an 80px column still has to come down
	// to 80 (and its percentage-width svg then lays out at 8, not 30). An auto inline size in a column
	// that does not stretch is the same fit-content size (CSS Flexbox 9.4: an item with no cross-size
	// stretch is sized from its content), so it is clamped the same way.
	const int cross_predef = cross_prop.is_predefined() ? (int) cross_prop.predef() : -1;
	if (!m_container_wm.is_vertical() && (cross_predef == 1 || (cross_predef == 0 && !stretch)))
	{
		const pixel_t available = self_size.render_width.value - el->content_offset_width() + el->box_sizing_width();
		auto current_inline = [&]()
		{
			return el->inline_size(m_container_wm) - el->content_offset_inline(m_container_wm) +
				   el->box_sizing_inline(m_container_wm);
		};
		if (current_inline() > available)
		{
			containing_block_context min_cb = child_cb;
			min_cb.size_mode |= containing_block_context::size_mode_min_content;
			el->measure(min_cb, fmt_ctx);
			const pixel_t fit = std::max((pixel_t) current_inline(), available);
			containing_block_context fit_cb = child_cb;
			fit_cb.width = fit;
			fit_cb.render_width = fit;
			fit_cb.size_mode |= containing_block_context::size_mode_exact_width;
			fit_cb.size_mode &= ~(containing_block_context::size_mode_content |
								  containing_block_context::size_mode_min_content);
			el->measure(fit_cb, fmt_ctx);
		}
	}
	if (!(self_size.size_mode & containing_block_context::size_mode_measure))
	{
		// A shrink-to-fit item has its width by now: place it with that width as an exact one, or the
		// text-align of its lines would be skipped (see the row direction).
		containing_block_context place_cb = child_cb;
		if (!m_container_wm.is_vertical() && el->css().get_writing_mode() == writing_mode_horizontal_tb && cross_prop.is_predefined() && (!stretch || cross_prop.predef() != 0))
		{
			place_cb.width = el->inline_size(m_container_wm) - el->content_offset_inline(m_container_wm) + el->box_sizing_inline(m_container_wm);
			place_cb.width.type = containing_block_context::cbc_value_type_absolute;
			place_cb.render_width = place_cb.width;
			place_cb.size_mode |= containing_block_context::size_mode_exact_width;
			place_cb.size_mode &= ~containing_block_context::size_mode_content;
			// The content was laid out for the shrink-to-fit width before min/max-width clamped it, and
			// percentage widths of the children resolved against that unclamped width.
			el->measure(place_cb, fmt_ctx);
		}
		el->place(0, 0, place_cb, fmt_ctx);
	}
}

void litehtml::flex_item_column_direction::align_stretch(flex_line &ln, const containing_block_context &self_size_const,
													  formatting_context *fmt_ctx)
{
	containing_block_context self_size = self_size_const;
	self_size.width = self_size.render_width;
	self_size.height = self_size.render_height;

	set_cross_position(ln.cross_start);
	const css_length& cross_prop = m_container_wm.get_inline_size(el->css());
	if ((cross_prop.is_predefined() && cross_prop.predef() == 0))
	{
        pixel_t new_inline_size = ln.cross_size - el->content_offset_inline(m_container_wm) + el->box_sizing_inline(m_container_wm);
        
        // Use current physical dimensions converted to logical block size
        satoru::logical_size current_content_size = m_container_wm.to_logical(el->pos().width, el->pos().height);
        pixel_t new_block_size = current_content_size.block_size + el->box_sizing_block(m_container_wm);

        containing_block_context::typed_pixel tp_is(new_inline_size, containing_block_context::cbc_value_type_absolute);
        containing_block_context::typed_pixel tp_bs(new_block_size, containing_block_context::cbc_value_type_absolute);

        containing_block_context cb = m_container_wm.update_logical(self_size, tp_is, tp_bs);
        cb.size_mode = containing_block_context::size_mode_exact_width | containing_block_context::size_mode_exact_height;
        // A stretched item is a definite size for its percentage children only when the container's
        // own height is definite (mirrors the row direction's align_stretch).
        if ((self_size_const.height.type == containing_block_context::cbc_value_type_auto || self_size_const.height_cyclic) &&
            el->css().get_height().is_predefined())
            cb.size_mode |= containing_block_context::size_mode_cyclic_height;

		if (self_size.size_mode & containing_block_context::size_mode_measure)
		{
			el->measure(cb, fmt_ctx);
		} else
		{
			el->measure(cb, fmt_ctx);
			el->place(el->left(), el->top(), cb, fmt_ctx);
		}
		apply_main_auto_margins();
	}
}

void litehtml::flex_item_column_direction::align_baseline(litehtml::flex_line &ln,
													   const containing_block_context &self_size,
													   formatting_context *fmt_ctx)
{
	// A column container has no inline baseline: the items sharing it line up their border edges on the
	// start of the line, the widest start margin setting that edge.
	pixel_t shared_margin = 0;
	for (auto& it : ln.items)
	{
		if ((it->align & 0xFF) == flex_align_items_baseline)
			shared_margin = std::max(shared_margin, m_container_wm.inline_start(it->el->get_margins()));
	}
	set_cross_position(ln.cross_start + shared_margin - m_container_wm.inline_start(el->get_margins()));
}

litehtml::pixel_t litehtml::flex_item_column_direction::get_el_main_size()
{
	return el->block_size(m_container_wm);
}

litehtml::pixel_t litehtml::flex_item_column_direction::get_el_cross_size()
{
	return el->inline_size(m_container_wm);
}
