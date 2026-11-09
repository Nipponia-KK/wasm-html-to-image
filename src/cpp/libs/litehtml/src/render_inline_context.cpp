#include "render_inline_context.h"
#include "document.h"
#include "document_container.h"
#include "el_text.h"
#include "iterators.h"
#include "types.h"

#include <algorithm>
#include <cstring>

namespace
{
// Ruby annotations (<ruby> with its <rt>) are laid out as an atomic inline-flex box.
// In vertical writing the engine otherwise re-measures such an inline-level flex box at
// its min-content inline size, which lets the annotation wrap its base text into a second
// column. Detecting the box here lets place_inline keep it atomic (content-sized inline
// extent, no fragmenting re-measure) and send it whole to the next column when it does not
// fit, matching Chromium. Only affects <ruby>-like inline-flex boxes in vertical writing.
bool satoru_is_ruby_box(const std::shared_ptr<litehtml::render_item>& el)
{
	if(!el || !el->src_el()) return false;
	const char* tag = el->src_el()->get_tagName();
	if(tag && strcmp(tag, "ruby") == 0) return true;
	if(el->src_el()->css().get_display() == litehtml::display_inline_flex)
	{
		for(const auto& child : el->src_el()->children())
		{
			const char* ct = child ? child->get_tagName() : nullptr;
			if(ct && strcmp(ct, "rt") == 0) return true;
		}
	}
	return false;
}

// text-wrap-style: balance/pretty stop after a handful of lines so a long
// paragraph never pays for the extra layout passes (Chromium does the same).
constexpr size_t k_max_balance_lines = 6;
constexpr size_t k_max_pretty_lines  = 10;
// Bisection steps for the narrowest wrap width that keeps the line count. The
// search range is at most a block width, so 8 steps settle well below 2px.
constexpr int k_wrap_width_search_steps = 8;

// Words (non-space, non-break text runs) placed on a line.
size_t count_line_words(const litehtml::line_box& box)
{
	size_t words = 0;
	for(const auto& item : box.items())
	{
		if(item->get_type() != litehtml::line_box_item::type_text_part) continue;
		const auto& src = item->get_el()->src_el();
		if(src->is_white_space() || src->is_break()) continue;
		++words;
	}
	return words;
}

// Inline size of the widest unbreakable text run in the block. A wrap width at
// or below it cannot break any line further, so it is the lower bound of the
// text-wrap-style search.
litehtml::pixel_t widest_word(const std::vector<std::unique_ptr<litehtml::line_box>>& boxes)
{
	litehtml::pixel_t widest = 0;
	for(const auto& box : boxes)
	{
		for(const auto& item : box->items())
		{
			if(item->get_type() != litehtml::line_box_item::type_text_part) continue;
			if(item->get_el()->src_el()->is_white_space()) continue;
			// A break-word word is one item per character; the lower bound is the
			// width of the whole word, not of a single glyph.
			if(auto txt = std::dynamic_pointer_cast<litehtml::el_text>(item->get_el()->src_el()))
			{
				if(txt->is_word_break_part())
				{
					if(txt->is_word_break_start()) widest = std::max(widest, txt->word_break_width());
					continue;
				}
			}
			widest = std::max(widest, item->width());
		}
	}
	return widest;
}
}

void litehtml::render_item_inline_context::layout_lines(const containing_block_context &self_size, formatting_context* fmt_ctx, pixel_t wrap_end)
{
    m_line_boxes.clear();
	m_max_line_width = 0;
	m_measure_float_width = 0;
	m_is_clamped = false;
	m_wrap_eligible = true;
	m_wrap_end = (wrap_end < 0) ? (pixel_t)self_size.render_inline_size() : wrap_end;

    white_space ws = src_el()->css().get_white_space();
    bool skip_spaces = false;
    if (ws == white_space_normal ||
        ws == white_space_nowrap ||
        ws == white_space_pre_line)
    {
        skip_spaces = true;
    }

    bool was_space = false;

    go_inside_inline go_inside_inlines_selector;
    inline_selector select_inlines;
    elements_iterator inlines_iter(true, &go_inside_inlines_selector, &select_inlines);

    inlines_iter.process(shared_from_this(), [&](const std::shared_ptr<render_item>& el, iterator_item_type item_type)
        {
			switch (item_type)
			{
				case iterator_item_type_child:
					{
						// skip spaces to make rendering a bit faster
						if (skip_spaces)
						{
							if (el->src_el()->is_white_space())
							{
								if (was_space)
								{
									el->skip(true);
									return;
								} else
								{
									was_space = true;
								}
							} else
							{
								// skip all spaces after line break
								was_space = el->src_el()->is_break();
							}
						}
						// place element into rendering flow
						place_inline(std::make_unique<line_box_item>(el), self_size, fmt_ctx);
					}
					break;

				case iterator_item_type_start_parent:
					{
						el->clear_inline_boxes();
						place_inline(std::make_unique<lbi_start>(el), self_size, fmt_ctx);
					}
					break;

				case iterator_item_type_end_parent:
				{
					place_inline(std::make_unique<lbi_end>(el), self_size, fmt_ctx);
				}
					break;
			}
        });

    finish_last_box(true, self_size);
}

