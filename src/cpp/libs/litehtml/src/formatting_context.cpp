#include "render_item.h"
#include "types.h"
#include "formatting_context.h"
#include <algorithm>

void litehtml::formatting_context::add_float(const std::shared_ptr<render_item> &el, pixel_t min_width, int context, const render_item* scope)
{
	floated_box fb;
    satoru::WritingModeContext wm(m_writing_mode, 0, 0); // Temporary context for mapping

    // Calculate logical position relative to BFC origin
    // inline_start_pos()/block_start_pos() address the content box, while the ledger entry
    // spans the margin box (inline_size()/block_size() include margin, border and padding).
    pixel_t el_inline_offset = el->inline_start_pos() - wm.inline_start(el->get_margins()) -
                               wm.inline_start(el->get_borders()) -
                               wm.inline_start(el->get_paddings());
    pixel_t el_block_offset  = el->block_start_pos() - wm.block_start(el->get_margins()) -
                               wm.block_start(el->get_borders()) -
                               wm.block_start(el->get_paddings());

	fb.pos.x		= el_inline_offset + m_current_inline_pos;
	fb.pos.y		= el_block_offset + m_current_block_pos;
	fb.pos.width	= el->inline_size();
	fb.pos.height	= el->block_size();
	fb.float_side	= el->src_el()->css().get_float();
	fb.clear_floats	= el->src_el()->css().get_clear();
	fb.el			= el;
	fb.scope		= scope;
	fb.seq			= ++m_float_seq;
	fb.context		= context;
	fb.min_width	= min_width;

	if(fb.float_side == float_left)
	{
		if(m_floats_left.empty())
		{
			m_floats_left.push_back(fb);
		} else
		{
			bool inserted = false;
			for(auto i = m_floats_left.begin(); i != m_floats_left.end(); i++)
			{
				if(fb.pos.right() > i->pos.right())
				{
					m_floats_left.insert(i, std::move(fb));
					inserted = true;
					break;
				}
			}
			if(!inserted)
			{
				m_floats_left.push_back(std::move(fb));
			}
		}
		m_cache_line_left.invalidate();
	} else if(fb.float_side == float_right)
	{
		if(m_floats_right.empty())
		{
			m_floats_right.push_back(std::move(fb));
		} else
		{
			bool inserted = false;
			for(auto i = m_floats_right.begin(); i != m_floats_right.end(); i++)
			{
				if(fb.pos.left() < i->pos.left())
				{
					m_floats_right.insert(i, std::move(fb));
					inserted = true;
					break;
				}
			}
			if(!inserted)
			{
				m_floats_right.push_back(fb);
			}
		}
		m_cache_line_right.invalidate();
	}
}

void litehtml::formatting_context::remove_float(const std::shared_ptr<render_item> &el,
												const render_item* scope)
{
	// The float's own entry goes, and so does every entry the same layout pass placed after
	// it: a repeated pass re-runs its children in order, so those entries are stale copies
	// from the previous run and would push the re-placed floats aside.
	unsigned int old_seq = 0;
	if (scope)
	{
		for(const auto& fb : m_floats_left)  if (fb.el == el && fb.scope == scope) old_seq = fb.seq;
		for(const auto& fb : m_floats_right) if (fb.el == el && fb.scope == scope) old_seq = fb.seq;
	}
	auto stale = [&el, scope, old_seq](const floated_box& fb) {
		return fb.el == el || (old_seq != 0 && fb.scope == scope && fb.seq > old_seq);
	};
	m_floats_left.remove_if(stale);
	m_floats_right.remove_if(stale);
	m_cache_line_left.invalidate();
	m_cache_line_right.invalidate();
}

