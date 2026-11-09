#include "render_block.h"
#include "render_inline_context.h"
#include "render_block_context.h"
#include "document.h"
#include "document_container.h"
#include "html_tag.h"
#include "types.h"

litehtml::pixel_t litehtml::render_item_block::place_float(const std::shared_ptr<render_item> &el, pixel_t block_offset, const containing_block_context &self_size, formatting_context* fmt_ctx)
{
	// In measure mode the float is not placed, so its position is stale; registering it would
	// pollute a shared formatting context with a phantom float (see skip_ledger below). In a
	// place pass, drop any entry this float left in the ledger on an earlier pass first.
	const bool skip_ledger_top = (self_size.size_mode & containing_block_context::size_mode_measure) &&
					   self_size.mode == writing_mode_horizontal_tb;
	if (!skip_ledger_top) fmt_ctx->remove_float(el, this);
    pixel_t line_block_start = fmt_ctx->get_cleared_top(el, block_offset);
    pixel_t line_inline_start = 0;
    pixel_t line_inline_end   = self_size.render_inline_size();
	fmt_ctx->get_line_left_right(line_block_start, self_size.render_inline_size(), line_inline_start, line_inline_end);

    pixel_t ret_inline_size = 0;

	// A width:auto float is as wide as its content: measure it in content mode so that a child held
	// up only by min-width (no content of its own) still counts.
	const bool auto_inline = el->src_el()->css().get_width().is_predefined();
	int float_mode = self_size.size_mode & (containing_block_context::size_mode_content | containing_block_context::size_mode_min_content);
	if(auto_inline) float_mode |= containing_block_context::size_mode_content;
	// A percentage width is resolved against the containing block, not against the space that is left
	// on the line: a `width:50%` float beside another float is half the block (Blink resolves the
	// percentage before floats take their room), so measuring it at `line_inline_end` makes it too narrow
	// or, when the line is full, zero.
	const pixel_t float_cb_inline = auto_inline ? line_inline_end : self_size.render_inline_size();
	pixel_t min_rendered_inline_size = el->measure(self_size.new_inline_size(float_cb_inline, float_mode), fmt_ctx);
	// A width:auto float is shrink-to-fit: min(max-content, max(min-content, available)). The content
	// measure answers with the max-content width, so a float whose content cannot fit the space on the
	// line has to fall back to its min-content width instead of overflowing at the max-content width.
	if (auto_inline && min_rendered_inline_size > line_inline_end)
	{
		pixel_t min_content_size = el->measure(self_size.new_inline_size(line_inline_end, float_mode | containing_block_context::size_mode_min_content), fmt_ctx);
		pixel_t fit_inline_size = std::max(min_content_size, line_inline_end);
		if (fit_inline_size < min_rendered_inline_size)
		{
			el->measure(self_size.new_inline_size(fit_inline_size, self_size.size_mode & (containing_block_context::size_mode_content | containing_block_context::size_mode_min_content)), fmt_ctx);
			min_rendered_inline_size = fit_inline_size;
		}
	}
	if (!(self_size.size_mode & containing_block_context::size_mode_measure))
	{
		el->place_logical(line_inline_start, line_block_start, self_size.new_inline_size(float_cb_inline, self_size.size_mode & (containing_block_context::size_mode_content | containing_block_context::size_mode_min_content)), fmt_ctx);
	}

	// In measure mode the float is not placed (see the guards above), so its position is
	// stale. Registering it would pollute a shared formatting context with a phantom
	// float whose bogus position later place-phase queries mistake for a real one
	// (e.g. a leading float:right pushes the first text line down). Skip the ledger
	// updates in measure mode for horizontal writing and report the float's own width.
	// Vertical writing keeps the legacy behavior (float cases 30/77 depend on it).
	const bool skip_ledger = skip_ledger_top;

	// A width:auto float that shrank to fit its content is laid out at that width. Every later
	// placement must hand it the same inline size: passing the container's width instead makes it
	// lay its children out again at the wide size, so a descendant positioned from the right edge
	// (an inner float:right, text-align:right) ends up outside the narrow box.
	// (content that cannot shrink below its min-content width makes the float wider than the space it has)
	const bool shrunk_to_fit = (min_rendered_inline_size < el->inline_size() || min_rendered_inline_size > line_inline_end - line_inline_start) &&
		el->src_el()->css().get_width().is_predefined();
	if(shrunk_to_fit)
	{
		// (a box held up by min-width reports 0 once it is given its width exactly: keep the content size)
		min_rendered_inline_size = std::max(min_rendered_inline_size, el->measure(self_size.new_inline_size(min_rendered_inline_size, self_size.size_mode & (containing_block_context::size_mode_content | containing_block_context::size_mode_min_content)), fmt_ctx));
		if (!(self_size.size_mode & containing_block_context::size_mode_measure))
		{
			el->place_logical(line_inline_start, line_block_start, self_size.new_inline_size(min_rendered_inline_size, self_size.size_mode & (containing_block_context::size_mode_content | containing_block_context::size_mode_min_content)), fmt_ctx);
		}
	}

    if (el->src_el()->css().get_float() == float_left)
    {
        // (percentage widths that sum to exactly 100% overshoot by float rounding error)
        if(el->inline_end_pos() > line_inline_end + 0.01)
        {
			line_block_start = fmt_ctx->find_next_line_top(line_block_start, el->inline_size(), self_size.render_inline_size());
			if (!(self_size.size_mode & containing_block_context::size_mode_measure))
			{
				el->place_logical(fmt_ctx->get_line_left(line_block_start), line_block_start, self_size.new_inline_size(shrunk_to_fit ? min_rendered_inline_size : float_cb_inline, self_size.size_mode & (containing_block_context::size_mode_content | containing_block_context::size_mode_min_content)), fmt_ctx);
			}
        }
		if(skip_ledger)
		{
			// A float that does not fit beside the earlier ones sits below them, so the box that
			// contains the floats has to reach that bottom, not just this float's own height.
			// The returned size is the extent of the float's line: floats sharing a line sum
			// into the container's intrinsic width, as find_min_left/right report in the place pass.
			const pixel_t measured_top = fmt_ctx->place_measured_float(el.get(), this, line_block_start, el->inline_size(),
																	 el->height(), float_left,
																	 self_size.render_inline_size(),
																	 &ret_inline_size);
			fmt_ctx->note_measured_float_bottom(measured_top + el->height());
		} else
		{
			fmt_ctx->add_float(el, min_rendered_inline_size, self_size.context_idx, this);
			fix_line_width(float_left, self_size, fmt_ctx);

			ret_inline_size = fmt_ctx->find_min_left(line_block_start, self_size.context_idx);
		}
    } else if (el->src_el()->css().get_float() == float_right)
    {
        if(line_inline_start + el->inline_size() > line_inline_end + 0.01)
        {
            pixel_t new_block_start = fmt_ctx->find_next_line_top(line_block_start, el->inline_size(), self_size.render_inline_size());
			if (!(self_size.size_mode & containing_block_context::size_mode_measure))
			{
				el->place_logical(fmt_ctx->get_line_right(new_block_start, self_size.render_inline_size()) - el->inline_size(), new_block_start, self_size.new_inline_size(shrunk_to_fit ? min_rendered_inline_size : self_size.render_inline_size(), self_size.size_mode & (containing_block_context::size_mode_content | containing_block_context::size_mode_min_content)), fmt_ctx);
			}
        } else
        {
			if (!(self_size.size_mode & containing_block_context::size_mode_measure))
			{
				el->place_logical(line_inline_end - el->inline_size(), line_block_start, self_size.new_inline_size(shrunk_to_fit ? min_rendered_inline_size : self_size.render_inline_size(), self_size.size_mode & (containing_block_context::size_mode_content | containing_block_context::size_mode_min_content)), fmt_ctx);
			}
        }
		if(skip_ledger)
		{
			// The returned size is the extent of the float's line, as in the place pass.
			const pixel_t measured_top = fmt_ctx->place_measured_float(el.get(), this, line_block_start, el->inline_size(),
																	 el->height(), float_right,
																	 self_size.render_inline_size(),
																	 &ret_inline_size);
			fmt_ctx->note_measured_float_bottom(measured_top + el->height());
		} else
		{
			fmt_ctx->add_float(el, min_rendered_inline_size, self_size.context_idx, this);
			fix_line_width(float_right, self_size, fmt_ctx);
			line_inline_end = fmt_ctx->find_min_right(line_block_start, self_size.render_inline_size(), self_size.context_idx);
			ret_inline_size = self_size.render_inline_size() - line_inline_end;
		}
    }
    return ret_inline_size;
}