void litehtml::render_item_inline_context::apply_text_wrap_style(const containing_block_context &self_size, formatting_context* fmt_ctx)
{
    const text_wrap_style wrap_style = src_el()->css().get_text_wrap_style();
    if(wrap_style != text_wrap_style_balance && wrap_style != text_wrap_style_pretty) return;
    if(!text_wrap_eligible(self_size)) return;

    const size_t line_count = m_line_boxes.size();
    if(line_count < 2) return;
    if(wrap_style == text_wrap_style_balance)
    {
        if(line_count > k_max_balance_lines) return;
    } else
    {
        if(line_count > k_max_pretty_lines) return;
        // pretty only repairs an orphan: a last line carrying a single word.
        if(count_line_words(*m_line_boxes.back()) != 1) return;
    }

    const pixel_t full_width = self_size.render_inline_size();
    const pixel_t min_word = widest_word(m_line_boxes);
    if(min_word <= 0 || min_word + 2 >= full_width) return;

    // The narrowest wrap width that still produces the same number of lines packs
    // every line as full as the break opportunities allow, which is the layout
    // Chromium produces for text-wrap-style: balance. Line boxes keep the full
    // content width, so alignment is unaffected by the narrowing.
    pixel_t low  = min_word;    // more lines than the original layout
    pixel_t high = full_width;  // the original layout (line_count lines)
    for(int step = 0; step < k_wrap_width_search_steps && high - low > 1; ++step)
    {
        const pixel_t probe = low + (high - low) / 2;
        layout_lines(self_size, fmt_ctx, probe);
        if(m_line_boxes.size() == line_count)
            high = probe;
        else
            low = probe;
    }

    layout_lines(self_size, fmt_ctx, high);
    bool applied = m_line_boxes.size() == line_count;
    if(applied && wrap_style == text_wrap_style_pretty)
    {
        applied = count_line_words(*m_line_boxes.back()) != 1;
    }
    if(!applied)
    {
        // The narrowed pass changed the line count (it would move the following
        // content) or kept the orphan: keep the normal wrapping.
        layout_lines(self_size, fmt_ctx);
    }
}

bool litehtml::render_item_inline_context::text_wrap_eligible(const containing_block_context &self_size) const
{
    // Only the plain horizontal case is handled: floats, atomic inlines and
    // forced breaks clear m_wrap_eligible while the lines are collected. Vertical
    // writing, shrink-to-fit widths (their intrinsic size comes from the
    // unbalanced pass), line clamping and ellipsis are left on the normal path.
    if(self_size.mode != writing_mode_horizontal_tb) return false;
    if(self_size.size_mode & (containing_block_context::size_mode_measure |
                              containing_block_context::size_mode_content |
                              containing_block_context::size_mode_min_content)) return false;
    if(!m_wrap_eligible || m_is_clamped) return false;
    if(src_el()->css().get_text_wrap_mode() != text_wrap_mode_wrap) return false;
    if(src_el()->css().get_white_space() != white_space_normal) return false;
    if(src_el()->css().get_text_indent().val() != 0) return false;
    if(src_el()->css().get_text_overflow() == text_overflow_ellipsis) return false;
    if(src_el()->css().get_line_clamp() > 0 || src_el()->css().get_max_lines() > 0) return false;
    if(self_size.render_inline_size() <= 0) return false;
    return true;
}