litehtml::pixel_t litehtml::formatting_context::place_measured_float(const render_item* key,
																 const render_item* scope, pixel_t top,
																 pixel_t inline_size, pixel_t block_size,
																 element_float side,
																 pixel_t container_inline_size,
																 pixel_t* out_extent)
{
	// A repeated measure of the same float replaces its earlier record, and every record the
	// same pass placed after it goes with it: the pass re-runs its children in order, so those
	// records are stale copies from the previous run and would push the float aside.
	if (key)
	{
		unsigned int old_seq = 0;
		for(const auto& fb : m_measure_floats)
			if (fb.key == key && fb.scope == scope) old_seq = fb.seq;
		m_measure_floats.erase(std::remove_if(m_measure_floats.begin(), m_measure_floats.end(),
			[key, scope, old_seq](const measured_float& fb) {
				return fb.key == key || (old_seq != 0 && fb.scope == scope && fb.seq > old_seq);
			}), m_measure_floats.end());
	}
	// A float is placed as high as it fits beside the floats already on that line; one that does not
	// fit is pushed below them (Blink's LayoutBlockFlow::positionNewFloats does the same with the
	// real ledger). In a measure pass the floats are not in the ledger, so their extent is tracked
	// here instead: floats on the same line accumulate from their side, so the line's extent is the
	// sum of their widths -- a shrink-to-fit container holding two floats is as wide as both.
	while (true)
	{
		pixel_t limit_left = 0;
		pixel_t limit_right = container_inline_size;
		for(const auto& fb : m_measure_floats)
		{
			// Records are kept in the measuring block's local frame, so only floats of the
			// same block (same scope) can share this float's line; another block's records
			// are in a different frame and must not constrain or extend this one.
			if (fb.scope != scope) continue;
			if (top >= fb.bottom || top + block_size <= fb.top) continue;
			if (fb.side == float_left)	limit_left = std::max(limit_left, fb.right);
			else						limit_right = std::min(limit_right, fb.left);
		}
		pixel_t left, right;
		if (side == float_left)	{ left = limit_left; right = left + inline_size; }
		else					{ right = limit_right; left = right - inline_size; }
		const bool fits = side == float_left ? (right <= limit_right + 0.01f)
											 : (left >= limit_left - 0.01f);
		if (!fits)
		{
			// Push below the nearest float that blocks this line; if nothing blocks it (the float
			// alone is wider than the container) it stays and overflows.
			pixel_t new_top = -1;
			for(const auto& fb : m_measure_floats)
			{
				if (fb.scope != scope) continue;
				if (top >= fb.bottom || top + block_size <= fb.top) continue;
				if (new_top < 0 || fb.bottom < new_top) new_top = fb.bottom;
			}
			if (new_top > top)
			{
				top = new_top;
				continue;
			}
		}
		measured_float fb{ top, top + block_size, left, right, side, key, scope, ++m_measure_seq };
		m_measure_floats.push_back(fb);
		if (fb.bottom > m_measured_float_bottom) m_measured_float_bottom = fb.bottom;
		if (out_extent)
		{
			// The line's intrinsic extent spans both sides: floats from the opposite side that
			// share the line add their extent to this float's.
			pixel_t opp = 0;
			for(const auto& fb : m_measure_floats)
			{
				if (fb.scope != scope) continue;
				if (fb.side == side) continue;
				if (top >= fb.bottom || top + block_size <= fb.top) continue;
				opp = std::max(opp, fb.side == float_left ? fb.right
														  : container_inline_size - fb.left);
			}
			*out_extent = (side == float_left ? right : container_inline_size - left) + opp;
		}
		return top;
	}
}

litehtml::pixel_t litehtml::formatting_context::get_floats_height(element_float el_float) const
{
	pixel_t h = m_current_block_pos;

	for(const auto& fb : m_floats_left)
	{
		bool process = false;
		switch(el_float)
		{
			case float_none:
				process = true;
				break;
			case float_left:
				if (fb.clear_floats == clear_left || fb.clear_floats == clear_both)
				{
					process = true;
				}
				break;
			case float_right:
				if (fb.clear_floats == clear_right || fb.clear_floats == clear_both)
				{
					process = true;
				}
				break;
		}
		if(process)
		{
			if(el_float == float_none)
			{
				h = std::max(h, fb.pos.bottom());
			} else
			{
				h = std::max(h, fb.pos.top());
			}
		}
	}


	for(const auto& fb : m_floats_right)
	{
		bool process = false;
		switch(el_float)
		{
			case float_none:
				process = true;
				break;
			case float_left:
				if (fb.clear_floats == clear_left || fb.clear_floats == clear_both)
				{
					process = true;
				}
				break;
			case float_right:
				if (fb.clear_floats == clear_right || fb.clear_floats == clear_both)
				{
					process = true;
				}
				break;
		}
		if(process)
		{
			if(el_float == float_none)
			{
				h = std::max(h, fb.pos.bottom());
			} else
			{
				h = std::max(h, fb.pos.top());
			}
		}
	}

	return h - m_current_block_pos;
}

litehtml::pixel_t litehtml::formatting_context::get_left_floats_height() const
{
	pixel_t h = 0;
	if(!m_floats_left.empty())
	{
		for (const auto& fb : m_floats_left)
		{
			h = std::max(h, fb.pos.bottom());
		}
	}
	return h - m_current_block_pos;
}