std::shared_ptr<litehtml::render_item> litehtml::render_item_block::init()
{
    std::shared_ptr<render_item> ret;

    // Initialize indexes for list items
    if(src_el()->css().get_display() == display_list_item && src_el()->css().get_list_style_type() >= list_style_type_armenian)
    {
        if (auto p = src_el()->parent())
        {
            // <ol reversed> counts down (default start = number of items); li@value resets the counter.
            const bool reversed = p->get_attr("reversed") != nullptr;
            const int step = reversed ? -1 : 1;
            int val = 1;
            if(const char* start_attr = p->get_attr("start"))
            {
                val = atoi(start_attr);
            } else if(reversed)
            {
                val = 0;
                for(const auto &child : p->children())
                {
                    if(child->css().get_display() == display_list_item) val++;
                }
            }
            for(const auto &child : p->children())
            {
                if(child->css().get_display() != display_list_item && child != src_el()) continue;
                if(const char* value_attr = child->get_attr("value"))
                {
                    val = atoi(value_attr);
                }
                if (child == src_el())
                {
                    src_el()->set_attr("list_index", std::to_string(val).c_str());
                    if (auto tag = std::dynamic_pointer_cast<html_tag>(src_el()))
                    {
                        tag->measure_list_marker(val);
                    }
                    break;
                }
                val += step;
            }
        }
    }
    // Split inline blocks with box blocks inside
    auto iter = m_children.begin();
    while (iter != m_children.end())
    {
        const auto& el = *iter;
        if(el->src_el()->css().get_display() == display_inline && !el->children().empty())
        {
            auto split_el = el->split_inlines();
            if(std::get<0>(split_el))
            {
                iter = m_children.erase(iter);
                iter = m_children.insert(iter, std::get<2>(split_el));
                iter = m_children.insert(iter, std::get<1>(split_el));
                iter = m_children.insert(iter, std::get<0>(split_el));

                std::get<0>(split_el)->parent(shared_from_this());
                std::get<1>(split_el)->parent(shared_from_this());
                std::get<2>(split_el)->parent(shared_from_this());
                continue;
            }
        }
        ++iter;
    }

    bool has_block_level = false;
	bool has_inlines = false;
    for (const auto& el : m_children)
    {
	const auto el_pos = el->src_el()->css().get_position();
	const bool out_of_flow = src_el()->css().get_writing_mode() == writing_mode_horizontal_tb &&
	                         (el_pos == element_position_absolute || el_pos == element_position_fixed);
	if(!el->src_el()->is_float() && !out_of_flow)
		{
			if (el->src_el()->is_block_box())
			{
				has_block_level = true;
			} else if (el->src_el()->is_inline())
			{
				has_inlines = true;
			}
		}
        if(has_block_level && has_inlines)
            break;
    }
    if(has_block_level)
    {
        ret = std::make_shared<render_item_block_context>(src_el());
        ret->parent(parent());

        auto doc = src_el()->get_document();
        decltype(m_children) new_children;
        decltype(m_children) inlines;
        bool not_ws_added = false;
        for (const auto& el : m_children)
        {
            if(el->src_el()->is_inline())
            {
                inlines.push_back(el);
                if(!el->src_el()->is_white_space())
                    not_ws_added = true;
            } else
            {
                if(not_ws_added)
                {
                    auto anon_el = std::make_shared<html_tag>(src_el());
                    auto anon_ri = std::make_shared<render_item_block>(anon_el);
                    for(const auto& inl : inlines)
                    {
                        anon_ri->add_child(inl);
                    }

                    not_ws_added = false;
                    new_children.push_back(anon_ri);
                    anon_ri->parent(ret);
                }
                new_children.push_back(el);
                el->parent(ret);
                inlines.clear();
            }
        }
        if(!inlines.empty() && not_ws_added)
        {
            auto anon_el = std::make_shared<html_tag>(src_el());
            auto anon_ri = std::make_shared<render_item_block>(anon_el);
            for(const auto& inl : inlines)
            {
                anon_ri->add_child(inl);
            }

            new_children.push_back(anon_ri);
            anon_ri->parent(ret);
        }
        ret->children() = new_children;
    }

    if(!ret)
    {
        ret = std::make_shared<render_item_inline_context>(src_el());
        ret->parent(parent());
        ret->children() = children();
        for (const auto &el: ret->children())
        {
            el->parent(ret);
        }
    }

    ret->src_el()->add_render(ret);

    for(auto& el : ret->children())
    {
        el = el->init();
    }

    return ret;
}