litehtml::pixel_t litehtml::render_item_inline_context::_render_content(pixel_t /*x*/, pixel_t /*y*/, bool /*second_pass*/, const containing_block_context &self_size, formatting_context* fmt_ctx)
{
    // min-content size of inline content: every break opportunity breaks, so the widest line is the
    // widest unbreakable run (a word, an atomic inline). Lay it out in a 1px line to get that.
    // Max-content is the mirror image: lay it out unbounded, so nothing wraps and the widest
    // line is the content's true max-content size. (At the width on offer the widest WRAPPED
    // line came back instead -- a few px short of the space, and far short of the max-content --
    // so every shrink-to-fit consumer under-sized.) Only the inline layout is unbounded: the
    // context the caller gave still governs block-level children, whose formatting contexts
    // resolve against the space on offer on purpose (an fr track's share, a flex line's break).
    if ((self_size.size_mode & containing_block_context::size_mode_min_content) &&
        (self_size.size_mode & containing_block_context::size_mode_content) &&
        self_size.mode == writing_mode_horizontal_tb)
    {
        layout_lines(self_size.new_inline_size(1, self_size.size_mode), fmt_ctx);
    } else if ((self_size.size_mode & containing_block_context::size_mode_content) &&
               self_size.mode == writing_mode_horizontal_tb &&
               self_size.render_inline_size().type == containing_block_context::cbc_value_type_auto)
    {
        // (only when the inline size on offer is indefinite: a query that already carries a
        // definite inline size -- a column flex item's height measured at its capped width,
        // react.dev's max-w-3xl paragraphs -- wants the wrapped height at that size, and
        // laying the lines out unbounded would report a single-line height for it)
        layout_lines(self_size.new_inline_size(100000, self_size.size_mode), fmt_ctx);
    } else
    {
        layout_lines(self_size, fmt_ctx);
    }
    apply_text_wrap_style(self_size, fmt_ctx);

    if (!m_line_boxes.empty())
    {
        if (collapse_top_margin())
        {
            pixel_t old_block_start = margin_block_start();
            margin_block_start(std::max(m_line_boxes.front()->top_margin(), old_block_start));
            if (margin_block_start() != old_block_start)
            {
                fmt_ctx->update_floats(margin_block_start() - old_block_start, shared_from_this());
            }
        }
        if (collapse_bottom_margin())
        {
            margin_block_end(std::max(m_line_boxes.back()->bottom_margin(), margin_block_end()));
			if (self_size.mode == writing_mode_horizontal_tb)
				m_pos.height = m_line_boxes.back()->bottom() - m_line_boxes.back()->bottom_margin();
			else
            {
                if (self_size.mode == writing_mode_vertical_rl || self_size.mode == writing_mode_sideways_rl)
				    m_pos.width = m_line_boxes.front()->right() - m_line_boxes.back()->left();
                else
                    m_pos.width = m_line_boxes.back()->right() - m_line_boxes.front()->left();
                m_pos.height = m_max_line_width;
            }
        }
        else
        {
			if (self_size.mode == writing_mode_horizontal_tb)
				m_pos.height = m_line_boxes.back()->bottom();
			else
            {
                if (self_size.mode == writing_mode_vertical_rl || self_size.mode == writing_mode_sideways_rl)
				    m_pos.width = m_line_boxes.front()->right() - m_line_boxes.back()->left();
                else
                    m_pos.width = m_line_boxes.back()->right() - m_line_boxes.front()->left();
                m_pos.height = m_max_line_width;
                
                // Shift lines for vertical-rl to fit into the new width
                // ONLY when in shrink-to-fit mode (size_mode_content)
                if ((self_size.mode == writing_mode_vertical_rl || self_size.mode == writing_mode_sideways_rl) && 
                    (self_size.size_mode & (containing_block_context::size_mode_content | containing_block_context::size_mode_min_content)))
                {
                    pixel_t shift = self_size.render_block_size() - m_pos.width;
                    if (shift != 0)
                    {
                        for (auto& box : m_line_boxes)
                        {
                            box->x_shift(-shift);
                        }
                    }
                }
            }
        }
    } else if (self_size.mode == writing_mode_horizontal_tb)
    {
        // no line to take a height from: an earlier layout at another size must not leave its height behind
        m_pos.height = 0;
    }

	if (!(self_size.size_mode & containing_block_context::size_mode_measure) && src_el()->css().get_text_fit() != 0)
	{
		src_el()->set_text_fit_measure(m_max_line_width, self_size.render_inline_size());
	}

	if (self_size.size_mode & containing_block_context::size_mode_measure)
	{
		pixel_t ret = m_max_line_width;
		m_has_measured_baseline = !m_line_boxes.empty() && !get_wm_context().is_vertical();
		if (m_has_measured_baseline)
		{
			m_measured_first_baseline = get_first_baseline();
			m_measured_last_baseline = get_last_baseline();
		}
		m_line_boxes.clear();
		return ret;
	}

    return m_max_line_width;
}

void litehtml::render_item_inline_context::fix_line_width(element_float flt,
														  const containing_block_context &self_size,
														  formatting_context* fmt_ctx)
{
    if(!m_line_boxes.empty())
    {
		auto el_front = m_line_boxes.back()->get_first_text_part();

        std::vector<std::shared_ptr<render_item>> els;
        bool was_cleared = false;
        if(el_front && el_front->src_el()->css().get_clear() != clear_none)
        {
            if(el_front->src_el()->css().get_clear() == clear_both)
            {
                was_cleared = true;
            } else
            {
                if(	(flt == float_left	&& el_front->src_el()->css().get_clear() == clear_left) ||
                       (flt == float_right	&& el_front->src_el()->css().get_clear() == clear_right) )
                {
                    was_cleared = true;
                }
            }
        }

        if(!was_cleared)
        {
			std::list<std::unique_ptr<line_box_item> > items = std::move(m_line_boxes.back()->items());
            m_line_boxes.pop_back();

            for(auto& item : items)
            {
                place_inline(std::move(item), self_size, fmt_ctx);
            }
        } else
        {
            pixel_t line_top = 0;
            line_top = m_line_boxes.back()->top();

            pixel_t line_left	= 0;
            pixel_t line_right	= self_size.render_width;
            fmt_ctx->get_line_left_right(line_top, self_size.render_width, line_left, line_right);

            if(m_line_boxes.size() == 1)
            {
                if (src_el()->css().get_list_style_type() != list_style_type_none && src_el()->css().get_list_style_position() == list_style_position_inside)
                {
                    pixel_t sz_font = src_el()->css().get_font_size();
                    line_left += sz_font;
                }

                if (src_el()->css().get_text_indent().val() != 0)
                {
                    line_left += src_el()->css().get_text_indent().calc_percent(self_size.width);
                }

            }

            auto items = m_line_boxes.back()->new_inline_size(line_left, line_right);
            for(auto& item : items)
            {
                place_inline(std::move(item), self_size, fmt_ctx);
            }
        }
    }
}