litehtml::pixel_t litehtml::formatting_context::get_right_floats_height() const
{
	pixel_t h = 0;
	if(!m_floats_right.empty())
	{
		for(const auto& fb : m_floats_right)
		{
			h = std::max(h, fb.pos.bottom());
		}
	}
	return h - m_current_block_pos;
}

litehtml::pixel_t litehtml::formatting_context::get_line_left(pixel_t block_pos )
{
	block_pos += m_current_block_pos;

	if(m_cache_line_left.is_valid && m_cache_line_left.hash == block_pos)
	{
		if(m_cache_line_left.val - m_current_inline_pos < 0)
		{
			return 0;
		}
		return m_cache_line_left.val - m_current_inline_pos;
	}

	pixel_t w = 0;
	for(const auto& fb : m_floats_left)
	{
		if (block_pos >= fb.pos.top() && block_pos < fb.pos.bottom())
		{
			w = std::max(w, fb.pos.right());
		}
	}
	m_cache_line_left.set_value(block_pos, w);
	w -= m_current_inline_pos;
	if(w < 0) return 0;
	return w;
}

litehtml::pixel_t litehtml::formatting_context::get_line_right(pixel_t block_pos, pixel_t def_inline_end )
{
	block_pos += m_current_block_pos;
	def_inline_end += m_current_inline_pos;
	if(m_cache_line_right.is_valid && m_cache_line_right.hash == block_pos)
	{
		if(m_cache_line_right.is_default)
		{
			return def_inline_end - m_current_inline_pos;
		} else
		{
			pixel_t w = std::min(m_cache_line_right.val, def_inline_end) - m_current_inline_pos;
			if(w < 0) return 0;
			return w;
		}
	}

	pixel_t w = def_inline_end;
	m_cache_line_right.is_default = true;
	for(const auto& fb : m_floats_right)
	{
		if(block_pos >= fb.pos.top() && block_pos < fb.pos.bottom())
		{
			w = std::min(w, fb.pos.left());
			m_cache_line_right.is_default = false;
		}
	}
	m_cache_line_right.set_value(block_pos, w);
	w -= m_current_inline_pos;
	if(w < 0) return 0;
	return w;
}


void litehtml::formatting_context::clear_floats(int context)
{
	auto iter = m_floats_left.begin();
	while(iter != m_floats_left.end())
	{
		if(iter->context >= context)
		{
			iter = m_floats_left.erase(iter);
			m_cache_line_left.invalidate();
		} else
		{
			iter++;
		}
	}

	iter = m_floats_right.begin();
	while(iter != m_floats_right.end())
	{
		if(iter->context >= context)
		{
			iter = m_floats_right.erase(iter);
			m_cache_line_right.invalidate();
		} else
		{
			iter++;
		}
	}
}

litehtml::pixel_t litehtml::formatting_context::get_cleared_top(const std::shared_ptr<render_item> &el, pixel_t line_top) const
{
	switch(el->src_el()->css().get_clear())
	{
		case clear_left:
			{
				pixel_t fh = get_left_floats_height();
				if(fh != 0 && fh > line_top)
				{
					line_top = fh;
				}
			}
			break;
		case clear_right:
			{
				pixel_t fh = get_right_floats_height();
				if(fh != 0 && fh > line_top)
				{
					line_top = fh;
				}
			}
			break;
		case clear_both:
			{
				pixel_t fh = get_floats_height(float_none);
				if(fh != 0 && fh > line_top)
				{
					line_top = fh;
				}
			}
			break;
		default:
			if(el->src_el()->css().get_float() != float_none)
			{
				pixel_t fh = get_floats_height(el->src_el()->css().get_float());
				if(fh != 0 && fh > line_top)
				{
					line_top = fh;
				}
			}
			break;
	}
	return line_top;
}

litehtml::pixel_t litehtml::formatting_context::find_next_line_top(pixel_t top, pixel_t width, pixel_t def_right )
{
	top += m_current_block_pos;
	def_right += m_current_inline_pos;

	pixel_t new_top = top;
	pixel_vector points;

	for(const auto& fb : m_floats_left)
	{
		if(fb.pos.top() >= top)
		{
			if(find(points.begin(), points.end(), fb.pos.top()) == points.end())
			{
				points.push_back(fb.pos.top());
			}
		}
		if (fb.pos.bottom() >= top)
		{
			if (find(points.begin(), points.end(), fb.pos.bottom()) == points.end())
			{
				points.push_back(fb.pos.bottom());
			}
		}
	}

	for (const auto& fb : m_floats_right)
	{
		if (fb.pos.top() >= top)
		{
			if (find(points.begin(), points.end(), fb.pos.top()) == points.end())
			{
				points.push_back(fb.pos.top());
			}
		}
		if (fb.pos.bottom() >= top)
		{
			if (find(points.begin(), points.end(), fb.pos.bottom()) == points.end())
			{
				points.push_back(fb.pos.bottom());
			}
		}
	}

	if(!points.empty())
	{
		sort(points.begin(), points.end(), std::less<pixel_t>( ));
		new_top = points.back();

		for(auto pt : points)
		{
			pixel_t pos_left	= 0;
			pixel_t pos_right	= def_right;
			get_line_left_right(pt - m_current_block_pos, def_right - m_current_inline_pos, pos_left, pos_right);

			if(pos_right - pos_left >= width)
			{
				new_top = pt;
				break;
			}
		}
	}
	return new_top - m_current_block_pos;
}