litehtml::pixel_t litehtml::render_item_block::_measure(const containing_block_context &containing_block_size, formatting_context* fmt_ctx)
{
	containing_block_context self_size = m_self_size;
	
	// If the block has max-inline-size, we should restrict the available inline-size for children
	bool capped_by_max_inline = false;
	const pixel_t offered_inline_size = self_size.render_inline_size();
	if (self_size.max_inline_size().type != containing_block_context::cbc_value_type_none)
	{
		if (self_size.render_inline_size() > self_size.max_inline_size())
		{
			capped_by_max_inline = true;
			// Only the value: copying the typed_pixel would turn an auto inline size into an absolute
			// one, and a shrink-to-fit block would then take max-width as its width.
			self_size.render_inline_size() = (pixel_t)self_size.max_inline_size();
			self_size.inline_size() = (pixel_t)self_size.render_inline_size();
		}
	}

	// The min-inline-size clamp has to bound the measure itself: a box whose
	// definite inline size (a percentage, say) resolves below its min-width is
	// laid out at the min-width, and its content -- percentage sizes of the
	// children included -- follows that width. Clamping only m_pos.width after
	// the content render keeps the height the narrower layout produced (a grid
	// row then sizes to the unclamped measure: width:10% + min-width:40px in a
	// 50px track measured 92.1 tall instead of 105).
	if (self_size.min_inline_size().type != containing_block_context::cbc_value_type_none &&
	    self_size.render_inline_size().type != containing_block_context::cbc_value_type_auto &&
	    self_size.render_inline_size().type != containing_block_context::cbc_value_type_none &&
	    self_size.render_inline_size() < self_size.min_inline_size())
	{
		self_size.render_inline_size() = (pixel_t)self_size.min_inline_size();
		self_size.inline_size() = (pixel_t)self_size.render_inline_size();
	}

	// For measurement, we pass size_mode_measure
	containing_block_context measure_size = self_size;
	measure_size.size_mode |= containing_block_context::size_mode_measure;

	pixel_t ret_inline_size = _render_content(0, 0, false, measure_size, fmt_ctx);

	if (src_el()->css().get_display() == display_list_item)
	{
		if(m_pos.height == 0)
		{
			m_pos.height = css().line_height().computed_value;
		}
	}

	// Set block inline-size (and physical width/height)
	if(!(containing_block_size.size_mode & containing_block_context::size_mode_content))
	{
		if(self_size.render_inline_size().type == containing_block_context::cbc_value_type_absolute)
		{
			ret_inline_size = self_size.render_inline_size();
		}
        if (self_size.mode == writing_mode_horizontal_tb)
        {
            m_pos.width = self_size.render_width;
        }
        else
        {
            m_pos.height = self_size.render_height;
        }
	} else
	{
		pixel_t calculated_size = (self_size.inline_size().type != containing_block_context::cbc_value_type_auto) ? (pixel_t)self_size.render_inline_size() : ret_inline_size;

		bool is_exact_inline = (self_size.mode == writing_mode_horizontal_tb) ?
		    (containing_block_size.size_mode & containing_block_context::size_mode_exact_width) :
		(containing_block_size.size_mode & containing_block_context::size_mode_exact_height);
		if (is_exact_inline)
               {
                       calculated_size = self_size.render_inline_size();
               }
		if(self_size.render_inline_size().type == containing_block_context::cbc_value_type_absolute)
		{
			ret_inline_size = self_size.render_inline_size();
		}
        
        if (self_size.mode == writing_mode_horizontal_tb)
            m_pos.width = calculated_size;
        else
            m_pos.height = calculated_size;
	}

	// Fix inline-size with max-inline-size attribute
	if(self_size.max_inline_size().type != containing_block_context::cbc_value_type_none)
	{
        pixel_t& physical_inline_size = (self_size.mode == writing_mode_horizontal_tb) ? m_pos.width : m_pos.height;
		if(physical_inline_size > self_size.max_inline_size())
		{
			physical_inline_size = self_size.max_inline_size();
		}
	}
	
	// The returned inline-size should not exceed the element's actual inline-size
    pixel_t current_inline_size = (self_size.mode == writing_mode_horizontal_tb) ? m_pos.width : m_pos.height;
    


	ret_inline_size = std::min(ret_inline_size, current_inline_size);

	// When the max-inline-size cap bounded this measure, the content was laid out
	// at the cap and wrapped there, so the size it reports is the widest wrapped
	// line -- below the cap, and far below the content's true max-content, which
	// by construction exceeds the cap (that is why it wrapped). For a max-content
	// query the box's intrinsic size is the cap itself: a shrink-to-fit box with
	// max-width (flex item, float, inline-block) takes its max-width, not the
	// widest line that happened to fit under it.
	if (capped_by_max_inline &&
	    (containing_block_size.size_mode & containing_block_context::size_mode_content) &&
	    !(containing_block_size.size_mode & containing_block_context::size_mode_min_content))
	{
		const pixel_t cap_value = (pixel_t)self_size.max_inline_size();
		if (ret_inline_size < cap_value)
		{
			// The offer exceeded the cap, but the content may not: short content
			// laid out at the cap already reports its true max-content. Only when
			// the content also exceeds the cap at the offered size was the capped
			// result a wrapped line, and only then is the intrinsic size the cap.
			containing_block_context uncapped_size = measure_size;
			uncapped_size.render_inline_size() = offered_inline_size;
			uncapped_size.inline_size() = offered_inline_size;
			const position keep_pos = m_pos;
			const pixel_t uncapped_ret = _render_content(0, 0, false, uncapped_size, fmt_ctx);
			m_pos = keep_pos;
			if (uncapped_ret > cap_value)
			{
				ret_inline_size = cap_value;
				// Consumers read the measured size off the box itself (flex_base_size
				// comes from get_el_main_size(), the cross size of a column flex item
				// from its measured width), so the box must carry the same size.
				pixel_t& measured_inline_size = (self_size.mode == writing_mode_horizontal_tb) ? m_pos.width : m_pos.height;
				measured_inline_size = std::max(measured_inline_size, cap_value);
			}
		}
	}

	// Fix inline-size with min-inline-size attribute
	if(self_size.min_inline_size().type != containing_block_context::cbc_value_type_none)
	{
        pixel_t& physical_inline_size = (self_size.mode == writing_mode_horizontal_tb) ? m_pos.width : m_pos.height;
		if(physical_inline_size < self_size.min_inline_size())
		{
			physical_inline_size = self_size.min_inline_size();
		}
		// min-width is part of the content size this box reports to a shrink-to-fit parent
		if(containing_block_size.size_mode & containing_block_context::size_mode_content)
		{
			ret_inline_size = std::max(ret_inline_size, physical_inline_size);
		}
	} else {
        pixel_t& physical_inline_size = (self_size.mode == writing_mode_horizontal_tb) ? m_pos.width : m_pos.height;
        if(physical_inline_size < 0) physical_inline_size = 0;
    }

	// Set block-size
	bool is_flex_item = parent() && (parent()->css().get_display() == display_flex || parent()->css().get_display() == display_inline_flex);
	if (self_size.block_size().type != containing_block_context::cbc_value_type_auto &&
	    (!(containing_block_size.size_mode & containing_block_context::size_mode_content) ||
      src_el()->css().get_display() == display_table_cell ||
      src_el()->css().get_position() == element_position_absolute ||
      src_el()->css().get_position() == element_position_fixed ||
      self_size.block_size().type == containing_block_context::cbc_value_type_absolute ||
      (!is_flex_item && self_size.block_size().type == containing_block_context::cbc_value_type_percentage && self_size.block_size().value > 0)))
	{
        pixel_t& physical_block_size = (self_size.mode == writing_mode_horizontal_tb) ? m_pos.height : m_pos.width;
		if (src_el()->css().get_display() == display_table_cell)
		{
			physical_block_size = std::max(physical_block_size, (pixel_t)self_size.render_block_size());
		}
		else
		{
			physical_block_size = self_size.render_block_size();
		}
	} else if(!css().get_aspect_ratio().is_auto())
	{
		aspect_ratio ar = css().get_aspect_ratio();
        if (self_size.mode == writing_mode_horizontal_tb)
    		m_pos.height = m_pos.width * ar.height / ar.width;
        else
            m_pos.width = m_pos.height * ar.width / ar.height;
	} else if (src_el()->is_block_formatting_context())
	{
		// add the floats' height to the block height
		pixel_t floats_block_size = std::max(fmt_ctx->get_floats_height(), fmt_ctx->measured_float_bottom());
        pixel_t& physical_block_size = (self_size.mode == writing_mode_horizontal_tb) ? m_pos.height : m_pos.width;
		if (floats_block_size > physical_block_size)
		{
			physical_block_size = floats_block_size;
		}
	}
	if(containing_block_size.size_mode & containing_block_context::size_mode_content)
	{
		if(self_size.block_size().type == containing_block_context::cbc_value_type_absolute)  
		{
            pixel_t& physical_block_size = (self_size.mode == writing_mode_horizontal_tb) ? m_pos.height : m_pos.width;
			if(physical_block_size > self_size.block_size())
			{
				physical_block_size = self_size.block_size();
			}
		}
	}
	// Fix block-size with min-block-size attribute
	if(self_size.min_block_size().type != containing_block_context::cbc_value_type_none)
	{
        pixel_t& physical_block_size = (self_size.mode == writing_mode_horizontal_tb) ? m_pos.height : m_pos.width;
		if(physical_block_size < self_size.min_block_size())
		{
			physical_block_size = self_size.min_block_size();
		}
	} else {
        pixel_t& physical_block_size = (self_size.mode == writing_mode_horizontal_tb) ? m_pos.height : m_pos.width;
        if(physical_block_size < 0) physical_block_size = 0;
    }

	// Fix block-size with max-block-size attribute
	if(self_size.max_block_size().type != containing_block_context::cbc_value_type_none)
	{
        pixel_t& physical_block_size = (self_size.mode == writing_mode_horizontal_tb) ? m_pos.height : m_pos.width;
		if(physical_block_size > self_size.max_block_size())
		{
			physical_block_size = self_size.max_block_size();
		}
	}

    if (src_el()->css().get_display() == display_list_item)
    {
        string list_image = src_el()->css().get_list_style_image();
        if (!list_image.empty())
        {
            size sz;
            string list_image_baseurl = src_el()->css().get_list_style_image_baseurl();
            src_el()->get_document()->container()->get_image_size(list_image.c_str(), list_image_baseurl.c_str(), sz);
            pixel_t& physical_block_size = (self_size.mode == writing_mode_horizontal_tb) ? m_pos.height : m_pos.width;
            if (physical_block_size < sz.height)
            {
			    physical_block_size = sz.height;
            }
        }
    }

	return ret_inline_size + content_offset_inline(get_wm_context());
}