std::list<std::unique_ptr<litehtml::line_box_item> > litehtml::render_item_inline_context::finish_last_box(bool end_of_render, const containing_block_context &self_size)
{
	std::list<std::unique_ptr<line_box_item> > ret;

    if(!m_line_boxes.empty())
    {
		ret = m_line_boxes.back()->finish(end_of_render, self_size);

        if(m_line_boxes.back()->is_empty() && end_of_render)
        {
			// remove the last empty line
            m_line_boxes.pop_back();
        } else
		{
			m_max_line_width = std::max(m_max_line_width, m_line_boxes.back()->inline_size() + m_measure_float_width);
			m_measure_float_width = 0;
		}
    }
    return ret;
}

litehtml::pixel_t litehtml::render_item_inline_context::new_box(const std::unique_ptr<line_box_item>& el, line_context& line_ctx, const containing_block_context &self_size, formatting_context* fmt_ctx)
{
	auto items = finish_last_box(false, self_size);
	pixel_t line_block_pos = 0;
	if (self_size.mode == writing_mode_horizontal_tb)
	{
		if(!m_line_boxes.empty())
		{
			line_block_pos = m_line_boxes.back()->bottom();
		}
		line_ctx.top = fmt_ctx->get_cleared_top(el->get_el(), line_block_pos);
		line_ctx.left = 0;
		line_ctx.right = self_size.render_inline_size();
	}
	else
	{
		line_ctx.top = 0;
		if (m_line_boxes.empty())
		{
			if (self_size.mode == writing_mode_vertical_rl || self_size.mode == writing_mode_sideways_rl)
				line_ctx.left = self_size.render_block_size();
			else
				line_ctx.left = 0;
		}
		else
		{
			if (self_size.mode == writing_mode_vertical_rl || self_size.mode == writing_mode_sideways_rl)
				line_ctx.left = m_line_boxes.back()->left();
			else
				line_ctx.left = m_line_boxes.back()->right();
		}
		line_ctx.right = self_size.render_inline_size();
	}

    line_ctx.fix_top();
	if (self_size.mode == writing_mode_horizontal_tb)
	{
		fmt_ctx->get_line_left_right(line_ctx.top, self_size.render_inline_size(), line_ctx.left, line_ctx.right);
	}
	else
	{
		// Floats live in logical coordinates. In vertical writing line_ctx.top is the inline
		// start of the new column and line_ctx.left its physical block edge, so convert the
		// edge to a logical block offset to find the inline range left free by floats.
		bool block_from_right = self_size.mode == writing_mode_vertical_rl || self_size.mode == writing_mode_sideways_rl;
		pixel_t block_offset = block_from_right ? self_size.render_block_size() - line_ctx.left : line_ctx.left;
		pixel_t inline_start = 0;
		pixel_t inline_end = self_size.render_inline_size();
		fmt_ctx->get_line_left_right(block_offset, self_size.render_inline_size(), inline_start, inline_end);
		// The column occupies a block range, so a float touching its far side narrows it too.
		pixel_t column_thickness = css().line_height().computed_value;
		if (column_thickness > 1)
		{
			pixel_t far_start = 0;
			pixel_t far_end = self_size.render_inline_size();
			fmt_ctx->get_line_left_right(block_offset + column_thickness - 1, self_size.render_inline_size(), far_start, far_end);
			inline_start = std::max(inline_start, far_start);
			inline_end = std::min(inline_end, far_end);
		}
		line_ctx.top = inline_start;
		line_ctx.right = inline_end;
	}

    if(el->get_el()->src_el()->is_inline() || el->get_el()->src_el()->is_block_formatting_context())
    {
		pixel_t inline_size = (self_size.mode == writing_mode_horizontal_tb) ? el->get_el()->width() : el->get_el()->height();
        if (inline_size > line_ctx.right - line_ctx.left)
        {            
			if (self_size.mode == writing_mode_horizontal_tb)
			{
				line_ctx.top = fmt_ctx->find_next_line_top(line_ctx.top, inline_size, self_size.render_width);
				line_ctx.left = 0;
				line_ctx.right = self_size.render_width;
				line_ctx.fix_top();
				fmt_ctx->get_line_left_right(line_ctx.top, self_size.render_width, line_ctx.left, line_ctx.right);
			}
        }
    }

    pixel_t first_line_margin = 0;
    pixel_t text_indent = 0;
    if(m_line_boxes.empty())
    {
        if(src_el()->css().get_list_style_type() != list_style_type_none && src_el()->css().get_list_style_position() == list_style_position_inside)
        {            pixel_t sz_font = src_el()->css().get_font_size();
            first_line_margin = sz_font;
        }
        if(src_el()->css().get_text_indent().val() != 0)
        {
            text_indent = src_el()->css().get_text_indent().calc_percent(self_size.width);
        }
    }

    // list marker and text-indent push the first line along the inline axis
    pixel_t box_top = line_ctx.top;
    pixel_t box_left = line_ctx.left;
    if (self_size.mode == writing_mode_horizontal_tb)
        box_left += first_line_margin + text_indent;
    else
        box_top += first_line_margin + text_indent;

    m_line_boxes.emplace_back(std::make_unique<line_box>(
			box_top,
			box_left, line_ctx.right,
			css().line_height(),
			css().get_font_metrics(),
			css().get_text_align(),
			css().get_direction(),
			css().get_text_overflow(),
			css().get_overflow(), css().get_writing_mode(), css().get_inline_reverse()));

	m_line_boxes.back()->set_justify_options(css().get_text_align_last(), css().get_text_justify());

	// Break text at the narrowed inline end while the line box keeps the full
	// content width (text-wrap-style). Floats can narrow a line further.
	m_line_boxes.back()->set_wrap_end(std::min(m_wrap_end, (pixel_t)line_ctx.right));
	if (self_size.size_mode & containing_block_context::size_mode_min_content)
	{
		m_line_boxes.back()->set_min_content_measure(true);
	}

	// Add items returned by finish_last_box function into the new line
	for(auto& it : items)
	{
		m_line_boxes.back()->add_item(std::move(it));
	}

    return line_ctx.top;
}