litehtml::pixel_t litehtml::formatting_context::find_next_line_top_measured(const render_item* scope, pixel_t top, pixel_t width, pixel_t block_size, pixel_t container_inline_size)
{
	// The measured-float counterpart of find_next_line_top: measure passes keep floats
	// out of the real ledger, so a block-formatting-context child that must move below
	// a float finds no floats there and its container's measured height loses the
	// float's whole extent (a float followed by a grid sibling measured as tall as
	// the grid alone). Records are in the measuring block's local frame, like top.
	pixel_vector points;
	for(const auto& fb : m_measure_floats)
	{
		if (fb.scope != scope) continue;
		if (fb.top >= top && find(points.begin(), points.end(), fb.top) == points.end())
			points.push_back(fb.top);
		if (fb.bottom >= top && find(points.begin(), points.end(), fb.bottom) == points.end())
			points.push_back(fb.bottom);
	}
	if (points.empty()) return top;
	sort(points.begin(), points.end(), std::less<pixel_t>());
	pixel_t new_top = points.back();
	for(auto pt : points)
	{
		pixel_t limit_left = 0;
		pixel_t limit_right = container_inline_size;
		for(const auto& fb : m_measure_floats)
		{
			if (fb.scope != scope) continue;
			if (pt >= fb.bottom || pt + block_size <= fb.top) continue;
			if (fb.side == float_left)	limit_left = std::max(limit_left, fb.right);
			else						limit_right = std::min(limit_right, fb.left);
		}
		if (limit_right - limit_left >= width)
		{
			new_top = pt;
			break;
		}
	}
	return new_top;
}

void litehtml::formatting_context::update_floats(pixel_t dy, const std::shared_ptr<render_item> &parent)
{
	bool reset_cache = false;
	for(auto fb = m_floats_left.rbegin(); fb != m_floats_left.rend(); fb++)
	{
		if(fb->el->src_el()->is_ancestor(parent->src_el()))
		{
			reset_cache	= true;
			fb->pos.y	+= dy;
		}
	}
	if(reset_cache)
	{
		m_cache_line_left.invalidate();
	}
	reset_cache = false;
	for(auto fb = m_floats_right.rbegin(); fb != m_floats_right.rend(); fb++)
	{
		if(fb->el->src_el()->is_ancestor(parent->src_el()))
		{
			reset_cache	= true;
			fb->pos.y	+= dy;
		}
	}
	if(reset_cache)
	{
		m_cache_line_right.invalidate();
	}
}

void litehtml::formatting_context::apply_relative_shift(const containing_block_context &containing_block_size)
{
	for (const auto& fb : m_floats_left)
	{
		fb.el->apply_relative_shift(containing_block_size);
	}
}

litehtml::pixel_t litehtml::formatting_context::find_min_left(pixel_t y, int context_idx)
{
	y += m_current_block_pos;
	pixel_t min_left = m_current_inline_pos;
	for(const auto& fb : m_floats_left)
	{
		if (y >= fb.pos.top() && y < fb.pos.bottom() && fb.context == context_idx)
		{
			min_left += fb.min_width;
		}
	}
	if(min_left < m_current_inline_pos) return 0;
	return min_left - m_current_inline_pos;
}

litehtml::pixel_t litehtml::formatting_context::find_min_right(pixel_t y, pixel_t right, int context_idx)
{
	y += m_current_block_pos;
	pixel_t min_right = right + m_current_inline_pos;
	for(const auto& fb : m_floats_right)
	{
		if (y >= fb.pos.top() && y < fb.pos.bottom() && fb.context == context_idx)
		{
			min_right -= fb.min_width;
		}
	}
	if(min_right < m_current_inline_pos) return 0;
	return min_right - m_current_inline_pos;
}