void litehtml::render_item_block::_place(pixel_t inline_pos, pixel_t block_pos, const containing_block_context &containing_block_size, formatting_context* fmt_ctx)
{
	containing_block_context self_size = m_self_size;
	
	// If the block has max-inline-size, we should restrict the available inline-size for children
	if (self_size.max_inline_size().type != containing_block_context::cbc_value_type_none)
	{
		if (self_size.render_inline_size() > self_size.max_inline_size())
		{
			// Only the value: copying the typed_pixel would turn an auto inline size into an absolute
			// one, and a shrink-to-fit block would then take max-width as its width.
			self_size.render_inline_size() = (pixel_t)self_size.max_inline_size();
			self_size.inline_size() = (pixel_t)self_size.render_inline_size();
		}
	}

	_render_content(inline_pos, block_pos, false, self_size, fmt_ctx);

	// Re-apply constraints after _render_content because it might have overwritten m_pos.width/height
	
	// Set block inline-size
	if(!(containing_block_size.size_mode & containing_block_context::size_mode_content))
	{
        if (self_size.mode == writing_mode_horizontal_tb)
            m_pos.width = self_size.render_width;
        else
            m_pos.height = self_size.render_height;
	} else
	{
        pixel_t& physical_inline_size = (self_size.mode == writing_mode_horizontal_tb) ? m_pos.width : m_pos.height;
		if (self_size.inline_size().type != containing_block_context::cbc_value_type_auto)
		{
			physical_inline_size = self_size.render_inline_size();
		}
		bool is_exact_inline = (self_size.mode == writing_mode_horizontal_tb) ?
		(containing_block_size.size_mode & containing_block_context::size_mode_exact_width) :
		(containing_block_size.size_mode & containing_block_context::size_mode_exact_height);
		if (is_exact_inline)
        {
            physical_inline_size = self_size.render_inline_size();
        }
	}

	// Fix inline-size with max-inline-size attribute
	if(self_size.max_inline_size().type != containing_block_context::cbc_value_type_none)
	{
        pixel_t& physical_inline_size = (self_size.mode == writing_mode_horizontal_tb) ? m_pos.width : m_pos.height;
		if(physical_inline_size > self_size.max_inline_size())
		{
			physical_inline_size = self_size.max_inline_size();
		}
	}
	
	// Fix inline-size with min-inline-size attribute
	if(self_size.min_inline_size().type != containing_block_context::cbc_value_type_none)
	{
        pixel_t& physical_inline_size = (self_size.mode == writing_mode_horizontal_tb) ? m_pos.width : m_pos.height;
		if(physical_inline_size < self_size.min_inline_size())
		{
			physical_inline_size = self_size.min_inline_size();
		}
	}

	// Set block-size
	bool is_flex_item = parent() && (parent()->css().get_display() == display_flex || parent()->css().get_display() == display_inline_flex);
	if (self_size.block_size().type != containing_block_context::cbc_value_type_auto &&
	    (!(containing_block_size.size_mode & containing_block_context::size_mode_content) ||
      src_el()->css().get_display() == display_table_cell ||
      src_el()->css().get_position() == element_position_absolute ||
      src_el()->css().get_position() == element_position_fixed ||
      self_size.block_size().type == containing_block_context::cbc_value_type_absolute ||
      (!is_flex_item && self_size.block_size().type == containing_block_context::cbc_value_type_percentage && self_size.block_size().value > 0)))
	{
        pixel_t& physical_block_size = (self_size.mode == writing_mode_horizontal_tb) ? m_pos.height : m_pos.width;
		if (src_el()->css().get_display() == display_table_cell)
		{
			physical_block_size = std::max(physical_block_size, (pixel_t)self_size.render_block_size());
		}
		else
		{
			physical_block_size = self_size.render_block_size();
		}
	} else if(!css().get_aspect_ratio().is_auto())
	{
		aspect_ratio ar = css().get_aspect_ratio();
        if (self_size.mode == writing_mode_horizontal_tb)
    		m_pos.height = m_pos.width * ar.height / ar.width;
        else
            m_pos.width = m_pos.height * ar.width / ar.height;
	} else if (src_el()->is_block_formatting_context())
	{
		// In the place pass every float of this formatting context has been registered here, so the
		// measure pass's estimate (which only saw the trial widths) must not win over them: a float
		// that shrank after that estimate (a percentage-width svg) left it too tall.
		pixel_t floats_block_size = fmt_ctx->get_floats_height();
		if (self_size.size_mode & containing_block_context::size_mode_measure)
		{
			floats_block_size = std::max(floats_block_size, fmt_ctx->measured_float_bottom());
		}
        pixel_t& physical_block_size = (self_size.mode == writing_mode_horizontal_tb) ? m_pos.height : m_pos.width;
		if (floats_block_size > physical_block_size)
		{
			physical_block_size = floats_block_size;
		}
	}

	// Fix block-size with min-block-size attribute
	if(self_size.min_block_size().type != containing_block_context::cbc_value_type_none)
	{
        pixel_t& physical_block_size = (self_size.mode == writing_mode_horizontal_tb) ? m_pos.height : m_pos.width;
		if(physical_block_size < self_size.min_block_size())
		{
			physical_block_size = self_size.min_block_size();
		}
	}

	// Fix block-size with max-block-size attribute
	if(self_size.max_block_size().type != containing_block_context::cbc_value_type_none)
	{
        pixel_t& physical_block_size = (self_size.mode == writing_mode_horizontal_tb) ? m_pos.height : m_pos.width;
		if(physical_block_size > self_size.max_block_size())
		{
			physical_block_size = self_size.max_block_size();
		}
	}
}

