#ifndef LITEHTML_FLOATS_HOLDER_H
#define LITEHTML_FLOATS_HOLDER_H

#include <list>
#include <vector>
#include "types.h"

namespace litehtml
{
	class formatting_context
	{
	private:
		std::list<floated_box> m_floats_left;
		std::list<floated_box> m_floats_right;
		// Floats of a measure pass, which are not registered in the ledger: their final inline size is
		// not known yet. Only their extent is kept, so that a later float that does not fit beside an
		// earlier one is known to sit below it (see place_measured_float).
		struct measured_float
		{
			pixel_t top = 0;
			pixel_t bottom = 0;
			pixel_t left = 0;
			pixel_t right = 0;
			element_float side = float_left;
			// Identity of the float, so a repeated measure of the same float replaces its record
			// instead of stacking a second copy beside the first.
			const render_item* key = nullptr;
			// The block whose measure pass placed the float, and the placement order within the
			// pass: re-placing a float invalidates the records placed after it in the same pass,
			// because a repeated measure re-runs the children in order and those records belong
			// to the previous run.
			const render_item* scope = nullptr;
			unsigned int seq = 0;
		};
		std::vector<measured_float> m_measure_floats;
		unsigned int m_measure_seq = 0;
		unsigned int m_float_seq = 0;
		pixel_pixel_cache m_cache_line_left;
		pixel_pixel_cache m_cache_line_right;
		pixel_t m_current_block_pos;
		pixel_t m_measured_float_bottom = 0;
		pixel_t m_current_inline_pos;
		writing_mode m_writing_mode;

	public:
		formatting_context(writing_mode mode = writing_mode_horizontal_tb) : m_current_block_pos(0), m_current_inline_pos(0), m_writing_mode(mode)	{}

		void push_position(pixel_t x, pixel_t y)
		{
            if (m_writing_mode != writing_mode_horizontal_tb)
            {
                m_current_inline_pos += y;
                m_current_block_pos += x;
            }
            else
            {
                m_current_inline_pos += x;
                m_current_block_pos += y;
            }
		}
		void pop_position(pixel_t x, pixel_t y)
		{
            if (m_writing_mode != writing_mode_horizontal_tb)
            {
                m_current_inline_pos -= y;
                m_current_block_pos -= x;
            }
            else
            {
                m_current_inline_pos -= x;
                m_current_block_pos -= y;
            }
		}

		void add_float(const std::shared_ptr<render_item> &el, pixel_t min_width, int context,
					   const render_item* scope = nullptr);
		// Drops a float's ledger entries: a float that is laid out again (a shrink-to-fit parent
		// lays its children out at a trial width before the final one) must not avoid its own
		// stale entry from the earlier pass.
		void remove_float(const std::shared_ptr<render_item> &el, const render_item* scope = nullptr);
		void clear_floats(int context);
		pixel_t find_next_line_top( pixel_t top, pixel_t width, pixel_t def_right );
		pixel_t find_next_line_top_measured(const render_item* scope, pixel_t top, pixel_t width, pixel_t block_size, pixel_t container_inline_size);
		pixel_t get_floats_height(element_float el_float = float_none) const;
		// Measure passes do not register floats; this keeps how far down they reach so that a box which
		// contains its floats (a flow root) still grows to them.
		void note_measured_float_bottom(pixel_t bottom) { if (bottom > m_measured_float_bottom) m_measured_float_bottom = bottom; }
		pixel_t measured_float_bottom() const { return m_measured_float_bottom; }
		// Where a float measured in a measure pass would sit: as high as it fits beside the floats
		// measured before it, which is below them when the container is too narrow for both on one
		// line (as in the place pass, Blink's float placement). Records the float's extent.
		// out_extent, when given, receives the inline extent the float's line reaches from its
		// side (the accumulated width of the floats sharing the line), for intrinsic sizing.
		pixel_t place_measured_float(const render_item* key, const render_item* scope, pixel_t top,
									 pixel_t inline_size, pixel_t block_size, element_float side,
									 pixel_t container_inline_size,
									 pixel_t* out_extent = nullptr);
		pixel_t get_left_floats_height() const;
		pixel_t get_right_floats_height() const;
		pixel_t get_line_left( pixel_t y );
		void get_line_left_right( pixel_t y, pixel_t def_right, pixel_t& ln_left, pixel_t& ln_right )
		{
			ln_left		= get_line_left(y);
			ln_right	= get_line_right(y, def_right);
		}
		pixel_t get_line_right( pixel_t y, pixel_t def_right );
		pixel_t get_cleared_top(const std::shared_ptr<render_item> &el, pixel_t line_top) const;
		void update_floats(pixel_t dy, const std::shared_ptr<render_item> &parent);
		void apply_relative_shift(const containing_block_context &containing_block_size);
		pixel_t find_min_left(pixel_t y, int context_idx);
		pixel_t find_min_right(pixel_t y, pixel_t right, int context_idx);
	};
}

#endif //LITEHTML_FLOATS_HOLDER_H