void litehtml::render_item_inline_context::place_inline(std::unique_ptr<line_box_item> item, const containing_block_context &self_size, formatting_context* fmt_ctx)
{
    if(m_is_clamped || item->get_el()->src_el()->css().get_display() == display_none)
	{
		item->get_el()->skip(true);
		return;
	}

	// Floats, atomic inlines (images, inline-blocks, ruby), block-level items and
	// forced breaks are outside the text-wrap-style balancing passes.
	if(item->get_el()->src_el()->is_float() ||
	   item->get_el()->src_el()->is_break() ||
	   !item->get_el()->src_el()->is_inline() ||
	   item->get_el()->src_el()->is_inline_box())
	{
		m_wrap_eligible = false;
	}

    if(item->get_el()->src_el()->is_float())
    {
        pixel_t line_top = 0;
        if(!m_line_boxes.empty())
        {
            line_top = m_line_boxes.back()->top();
            // A float that occurs mid-line joins the line only if it fits
            // beside the content already on it; otherwise it drops below the
            // line and the content stays put (Blink). place_float alone would
            // put it at the line's edge and fix_line_width would then push the
            // line's content into the remaining segment, however narrow.
            if (!(self_size.size_mode & containing_block_context::size_mode_measure) &&
                self_size.mode == writing_mode_horizontal_tb)
            {
                const pixel_t avail = self_size.render_inline_size();
                // The line's content width, not counting trailing collapsible
                // whitespace: a space before the float hangs at the line end
                // and does not keep the float from joining the line.
                pixel_t used_w = 0;
                {
                    const auto& items = m_line_boxes.back()->items();
                    for (auto it = items.rbegin(); it != items.rend(); ++it)
                    {
                        if (used_w == 0 &&
                            ((*it)->get_el()->src_el()->is_white_space() || (*it)->get_el()->src_el()->is_break()))
                            continue;
                        used_w += (*it)->width();
                    }
                }
                if (used_w > 0)
                {
                    int float_mode = self_size.size_mode & (containing_block_context::size_mode_content | containing_block_context::size_mode_min_content);
                    if (item->get_el()->src_el()->css().get_width().is_predefined()) float_mode |= containing_block_context::size_mode_content;
                    const pixel_t float_w = item->get_el()->measure(self_size.new_inline_size(avail, float_mode), fmt_ctx);
                    if (used_w + float_w > avail + 0.01f)
                        line_top += m_line_boxes.back()->height();
                }
            }
        }
        pixel_t ret = place_float(item->get_el(), line_top, self_size, fmt_ctx);
		if((self_size.size_mode & containing_block_context::size_mode_measure) &&
		   self_size.mode == writing_mode_horizontal_tb &&
		   item->get_el()->src_el()->css().get_clear() == clear_none)
		{
			// ret is the float's line extent, not its own width: the latest float's
			// extent is the line's float total.
			m_measure_float_width = ret;
		}
		if(ret > m_max_line_width)
		{
			m_max_line_width = ret;
		}
		return;
    }

    line_context line_ctx;
    if (!m_line_boxes.empty())
    {
        line_ctx.top = m_line_boxes.back().get()->top();
    }
    line_ctx.right = self_size.render_inline_size();
    line_ctx.fix_top();
	if (self_size.mode == writing_mode_horizontal_tb)
	{
		fmt_ctx->get_line_left_right(line_ctx.top, self_size.render_inline_size(), line_ctx.left, line_ctx.right);
	}

    int base_level = (css().get_direction() == direction_rtl ? 1 : 0);
    item->set_bidi_level(base_level);

    if (item->get_type() == line_box_item::type_text_part)
    {
    if (item->get_el()->src_el()->is_inline_box())
    {
    // A ruby box is atomic: measure it at its content inline size so its base text and
    // annotation are never split, and never re-measure it at the min-content size.
    bool ruby_atomic = self_size.mode != writing_mode_horizontal_tb &&
                       satoru_is_ruby_box(item->get_el());
    uint32_t cb_size_mode = self_size.size_mode & (containing_block_context::size_mode_content | containing_block_context::size_mode_min_content);
    // An auto-width inline box is shrink-to-fit: its first measure asks for the content size, so
    // percentages inside it (e.g. a flex-basis) contribute their content instead of resolving
    // against the line width. A replaced box has no content to shrink to: its auto inline size comes
    // from the containing block (a ratio-only svg fills the line width), so asking for a content size
    // only makes the box cyclic-0 in a shrink-to-fit ancestor. (Blink: LayoutReplaced::ComputeReplacedSize
    // resolves it against the available inline size.)
    if (ruby_atomic || (item->get_el()->src_el()->css().get_width().is_predefined() &&
                        !item->get_el()->src_el()->is_replaced()))
    {
        cb_size_mode |= containing_block_context::size_mode_content;
    }
    // A ratio-only replaced box or a percentage width takes its size from the containing block, not from what
    // a float left of the line: the box then may not fit beside the float and moves below it.
    const bool percent_width = !item->get_el()->css().get_width().is_predefined() &&
        item->get_el()->css().get_width().units() == css_units_percentage;
    const bool ratio_only = item->get_el()->src_el()->is_ratio_only_replaced();
    // During an intrinsic-size query the containing block inline size is indefinite (auto): an auto-width
    // ratio-only box is 100% of it, so the percentage is cyclic and contributes 0 instead of the trial width
    // the query measures inside (Blink resolves such an auto inline size to 0 for the contribution). A
    // definite containing block keeps its width, so the box still fills it.
    const bool cyclic_ratio_only = ratio_only && !percent_width &&
        (self_size.size_mode & containing_block_context::size_mode_content) &&
        self_size.render_inline_size().type == containing_block_context::cbc_value_type_auto;
    const pixel_t cb_inline = cyclic_ratio_only ? 0 :
        (((ratio_only || percent_width) && self_size.mode == writing_mode_horizontal_tb)
        ? (pixel_t)self_size.render_inline_size() : line_ctx.right);
    auto cb = self_size.new_inline_size(cb_inline, cb_size_mode);
    pixel_t min_rendered_width = item->get_el()->measure(cb, fmt_ctx);
    // An auto-width (or fit-content) atomic inline is shrink-to-fit against the space on
    // offer: min(max-content, max(min-content, available)). The content measure answers
    // with the true max-content, which can be wider than the line (an inline-block whose
    // padding alone exceeds the column): fit it here, or it is placed at its max-content
    // width and its content never wraps inside it.
    const css_length& atomic_width = item->get_el()->css().get_width();
    if (!ruby_atomic && atomic_width.is_predefined() && (atomic_width.predef() == 0 || atomic_width.predef() == 1) &&
        !item->get_el()->src_el()->is_replaced() && self_size.mode == writing_mode_horizontal_tb &&
        min_rendered_width > cb_inline)
    {
        auto cb_min = self_size.new_inline_size(cb_inline, cb_size_mode | containing_block_context::size_mode_min_content);
        const pixel_t min_content_width = item->get_el()->measure(cb_min, fmt_ctx);
        const pixel_t fit_width = std::min(min_rendered_width, std::max(min_content_width, cb_inline));
        if (fit_width < min_rendered_width)
        {
            auto cb_fit = self_size.new_inline_size(fit_width, containing_block_context::size_mode_exact_width);
            item->get_el()->measure(cb_fit, fmt_ctx);
            min_rendered_width = fit_width;
        }
    }
    if (!(self_size.size_mode & containing_block_context::size_mode_measure))
            {
    // Lay the ruby out in a content-sized inline extent. A small slack keeps text that exactly
    // fills the extent from wrapping because of floating point rounding of glyph advances.
    // The whole ruby stays atomic and can_hold can move it to the next column intact.
    auto place_cb = ruby_atomic
        ? self_size.new_inline_size(min_rendered_width + 0.5f, cb_size_mode)
        : cb;
    // A shrink-to-fit box has its width by now; placing it in the shrink-to-fit mode would skip the
    // text-align of its lines, which only apply inside an exact width.
    if (!ruby_atomic && cb_size_mode && self_size.mode == writing_mode_horizontal_tb &&
        item->get_el()->css().get_writing_mode() == writing_mode_horizontal_tb)
    {
        place_cb = self_size.new_inline_size(item->get_el()->inline_size() - item->get_el()->content_offset_width() + item->get_el()->box_sizing_width(),
                                             containing_block_context::size_mode_exact_width);
    }
    item->get_el()->place(line_ctx.left, line_ctx.top, place_cb, fmt_ctx);
    }

    if (!ruby_atomic &&
    min_rendered_width < item->get_el()->inline_size() &&
    item->get_el()->src_el()->css().get_width().is_predefined())
    {
    // Re-measure at the content's own size. A hair of slack keeps content that exactly fits
    // from wrapping because of floating point rounding of the glyph advances.
    auto cb2 = self_size.new_inline_size(min_rendered_width + 0.01f, self_size.size_mode & (containing_block_context::size_mode_content | containing_block_context::size_mode_min_content));
    item->get_el()->measure(cb2, fmt_ctx);
    if (!(self_size.size_mode & containing_block_context::size_mode_measure))
    {
        auto place_cb2 = cb2;
        if (self_size.mode == writing_mode_horizontal_tb &&
            item->get_el()->css().get_writing_mode() == writing_mode_horizontal_tb)
        {
            place_cb2 = self_size.new_inline_size(item->get_el()->inline_size() - item->get_el()->content_offset_width() + item->get_el()->box_sizing_width(),
                                                  containing_block_context::size_mode_exact_width);
        }
        item->get_el()->place(line_ctx.left, line_ctx.top, place_cb2, fmt_ctx);
    }
    }
    item->set_rendered_min_width(min_rendered_width);

    // For inline-block, we still want to detect if it's primarily RTL or LTR
            int cached_level = 0;
            if (item->get_el()->get_cached_bidi_level(base_level, cached_level))
            {
                item->set_bidi_level(cached_level);
            }
            else
            {
                string text;
                item->get_el()->src_el()->get_text(text);
                int level = src_el()->get_document()->container()->get_bidi_level(text.c_str(), base_level);
                item->get_el()->set_cached_bidi_level(base_level, level);
                item->set_bidi_level(level);
            }
    }
    else if (item->get_el()->src_el()->css().get_display() == display_inline_text)
    {
    litehtml::size sz;
        item->get_el()->src_el()->get_content_size(sz, line_ctx.right);
            item->get_el()->pos() = sz;
            if (self_size.mode == writing_mode_horizontal_tb)
                item->set_rendered_min_width(sz.width);
            else
                item->set_rendered_min_width(sz.height);

            // Detect BiDi level for text part
            int cached_level = 0;
            if (item->get_el()->get_cached_bidi_level(base_level, cached_level))
            {
                item->set_bidi_level(cached_level);
            }
            else
            {
                const string* text_ptr = nullptr;
                string text;
                if (item->get_el()->src_el()->is_text())
                {
                    if (auto el_text = std::dynamic_pointer_cast<litehtml::el_text>(item->get_el()->src_el()))
                    {
                        text_ptr = &el_text->text();
                    }
                }
                if (!text_ptr)
                {
                    item->get_el()->src_el()->get_text(text);
                    text_ptr = &text;
                }
                int level = src_el()->get_document()->container()->get_bidi_level(text_ptr->c_str(), base_level);
                item->get_el()->set_cached_bidi_level(base_level, level);
                item->set_bidi_level(level);
            }
        }
    }

    // Phase 1: unicode-bidi override values force the line item to the embedding
    // level of the writing direction instead of the run's first strong character.
    // This reorders inline boxes/runs; embedding and isolate boundaries are still
    // handled only by the existing per-run BiDi analysis (see compatibility notes).
    {
        unicode_bidi ub = item->get_el()->src_el()->css().get_unicode_bidi();
        if (ub == unicode_bidi_bidi_override || ub == unicode_bidi_isolate_override)
        {
            direction dir = item->get_el()->src_el()->css().get_direction();
            int level = (dir == direction_rtl)
                ? ((base_level % 2 == 1) ? base_level : base_level + 1)
                : ((base_level % 2 == 0) ? base_level : base_level + 1);
            item->set_bidi_level(level);
        }
    }

    bool add_box = true;
    if(!m_line_boxes.empty())
    {
        if(m_line_boxes.back()->can_hold(item, src_el()->css().get_white_space()))
        {
            add_box = false;
        }
    }
    if(add_box)
    {
        int line_clamp = src_el()->css().get_line_clamp();
        style_display display = src_el()->css().get_display();
        box_orient orient = src_el()->css().get_webkit_box_orient();

        int max_lines = src_el()->css().get_max_lines();
        bool webkit_clamp = line_clamp > 0 &&
            (display == display_webkit_box || display == display_webkit_inline_box) &&
            orient == box_orient_vertical;
        // max-lines clamps any block container; -webkit-line-clamp needs a vertical -webkit-box.
        size_t limit = webkit_clamp ? (size_t)line_clamp : (max_lines > 0 ? (size_t)max_lines : 0);
        if (limit > 0 && m_line_boxes.size() >= limit)
        {
            // We reached the limit. Force ellipsis on the last allowed line.
            m_is_clamped = true;
            auto& last_line = m_line_boxes.back();
            last_line->set_text_overflow(text_overflow_ellipsis);
            last_line->set_overflow(overflow_hidden);

            // Add the item to the last line even if it doesn't fit,
            // so that line_box::finish can perform the ellipsis.
            last_line->add_item(std::move(item));
            return;
        }

        new_box(item, line_ctx, self_size, fmt_ctx);
    } else if(!m_line_boxes.empty())
    {
        line_ctx.top = m_line_boxes.back()->top();
    }

    if (line_ctx.top != line_ctx.calculatedTop)
    {
        line_ctx.left = 0;
        line_ctx.right = self_size.render_inline_size();
        line_ctx.fix_top();
		if (self_size.mode == writing_mode_horizontal_tb)
		{
			fmt_ctx->get_line_left_right(line_ctx.top, self_size.render_inline_size(), line_ctx.left, line_ctx.right);
		}
    }

    if(!item->get_el()->src_el()->is_inline())
    {
		satoru::WritingModeContext wm = get_wm_context();
        if(m_line_boxes.size() == 1)
        {
            if(collapse_top_margin())
            {
                pixel_t shift = wm.block_start(item->get_el()->get_margins());
                if(shift >= 0)
                {
					line_ctx.top -= shift;
                    m_line_boxes.back()->y_shift(-shift);
                }
            }
        } else
        {
            pixel_t shift = 0;
            pixel_t prev_margin = m_line_boxes[m_line_boxes.size() - 2]->bottom_margin();

            if(prev_margin > wm.block_start(item->get_el()->get_margins()))
            {
                shift = wm.block_start(item->get_el()->get_margins());
            } else
            {
                shift = prev_margin;
            }
            if(shift >= 0)
            {
                line_ctx.top -= shift;
                m_line_boxes.back()->y_shift(-shift);
            }
        }
    }

	m_line_boxes.back()->add_item(std::move(item));
}

