#ifndef LITEHTML_RENDER_INLINE_CONTEXT_H
#define LITEHTML_RENDER_INLINE_CONTEXT_H

#include "render_block.h"

namespace litehtml
{
	/**
	 * An inline formatting context is established by a block container box that contains no block-level boxes.
	 * https://www.w3.org/TR/CSS22/visuren.html#inline-formatting
	 */
	class render_item_inline_context : public render_item_block
	{
		/**
		 *	Structure contains elements with display: inline
		 *	members:
		 *	- element: 		render_item with display: inline
		 *	- boxes: 		rectangles represented inline element content. There are can be many boxes if content
		 *			 is split into some lines
		 *	- start_box:	the start position of currently calculated box
		 */
		struct inlines_item
		{
			std::shared_ptr<render_item> element;
			position::vector boxes;
			position	start_box;

			explicit inlines_item(const std::shared_ptr<render_item>& el) : element(el) {}
		};
	protected:
		std::vector<std::unique_ptr<litehtml::line_box> > m_line_boxes;
		pixel_t m_max_line_width;
		// A measure pass drops its line boxes, yet an inline-block measured inside a parent's measure
		// pass is never placed, so its baseline would fall back to its bottom edge. The baselines of the
		// last measured layout are kept for that case (horizontal writing only).
		bool m_has_measured_baseline = false;
		pixel_t m_measured_first_baseline = 0;
	public:
		bool has_baseline() override { return !m_line_boxes.empty() || m_has_measured_baseline; }
	protected:
		pixel_t m_measured_last_baseline = 0;
		// Measure pass only: width of the floats met since the last finished line. Floats are not
		// placed while measuring, so they cannot narrow a line; the line they sit beside needs their
		// width on top of its own for the box to be as wide as its content (max-content).
		pixel_t m_measure_float_width = 0;
		bool m_is_clamped;
		// Line-breaking inline end used by the running layout pass. It equals the
		// block's content inline-size unless text-wrap-style narrowing is active.
		pixel_t m_wrap_end;
		// Cleared when the flow holds content the text-wrap-style balancing passes
		// do not handle (floats, atomic inlines, forced breaks).
		bool m_wrap_eligible;

		pixel_t _render_content(pixel_t x, pixel_t y, bool second_pass, const containing_block_context &self_size, formatting_context* fmt_ctx) override;
		void layout_lines(const containing_block_context &self_size, formatting_context* fmt_ctx, pixel_t wrap_end = -1);
		void apply_text_wrap_style(const containing_block_context &self_size, formatting_context* fmt_ctx);
		bool text_wrap_eligible(const containing_block_context &self_size) const;
		void fix_line_width(element_float flt,
							const containing_block_context &self_size, formatting_context* fmt_ctx) override;

		std::list<std::unique_ptr<line_box_item> > finish_last_box(bool end_of_render, const containing_block_context &self_size);
		void place_inline(std::unique_ptr<line_box_item> item, const containing_block_context &self_size, formatting_context* fmt_ctx);
		pixel_t new_box(const std::unique_ptr<line_box_item>& el, line_context& line_ctx, const containing_block_context &self_size, formatting_context* fmt_ctx);
		void apply_vertical_align() override;
	public:
		// Entry for the enclosing block context: a float it placed after this
		// run was laid out narrows the run's last line exactly as a float met
		// inside the run does (fix_line_width).
		void fix_last_line_width(element_float flt, const containing_block_context &self_size, formatting_context* fmt_ctx)
		{
			fix_line_width(flt, self_size, fmt_ctx);
		}

		explicit render_item_inline_context(std::shared_ptr<element>  src_el) : render_item_block(std::move(src_el)), m_max_line_width(0), m_is_clamped(false), m_wrap_end(0), m_wrap_eligible(true)
		{}

		std::shared_ptr<render_item> clone() override
		{
			return std::make_shared<render_item_inline_context>(src_el());
		}

		pixel_t get_first_baseline() override;
		pixel_t get_last_baseline() override;

		// Height and used width of the last line box (false when there is none): a float that follows this
		// run of inline content joins that line when it fits.
		bool last_line_extent(pixel_t& height, pixel_t& used_width) const
		{
			if(m_line_boxes.empty()) return false;
			height = m_line_boxes.back()->height();
			used_width = m_line_boxes.back()->width();
			return true;
		}

		// Absolute document-space rects of the laid-out line boxes (P3 pagination).
		void get_line_boundaries(position::vector& boxes) const override
		{
			pixel_t block_top = get_placement().y;
			for(const auto& box : m_line_boxes)
			{
				if(box->height() <= 0) continue;
				position p;
				p.x = 0;
				p.width = 0;
				p.y = block_top + box->top();
				p.height = box->height();
				boxes.push_back(p);
			}
		}
	};
}

#endif //LITEHTML_RENDER_INLINE_CONTEXT_H