void litehtml::render_item_block::apply_vertical_align()
{
    pixel_t content_block_end = 0;
    satoru::WritingModeContext wm = get_wm_context();
    for (const auto& el : m_children)
    {
        if (el->src_el()->css().get_display() != display_none)
        {
            content_block_end = std::max(content_block_end, el->block_end_pos(wm));
        }
    }

    pixel_t shift = 0;
    vertical_align va = src_el()->css().get_vertical_align();
    // the block axis is horizontal in vertical writing
    const pixel_t block_extent = wm.is_vertical() ? m_pos.width : m_pos.height;
    if (va == va_middle)
    {
        shift = (block_extent - content_block_end) / 2;
    }
    else if (va == va_bottom)
    {
        shift = block_extent - content_block_end;
    }

    if (shift > 0)
    {
        for (auto& el : m_children)
        {
            el->block_shift(shift);
        }
    }
}

bool litehtml::render_item_block::has_baseline()
{
    for (const auto& el : m_children)
    {
        if (el->src_el()->css().get_display() != display_none &&
            el->src_el()->css().get_float() == float_none &&
            !el->src_el()->is_positioned() && el->has_baseline())
        {
            return true;
        }
    }
    return false;
}

litehtml::pixel_t litehtml::render_item_block::get_first_baseline()
{
    for (const auto& el : m_children)
    {
        if (el->src_el()->css().get_display() != display_none &&
            el->src_el()->css().get_float() == float_none &&
            !el->src_el()->is_positioned() && el->has_baseline())
        {
            return el->pos().y - el->content_offset_top() + el->get_first_baseline();
        }
    }
    return render_item::get_first_baseline();
}

litehtml::pixel_t litehtml::render_item_block::get_last_baseline()
{
    for (auto it = m_children.rbegin(); it != m_children.rend(); ++it)
    {
        const auto& el = *it;
        if (el->src_el()->css().get_display() != display_none &&
            el->src_el()->css().get_float() == float_none &&
            !el->src_el()->is_positioned())
        {
            return el->pos().y - el->content_offset_top() + el->get_last_baseline();
        }
    }
    return render_item::get_last_baseline();
}