void litehtml::render_item_inline_context::apply_vertical_align()
{
    if(!m_line_boxes.empty() && get_wm_context().is_vertical())
    {
        // Cell alignment runs along the block axis, which is horizontal in vertical writing.
        const bool from_right = css().get_writing_mode() == writing_mode_vertical_rl;
        pixel_t content_thickness = from_right
            ? m_line_boxes.front()->right() - m_line_boxes.back()->left()
            : m_line_boxes.back()->right() - m_line_boxes.front()->left();
        pixel_t extra = m_pos.width - content_thickness;
        pixel_t add = 0;
        if(extra > 0)
        {
            switch(src_el()->css().get_vertical_align())
            {
                case va_middle: add = extra / 2; break;
                case va_bottom: add = extra; break;
                default: break;
            }
        }
        if(add != 0)
        {
            for(auto & box : m_line_boxes)
            {
                box->x_shift(from_right ? -add : add);
            }
        }
        return;
    }
    if(!m_line_boxes.empty())
    {
        pixel_t add = 0;
        pixel_t content_height	= m_line_boxes.back()->bottom();

        if(m_pos.height > content_height)
        {
            switch(src_el()->css().get_vertical_align())
            {
                case va_middle:
                    add = (m_pos.height - content_height) / 2;
                    break;
                case va_bottom:
                    add = m_pos.height - content_height;
                    break;
                default:
                    add = 0;
                    break;
            }
        }

        if(add != 0)
        {
            for(auto & box : m_line_boxes)
            {
                box->y_shift(add);
            }
        }
    }
}

litehtml::pixel_t litehtml::render_item_inline_context::get_first_baseline()
{
	pixel_t bl;
	if(!m_line_boxes.empty())
	{
		const auto &line = m_line_boxes.front();
        if (get_wm_context().is_vertical())
        {
            if (css().get_writing_mode() == writing_mode_vertical_rl || css().get_writing_mode() == writing_mode_sideways_rl)
            {
                bl = m_pos.width - (line->left() + line->baseline()) + content_offset_right();
            }
            else
            {
                bl = line->left() + (line->block_size() - line->baseline()) + content_offset_left();
            }
        }
        else
        {
		    bl = line->bottom() - line->baseline() + content_offset_top() + m_control_shift;
        }
	} else
	{
		bl = m_has_measured_baseline ? m_measured_first_baseline : default_baseline();
	}
	return bl;
}

litehtml::pixel_t litehtml::render_item_inline_context::get_last_baseline()
{
	pixel_t bl;
	if(!m_line_boxes.empty())
	{
		const auto &line = m_line_boxes.back();
        if (get_wm_context().is_vertical())
        {
            if (css().get_writing_mode() == writing_mode_vertical_rl || css().get_writing_mode() == writing_mode_sideways_rl)
            {
                bl = m_pos.width - (line->left() + line->baseline()) + content_offset_right();
            }
            else
            {
                bl = line->left() + (line->block_size() - line->baseline()) + content_offset_left();
            }
        }
        else
        {
		    bl = line->bottom() - line->baseline() + content_offset_top() + m_control_shift;
        }
	} else
	{
		bl = get_wm_context().is_vertical() ? default_baseline() : (m_has_measured_baseline ? m_measured_last_baseline : (css().get_display() == display_inline_block ? height() : default_baseline()));
	}
	return bl;
}
