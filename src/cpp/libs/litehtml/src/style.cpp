#include "html.h"
#include "style.h"
#include "css_parser.h"
#include "internal.h"
#include <set>
#include <cstdio>
#include "html_tag.h"
#include "document.h"
#include "document_container.h"

namespace litehtml
{
  bool evaluate_calc(css_token_vector &tokens, const html_tag *el);
  bool evaluate_light_dark(css_token_vector &tokens, document_container *container);

  bool parse_bg_image(const css_token &token, image &bg_image, document_container *container);
  bool parse_bg_position_size(const css_token_vector &tokens, int &index, css_length &x, css_length &y, css_size &size);
  bool parse_bg_size(const css_token_vector &tokens, int &index, css_size &size);
  bool parse_two_lengths(const css_token_vector &tokens, css_length len[2], int options);
  template <class T, class... Args>
  int parse_1234_values(const css_token_vector &tokens, T result[4], bool (*func)(const css_token &, T &, Args...), Args... args);
  int parse_1234_lengths(const css_token_vector &tokens, css_length len[4], int options, string keywords = "");
  bool parse_border_width(const css_token &tok, css_length &width);
  bool parse_font_family(const css_token_vector &tokens, string &font_family);
  bool parse_font_weight(const css_token &tok, css_length &weight);

  namespace
  {
    bool offset_tok_ws(const css_token &tok) { return tok.type == WHITESPACE; }

    bool offset_ident_is(const css_token &tok, const char *name)
    {
      return tok.type == IDENT && lowcase(tok.ident()) == name;
    }

    bool offset_angle_token(const css_token &tok)
    {
      if (tok.type != DIMENSION) return false;
      string unit = lowcase(tok.unit);
      return unit == "deg" || unit == "rad" || unit == "grad" || unit == "turn";
    }

    // calc()/min()/max()/clamp() are length-ish values. They are evaluated
    // before the shorthand is parsed, but an expression that cannot be reduced
    // to a single value stays a function token and must still be accepted where
    // a <length-percentage> is expected.
    bool offset_calc_token(const css_token &tok)
    {
      if (tok.type != CV_FUNCTION) return false;
      const string name = lowcase(tok.name);
      return name == "calc" || name == "min" || name == "max" || name == "clamp";
    }

    // <length-percentage> (unitless zero is a valid length, angles are not).
    bool offset_lp_token(const css_token &tok)
    {
      if (tok.type == PERCENTAGE) return true;
      if (tok.type == NUMBER) return tok.n.number == 0;
      if (tok.type == DIMENSION) return !offset_angle_token(tok);
      return offset_calc_token(tok);
    }

    bool offset_coord_box(const css_token &tok)
    {
      if (tok.type != IDENT) return false;
      string id = lowcase(tok.ident());
      return id == "content-box" || id == "padding-box" || id == "border-box" ||
             id == "fill-box" || id == "stroke-box" || id == "view-box";
    }

    bool offset_h_keyword(const css_token &tok)
    {
      return offset_ident_is(tok, "left") || offset_ident_is(tok, "right");
    }

    bool offset_v_keyword(const css_token &tok)
    {
      return offset_ident_is(tok, "top") || offset_ident_is(tok, "bottom");
    }

    bool offset_position_keyword(const css_token &tok)
    {
      return offset_ident_is(tok, "left") || offset_ident_is(tok, "right") ||
             offset_ident_is(tok, "top") || offset_ident_is(tok, "bottom") ||
             offset_ident_is(tok, "center");
    }

    bool offset_center_keyword(const css_token &tok)
    {
      return offset_ident_is(tok, "center");
    }

    // The functions that may be used as <offset-path>.
    bool offset_path_function(const css_token &tok)
    {
      if (tok.type != CV_FUNCTION) return false;
      const string name = lowcase(tok.name);
      return name == "path" || name == "ray" || name == "url";
    }

    // path() and url() need a non-empty argument, ray() takes an angle.
    bool offset_path_function_ok(const css_token &tok)
    {
      if (tok.type != CV_FUNCTION) return true;
      if (lowcase(tok.name) == "ray") return true;
      for (const auto &arg : tok.value)
        if (arg.type == STRING || arg.type == URL) return !arg.str.empty();
      return false;
    }

    // Validate the <position> value of offset-position. The accepted forms are:
    //   * a single <length-percentage> or one of left/right/top/bottom/center
    //   * two values: [ left | center | right | <length-percentage> ]
    //                 [ top | center | bottom | <length-percentage> ]
    //     Both of them may be keywords and then their order is free, but a
    //     length keeps its axis, so `10px left` stays invalid.
    //   * the edge-offset form [ left | right ] <length-percentage>
    //     [ top | bottom ] <length-percentage>, the axes may be swapped.
    bool valid_offset_position(const css_token_vector &tokens)
    {
      if (tokens.empty()) return false;
      const size_t n = tokens.size();

      if (n == 1)
        return offset_lp_token(tokens[0]) || offset_position_keyword(tokens[0]);

      if (n == 2)
      {
        const bool first_h = offset_h_keyword(tokens[0]) || offset_center_keyword(tokens[0]) ||
                             offset_lp_token(tokens[0]);
        const bool second_v = offset_v_keyword(tokens[1]) || offset_center_keyword(tokens[1]) ||
                              offset_lp_token(tokens[1]);
        if (first_h && second_v) return true;
        // The keyword order may be swapped, but only when both values are
        // keywords: a length is always tied to its axis.
        const bool first_v = offset_v_keyword(tokens[0]) || offset_center_keyword(tokens[0]);
        const bool second_h = offset_h_keyword(tokens[1]) || offset_center_keyword(tokens[1]);
        const bool first_len = offset_lp_token(tokens[0]);
        const bool second_len = offset_lp_token(tokens[1]);
        return !first_len && !second_len && first_v && second_h;
      }

      if (n == 4)
      {
        // Edge-offset form: [ left | right ] <length-percentage>
        //                   [ top | bottom ] <length-percentage>
        if (offset_h_keyword(tokens[0]) && offset_lp_token(tokens[1]) &&
            offset_v_keyword(tokens[2]) && offset_lp_token(tokens[3]))
          return true;
        if (offset_v_keyword(tokens[0]) && offset_lp_token(tokens[1]) &&
            offset_h_keyword(tokens[2]) && offset_lp_token(tokens[3]))
          return true;
        return false;
      }
      return false;
    }

    // Parse the `offset` shorthand (CSS Motion Path):
    //   <offset-position>? [ <offset-path> | <offset-distance> ]? <offset-rotate>?
    // Components are consumed in order, so a position must precede the path and a
    // distance must follow the path. Out-of-order or unknown components make the
    // whole declaration invalid.
    bool parse_offset_shorthand(const css_token_vector &value, css_token_vector &path,
                                css_token_vector &distance, css_token_vector &rotate,
                                css_token_vector &position)
    {
      css_token_vector tokens;
      for (const auto &tok : value)
        if (!offset_tok_ws(tok)) tokens.push_back(tok);
      if (tokens.empty()) return false;

      size_t i = 0;
      bool have_position = false, have_path = false;

      // 1. Optional <offset-position>, which must be the first component.
      if (offset_ident_is(tokens[0], "normal") || offset_ident_is(tokens[0], "auto"))
      {
        position.push_back(tokens[0]);
        i = 1;
        have_position = true;
      }
      else
      {
        size_t end = 0;
        while (end < tokens.size() &&
               (offset_lp_token(tokens[end]) || offset_position_keyword(tokens[end])))
          end++;
        if (end > 0)
        {
          css_token_vector candidate(tokens.begin(), tokens.begin() + end);
          if (!valid_offset_position(candidate)) return false;
          position = candidate;
          i = end;
          have_position = true;
        }
      }

      // 2. Optional <offset-path>: none | <coord-box> | <function> [<coord-box>].
      if (i < tokens.size())
      {
        if (offset_ident_is(tokens[i], "none"))
        {
          path.push_back(tokens[i++]);
          have_path = true;
        }
        else if (offset_coord_box(tokens[i]))
        {
          path.push_back(tokens[i++]);
          have_path = true;
        }
        else if (offset_path_function(tokens[i]) || tokens[i].type == URL)
        {
          if (!offset_path_function_ok(tokens[i])) return false;
          path.push_back(tokens[i++]);
          if (i < tokens.size() && offset_coord_box(tokens[i]))
            path.push_back(tokens[i++]);
          have_path = true;
        }
      }

      // A position can only be followed by a path (not by a bare distance or
      // rotate value).
      if (have_position && !have_path) return i == tokens.size();
      if (!have_path) return false;

      // 3. Optional <offset-distance> and <offset-rotate>, each at most once.
      // Chromium accepts either order after the path (`path(...) 30deg 60px` and
      // `path(...) 60px 30deg` both work), so both orders are allowed here.
      bool have_distance = false, have_rotate_keyword = false, have_rotate_angle = false;
      while (i < tokens.size())
      {
        if (!have_distance && offset_lp_token(tokens[i]))
        {
          distance.push_back(tokens[i++]);
          have_distance = true;
        }
        else if (!have_rotate_keyword &&
                 (offset_ident_is(tokens[i], "auto") || offset_ident_is(tokens[i], "reverse")))
        {
          rotate.push_back(tokens[i++]);
          have_rotate_keyword = true;
        }
        else if (!have_rotate_angle && offset_angle_token(tokens[i]))
        {
          // A unitless zero is not accepted here: it is ambiguous between a
          // distance and an angle and Chromium resolves it as a distance.
          rotate.push_back(tokens[i++]);
          have_rotate_angle = true;
        }
        else
          return false;
      }
      return true;
    }
  }  // namespace

  std::map<string_id, string> style::m_valid_values =
      {
          {_display_, style_display_strings},
          {_direction_, direction_strings},
          {_unicode_bidi_, unicode_bidi_strings},
          {_writing_mode_, writing_mode_strings},
          {_text_orientation_, text_orientation_strings},
          {_visibility_, visibility_strings},
          {_position_, element_position_strings},
          {_float_, element_float_strings},
          {_clear_, element_clear_strings},
          {_overflow_, overflow_strings},
          {_appearance_, appearance_strings},
          {_box_sizing_, box_sizing_strings},

          {_overflow_x_, overflow_strings},
          {_overflow_y_, overflow_strings},

          {_text_align_, text_align_strings},
          {_vertical_align_, vertical_align_strings},
          {_text_transform_, text_transform_strings},
          {_white_space_, white_space_strings},
          {_white_space_collapse_, white_space_collapse_strings},
          {_text_wrap_, text_wrap_strings},
          {_text_wrap_mode_, text_wrap_mode_strings},
          {_text_wrap_style_, text_wrap_style_strings},
          {_word_break_, word_break_strings},
          {_overflow_wrap_, overflow_wrap_strings},
          {_word_wrap_, overflow_wrap_strings},

          {_break_before_, break_flow_strings},
          {_break_after_, break_flow_strings},
          {_break_inside_, break_inside_strings},

          {_font_style_, font_style_strings},
          {_font_variant_, font_variant_strings},
          {_font_weight_, font_weight_strings},

          {_list_style_type_, list_style_type_strings},
          {_list_style_position_, list_style_position_strings},

          {_border_left_style_, border_style_strings},
          {_border_right_style_, border_style_strings},
          {_border_top_style_, border_style_strings},
          {_border_bottom_style_, border_style_strings},
          {_border_inline_start_style_, border_style_strings},
          {_border_inline_end_style_, border_style_strings},
          {_border_block_start_style_, border_style_strings},
          {_border_block_end_style_, border_style_strings},
          {_column_rule_style_, border_style_strings},
          {_outline_style_, border_style_strings},
          {_border_collapse_, border_collapse_strings},
          {_table_layout_, table_layout_strings},

          {_background_attachment_, background_attachment_strings},
          {_background_repeat_, background_repeat_strings},
          {_background_clip_, background_box_strings},
          {_background_origin_, background_box_strings},

          {_flex_direction_, flex_direction_strings},
          {_flex_wrap_, flex_wrap_strings},
          {_justify_content_, flex_justify_content_strings},
          {_justify_self_, flex_justify_content_strings},
          {_justify_items_, flex_justify_content_strings},
          {_align_content_, flex_align_content_strings},
          {_align_items_, flex_align_items_strings},
          {_align_self_, flex_align_items_strings},

          {_caption_side_, caption_side_strings},
          {_object_fit_, object_fit_strings},
          {_image_rendering_, image_rendering_strings},

          {__webkit_box_orient_, box_orient_strings},
          {_text_decoration_style_, style_text_decoration_style_strings},
          {_text_decoration_skip_ink_, style_text_decoration_skip_ink_strings},
          {_text_underline_position_, style_text_underline_position_strings},
          {_text_emphasis_position_, style_text_emphasis_position_strings},
          {_text_overflow_, text_overflow_strings},
          {_stroke_linejoin_, stroke_linejoin_strings},
          {_box_decoration_break_, box_decoration_break_strings},
          {_line_break_, line_break_strings},
          {_text_align_last_, text_align_last_strings},
          {_text_justify_, text_justify_strings},
          {_hyphens_, hyphens_strings},
          {__webkit_hyphens_, hyphens_strings},
          {_font_optical_sizing_, font_optical_sizing_strings},
          {_font_synthesis_weight_, font_synthesis_value_strings},
          {_font_synthesis_style_, font_synthesis_value_strings},
          {_font_synthesis_small_caps_, font_synthesis_value_strings},
          {_font_synthesis_position_, font_synthesis_value_strings},
          {__webkit_box_decoration_break_, box_decoration_break_strings},
          {_text_combine_upright_, text_combine_upright_strings},
          {_container_type_, container_type_strings},
          {_mix_blend_mode_, blend_mode_strings},
          {_background_blend_mode_, blend_mode_strings},
          {_isolation_, isolation_strings},
  };
  std::map<string_id, vector<string_id>> shorthands =
      {
          {_font_, {_font_style_, _font_variant_, _font_variant_ligatures_, _font_variant_numeric_, _font_variant_east_asian_, _font_variant_caps_, _font_variant_position_, _font_feature_settings_, _font_stretch_, _font_weight_, _font_size_, _line_height_, _font_family_}},
          {_font_variant_, {_font_variant_ligatures_, _font_variant_numeric_, _font_variant_east_asian_, _font_variant_caps_, _font_variant_position_}},

          {_background_, {_background_color_, _background_position_x_, _background_position_y_, _background_repeat_, _background_attachment_, _background_image_, _background_image_baseurl_, _background_size_, _background_origin_, _background_clip_}},

          {_list_style_, {_list_style_image_, _list_style_image_baseurl_, _list_style_position_, _list_style_type_}},

          {_margin_, {_margin_top_, _margin_right_, _margin_bottom_, _margin_left_}},
          {_padding_, {_padding_top_, _padding_right_, _padding_bottom_, _padding_left_}},

          {_border_width_, {_border_top_width_, _border_right_width_, _border_bottom_width_, _border_left_width_}},
          {_border_style_, {_border_top_style_, _border_right_style_, _border_bottom_style_, _border_left_style_}},
          {_border_color_, {_border_top_color_, _border_right_color_, _border_bottom_color_, _border_left_color_}},
          {_border_top_, {_border_top_width_, _border_top_style_, _border_top_color_}},
          {_border_right_, {_border_right_width_, _border_right_style_, _border_right_color_}},
          {_border_bottom_, {_border_bottom_width_, _border_bottom_style_, _border_bottom_color_}},
          {_border_left_, {_border_left_width_, _border_left_style_, _border_left_color_}},
          {_border_, {_border_top_width_, _border_right_width_, _border_bottom_width_, _border_left_width_, _border_top_style_, _border_right_style_, _border_bottom_style_, _border_left_style_, _border_top_color_, _border_right_color_, _border_bottom_color_, _border_left_color_}},

          {_flex_, {_flex_grow_, _flex_shrink_, _flex_basis_}},
          {_flex_flow_, {_flex_direction_, _flex_wrap_}},
          {_gap_, {_row_gap_, _column_gap_}},
          {_grid_area_, {_grid_row_start_, _grid_column_start_, _grid_row_end_, _grid_column_end_}},
          {_grid_column_, {_grid_column_start_, _grid_column_end_}},
          {_grid_row_, {_grid_row_start_, _grid_row_end_}},
          {_grid_template_, {_grid_template_rows_, _grid_template_columns_, _grid_template_areas_}},
          {_grid_, {_grid_template_rows_, _grid_template_columns_, _grid_template_areas_, _grid_auto_rows_, _grid_auto_columns_, _grid_auto_flow_}},
          {_grid_gap_, {_row_gap_, _column_gap_}},
          {_grid_row_gap_, {_row_gap_}},
          {_grid_column_gap_, {_column_gap_}},
          {_outline_, {_outline_width_, _outline_style_, _outline_color_}},
          {_columns_, {_column_width_, _column_count_}},
          {_column_rule_, {_column_rule_width_, _column_rule_style_, _column_rule_color_}},

          {_text_decoration_, {_text_decoration_color_, _text_decoration_line_, _text_decoration_style_, _text_decoration_thickness_}},
          {_text_emphasis_, {_text_emphasis_style_, _text_emphasis_color_}},
          {__webkit_text_stroke_, {__webkit_text_stroke_width_, __webkit_text_stroke_color_}},
          {_container_, {_container_name_, _container_type_}},
          {_border_image_, {_border_image_source_, _border_image_slice_, _border_image_width_, _border_image_outset_, _border_image_repeat_}},
          // NOTE: the `offset` shorthand covers offset-position/path/distance/rotate only.
          // offset-anchor is NOT part of it and must not be reset by it.
          {_offset_, {_offset_position_, _offset_path_, _offset_distance_, _offset_rotate_}},
  };

  void style::add(const string &txt, const string &baseurl, document_container *container, int layer, selector_specificity specificity)
  {
    auto tokens = normalize(txt, f_componentize);
    add(tokens, baseurl, container, layer, specificity);
  }

  void style::add(const css_token_vector &tokens, const string &baseurl, document_container *container, int layer, selector_specificity specificity)
  {
    m_layer = layer;
    m_specificity = specificity;
    raw_declaration::vector decls;
    raw_rule::vector rules;
    css_parser(tokens).consume_style_block_contents(decls, rules);
    if (!rules.empty())
      css_parse_error("rule inside a style block");
    if (decls.empty())
      return;

    for (auto &decl : decls)
    {
      remove_whitespace(decl.value);
      string name = decl.name.substr(0, 2) == "--" ? decl.name : lowcase(decl.name);
      add_property(_id(name), decl.value, baseurl, decl.important, container, m_layer, m_specificity);
    }
  }

  bool has_var(const css_token_vector &tokens)
  {
    for (auto &tok : tokens)
    {
      if (tok.type == CV_FUNCTION && lowcase(tok.name) == "var")
        return true;
      if (tok.is_component_value() && has_var(tok.value))
        return true;
    }
    return false;
  }

  void style::inherit_property(string_id name, bool important)
  {
    auto atomic_properties = at(shorthands, name);
    if (!atomic_properties.empty())
    {
      for (auto atomic : atomic_properties)
        add_parsed_property(atomic, property_value(inherit(), important, false, m_layer, m_specificity));
    }
    else
      add_parsed_property(name, property_value(inherit(), important, false, m_layer, m_specificity));
  }

  // `initial` / `unset` are stored as their own value types (expanding shorthands like
  // `inherit` does); the inherited-vs-initial decision for `unset` happens in
  // element::get_property, where the property's inherited flag is known.
  void style::initial_property(string_id name, bool important)
  {
    auto atomic_properties = at(shorthands, name);
    if (!atomic_properties.empty())
    {
      for (auto atomic : atomic_properties)
        add_parsed_property(atomic, property_value(initial(), important, false, m_layer, m_specificity));
    }
    else
      add_parsed_property(name, property_value(initial(), important, false, m_layer, m_specificity));
  }

  void style::unset_property(string_id name, bool important)
  {
    auto atomic_properties = at(shorthands, name);
    if (!atomic_properties.empty())
    {
      for (auto atomic : atomic_properties)
        add_parsed_property(atomic, property_value(unset(), important, false, m_layer, m_specificity));
    }
    else
      add_parsed_property(name, property_value(unset(), important, false, m_layer, m_specificity));
  }

  void style::add_length_property(string_id name, css_token val, string keywords, int options, bool important)
  {
    css_length length;
    if (length.from_token(val, options, keywords))
      add_parsed_property(name, property_value(length, important, false, m_layer, m_specificity));
  }

  void style::add_property(string_id name, const css_token_vector &value, const string &baseurl, bool important, document_container *container, int layer, selector_specificity specificity, int order)
  {
    m_layer = layer;
    m_specificity = specificity;
    // When re-parsing a var() declaration (style::subst_vars), pin every
    // resulting property to the declaration's original cascade position so
    // the re-parse competes with the other declarations by source order
    // rather than by re-parse order. Restored on scope exit.
    struct forced_order_guard {
      style* s;
      int prev;
      ~forced_order_guard() { s->m_forced_order = prev; }
    } guard{this, m_forced_order};
    if (order >= 0) m_forced_order = order;
    if (value.empty() && _s(name).substr(0, 2) != "--")
      return;

    css_token_vector evaluated_value = value;
    evaluate_calc(evaluated_value, nullptr);
    evaluate_light_dark(evaluated_value, container);

    if (has_var(evaluated_value))
      return add_parsed_property(name, property_value(evaluated_value, important, true, m_layer, m_specificity, order));

    css_token val = evaluated_value.size() == 1 ? evaluated_value[0] : css_token();
    string ident = val.ident();

    if (ident == "inherit")
      return inherit_property(name, important);

    // CSS-wide keywords. `initial` resets to the property's initial value; `unset`
    // behaves as `inherit` for inherited properties and as `initial` otherwise. The
    // value type carries the keyword so the inherited/non-inherited decision is made
    // where the property's inherited flag is known (element::get_property).
    if (ident == "initial")
      return initial_property(name, important);
    if (ident == "unset")
      return unset_property(name, important);

    int idx[4];
    web_color clr[4];
    css_length len[4];
    string str;

    switch (name)
    {
    case _transform_:
    case _transform_origin_:
    case _translate_:
    case _rotate_:
    case _scale_:
    case _filter_:
    case _backdrop_filter_:
    case __webkit_backdrop_filter_:
    case _clip_path_:
    case _clip_rule_:
    case _object_position_:
    case _mask_:
    case __webkit_mask_:
    case _mask_image_:
    case __webkit_mask_image_:
    case _mask_size_:
    case __webkit_mask_size_:
    case _mask_repeat_:
    case __webkit_mask_repeat_:
    case _mask_position_:
    case __webkit_mask_position_:
    case _pointer_events_:
    case _user_select_:
    case __webkit_user_select_:
    case _scroll_behavior_:
    case _transform_style_:
    case _perspective_:
    case _backface_visibility_:
    case _offset_path_:
    case _offset_distance_:
    case _offset_rotate_:
    case _offset_anchor_:
    case _offset_position_:
    case _clip_:
      add_parsed_property(name, property_value(value, important, false, m_layer, m_specificity));
      break;

    case _place_items_:
    case _place_content_:
    case _place_self_:
      parse_place_shorthand(name, value, important);
      break;

    // Legacy CSS 2.1 page-break-* aliases. Remap onto break-* at parse time so the
    // layout/pagination code only ever sees the modern fragmentation properties.
    case _page_break_before_:
    case _page_break_after_:
    {
      if (int index = value_index(ident, page_break_flow_strings); index >= 0)
      {
        // auto->auto, always->page, avoid->avoid, left/right->page
        int mapped = (index == page_break_flow_auto)   ? break_flow_auto
                     : (index == page_break_flow_avoid) ? break_flow_avoid
                                                        : break_flow_page;
        string_id modern = (name == _page_break_before_) ? _break_before_ : _break_after_;
        add_parsed_property(modern, property_value(mapped, important, false, m_layer, m_specificity));
      }
      break;
    }
    case _page_break_inside_:
      if (int index = value_index(ident, page_break_inside_strings); index >= 0)
        add_parsed_property(_break_inside_,
                            property_value(index, important, false, m_layer, m_specificity));
      break;

    // Orphans / widows: positive <integer> (CSS Fragmentation). Values < 1 are
    // rejected at parse time and fall back to the initial value (2).
    case _orphans_:
    case _widows_:
      if (val.type == NUMBER && val.n.number_type == css_number_integer && (int)val.n.number >= 1)
        add_parsed_property(name, property_value((int)val.n.number, important, false, m_layer, m_specificity));
      break;

    // white-space is expanded into its CSS Text 4 longhands here, so that a later
    // white-space-collapse / text-wrap-mode declaration in the same block overrides
    // only the half it names (declaration order is preserved).
    case _white_space_:
    {
      int index = value_index(ident, white_space_strings);
      if (index < 0) break;
      add_parsed_property(name, property_value(index, important, false, m_layer, m_specificity));
      int collapse = white_space_collapse_collapse;
      int mode = text_wrap_mode_wrap;
      switch (index)
      {
        case white_space_nowrap: mode = text_wrap_mode_nowrap; break;
        case white_space_pre: collapse = white_space_collapse_preserve; mode = text_wrap_mode_nowrap; break;
        case white_space_pre_line: collapse = white_space_collapse_preserve_breaks; break;
        case white_space_pre_wrap: collapse = white_space_collapse_preserve; break;
        case white_space_break_spaces: collapse = white_space_collapse_break_spaces; break;
        default: break;
      }
      add_parsed_property(_white_space_collapse_,
                          property_value(collapse, important, false, m_layer, m_specificity));
      add_parsed_property(_text_wrap_mode_,
                          property_value(mode, important, false, m_layer, m_specificity));
      break;
    }

    // text-wrap = <text-wrap-mode> || <text-wrap-style>. `balance`/`pretty` imply
    // wrapping, `nowrap` only changes the mode.
    case _text_wrap_:
    {
      int index = value_index(ident, text_wrap_strings);
      if (index < 0) break;
      add_parsed_property(name, property_value(index, important, false, m_layer, m_specificity));
      int mode = text_wrap_mode_wrap;
      int wrap_style = text_wrap_style_auto;
      if (index == text_wrap_nowrap) mode = text_wrap_mode_nowrap;
      else if (index == text_wrap_balance) wrap_style = text_wrap_style_balance;
      else if (index == text_wrap_pretty) wrap_style = text_wrap_style_pretty;
      add_parsed_property(_text_wrap_mode_,
                          property_value(mode, important, false, m_layer, m_specificity));
      add_parsed_property(_text_wrap_style_,
                          property_value(wrap_style, important, false, m_layer, m_specificity));
      break;
    }

    case _display_:
    case _direction_:
    case _unicode_bidi_:
    case _writing_mode_:
    case _text_orientation_:
    case _visibility_:
    case _position_:
    case _float_:
    case _clear_:
    case _appearance_:
    case _box_sizing_:
    case _overflow_x_:
    case _overflow_y_:
    case _text_overflow_:
    case _stroke_linejoin_:
    case _box_decoration_break_:
    case __webkit_box_decoration_break_:
    case _line_break_:
    case _text_align_last_:
    case _text_justify_:
    case _hyphens_:
    case __webkit_hyphens_:
    case _font_optical_sizing_:
    case _font_synthesis_weight_:
    case _font_synthesis_style_:
    case _font_synthesis_small_caps_:
    case _font_synthesis_position_:
    case _text_align_:
    case _vertical_align_:
    case _text_transform_:
    case _white_space_collapse_:
    case _text_wrap_mode_:
    case _text_wrap_style_:
    case _word_break_:
    case _text_combine_upright_:
    case _overflow_wrap_:
    case _word_wrap_:
    case _break_before_:
    case _break_after_:
    case _break_inside_:
    case _font_style_:
    case _text_decoration_style_:
    case _text_decoration_skip_ink_:
    case _text_underline_position_:
    case _list_style_type_:
    case _list_style_position_:
    case _border_top_style_:
    case _border_bottom_style_:
    case _border_left_style_:
    case _border_right_style_:
    case _border_inline_start_style_:
    case _border_inline_end_style_:
    case _border_block_start_style_:
    case _border_block_end_style_:
    case _border_collapse_:
    case _table_layout_:
    case _flex_direction_:
    case _flex_wrap_:
    case _justify_content_:
    case _justify_self_:
    case _justify_items_:
    case _align_content_:
    case _caption_side_:
    case _object_fit_:
    case _image_rendering_:
    case __webkit_box_orient_:
    case _container_type_:
    case _mix_blend_mode_:
    case _background_blend_mode_:
    case _isolation_:
      if (int index = value_index(ident, m_valid_values[name]); index >= 0)



        add_parsed_property(name, property_value(index, important, false, m_layer, m_specificity));
      break;

    case _z_index_:
      return add_length_property(name, val, "auto", f_integer, important);

    case _text_indent_:
      return add_length_property(name, val, "", f_length_percentage, important);

    case _padding_left_:
    case _padding_right_:
    case _padding_top_:
    case _padding_bottom_:
      return add_length_property(name, val, "", f_length_percentage | f_positive, important);

    case _left_:
    case _right_:
    case _top_:
    case _bottom_:
    case _margin_left_:
    case _margin_right_:
    case _margin_top_:
    case _margin_bottom_:
      return add_length_property(name, val, "auto", f_length_percentage, important);

    case _width_:
    case _inline_size_:
      // intrinsic sizing keywords: predef 1 fit-content, 2 max-content, 3 min-content (0 = auto)
      return add_length_property(name, val, "auto;fit-content;max-content;min-content", f_length_percentage | f_positive, important);

    case _height_:
    case _block_size_:
      // intrinsic sizing keywords lay out like auto, but a box with one is not stretched
      return add_length_property(name, val, "auto;fit-content;max-content;min-content", f_length_percentage | f_positive, important);

    case _min_width_:
    case _min_height_:
    case _min_inline_size_:
    case _min_block_size_:
      return add_length_property(name, val, "auto", f_length_percentage | f_positive, important);

    case _max_width_:
    case _max_height_:
    case _max_inline_size_:
    case _max_block_size_:
      return add_length_property(name, val, "none", f_length_percentage | f_positive, important);

    case _letter_spacing_:
    case _word_spacing_:
      return add_length_property(name, val, "normal", f_length, important);

    case _line_height_:
      return add_length_property(name, val, "normal", f_number | f_length_percentage | f_positive, important);

    case _font_size_:
      return add_length_property(name, val, font_size_strings, f_length_percentage | f_positive, important);

    case _margin_inline_:
      if (int n = parse_1234_lengths(value, len, f_length_percentage, "auto"))
        add_two_properties(_margin_inline_start_, len, n, important);
      break;
    case _margin_block_:
      if (int n = parse_1234_lengths(value, len, f_length_percentage, "auto"))
        add_two_properties(_margin_block_start_, len, n, important);
      break;
    case _padding_inline_:
      if (int n = parse_1234_lengths(value, len, f_length_percentage | f_positive))
        add_two_properties(_padding_inline_start_, len, n, important);
      break;
    case _padding_block_:
      if (int n = parse_1234_lengths(value, len, f_length_percentage | f_positive))
        add_two_properties(_padding_block_start_, len, n, important);
      break;
    case _inset_inline_:
      if (int n = parse_1234_lengths(value, len, f_length_percentage, "auto"))
        add_two_properties(_inset_inline_start_, len, n, important);
      break;
    case _inset_block_:
      if (int n = parse_1234_lengths(value, len, f_length_percentage, "auto"))
        add_two_properties(_inset_block_start_, len, n, important);
      break;

    case _margin_inline_start_:
    case _margin_inline_end_:
    case _margin_block_start_:
    case _margin_block_end_:
    case _padding_inline_start_:
    case _padding_inline_end_:
    case _padding_block_start_:
    case _padding_block_end_:
    case _inset_inline_start_:
    case _inset_inline_end_:
    case _inset_block_start_:
    case _inset_block_end_:
      return add_length_property(name, val, "auto", f_length_percentage, important);

    case _border_inline_:
      parse_border_side(_border_inline_start_, value, important, container);
      parse_border_side(_border_inline_end_, value, important, container);
      break;
    case _border_block_:
      parse_border_side(_border_block_start_, value, important, container);
      parse_border_side(_border_block_end_, value, important, container);
      break;

    case _border_inline_width_:
    case _border_inline_style_:
    case _border_inline_color_:
    case _border_block_width_:
    case _border_block_style_:
    case _border_block_color_:
      add_parsed_property(name, property_value(value, important, false, m_layer, m_specificity));
      break;

    case _border_start_start_radius_:
      add_property(_border_top_left_radius_, value, baseurl, important, container, layer, specificity);
      return;
    case _border_start_end_radius_:
      add_property(_border_top_right_radius_, value, baseurl, important, container, layer, specificity);
      return;
    case _border_end_start_radius_:
      add_property(_border_bottom_left_radius_, value, baseurl, important, container, layer, specificity);
      return;
    case _border_end_end_radius_:
      add_property(_border_bottom_right_radius_, value, baseurl, important, container, layer, specificity);
      return;

    case _inset_:
      if (int n = parse_1234_lengths(value, len, f_length_percentage, "auto"))
        add_four_properties(_top_, len, n, important);
      return;

    case _margin_:
      if (int n = parse_1234_lengths(value, len, f_length_percentage, "auto"))
        add_four_properties(_margin_top_, len, n, important);
      break;

    case _padding_:
      if (int n = parse_1234_lengths(value, len, f_length_percentage | f_positive))
        add_four_properties(_padding_top_, len, n, important);
      break;

    case _color_:
      if (ident == "currentcolor")
        return inherit_property(name, important);
    case _background_color_:
    case _border_top_color_:
    case _border_bottom_color_:
    case _border_left_color_:
    case _border_right_color_:
    case _border_inline_start_color_:
    case _border_inline_end_color_:
    case _border_block_start_color_:
    case _border_block_end_color_:
    case __webkit_text_fill_color_:
    case __webkit_text_stroke_color_:
      if (ident == "transparent")
      {
        web_color transparent_color(0, 0, 0, 0);
        add_parsed_property(name, property_value(transparent_color, important, false, m_layer, m_specificity));
      }
      else if (parse_color(val, *clr, container))
        add_parsed_property(name, property_value(*clr, important, false, m_layer, m_specificity));
      break;

    case _background_:
      parse_background(value, baseurl, important, container);
      break;

    case _background_image_:
      parse_background_image(value, baseurl, important, container);
      break;

    case _background_position_:
      parse_background_position(value, important);
      break;

    case _background_size_:
      parse_background_size(value, important);
      break;

    case _background_repeat_:
    case _background_attachment_:
    case _background_origin_:
    case _background_clip_:
      parse_keyword_comma_list(name, value, important);
      break;

    case __webkit_background_clip_:
      parse_keyword_comma_list(_background_clip_, value, important);
      break;

    case _border_:
      parse_border(value, important, container);
      break;

    case _border_left_:
    case _border_right_:
    case _border_top_:
    case _border_bottom_:
    case _border_inline_start_:
    case _border_inline_end_:
    case _border_block_start_:
    case _border_block_end_:
      parse_border_side(name, value, important, container);
      break;

    case _border_width_:
      if (int n = parse_1234_values(value, len, parse_border_width))
        add_four_properties(_border_top_width_, len, n, important);
      break;
    case _border_style_:
      if (int n = parse_1234_values(value, idx, parse_keyword, (string)border_style_strings, 0))
        add_four_properties(_border_top_style_, idx, n, important);
      break;
    case _border_color_:
      if (int n = parse_1234_values(value, clr, parse_color, container))
        add_four_properties(_border_top_color_, clr, n, important);
      break;

    case _border_top_width_:
    case _border_bottom_width_:
    case _border_left_width_:
    case _border_right_width_:
    case _border_inline_start_width_:
    case _border_inline_end_width_:
    case _border_block_start_width_:
    case _border_block_end_width_:
      if (parse_border_width(val, *len))
        add_parsed_property(name, property_value(*len, important, false, m_layer, m_specificity));
      break;

    case _border_bottom_left_radius_:
    case _border_bottom_right_radius_:
    case _border_top_right_radius_:
    case _border_top_left_radius_:
      if (parse_two_lengths(value, len, f_length_percentage | f_positive))
      {
        add_parsed_property(_id(_s(name) + "-x"), property_value(len[0], important, false, m_layer, m_specificity));
        add_parsed_property(_id(_s(name) + "-y"), property_value(len[1], important, false, m_layer, m_specificity));
      }
      break;

    case _border_radius_x_:
    case _border_radius_y_:
    {
      string_id top_left = name == _border_radius_x_ ? _border_top_left_radius_x_ : _border_top_left_radius_y_;

      if (int n = parse_1234_lengths(value, len, f_length_percentage | f_positive))
        add_four_properties(top_left, len, n, important);
      break;
    }

    case _border_radius_:
      parse_border_radius(value, important);
      break;

    case _border_image_:
      parse_border_image(value, baseurl, important, container);
      break;

    case _border_image_source_:
    {
      image img;
      if (parse_bg_image(val, img, container))
      {
        add_parsed_property(name, property_value(img, important, false, m_layer, m_specificity));
        add_parsed_property(_id(_s(name) + "-baseurl"), property_value(baseurl, important, false, m_layer, m_specificity));
      }
      break;
    }

    case _border_image_slice_:
    {
      css_length slices[4];
      bool fill = false;
      css_token_vector slice_tokens;
      for (const auto& t : value)
      {
        if (t.ident() == "fill") fill = true;
        else slice_tokens.push_back(t);
      }
      if (int n = parse_1234_lengths(slice_tokens, slices, f_length_percentage | f_positive))
      {
        add_four_properties(_border_image_slice_top_, slices, n, important);
        add_parsed_property(_id("border-image-slice-fill"), property_value(fill ? 1 : 0, important, false, m_layer, m_specificity));
      }
      break;
    }

    case _border_image_width_:
      if (int n = parse_1234_lengths(value, len, f_length_percentage | f_number | f_positive, "auto"))
        add_four_properties(_border_image_width_top_, len, n, important);
      break;

    case _border_image_outset_:
      if (int n = parse_1234_lengths(value, len, f_length | f_number | f_positive))
        add_four_properties(_border_image_outset_top_, len, n, important);
      break;

    case _border_image_repeat_:
      parse_border_image_repeat(value, important);
      break;

    case _border_spacing_:
      if (parse_two_lengths(value, len, f_length | f_positive))
      {
        add_parsed_property(__litehtml_border_spacing_x_, property_value(len[0], important, false, m_layer, m_specificity));
        add_parsed_property(__litehtml_border_spacing_y_, property_value(len[1], important, false, m_layer, m_specificity));
      }
      break;

    case _list_style_image_:
      if (string url; parse_list_style_image(val, url))
      {
        add_parsed_property(_list_style_image_, property_value(url, important, false, m_layer, m_specificity));
        add_parsed_property(_list_style_image_baseurl_, property_value(baseurl, important, false, m_layer, m_specificity));
      }
      break;

    case _outline_:
    case _column_rule_:
      parse_border_side(name, value, important, container);
      break;

    case _outline_offset_:
      return add_length_property(name, val, "0", f_length, important);

    case _list_style_:
      parse_list_style(value, baseurl, important);
      break;

    case _font_:
      parse_font(value, important);
      break;

    case _font_variant_:
      parse_font_variant(value, important);
      break;

    case _font_variant_ligatures_:
    case _font_variant_numeric_:
    case _font_variant_east_asian_:
    case _font_variant_caps_:
    case _font_variant_position_:
      parse_font_variant_longhand(name, value, important);
      break;

    case _font_feature_settings_:
      parse_font_feature_settings(value, important);
      break;

    case _font_kerning_:
      parse_font_kerning(value, important);
      break;

    case _font_variation_settings_:
      parse_font_variation_settings(value, important);
      break;

    case _font_stretch_:
      parse_font_stretch(value, important);
      break;

    case _font_family_:
      if (parse_font_family(value, str))
        add_parsed_property(name, property_value(str, important, false, m_layer, m_specificity));
      break;

    case _font_weight_:
      if (parse_font_weight(val, *len))
        add_parsed_property(name, property_value(*len, important, false, m_layer, m_specificity));
      break;

    case _text_decoration_:
      parse_text_decoration(value, important, container);
      break;

    case _text_underline_offset_:
      add_length_property(name, val, "auto", f_length_percentage, important);
      break;

    case _text_decoration_thickness_:
      add_length_property(name, val, style_text_decoration_thickness_strings, f_length_percentage | f_positive, important);
      break;

    case _text_decoration_color_:
      parse_text_decoration_color(val, important, container);
      break;

    case _text_decoration_line_:
      parse_text_decoration_line(value, important);
      break;

    case _text_emphasis_:
      parse_text_emphasis(value, important, container);
      break;

    case __webkit_text_stroke_:
      parse_text_stroke(value, important, container);
      break;

    case _text_emphasis_style_:
      str = get_repr(value, 0, -1, true);
      add_parsed_property(name, property_value(str, important, false, m_layer, m_specificity));
      break;

    case _text_emphasis_color_:
      parse_text_emphasis_color(val, important, container);
      break;

    case _text_emphasis_position_:
      parse_text_emphasis_position(value, important);
      break;

    case _flex_:
      parse_flex(value, important);
      break;

    case _opacity_:
    {
      css_length length;
      if (length.from_token(val, f_number | f_percentage))
      {
        float opacity = 1.0f;
        if (length.units() == css_units_percentage)
          opacity = length.val() / 100.0f;
        else
          opacity = length.val();
        if (opacity < 0)
          opacity = 0;
        if (opacity > 1)
          opacity = 1;
        add_parsed_property(name, property_value(opacity, important, false, m_layer, m_specificity));
      }
      break;
    }

    case _flex_grow_:
    case _flex_shrink_:
      if (val.type == NUMBER && val.n.number >= 0)
        add_parsed_property(name, property_value(val.n.number, important, false, m_layer, m_specificity));
      break;

    case _flex_basis_:
      add_length_property(name, val, flex_basis_strings, f_length_percentage | f_positive, important);
      break;

    case _flex_flow_:
      parse_flex_flow(value, important);
      break;

    case _align_items_:
    case _align_self_:
      parse_align_self(name, value, important);
      break;

    case _grid_template_columns_:
    case _grid_template_rows_:
    case _grid_auto_columns_:
    case _grid_auto_rows_:
      parse_grid_template(name, value, important);
      break;

    case _grid_auto_flow_:
    {
      // grid-auto-flow: row | column, optionally `dense`.
      int flow = -1;
      bool dense = false;
      for (const auto& tok : value)
      {
        if (tok.type != IDENT) continue;
        string id = lowcase(tok.ident());
        if (id == "row") flow = grid_auto_flow_row;
        else if (id == "column") flow = grid_auto_flow_column;
        else if (id == "dense") dense = true;
      }
      // A lone `dense` means `row dense`.
      if (flow < 0 && dense) flow = grid_auto_flow_row;
      if (flow >= 0)
      {
        int mapped = dense ? (flow == grid_auto_flow_column ? grid_auto_flow_column_dense
                                                            : grid_auto_flow_row_dense)
                           : flow;
        add_parsed_property(name, property_value(mapped, important, false, m_layer, m_specificity));
      }
      break;
    }

    case _grid_template_:
    case _grid_:
    {
      // grid-template: none | <rows> / <columns> | [<line-names>? <string> <track-size>? <line-names>?]+ [/ <columns>]
      // grid:          <grid-template> | <rows> / auto-flow [dense] <auto-columns>? | auto-flow [dense] <auto-rows>? / <columns>
      css_token_vector left, right;
      bool slash = false;
      for (const auto &tok : value)
      {
        if (tok.type == WHITESPACE) continue;
        if (tok.ch == '/' && !slash) { slash = true; continue; }
        (slash ? right : left).push_back(tok);
      }
      auto is_ident = [](const css_token &t, const char *id) { return t.type == IDENT && lowcase(t.ident()) == id; };
      // Take `auto-flow` / `dense` off the front of a token list; returns true when auto-flow was found.
      auto take_auto_flow = [&](css_token_vector &tokens, bool &dense) -> bool
      {
        bool found = false;
        css_token_vector rest;
        for (const auto &t : tokens)
        {
          if (is_ident(t, "auto-flow")) found = true;
          else if (is_ident(t, "dense")) dense = true;
          else rest.push_back(t);
        }
        if (found) tokens = rest;
        return found;
      };
      auto set_tracks = [&](string_id id, const css_token_vector &tokens)
      {
        if (tokens.empty() || (tokens.size() == 1 && is_ident(tokens[0], "none")))
          add_parsed_property(id, property_value(length_vector(), important, false, m_layer, m_specificity));
        else
          parse_grid_template(id, tokens, important);
      };
      auto set_flow = [&](bool column, bool dense)
      {
        int flow = column ? (dense ? grid_auto_flow_column_dense : grid_auto_flow_column)
                          : (dense ? grid_auto_flow_row_dense : grid_auto_flow_row);
        add_parsed_property(_grid_auto_flow_, property_value(flow, important, false, m_layer, m_specificity));
      };
      auto set_areas = [&](const css_token_vector &areas)
      {
        css_token_vector tokens = areas;
        if (tokens.empty()) tokens.push_back(css_token(IDENT, string("none")));
        add_parsed_property(_grid_template_areas_, property_value(tokens, important, false, m_layer, m_specificity));
      };
      auto apply_template = [&]()
      {
        bool has_strings = false;
        for (const auto &t : left) has_strings = has_strings || t.type == STRING;
        if (has_strings)
        {
          // Each row: <string> followed by an optional track size (default auto).
          css_token_vector areas, rows;
          for (size_t i = 0; i < left.size(); i++)
          {
            if (left[i].type == STRING)
            {
              areas.push_back(left[i]);
              const bool has_size = i + 1 < left.size() && left[i + 1].type != STRING && left[i + 1].type != '[';
              if (!has_size) rows.push_back(css_token(IDENT, string("auto")));
            }
            else
            {
              rows.push_back(left[i]);
            }
          }
          set_areas(areas);
          set_tracks(_grid_template_rows_, rows);
        }
        else
        {
          set_areas(css_token_vector());
          set_tracks(_grid_template_rows_, left);
        }
        set_tracks(_grid_template_columns_, right);
      };

      if (name == _grid_template_)
      {
        apply_template();
        break;
      }

      bool dense = false;
      css_token_vector left_copy = left, right_copy = right;
      if (take_auto_flow(left_copy, dense))
      {
        // auto-flow [dense] <auto-rows>? / <template-columns>
        set_flow(false, dense);
        set_tracks(_grid_auto_rows_, left_copy);
        set_tracks(_grid_auto_columns_, css_token_vector());
        set_tracks(_grid_template_columns_, right);
        set_tracks(_grid_template_rows_, css_token_vector());
        set_areas(css_token_vector());
      }
      else if (take_auto_flow(right_copy, dense))
      {
        // <template-rows> / auto-flow [dense] <auto-columns>?
        set_flow(true, dense);
        set_tracks(_grid_auto_columns_, right_copy);
        set_tracks(_grid_auto_rows_, css_token_vector());
        set_tracks(_grid_template_rows_, left);
        set_tracks(_grid_template_columns_, css_token_vector());
        set_areas(css_token_vector());
      }
      else
      {
        apply_template();
        set_flow(false, false);
        set_tracks(_grid_auto_rows_, css_token_vector());
        set_tracks(_grid_auto_columns_, css_token_vector());
      }
      break;
    }

    case _grid_column_start_:
    case _grid_column_end_:
    case _grid_row_start_:
    case _grid_row_end_:
    case _grid_template_areas_:
      add_parsed_property(name, property_value(value, important, false, m_layer, m_specificity));
      break;

    case _overflow_:
    {
      // overflow: <x> [<y>]
      css_token_vector axes;
      for (const auto& tok : value)
        if (tok.type != WHITESPACE) axes.push_back(tok);
      if (axes.size() == 2 && axes[0].type == IDENT && axes[1].type == IDENT)
      {
        int ix = value_index(lowcase(axes[0].ident()), m_valid_values[name]);
        int iy = value_index(lowcase(axes[1].ident()), m_valid_values[name]);
        if (ix >= 0 && iy >= 0)
        {
          add_parsed_property(_overflow_, property_value(ix, important, false, m_layer, m_specificity));
          add_parsed_property(_overflow_x_, property_value(ix, important, false, m_layer, m_specificity));
          add_parsed_property(_overflow_y_, property_value(iy, important, false, m_layer, m_specificity));
        }
      }
      else if (int index = value_index(ident, m_valid_values[name]); index >= 0)
        add_parsed_property(name, property_value(index, important, false, m_layer, m_specificity));
      break;
    }

    case _grid_area_:
    {
      // grid-area: <row-start> [/ <column-start> [/ <row-end> [/ <column-end>]]]
      std::vector<css_token_vector> parts;
      css_token_vector cur;
      for (const auto& tok : value)
      {
        if (tok.ch == '/') { parts.push_back(cur); cur.clear(); }
        else if (tok.type != WHITESPACE) cur.push_back(tok);
      }
      parts.push_back(cur);
      auto is_name = [](const css_token_vector& v) {
        if (v.size() != 1 || v[0].type != IDENT) return false;
        string id = lowcase(v[0].ident());
        return id != "auto" && id != "span";
      };
      css_token_vector rs = parts[0], cs, re, ce;
      if (parts.size() > 1) cs = parts[1]; else if (is_name(rs)) cs = rs;
      if (parts.size() > 2) re = parts[2]; else if (is_name(rs)) re = rs;
      if (parts.size() > 3) ce = parts[3]; else if (is_name(cs)) ce = cs;
      if (!rs.empty()) add_property(_grid_row_start_, rs, baseurl, important, container, layer, specificity);
      if (!cs.empty()) add_property(_grid_column_start_, cs, baseurl, important, container, layer, specificity);
      if (!re.empty()) add_property(_grid_row_end_, re, baseurl, important, container, layer, specificity);
      if (!ce.empty()) add_property(_grid_column_end_, ce, baseurl, important, container, layer, specificity);
      return;
    }

    case _column_rule_width_:
    case _outline_width_:
    case __webkit_text_stroke_width_:
      if (parse_border_width(val, *len))
        add_parsed_property(name, property_value(*len, important, false, m_layer, m_specificity));
      break;

    case _column_rule_style_:
    case _outline_style_:
      if (int index = value_index(ident, m_valid_values[name]); index >= 0)
        add_parsed_property(name, property_value(index, important, false, m_layer, m_specificity));
      break;

    case _column_rule_color_:
    case _outline_color_:
      if (parse_color(val, *clr, container))
        add_parsed_property(name, property_value(*clr, important, false, m_layer, m_specificity));
      break;

    case _grid_column_:
    case _grid_row_:
    {
      std::vector<css_token_vector> tokens_list;
      css_token_vector current;
      for (const auto& tok : value)
      {
        if (tok.ch == '/')
        {
          tokens_list.push_back(current);
          current.clear();
        }
        else
        {
          current.push_back(tok);
        }
      }
      tokens_list.push_back(current);

      if (tokens_list.size() >= 1)
      {
        string_id start_id = (name == _grid_column_ ? _grid_column_start_ : _grid_row_start_);
        string_id end_id = (name == _grid_column_ ? _grid_column_end_ : _grid_row_end_);
        
        add_property(start_id, tokens_list[0], baseurl, important, container, layer, specificity);
        if (tokens_list.size() >= 2)
          add_property(end_id, tokens_list[1], baseurl, important, container, layer, specificity);
        else
        {
          // A lone <custom-ident> sets the end line to the same ident (which
          // resolves to the <ident>-end line), per the grid-column/grid-row
          // shorthand rules -- grid-column: extended-full must span to the
          // extended-full-end line, not stop after one track. Any other
          // single value (a number, span, auto) leaves the end auto.
          css_token_vector single;
          for (const auto& tok : tokens_list[0])
            if (tok.type != WHITESPACE) single.push_back(tok);
          if (single.size() == 1 && single[0].type == IDENT)
          {
            string id = lowcase(single[0].ident());
            if (id != "auto" && id != "span")
              add_property(end_id, tokens_list[0], baseurl, important, container, layer, specificity);
          }
        }
      }
      return;
    }

    case _column_gap_:
    case _row_gap_:
    case _grid_column_gap_:
    case _grid_row_gap_:
      return add_length_property(name, val, "normal", f_length_percentage | f_positive, important);

    case _gap_:
    case _grid_gap_:
      if (parse_two_lengths(value, len, f_length_percentage | f_positive))
      {
        add_parsed_property(_row_gap_, property_value(len[0], important, false, m_layer, m_specificity));
        add_parsed_property(_column_gap_, property_value(len[1], important, false, m_layer, m_specificity));
      }
      break;

    case _text_shadow_:
    case _box_shadow_:
      parse_shadow(name, value, important, container);
      break;

    case _order_:
    case _line_clamp_:
    case __webkit_line_clamp_:
      if (val.type == NUMBER && val.n.number_type == css_number_integer)
        add_parsed_property(name, property_value((int)val.n.number, important, false, m_layer, m_specificity));
      else if (ident == "none")
        add_parsed_property(name, property_value(0, important, false, m_layer, m_specificity));
      break;

    case _hyphenate_character_:
      // auto | <string>; "auto" is stored as the empty string.
      if (val.type == STRING)
        add_parsed_property(name, property_value(val.str, important, false, m_layer, m_specificity));
      else if (ident == "auto")
        add_parsed_property(name, property_value(string(), important, false, m_layer, m_specificity));
      break;

    case _tab_size_:
      return add_length_property(name, val, "", f_length | f_number | f_positive, important);

    case _font_synthesis_:
    {
      // none | [ weight || style || small-caps || position ]
      bool weight = false, style = false, caps = false, position = false, ok = !value.empty();
      for (const auto &t : value)
      {
        string id = t.type == IDENT ? lowcase(t.ident()) : string();
        if (id == "weight") weight = true;
        else if (id == "style") style = true;
        else if (id == "small-caps") caps = true;
        else if (id == "position") position = true;
        else if (id != "none") ok = false;
      }
      if (ok)
      {
        const int none = 1, autov = 0;
        add_parsed_property(_font_synthesis_weight_, property_value(weight ? autov : none, important, false, m_layer, m_specificity));
        add_parsed_property(_font_synthesis_style_, property_value(style ? autov : none, important, false, m_layer, m_specificity));
        add_parsed_property(_font_synthesis_small_caps_, property_value(caps ? autov : none, important, false, m_layer, m_specificity));
        add_parsed_property(_font_synthesis_position_, property_value(position ? autov : none, important, false, m_layer, m_specificity));
      }
      break;
    }

    case _scrollbar_gutter_:
    {
      // auto | stable | stable both-edges ("both-edges" alone is invalid).
      int mode = -1;
      bool both = false, ok = !value.empty();
      for (const auto &t : value)
      {
        if (t.type == IDENT)
        {
          string id = lowcase(t.ident());
          if (id == "auto") mode = scrollbar_gutter_auto;
          else if (id == "stable") mode = scrollbar_gutter_stable;
          else if (id == "both-edges") both = true;
          else ok = false;
        }
        else ok = false;
      }
      if (ok && mode >= 0)
        add_parsed_property(name, property_value(both && mode == scrollbar_gutter_stable
                                                     ? (int)scrollbar_gutter_stable_both_edges : mode,
                                                 important, false, m_layer, m_specificity));
      break;
    }

    case _text_fit_:
    {
      // none | grow | shrink | grow shrink, optionally followed by a percentage limit.
      int mode = 0, limit = 0;
      bool ok = !value.empty();
      for (const auto &t : value)
      {
        if (t.type == IDENT)
        {
          string id = lowcase(t.ident());
          if (id == "grow") mode |= 1;
          else if (id == "shrink") mode |= 2;
          else if (id == "none") mode = 0;
          else if (id != "consistent" && id != "per-line" && id != "per-line-all") ok = false;
        }
        else if (t.type == PERCENTAGE && t.n.number > 0) limit = (int)t.n.number;
        else ok = false;
      }
      if (ok)
        add_parsed_property(name, property_value(mode | (limit << 2), important, false, m_layer, m_specificity));
      break;
    }

    case _corner_shape_:
    case _corner_top_left_shape_:
    case _corner_top_right_shape_:
    case _corner_bottom_right_shape_:
    case _corner_bottom_left_shape_:
    {
      // <corner-shape-value> = round | scoop | bevel | notch | square | squircle | superellipse(<number>)
      // Stored per corner as "<keyword index>:<superellipse parameter>".
      auto one = [&](const css_token &t, string &out) -> bool
      {
        int kw = -1;
        float param = 0;
        if (t.type == IDENT)
        {
          kw = value_index(lowcase(t.ident()), corner_shape_strings, -1);
        }
        else if (t.type == CV_FUNCTION && lowcase(t.name) == "superellipse" && t.value.size() == 1)
        {
          const css_token &a = t.value[0];
          if (a.type == NUMBER) { kw = corner_shape_superellipse; param = a.n.number; }
          else if (a.type == IDENT && lowcase(a.ident()) == "infinity") { kw = corner_shape_superellipse; param = 8; }
          else if (a.type == IDENT && lowcase(a.ident()) == "-infinity") { kw = corner_shape_superellipse; param = -8; }
        }
        if (kw < 0) return false;
        out = std::to_string(kw) + ":" + std::to_string(param);
        return true;
      };
      static const string_id corner_ids[4] = {_corner_top_left_shape_, _corner_top_right_shape_,
                                              _corner_bottom_right_shape_, _corner_bottom_left_shape_};
      if (name == _corner_shape_)
      {
        // 1-4 values, expanded like the margin shorthand (tl, tr, br, bl).
        string v[4];
        size_t n = 0;
        bool ok = !value.empty() && value.size() <= 4;
        for (size_t k = 0; ok && k < value.size(); k++) ok = one(value[k], v[n++]);
        if (ok)
        {
          string tl = v[0];
          string tr = n >= 2 ? v[1] : tl;
          string br = n >= 3 ? v[2] : tl;
          string bl = n >= 4 ? v[3] : (n >= 2 ? tr : tl);
          const string vals[4] = {tl, tr, br, bl};
          for (int k = 0; k < 4; k++)
            add_parsed_property(corner_ids[k], property_value(vals[k], important, false, m_layer, m_specificity));
        }
      }
      else
      {
        string v;
        if (value.size() == 1 && one(value[0], v))
          add_parsed_property(name, property_value(v, important, false, m_layer, m_specificity));
      }
      break;
    }

    case _max_lines_:
      if (val.type == NUMBER && val.n.number_type == css_number_integer && val.n.number >= 0)
        add_parsed_property(name, property_value((int)val.n.number, important, false, m_layer, m_specificity));
      else if (ident == "none")
        add_parsed_property(name, property_value(0, important, false, m_layer, m_specificity));
      break;

    case _block_ellipsis_:
      if (val.type == STRING)
        add_parsed_property(name, property_value(val.str, important, false, m_layer, m_specificity));
      else if (ident == "auto")
        add_parsed_property(name, property_value(string("\xE2\x80\xA6"), important, false, m_layer, m_specificity));
      else if (ident == "no-ellipsis")
        add_parsed_property(name, property_value(string(), important, false, m_layer, m_specificity));
      break;

    case _column_count_:
      if (val.type == NUMBER && val.n.number_type == css_number_integer)
        add_parsed_property(name, property_value((int)val.n.number, important, false, m_layer, m_specificity));
      else if (ident == "auto")
        add_parsed_property(name, property_value(0, important, false, m_layer, m_specificity));
      break;

    case _column_width_:
      return add_length_property(name, val, "auto", f_length | f_positive, important);

    case _columns_:
    {
      // `columns: <column-width> || <column-count>` — either order, `auto` = both auto.
      if (value.size() == 1 && value[0].type == IDENT && lowcase(value[0].name) == "auto")
      {
        add_parsed_property(_column_width_, property_value(css_length::predef_value(0), important, false, m_layer, m_specificity));
        add_parsed_property(_column_count_, property_value(0, important, false, m_layer, m_specificity));
        break;
      }
      if (value.size() > 2) break;
      css_length width;
      bool have_width = false, width_is_auto = false;
      int count = 0;
      bool have_count = false, ok = true;
      for (const auto& tok : value)
      {
        if (tok.type == NUMBER && tok.n.number_type == css_number_integer && !have_count)
        {
          if ((int)tok.n.number <= 0) { ok = false; break; }
          count = (int)tok.n.number;
          have_count = true;
        }
        else if (!have_width && width.from_token(tok, f_length | f_positive, "auto"))
        {
          have_width = true;
          width_is_auto = width.is_predefined();
        }
        else { ok = false; break; }
      }
      if (!ok) break;
      if (have_width)
        add_parsed_property(_column_width_, property_value(width_is_auto ? css_length::predef_value(0) : width, important, false, m_layer, m_specificity));
      if (have_count)
        add_parsed_property(_column_count_, property_value(count, important, false, m_layer, m_specificity));
      break;
    }

    case _counter_increment_:
    case _counter_reset_:
    {
      string_vector strings;
      for (const auto &tok : value)
        strings.push_back(tok.get_repr(true));
      add_parsed_property(name, property_value(strings, important, false, m_layer, m_specificity));
      break;
    }

    case _content_:
      str = get_repr(value, 0, -1, true);
      add_parsed_property(name, property_value(str, important, false, m_layer, m_specificity));
      break;

    case _cursor_:
      str = get_repr(value, 0, -1, true);
      add_parsed_property(name, property_value(str, important, false, m_layer, m_specificity));
      break;

    case _aspect_ratio_:
      parse_aspect_ratio(value, important);
      break;

    case _container_name_:
      if (val.type == IDENT || val.type == STRING)
        add_parsed_property(name, property_value(val.str, important, false, m_layer, m_specificity));
      break;

    case _container_:
    {
      css_token_vector name_tokens, type_tokens;
      bool slash_found = false;
      for (const auto &tok : value)
      {
        if (tok.ch == '/')
          slash_found = true;
        else if (slash_found)
          type_tokens.push_back(tok);
        else
          name_tokens.push_back(tok);
      }
      if (!name_tokens.empty())
        add_property(_container_name_, name_tokens, baseurl, important, container, layer, specificity);
      if (!type_tokens.empty())
        add_property(_container_type_, type_tokens, baseurl, important, container, layer, specificity);
      break;
    }

    case _offset_:
    {
      // CSS Motion Path shorthand. Components are parsed in grammar order instead of
      // being classified independently; any out-of-order or unknown component drops
      // the whole declaration. offset-anchor is intentionally not handled here.
      // `evaluated_value` is used so that calc() is reduced to a plain length
      // before the components are split up.
      css_token_vector path_t, dist_t, rot_t, pos_t;
      if (!parse_offset_shorthand(evaluated_value, path_t, dist_t, rot_t, pos_t))
        break;
      // Like every CSS shorthand, `offset` resets the longhands it does not mention
      // back to their initial value (none / normal / auto / normal).
      auto ident_value = [](const string &id) {
        css_token tok(IDENT, id);
        tok.repr = id;
        css_token_vector v;
        v.push_back(tok);
        return v;
      };
      add_parsed_property(_offset_path_, property_value(path_t.empty() ? ident_value("none") : path_t,
                                                       important, false, m_layer, m_specificity));
      add_parsed_property(_offset_distance_, property_value(dist_t.empty() ? ident_value("normal") : dist_t,
                                                             important, false, m_layer, m_specificity));
      add_parsed_property(_offset_rotate_, property_value(rot_t.empty() ? ident_value("auto") : rot_t,
                                                           important, false, m_layer, m_specificity));
      add_parsed_property(_offset_position_, property_value(pos_t.empty() ? ident_value("normal") : pos_t,
                                                            important, false, m_layer, m_specificity));
      break;
    }

    case _fill_:
    case _stroke_:
    {
      // The stylesheet has no whitespace tokens, so `color-mix(in srgb, #000 50%, #fff)` would run together
      // ("insrgb"): a paint that mixes colours is kept with its separators.
      string repr = get_repr(value);
      if (repr.find("color-mix(") != string::npos) repr = get_repr(value, 0, -1, true);
      add_parsed_property(name, property_value(repr, important, false, m_layer, m_specificity));
      break;
    }
    case _margin_trim_:
    case __moz_orient_:
    case __webkit_appearance_:
    case _contain_intrinsic_size_:
      add_parsed_property(name, property_value(get_repr(value), important, false, m_layer, m_specificity));
      break;

    default:
      if (_s(name).substr(0, 2) == "--" && _s(name).size() >= 3 &&
          (value.empty() || is_declaration_value(value)))
        add_parsed_property(name, property_value(value, important, false, m_layer, m_specificity));
      else if (container)
        container->on_unknown_property(_s(name), value);
    }
  }

  void style::add_property(string_id name, const string &value, const string &baseurl, bool important, document_container *container, int layer, selector_specificity specificity)
  {
    auto tokens = normalize(value, f_componentize | f_remove_whitespace);
    add_property(name, tokens, baseurl, important, container, layer, specificity);
  }

  bool style::parse_list_style_image(const css_token &tok, string &url)
  {
    if (tok.ident() == "none")
    {
      url = "";
      return true;
    }
    return parse_url(tok, url);
  }

  void style::parse_list_style(const css_token_vector &tokens, string baseurl, bool important)
  {
    int type = list_style_type_disc;
    int position = list_style_position_outside;
    string image = "";

    bool type_found = false;
    bool position_found = false;
    bool image_found = false;
    int none_count = 0;

    for (const auto &token : tokens)
    {
      if (token.ident() == "none")
      {
        none_count++;
        continue;
      }
      if (!type_found && parse_keyword(token, type, list_style_type_strings))
        type_found = true;
      else if (!position_found && parse_keyword(token, position, list_style_position_strings))
        position_found = true;
      else if (!image_found && parse_list_style_image(token, image))
        image_found = true;
      else
        return;
    }

    switch (none_count)
    {
    case 0:
      break;
    case 1:
      if (type_found && image_found)
        return;
      if (!type_found)
        type = list_style_type_none;
      break;
    case 2:
      if (type_found || image_found)
        return;
      type = list_style_type_none;
      break;
    default:
      return;
    }

    add_parsed_property(_list_style_type_, property_value(type, important, false, m_layer, m_specificity));
    add_parsed_property(_list_style_position_, property_value(position, important, false, m_layer, m_specificity));
    add_parsed_property(_list_style_image_, property_value(image, important, false, m_layer, m_specificity));
    add_parsed_property(_list_style_image_baseurl_, property_value(baseurl, important, false, m_layer, m_specificity));
  }

  void style::parse_border_radius(const css_token_vector &tokens, bool important)
  {
    int i;
    for (i = 0; i < (int)tokens.size() && tokens[i].ch != '/'; i++)
    {
    }

    if (i == (int)tokens.size())
    {
      css_length len[4];
      if (int n = parse_1234_lengths(tokens, len, f_length_percentage | f_positive))
      {
        add_four_properties(_border_top_left_radius_x_, len, n, important);
        add_four_properties(_border_top_left_radius_y_, len, n, important);
      }
    }
    else
    {
      auto raduis_x = slice(tokens, 0, i);
      auto raduis_y = slice(tokens, i + 1);

      css_length rx[4], ry[4];
      int n = parse_1234_lengths(raduis_x, rx, f_length_percentage | f_positive);
      int m = parse_1234_lengths(raduis_y, ry, f_length_percentage | f_positive);

      if (n && m)
      {
        add_four_properties(_border_top_left_radius_x_, rx, n, important);
        add_four_properties(_border_top_left_radius_y_, ry, m, important);
      }
    }
  }

    void style::parse_border_image(const css_token_vector& tokens, const string& baseurl, bool important, document_container* container)
    {
      image source;
      css_length slice[4];
      bool slice_fill = false;
      css_length width[4];
      css_length outset[4];
      int repeat[2] = {border_image_repeat_stretch, border_image_repeat_stretch};

      bool source_found = false;
      bool slice_found = false;
      bool width_found = false;
      bool outset_found = false;
      bool repeat_found = false;

      int n_slice = 0;
      int n_width = 0;
      int n_outset = 0;

      for (int i = 0; i < (int)tokens.size(); i++)
      {
        image img;
        if (!source_found && parse_bg_image(tokens[i], img, container))
        {
          source = img;
          source_found = true;
        }
        else if (!slice_found && (tokens[i].type == NUMBER || tokens[i].type == PERCENTAGE || tokens[i].ident() == "fill"))
        {
          css_token_vector slice_tokens;
          while (i < (int)tokens.size() && (tokens[i].type == NUMBER || tokens[i].type == PERCENTAGE || tokens[i].ident() == "fill"))
          {
            if (tokens[i].ident() == "fill") slice_fill = true;
            else slice_tokens.push_back(tokens[i]);
            i++;
          }
          n_slice = parse_1234_lengths(slice_tokens, slice, f_number | f_percentage | f_positive);
          if (n_slice)
          {
            slice_found = true;
          }
          i--;
          if (at(tokens, i + 1).ch == '/')
          {
            i += 2;
            css_token_vector width_tokens;
            while (i < (int)tokens.size() && (tokens[i].type == NUMBER || tokens[i].type == PERCENTAGE || tokens[i].type == DIMENSION || tokens[i].ident() == "auto"))
            {
              width_tokens.push_back(tokens[i]);
              i++;
            }
            n_width = parse_1234_lengths(width_tokens, width, f_length_percentage | f_number | f_positive, "auto");
            if (n_width)
            {
              width_found = true;
            }
            i--;
            if (at(tokens, i + 1).ch == '/')
            {
              i += 2;
              css_token_vector outset_tokens;
              while (i < (int)tokens.size() && (tokens[i].type == NUMBER || tokens[i].type == DIMENSION))
              {
                outset_tokens.push_back(tokens[i]);
                i++;
              }
              n_outset = parse_1234_lengths(outset_tokens, outset, f_length | f_number | f_positive);
              if (n_outset)
              {
                outset_found = true;
              }
              i--;
            }
          }
        }
        else if (!repeat_found && (value_index(tokens[i].ident(), border_image_repeat_strings) >= 0))
        {
          repeat[0] = value_index(tokens[i].ident(), border_image_repeat_strings);
          if (i + 1 < (int)tokens.size() && value_index(tokens[i + 1].ident(), border_image_repeat_strings) >= 0)
          {
            repeat[1] = value_index(tokens[i + 1].ident(), border_image_repeat_strings);
            i++;
          }
          else
          {
            repeat[1] = repeat[0];
          }
          repeat_found = true;
        }
      }

      if (source_found)
      {
        add_parsed_property(_border_image_source_, property_value(source, important, false, m_layer, m_specificity));
        add_parsed_property(_id("border-image-source-baseurl"), property_value(baseurl, important, false, m_layer, m_specificity));
      }
      if (slice_found)
      {
        add_four_properties(_border_image_slice_top_, slice, n_slice, important);
        add_parsed_property(_id("border-image-slice-fill"), property_value(slice_fill ? 1 : 0, important, false, m_layer, m_specificity));
      }
      if (width_found)
      {
        add_four_properties(_border_image_width_top_, width, n_width, important);
      }
      if (outset_found)
      {
        add_four_properties(_border_image_outset_top_, outset, n_outset, important);
      }
      if (repeat_found)
      {
        add_parsed_property(_border_image_repeat_, property_value(repeat[0] | (repeat[1] << 8), important, false, m_layer, m_specificity));
      }
    }

  void style::parse_border_image_repeat(const css_token_vector& tokens, bool important)
  {
    int repeat[2] = {border_image_repeat_stretch, border_image_repeat_stretch};
    if (tokens.size() >= 1)
    {
      repeat[0] = value_index(tokens[0].ident(), border_image_repeat_strings);
      if (tokens.size() >= 2)
        repeat[1] = value_index(tokens[1].ident(), border_image_repeat_strings);
      else
        repeat[1] = repeat[0];

      if (repeat[0] >= 0 && repeat[1] >= 0)
        add_parsed_property(_border_image_repeat_, property_value(repeat[0] | (repeat[1] << 8), important, false, m_layer, m_specificity));
    }
  }

  bool parse_border_width(const css_token &token, css_length &w)
  {
    css_length width;
    if (!width.from_token(token, f_length | f_positive, border_width_strings))
      return false;
    if (width.is_predefined())
      width.set_value(border_width_values[width.predef()], css_units_px);
    w = width;
    return true;
  }

  bool parse_border_helper(const css_token_vector &tokens, document_container *container,
                           css_length &width, border_style &style, web_color &color)
  {
    css_length _width = border_width_medium_value;
    border_style _style = border_style_none;
    web_color _color = web_color::current_color;

    bool width_found = false;
    bool style_found = false;
    bool color_found = false;

    for (const auto &token : tokens)
    {
      if (!width_found && parse_border_width(token, _width))
        width_found = true;
      else if (!style_found && parse_keyword(token, _style, border_style_strings))
        style_found = true;
      else if (!color_found && parse_color(token, _color, container))
        color_found = true;
      else
        return false;
    }

    width = _width;
    style = _style;
    color = _color;
    return true;
  }

  void style::parse_border(const css_token_vector &tokens, bool important, document_container *container)
  {
    css_length width;
    border_style style;
    web_color color;

    if (!parse_border_helper(tokens, container, width, style, color))
      return;

    for (auto name : {_border_left_width_, _border_right_width_, _border_top_width_, _border_bottom_width_})
      add_parsed_property(name, property_value(width, important, false, m_layer, m_specificity));

    for (auto name : {_border_left_style_, _border_right_style_, _border_top_style_, _border_bottom_style_})
      add_parsed_property(name, property_value(style, important, false, m_layer, m_specificity));

    for (auto name : {_border_left_color_, _border_right_color_, _border_top_color_, _border_bottom_color_})
      add_parsed_property(name, property_value(color, important, false, m_layer, m_specificity));
  }

  void style::parse_border_side(string_id name, const css_token_vector &tokens, bool important, document_container *container)
  {
    css_length width;
    border_style style;
    web_color color;

    if (!parse_border_helper(tokens, container, width, style, color))
      return;

    add_parsed_property(_id(_s(name) + "-width"), property_value(width, important, false, m_layer, m_specificity));
    add_parsed_property(_id(_s(name) + "-style"), property_value(style, important, false, m_layer, m_specificity));
    add_parsed_property(_id(_s(name) + "-color"), property_value(color, important, false, m_layer, m_specificity));
  }

  bool parse_length(const css_token &tok, css_length &length, int options, string keywords)
  {
    return length.from_token(tok, options, keywords);
  }

  bool parse_two_lengths(const css_token_vector &tokens, css_length len[2], int options)
  {
    auto n = tokens.size();
    if (n != 1 && n != 2)
      return false;

    css_length a, b;
    if (!a.from_token(tokens[0], options))
      return false;
    if (n == 1)
      b = a;
    if (n == 2 && !b.from_token(tokens[1], options))
      return false;

    len[0] = a;
    len[1] = b;
    return true;
  }

  template <class T, class... Args>
  int parse_1234_values(const css_token_vector &tokens, T result[4], bool (*parse)(const css_token &, T &, Args...), Args... args)
  {
    if (tokens.size() > 4)
      return 0;
    for (size_t i = 0; i < tokens.size(); i++)
    {
      if (!parse(tokens[i], result[i], args...))
        return 0;
    }
    return (int)tokens.size();
  }

  int parse_1234_lengths(const css_token_vector &tokens, css_length len[4], int options, string keywords)
  {
    return parse_1234_values(tokens, len, parse_length, options, keywords);
  }

  template <class T>
  void style::add_four_properties(string_id top_name, T val[4], int n, bool important)
  {
    string_id top = top_name;
    string_id right = string_id(top_name + 1);
    string_id bottom = string_id(top_name + 2);
    string_id left = string_id(top_name + 3);

    add_parsed_property(top, property_value(val[0], important, false, m_layer, m_specificity));
    add_parsed_property(right, property_value(val[n > 1], important, false, m_layer, m_specificity));
    add_parsed_property(bottom, property_value(val[n / 3 * 2], important, false, m_layer, m_specificity));
    add_parsed_property(left, property_value(val[n / 2 + n / 4], important, false, m_layer, m_specificity));
  }

  template <class T>
  void style::add_two_properties(string_id start_name, T val[2], int n, bool important)
  {
    string_id start = start_name;
    string_id end = string_id(start_name + 1);

    add_parsed_property(start, property_value(val[0], important, false, m_layer, m_specificity));
    add_parsed_property(end, property_value(val[n > 1], important, false, m_layer, m_specificity));
  }

  void style::parse_background(const css_token_vector &tokens, const string &baseurl, bool important, document_container *container)
  {
    auto layers = parse_comma_separated_list(tokens);
    if (layers.empty())
      return;

    web_color color;
    std::vector<image> images;
    length_vector x_positions, y_positions;
    size_vector sizes;
    int_vector repeats, attachments, origins, clips;

    for (size_t i = 0; i < layers.size(); i++)
    {
      background bg;
      if (!parse_bg_layer(layers[i], container, bg, i == layers.size() - 1))
        return;

      color = bg.m_color;
      images.push_back(bg.m_image[0]);
      x_positions.push_back(bg.m_position_x[0]);
      y_positions.push_back(bg.m_position_y[0]);
      sizes.push_back(bg.m_size[0]);
      repeats.push_back(bg.m_repeat[0]);
      attachments.push_back(bg.m_attachment[0]);
      origins.push_back(bg.m_origin[0]);
      clips.push_back(bg.m_clip[0]);
    }

    add_parsed_property(_background_color_, property_value(color, important, false, m_layer, m_specificity));
    add_parsed_property(_background_image_, property_value(images, important, false, m_layer, m_specificity));
    add_parsed_property(_background_image_baseurl_, property_value(baseurl, important, false, m_layer, m_specificity));
    add_parsed_property(_background_position_x_, property_value(x_positions, important, false, m_layer, m_specificity));
    add_parsed_property(_background_position_y_, property_value(y_positions, important, false, m_layer, m_specificity));
    add_parsed_property(_background_size_, property_value(sizes, important, false, m_layer, m_specificity));
    add_parsed_property(_background_repeat_, property_value(repeats, important, false, m_layer, m_specificity));
    add_parsed_property(_background_attachment_, property_value(attachments, important, false, m_layer, m_specificity));
    add_parsed_property(_background_origin_, property_value(origins, important, false, m_layer, m_specificity));
    add_parsed_property(_background_clip_, property_value(clips, important, false, m_layer, m_specificity));
  }

  bool style::parse_bg_layer(const css_token_vector &tokens, document_container *container, background &bg, bool final_layer)
  {
    bg.m_color = web_color::transparent;
    bg.m_image = {{}};
    bg.m_position_x = {css_length(0, css_units_percentage)};
    bg.m_position_y = {css_length(0, css_units_percentage)};
    bg.m_size = {css_size(css_length::predef_value(background_size_auto), css_length::predef_value(background_size_auto))};
    bg.m_repeat = {background_repeat_repeat};
    bg.m_attachment = {background_attachment_scroll};
    bg.m_origin = {background_box_padding};
    bg.m_clip = {background_box_border};

    bool color_found = false;
    bool image_found = false;
    bool position_found = false;
    bool repeat_found = false;
    bool attachment_found = false;
    bool origin_found = false;
    bool clip_found = false;

    for (int i = 0; i < (int)tokens.size(); i++)
    {
      if (!color_found && final_layer && parse_color(tokens[i], bg.m_color, container))
        color_found = true;
      else if (!image_found && parse_bg_image(tokens[i], bg.m_image[0], container))
        image_found = true;
      else if (!position_found && parse_bg_position_size(tokens, i, bg.m_position_x[0], bg.m_position_y[0], bg.m_size[0]))
        position_found = true, i--;
      else if (!repeat_found && parse_keyword(tokens[i], bg.m_repeat[0], background_repeat_strings))
        repeat_found = true;
      else if (!attachment_found && parse_keyword(tokens[i], bg.m_attachment[0], background_attachment_strings))
        attachment_found = true;
      else if (!origin_found && parse_keyword(tokens[i], bg.m_origin[0], background_box_strings))
        origin_found = true, bg.m_clip[0] = bg.m_origin[0];
      else if (!clip_found && parse_keyword(tokens[i], bg.m_clip[0], background_box_strings))
        clip_found = true;
      else
        return false;
    }
    return true;
  }

  bool parse_bg_position_size(const css_token_vector &tokens, int &index, css_length &x, css_length &y, css_size &size)
  {
    if (!parse_bg_position(tokens, index, x, y, true))
      return false;
    if (at(tokens, index).ch != '/')
      return true;
    if (!parse_bg_size(tokens, ++index, size))
    {
      index--;
      return false;
    }
    return true;
  }

  bool parse_bg_size(const css_token_vector &tokens, int &index, css_size &size)
  {
    css_length a, b;
    if (!a.from_token(at(tokens, index), f_length_percentage | f_positive, background_size_strings))
      return false;
    if (a.is_predefined() && a.predef() != background_size_auto)
    {
      size.width = size.height = a;
      index++;
      return true;
    }
    if (b.from_token(at(tokens, index + 1), f_length_percentage | f_positive, "auto"))
      index += 2;
    else
    {
      b.predef(background_size_auto);
      index++;
    }
    size.width = a;
    size.height = b;
    return true;
  }

  bool is_one_of_predef(const css_length &x, int idx1, int idx2)
  {
    return x.is_predefined() && is_one_of(x.predef(), idx1, idx2);
  }

  bool parse_bg_position(const css_token_vector &tokens, int &index, css_length &x, css_length &y, bool convert_keywords_to_percents)
  {
    enum
    {
      left = background_position_left,
      right = background_position_right,
      top = background_position_top,
      bottom = background_position_bottom,
      center = background_position_center
    };

    css_length a, b;
    if (!a.from_token(at(tokens, index), f_length_percentage, background_position_strings))
      return false;
    if (!b.from_token(at(tokens, index + 1), f_length_percentage, background_position_strings))
    {
      b.predef(center);
      if (is_one_of_predef(a, top, bottom))
        swap(a, b);
      index++;
    }
    else
    {
      if ((is_one_of_predef(a, top, bottom) && b.is_predefined()) ||
          (a.is_predefined() && is_one_of_predef(b, left, right)))
        swap(a, b);
      if (is_one_of_predef(a, top, bottom) || is_one_of_predef(b, left, right))
        return false;
      index += 2;
    }

    if (convert_keywords_to_percents)
    {
      if (a.is_predefined())
        a.set_value(background_position_percentages[a.predef()], css_units_percentage);
      if (b.is_predefined())
        b.set_value(background_position_percentages[b.predef()], css_units_percentage);
    }
    x = a;
    y = b;
    return true;
  }

  void style::parse_background_image(const css_token_vector &tokens, const string &baseurl, bool important, document_container *container)
  {
    auto layers = parse_comma_separated_list(tokens);
    if (layers.empty())
      return;
    std::vector<image> images;
    for (const auto &layer : layers)
    {
      image image;
      if (layer.size() != 1)
        return;
      if (!parse_bg_image(layer[0], image, container))
        return;
      images.push_back(image);
    }
    add_parsed_property(_background_image_, property_value(images, important, false, m_layer, m_specificity));
    add_parsed_property(_background_image_baseurl_, property_value(baseurl, important, false, m_layer, m_specificity));
  }

  bool parse_bg_image(const css_token &tok, image &bg_image, document_container *container)
  {
    if (tok.ident() == "none")
    {
      bg_image.type = image::type_none;
      return true;
    }
    string url;
    if (parse_url(tok, url))
    {
      bg_image.type = image::type_url;
      bg_image.url = url;
      return true;
    }
    if (parse_gradient(tok, bg_image.m_gradient, container))
    {
      bg_image.type = image::type_gradient;
      return true;
    }
    return false;
  }

  bool parse_url(const css_token &tok, string &url)
  {
    if (tok.type == URL)
    {
      url = trim(tok.str);
      return true;
    }
    if (tok.type == CV_FUNCTION && is_one_of(lowcase(tok.name), "url", "src") &&
        tok.value.size() == 1 && tok.value[0].type == STRING)
    {
      url = trim(tok.value[0].str);
      return true;
    }
    return false;
  }

  void style::parse_keyword_comma_list(string_id name, const css_token_vector &tokens, bool important)
  {
    auto layers = parse_comma_separated_list(tokens);
    if (layers.empty())
      return;
    int_vector vec;
    for (const auto &layer : layers)
    {
      int idx;
      if (layer.size() != 1)
        return;
      if (!parse_keyword(layer[0], idx, m_valid_values[name]))
        return;
      vec.push_back(idx);
    }
    add_parsed_property(name, property_value(vec, important, false, m_layer, m_specificity));
  }

  void style::parse_background_position(const css_token_vector &tokens, bool important)
  {
    auto layers = parse_comma_separated_list(tokens);
    if (layers.empty())
      return;
    length_vector x_positions, y_positions;
    for (const auto &layer : layers)
    {
      css_length x, y;
      int index = 0;
      if (!parse_bg_position(layer, index, x, y, true) || index != (int)layer.size())
        return;
      x_positions.push_back(x);
      y_positions.push_back(y);
    }
    add_parsed_property(_background_position_x_, property_value(x_positions, important, false, m_layer, m_specificity));
    add_parsed_property(_background_position_y_, property_value(y_positions, important, false, m_layer, m_specificity));
  }

  void style::parse_background_size(const css_token_vector &tokens, bool important)
  {
    auto layers = parse_comma_separated_list(tokens);
    if (layers.empty())
      return;
    size_vector sizes;
    for (const auto &layer : layers)
    {
      css_size size;
      int index = 0;
      if (!parse_bg_size(layer, index, size) || index != (int)layer.size())
        return;
      sizes.push_back(size);
    }
    add_parsed_property(_background_size_, property_value(sizes, important, false, m_layer, m_specificity));
  }

  bool parse_font_weight(const css_token &tok, css_length &weight)
  {
    if (int idx = value_index(tok.ident(), font_weight_strings); idx >= 0)
    {
      weight.predef(idx);
      return true;
    }
    if (tok.type == NUMBER && tok.n.number >= 1 && tok.n.number <= 1000)
    {
      weight.set_value(tok.n.number, css_units_none);
      return true;
    }
    return false;
  }

  // ── font-stretch / font-kerning / font-variation-settings ──
  // Percentage of the normal width for each font-stretch keyword. The order
  // matches font_stretch_strings.
  static const float font_stretch_keyword_percent[] = {
      50.0f,   // ultra-condensed
      62.5f,   // extra-condensed
      75.0f,   // condensed
      87.5f,   // semi-condensed
      100.0f,  // normal
      112.5f,  // semi-expanded
      125.0f,  // expanded
      150.0f,  // extra-expanded
      200.0f,  // ultra-expanded
  };

  // A font-stretch keyword (percentages are not allowed in the `font` shorthand,
  // where they would be ambiguous with the font-size percentage).
  bool parse_font_stretch_keyword(const css_token &tok, float &out)
  {
    if (tok.type != IDENT) return false;
    int idx = value_index(lowcase(tok.ident()), font_stretch_strings);
    if (idx < 0) return false;
    out = font_stretch_keyword_percent[idx];
    return true;
  }

  // A font-stretch keyword or percentage.
  bool parse_font_stretch_value(const css_token &tok, float &out)
  {
    if (parse_font_stretch_keyword(tok, out)) return true;
    if (tok.type == PERCENTAGE)
    {
      out = tok.n.number;
      if (out < 50.0f) out = 50.0f;
      if (out > 200.0f) out = 200.0f;
      return true;
    }
    return false;
  }

  bool parse_font_style_variant_weight(const css_token_vector &tokens, int &index,
                                       int &style, int &variant, css_length &weight,
                                       float &stretch)
  {
    bool style_found = false;
    bool variant_found = false;
    bool weight_found = false;
    bool stretch_found = false;
    bool res = false;
    // style, variant, weight and stretch can each appear once.
    int i = index, count = 0;
    while (i < (int)tokens.size() && count++ < 4)
    {
      const auto &tok = tokens[i++];
      if (tok.ident() == "normal")
      {
        index++;
        res = true;
      }
      else if (!style_found && parse_keyword(tok, style, font_style_strings))
      {
        style_found = true;
        index++;
        res = true;
      }
      else if (!variant_found && parse_keyword(tok, variant, font_variant_strings))
      {
        variant_found = true;
        index++;
        res = true;
      }
      else if (!weight_found && parse_font_weight(tok, weight))
      {
        weight_found = true;
        index++;
        res = true;
      }
      else if (!stretch_found && parse_font_stretch_keyword(tok, stretch))
      {
        stretch_found = true;
        index++;
        res = true;
      }
      else
        break;
    }
    return res;
  }

  bool is_custom_ident(const css_token &tok)
  {
    if (tok.type != IDENT)
      return false;
    return !is_one_of(lowcase(tok.name), "default", "initial", "inherit", "unset");
  }

  bool parse_font_family(const css_token_vector &tokens, string &font_family)
  {
    auto list = parse_comma_separated_list(tokens);
    if (list.empty())
      return false;
    string result;
    for (const auto &name : list)
    {
      if (name.size() == 1 && name[0].type == STRING)
      {
        result += name[0].str + ',';
        continue;
      }
      string str;
      for (const auto &tok : name)
      {
        if (!is_custom_ident(tok))
          return false;
        str += tok.name + ' ';
      }
      result += trim(str) + ',';
    }
    result.resize(result.size() - 1);
    font_family = result;
    return true;
  }

  // ── font-variant-* / font-feature-settings ──
  static const char *font_variant_group_keywords(int group)
  {
    switch (group)
    {
      case 0: return font_variant_ligatures_strings;
      case 1: return font_variant_numeric_strings;
      case 2: return font_variant_east_asian_strings;
      case 3: return font_variant_caps_strings;
      default: return font_variant_position_strings;
    }
  }

  static bool parse_font_variant_values(const css_token_vector &tokens, int group, string &out)
  {
    if (tokens.size() == 1 && lowcase(tokens[0].ident()) == "normal")
    {
      out = "normal";
      return true;
    }
    if (tokens.empty())
      return false;

    const char *keywords = font_variant_group_keywords(group);
    string result;
    for (const auto &tok : tokens)
    {
      string kw = lowcase(tok.ident());
      if (kw.empty() || kw == "normal" || value_index(kw, keywords) < 0)
        return false;
      if (!result.empty())
        result += ' ';
      result += kw;
    }
    out = result;
    return true;
  }

  static int font_variant_group_of(const string &kw)
  {
    if (kw == "normal")
      return -1;
    for (int group = 0; group < 5; ++group)
    {
      if (value_index(kw, font_variant_group_keywords(group), -1) >= 0)
        return group;
    }
    return -1;
  }

  void style::parse_font_variant_longhand(string_id name, const css_token_vector &tokens, bool important)
  {
    int group = name == _font_variant_ligatures_   ? 0
                : name == _font_variant_numeric_   ? 1
                : name == _font_variant_east_asian_ ? 2
                : name == _font_variant_caps_      ? 3
                                                   : 4;
    string value;
    if (parse_font_variant_values(tokens, group, value))
      add_parsed_property(name, property_value(value, important, false, m_layer, m_specificity));
  }

  void style::parse_font_variant(const css_token_vector &tokens, bool important)
  {
    string values[5] = {"normal", "normal", "normal", "normal", "normal"};
    if (!(tokens.size() == 1 && lowcase(tokens[0].ident()) == "normal"))
    {
      if (tokens.empty())
        return;
      for (const auto &tok : tokens)
      {
        string kw = lowcase(tok.ident());
        if (kw == "normal")
          return; // `normal` cannot be combined with other keywords
        int group = font_variant_group_of(kw);
        if (group < 0)
          return;
        if (values[group] == "normal")
          values[group] = kw;
        else
          values[group] += " " + kw;
      }
    }
    add_parsed_property(_font_variant_ligatures_,
                        property_value(values[0], important, false, m_layer, m_specificity));
    add_parsed_property(_font_variant_numeric_,
                        property_value(values[1], important, false, m_layer, m_specificity));
    add_parsed_property(_font_variant_east_asian_,
                        property_value(values[2], important, false, m_layer, m_specificity));
    add_parsed_property(_font_variant_caps_,
                        property_value(values[3], important, false, m_layer, m_specificity));
    add_parsed_property(_font_variant_position_,
                        property_value(values[4], important, false, m_layer, m_specificity));
  }

  void style::parse_font_feature_settings(const css_token_vector &tokens, bool important)
  {
    if (tokens.size() == 1 && lowcase(tokens[0].ident()) == "normal")
    {
      add_parsed_property(_font_feature_settings_,
                          property_value(tokens, important, false, m_layer, m_specificity));
      return;
    }
    if (tokens.empty())
      return;

    size_t i = 0;
    while (i < tokens.size())
    {
      if (tokens[i].type != STRING || tokens[i].str.size() != 4)
        return;
      ++i;
      if (i < tokens.size() && tokens[i].type != COMMA)
      {
        if (tokens[i].type == IDENT)
        {
          string id = lowcase(tokens[i].ident());
          if (id != "on" && id != "off")
            return;
        }
        else if (tokens[i].type != NUMBER || tokens[i].n.number_type != css_number_integer)
        {
          return;
        }
        ++i;
      }
      if (i < tokens.size())
      {
        if (tokens[i].type != COMMA)
          return;
        ++i;
        if (i == tokens.size())
          return; // trailing comma
      }
    }
    add_parsed_property(_font_feature_settings_,
                        property_value(tokens, important, false, m_layer, m_specificity));
  }

  void style::parse_font_kerning(const css_token_vector &tokens, bool important)
  {
    if (tokens.size() != 1 || tokens[0].type != IDENT) return;
    string kw = lowcase(tokens[0].ident());
    if (value_index(kw, font_kerning_strings) < 0) return;
    add_parsed_property(_font_kerning_,
                        property_value(kw, important, false, m_layer, m_specificity));
  }

  void style::parse_font_stretch(const css_token_vector &tokens, bool important)
  {
    if (tokens.size() != 1) return;
    float percent = 0;
    if (!parse_font_stretch_value(tokens[0], percent)) return;
    add_parsed_property(_font_stretch_,
                        property_value(percent, important, false, m_layer, m_specificity));
  }

  // font-variation-settings: normal | <string> <number> [, <string> <number>]*
  void style::parse_font_variation_settings(const css_token_vector &tokens, bool important)
  {
    if (tokens.size() == 1 && lowcase(tokens[0].ident()) == "normal")
    {
      add_parsed_property(_font_variation_settings_,
                          property_value(tokens, important, false, m_layer, m_specificity));
      return;
    }
    if (tokens.empty()) return;

    size_t i = 0;
    while (i < tokens.size())
    {
      // The axis tag is a 4 character string of printable ASCII.
      if (tokens[i].type != STRING || tokens[i].str.size() != 4) return;
      for (char c : tokens[i].str)
      {
        if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7E) return;
      }
      ++i;
      // The axis value is a number.
      if (i >= tokens.size() || tokens[i].type != NUMBER) return;
      ++i;
      if (i < tokens.size())
      {
        if (tokens[i].type != COMMA) return;
        ++i;
        if (i == tokens.size()) return; // trailing comma
      }
    }
    add_parsed_property(_font_variation_settings_,
                        property_value(tokens, important, false, m_layer, m_specificity));
  }

  void style::parse_font(css_token_vector tokens, bool important)
  {
    int style = font_style_normal;
    int variant = font_variant_normal;
    css_length weight = css_length::predef_value(font_weight_normal);
    css_length size = css_length::predef_value(font_size_medium);
    css_length line_height = css_length::predef_value(line_height_normal);
    float stretch = 100.0f;
    string font_family;

    if (tokens.size() == 1 && (tokens[0].type == STRING || tokens[0].type == IDENT) && value_in_list(tokens[0].str, font_system_family_name_strings))
    {
      font_family = tokens[0].str;
    }
    else
    {
      int index = 0;
      parse_font_style_variant_weight(tokens, index, style, variant, weight, stretch);
      if (!size.from_token(at(tokens, index), f_length_percentage | f_positive, font_size_strings))
        return;
      index++;
      if (at(tokens, index).ch == '/')
      {
        index++;
        if (!line_height.from_token(at(tokens, index), f_number | f_length_percentage, line_height_strings))
          return;
        index++;
      }
      remove(tokens, 0, index);
      if (!parse_font_family(tokens, font_family))
        return;
    }
    add_parsed_property(_font_style_, property_value(style, important, false, m_layer, m_specificity));
    add_parsed_property(_font_variant_, property_value(variant, important, false, m_layer, m_specificity));
    // `font` resets every font-variant longhand (font-variant-css2 only has small-caps),
    // font-stretch and font-feature-settings to their initial values.
    add_parsed_property(_font_variant_ligatures_, property_value(string("normal"), important, false, m_layer, m_specificity));
    add_parsed_property(_font_variant_numeric_, property_value(string("normal"), important, false, m_layer, m_specificity));
    add_parsed_property(_font_variant_east_asian_, property_value(string("normal"), important, false, m_layer, m_specificity));
    add_parsed_property(_font_variant_caps_, property_value(string(variant == font_variant_small_caps ? "small-caps" : "normal"), important, false, m_layer, m_specificity));
    add_parsed_property(_font_variant_position_, property_value(string("normal"), important, false, m_layer, m_specificity));
    add_parsed_property(_font_stretch_, property_value(stretch, important, false, m_layer, m_specificity));
    css_token_vector feature_normal;
    feature_normal.push_back(css_token(IDENT, "normal"));
    add_parsed_property(_font_feature_settings_, property_value(feature_normal, important, false, m_layer, m_specificity));
    add_parsed_property(_font_weight_, property_value(weight, important, false, m_layer, m_specificity));
    add_parsed_property(_font_size_, property_value(size, important, false, m_layer, m_specificity));
    add_parsed_property(_line_height_, property_value(line_height, important, false, m_layer, m_specificity));
    add_parsed_property(_font_family_, property_value(font_family, important, false, m_layer, m_specificity));
  }

  void style::parse_text_decoration(const css_token_vector &tokens, bool important, document_container *container)
  {
    css_length len;
    css_token_vector line_tokens;
    for (const auto &token : tokens)
    {
      if (parse_text_decoration_color(token, important, container))
        continue;
      if (parse_length(token, len, f_length_percentage | f_positive, style_text_decoration_thickness_strings))
      {
        add_parsed_property(_text_decoration_thickness_, property_value(len, important, false, m_layer, m_specificity));
      }
      else
      {
        if (token.type == IDENT)
        {
          int style = value_index(token.ident(), style_text_decoration_style_strings);
          if (style >= 0)
          {
            add_parsed_property(_text_decoration_style_, property_value(style, important, false, m_layer, m_specificity));
          }
          else
            line_tokens.push_back(token);
        }
        else
          line_tokens.push_back(token);
      }
    }
    if (!line_tokens.empty())
      parse_text_decoration_line(line_tokens, important);
  }

  bool style::parse_text_decoration_color(const css_token &token, bool important, document_container *container)
  {
    web_color _color;
    if (parse_color(token, _color, container))
    {
      add_parsed_property(_text_decoration_color_, property_value(_color, important, false, m_layer, m_specificity));
      return true;
    }
    if (token.type == IDENT && value_in_list(token.ident(), "auto;currentcolor"))
    {
      add_parsed_property(_text_decoration_color_, property_value(web_color::current_color, important, false, m_layer, m_specificity));
      return true;
    }
    return false;
  }

  void style::parse_text_decoration_line(const css_token_vector &tokens, bool important)
  {
    int val = 0;
    for (const auto &token : tokens)
    {
      if (token.type == IDENT)
      {
        int idx = value_index(token.ident(), style_text_decoration_line_strings);
        if (idx >= 0)
          val |= 1 << (idx - 1);
      }
    }
    add_parsed_property(_text_decoration_line_, property_value(val, important, false, m_layer, m_specificity));
  }

  // -webkit-text-stroke: <line-width> || <color>
  // Unspecified components are reset to their initial values (CSS shorthand
  // semantics), matching Chromium. Usage of keywords thin/medium/thick is
  // delegated to parse_border_width.
  void style::parse_text_stroke(const css_token_vector &tokens, bool important, document_container *container)
  {
    bool width_found = false;
    bool color_found = false;
    size_t consumed = 0;
    for (const auto &token : tokens)
    {
      if (!color_found)
      {
        web_color color;
        if (parse_color(token, color, container))
        {
          add_parsed_property(__webkit_text_stroke_color_, property_value(color, important, false, m_layer, m_specificity));
          color_found = true;
          consumed++;
          continue;
        }
      }
      if (!width_found)
      {
        css_length width;
        if (parse_border_width(token, width))
        {
          add_parsed_property(__webkit_text_stroke_width_, property_value(width, important, false, m_layer, m_specificity));
          width_found = true;
          consumed++;
          continue;
        }
      }
    }
    // Unconsumed tokens mean the whole value is invalid: drop the declaration.
    if (consumed != tokens.size())
      return;
    // Omitted components reset to their initial values (CSS shorthand semantics).
    if (!width_found)
      add_parsed_property(__webkit_text_stroke_width_, property_value(css_length::predef_value(0), important, false, m_layer, m_specificity));
    if (!color_found)
      add_parsed_property(__webkit_text_stroke_color_, property_value(web_color::current_color, important, false, m_layer, m_specificity));
  }

  void style::parse_aspect_ratio(const css_token_vector &tokens, bool important)
  {
    if (tokens.size() == 1 && tokens[0].ident() == "auto")
    {
      add_parsed_property(_aspect_ratio_, property_value(aspect_ratio(), important, false, m_layer, m_specificity));
      return;
    }

    float w = 1, h = 1;
    bool w_found = false, h_found = false;
    bool auto_found = false;

    for (size_t i = 0; i < tokens.size(); i++)
    {
      if (tokens[i].ident() == "auto")
      {
        auto_found = true;
      }
      else if (tokens[i].type == NUMBER)
      {
        if (!w_found)
        {
          w = tokens[i].n.number;
          w_found = true;
        }
        else if (!h_found)
        {
          if (i > 0 && tokens[i - 1].ch == '/')
          {
            h = tokens[i].n.number;
            h_found = true;
          }
          else
            return;
        }
        else
          return;
      }
      else if (tokens[i].ch == '/')
      {
        if (!w_found || h_found)
          return;
      }
      else
        return;
    }

    if (w_found)
    {
      add_parsed_property(_aspect_ratio_, property_value(aspect_ratio(w, h, auto_found), important, false, m_layer, m_specificity));
    }
  }

  void style::parse_text_emphasis(const css_token_vector &tokens, bool important, document_container *container)
  {
    string style;
    for (const auto &token : std::vector(tokens.rbegin(), tokens.rend()))
    {
      if (parse_text_emphasis_color(token, important, container))
        continue;
      style.insert(0, token.str + " ");
    }
    style = trim(style);
    if (!style.empty())
      add_parsed_property(_text_emphasis_style_, property_value(style, important, false, m_layer, m_specificity));
  }

  bool style::parse_text_emphasis_color(const css_token &token, bool important, document_container *container)
  {
    web_color _color;
    if (parse_color(token, _color, container))
    {
      add_parsed_property(_text_emphasis_color_, property_value(_color, important, false, m_layer, m_specificity));
      return true;
    }
    if (token.type == IDENT && value_in_list(token.ident(), "auto;currentcolor"))
    {
      add_parsed_property(_text_emphasis_color_, property_value(web_color::current_color, important, false, m_layer, m_specificity));
      return true;
    }
    return false;
  }

  void style::parse_text_emphasis_position(const css_token_vector &tokens, bool important)
  {
    int val = 0;
    for (const auto &token : tokens)
    {
      if (token.type == IDENT)
      {
        int idx = value_index(token.ident(), style_text_emphasis_position_strings);
        if (idx >= 0)
          val |= 1 << (idx - 1);
      }
    }
    add_parsed_property(_text_emphasis_position_, property_value(val, important, false, m_layer, m_specificity));
  }

  void style::parse_flex(const css_token_vector &tokens, bool important)
  {
    auto n = tokens.size();
    if (n > 3)
      return;
    const auto &a = at(tokens, 0);
    const auto &b = at(tokens, 1);
    const auto &c = at(tokens, 2);

    struct flex
    {
      float m_grow = 1;
      float m_shrink = 1;
      css_length m_basis = css_length(0, css_units_percentage);
      bool grow(const css_token &tok)
      {
        if (tok.type != NUMBER || tok.n.number < 0)
          return false;
        m_grow = tok.n.number;
        return true;
      }
      bool shrink(const css_token &tok)
      {
        if (tok.type != NUMBER || tok.n.number < 0)
          return false;
        m_shrink = tok.n.number;
        return true;
      }
      bool basis(const css_token &tok, bool unitless_zero_allowed = false)
      {
        if (!unitless_zero_allowed && tok.type == NUMBER && tok.n.number == 0)
          return false;
        return m_basis.from_token(tok, f_length_percentage | f_positive, flex_basis_strings);
      }
    };
    flex flex;

    if (n == 1)
    {
      string_id ident = _id(a.ident());
      if (is_one_of(ident, _initial_, _auto_, _none_))
      {
        css_length _auto = css_length::predef_value(flex_basis_auto);
        switch (ident)
        {
        case _initial_:
          flex = {0, 1, _auto};
          break;
        case _auto_:
          flex = {1, 1, _auto};
          break;
        case _none_:
          flex = {0, 0, _auto};
          break;
        default:;
        }
      }
      else
      {
        if (!(flex.grow(a) || flex.basis(a)))
          return;
      }
    }
    else if (n == 2)
    {
      if (!((flex.grow(a) && (flex.shrink(b) || flex.basis(b))) || (flex.basis(a) && flex.grow(b))))
        return;
    }
    else
    {
      if (!((flex.grow(a) && flex.shrink(b) && flex.basis(c, true)) || (flex.basis(a) && flex.grow(b) && flex.shrink(c))))
        return;
    }
    add_parsed_property(_flex_grow_, property_value(flex.m_grow, important, false, m_layer, m_specificity));
    add_parsed_property(_flex_shrink_, property_value(flex.m_shrink, important, false, m_layer, m_specificity));
    add_parsed_property(_flex_basis_, property_value(flex.m_basis, important, false, m_layer, m_specificity));
  }

  void style::parse_flex_flow(const css_token_vector &tokens, bool important)
  {
    int flex_direction = flex_direction_row;
    int flex_wrap = flex_wrap_nowrap;
    bool direction_found = false;
    bool wrap_found = false;
    for (const auto &token : tokens)
    {
      if (!direction_found && parse_keyword(token, flex_direction, flex_direction_strings))
        direction_found = true;
      else if (!wrap_found && parse_keyword(token, flex_wrap, flex_wrap_strings))
        wrap_found = true;
      else
        return;
    }
    add_parsed_property(_flex_direction_, property_value(flex_direction, important, false, m_layer, m_specificity));
    add_parsed_property(_flex_wrap_, property_value(flex_wrap, important, false, m_layer, m_specificity));
  }

  void style::parse_align_self(string_id name, const css_token_vector &tokens, bool important)
  {
    auto n = tokens.size();
    if (n > 2)
      return;
    if (tokens[0].type != IDENT || (n == 2 && tokens[1].type != IDENT))
      return;
    string a = tokens[0].ident();
    if (name == _align_items_ && a == "auto")
      return;
    if (n == 1)
    {
      int idx = value_index(a, flex_align_items_strings);
      if (idx >= 0)
        add_parsed_property(name, property_value(idx, important, false, m_layer, m_specificity));
      return;
    }
    string b = tokens[1].ident();
    if (a == "baseline")
      swap(a, b);
    if (b == "baseline" && is_one_of(a, "first", "last"))
    {
      int idx = flex_align_items_baseline | (a == "first" ? flex_align_items_first : flex_align_items_last);
      add_parsed_property(name, property_value(idx, important, false, m_layer, m_specificity));
      return;
    }
    int idx = value_index(b, self_position_strings);
    if (idx >= 0 && is_one_of(a, "safe", "unsafe"))
    {
      idx |= (a == "safe" ? flex_align_items_safe : flex_align_items_unsafe);
      add_parsed_property(name, property_value(idx, important, false, m_layer, m_specificity));
    }
  }

  void style::parse_grid_template(string_id name, const css_token_vector& tokens, bool important)
  {
    length_vector tracks;
    auto add_track = [&](const css_token& tok) {
      css_length len;
      // (the keywords reach the operands of minmax(): auto = 0, min-content = 1, max-content = 2)
      if (len.from_token(tok, f_length_percentage | f_positive, "auto;min-content;max-content"))
      {
        tracks.push_back(len);
        return true;
      }
      else if (tok.type == IDENT && tok.ident() == "auto")
      {
        len.predef(0);
        tracks.push_back(len);
        return true;
      }
      else if (tok.type == IDENT && (lowcase(tok.ident()) == "min-content" || lowcase(tok.ident()) == "max-content"))
      {
        // content-sized tracks: predef 1 = min-content, 2 = max-content (0 = auto)
        len.predef(lowcase(tok.ident()) == "min-content" ? 1 : 2);
        tracks.push_back(len);
        return true;
      }
      else if (tok.type == CV_FUNCTION && lowcase(tok.name) == "fit-content")
      {
        css_token_vector inner;
        for (const auto& t : tok.value)
          if (t.type != WHITESPACE) inner.push_back(t);
        css_length limit;
        if (inner.size() == 1 && limit.from_token(inner[0], f_length_percentage | f_positive))
        {
          std::vector<css_length> operands;
          operands.push_back(limit);
          len.set_math(css_length::op_fit_content, std::move(operands));
          tracks.push_back(len);
          return true;
        }
      }
      return false;
    };

    for (const auto& tok : tokens)
    {
      if (tok.type == CV_FUNCTION && lowcase(tok.name) == "repeat")
      {
        int count = 0;
        bool auto_fit = false;
        bool auto_fill = false;
        css_token_vector sub_tokens;
        bool comma_found = false;
        for (const auto& t : tok.value)
        {
          if (t.type == ',')
          {
            comma_found = true;
          }
          else if (!comma_found)
          {
            if (t.type == NUMBER)
              count = (int)t.n.number;
            else if (t.type == IDENT)
            {
              if (lowcase(t.name) == "auto-fit") auto_fit = true;
              else if (lowcase(t.name) == "auto-fill") auto_fill = true;
            }
          }
          else if (t.type != WHITESPACE)
          {
            sub_tokens.push_back(t);
          }
        }

        if (auto_fit || auto_fill)
        {
          std::vector<css_length> sub_tracks;
          for (const auto& st : sub_tokens)
          {
            css_length sl;
            if (sl.from_token(st, f_length_percentage | f_positive))
              sub_tracks.push_back(sl);
            else if (st.type == IDENT && st.ident() == "auto")
              sub_tracks.push_back(css_length::predef_value(0));
          }
          if (!sub_tracks.empty())
          {
            css_length rep;
            rep.set_math(auto_fit ? css_length::op_repeat_auto_fit : css_length::op_repeat_auto_fill, std::move(sub_tracks));
            tracks.push_back(rep);
          }
        }
        else if (count > 0)
        {
          for (int i = 0; i < count; i++)
          {
            for (const auto& st : sub_tokens)
              add_track(st);
          }
        }
      }
      else if (tok.type != WHITESPACE)
      {
        add_track(tok);
      }
    }
    if (!tracks.empty())
      add_parsed_property(name, property_value(tracks, important, false, m_layer, m_specificity));
    // Keep the raw tokens too: the grid layout reads named lines ([name]) from them.
    if (name == _grid_template_columns_)
      add_parsed_property(_satoru_grid_columns_tokens_,
                          property_value(tokens, important, false, m_layer, m_specificity));
    else if (name == _grid_template_rows_)
      add_parsed_property(_satoru_grid_rows_tokens_,
                          property_value(tokens, important, false, m_layer, m_specificity));
  }

  void style::add_parsed_property(string_id name, const property_value &propval_in)
  {
    property_value propval = propval_in;
    // Stamp the declaration's cascade position unless the caller pinned it
    // (combine stamps in application order; subst_vars preserves the stamp
    // of the declaration being re-parsed so a var() shorthand's expansion
    // cannot overwrite a later declaration it legitimately loses to).
    if (m_forced_order >= 0)
      propval.m_priority.order = m_forced_order;
    else if (propval.m_priority.order < 0)
      propval.m_priority.order = ++m_order_counter;
    for (auto &prop : m_properties)
    {
      if (prop.first == name)
      {
        if (propval.m_priority >= prop.second.m_priority)
        {
          prop.second = propval;
        }
        return;
      }
    }
    m_properties.push_back({name, propval});
  }

  void style::remove_property(string_id name, bool important)
  {
    for (auto it = m_properties.begin(); it != m_properties.end(); ++it)
    {
      if (it->first == name)
      {
        if (!it->second.m_priority.important || (important && it->second.m_priority.important))
        {
          m_properties.erase(it);
        }
        return;
      }
    }
  }

  void style::combine(const style &src, selector_specificity specificity)
  {
    for (const auto &property : src.m_properties)
    {
        property_value val = property.second;
        if (specificity != selector_specificity())
        {
            val.m_priority.specificity = specificity;
        }
        // Rules are combined in cascade order (apply_stylesheet sorts the
        // matched selectors), so stamp each property with its position in
        // that order: between declarations of equal importance, layer and
        // specificity the later one wins, as in CSS.
        val.m_priority.order = ++m_order_counter;
        add_parsed_property(property.first, val);
    }
  }

  const property_value &style::get_property(string_id name) const
  {
    for (const auto &prop : m_properties)
    {
      if (prop.first == name)
      {
        return prop.second;
      }
    }
    static property_value dummy;
    return dummy;
  }

  bool check_var_syntax(const css_token_vector &args)
  {
    if (args.empty())
      return false;
    if (args[0].type != IDENT)
      return false;
    string name = args[0].name;
    if (name.substr(0, 2) != "--" || name.size() <= 2)
      return false;
    if (args.size() > 1 && args[1].ch != ',')
      return false;
    return true;
  }

  bool evaluate_calc(css_token_vector &tokens, const html_tag *el);

  bool subst_var_nested(css_token_vector &tokens, const html_tag *el, std::set<string_id> used_vars)
  {
    bool replaced_any = false;
    for (int i = 0; i < (int)tokens.size(); i++)
    {
      auto &tok = tokens[i];
      if (tok.type == CV_FUNCTION && lowcase(tok.name) == "var")
      {
        auto args = tok.value;
        if (!check_var_syntax(args))
          continue;
        auto name = _id(args[0].name);
        std::set<string_id> next_used_vars = used_vars;
        next_used_vars.insert(name);
        css_token_vector value;
        // A reference to a property already being substituted is a
        // cycle: that property is invalid at computed-value time, so
        // the var() behaves exactly like one naming an undefined
        // property -- it takes its fallback when it has one. (The
        // token-system idiom `--x: var(--x, 1rem)` on Wikipedia's
        // .vector-icon relies on this; dropping the var() outright
        // emptied the whole declaration.)
        bool cyclic = used_vars.count(name) != 0;
        if (!cyclic && el->get_custom_property(name, value))
        {
          subst_var_nested(value, el, next_used_vars);
          remove(tokens, i);
          insert(tokens, i, value);
          i += (int)value.size() - 1;
          replaced_any = true;
        }
        else
        {
          if (args.size() > 1)
          {
            css_token_vector fallback = args;
            remove(fallback, 0, 2);
            subst_var_nested(fallback, el, next_used_vars);
            remove(tokens, i);
            insert(tokens, i, fallback);
            i += (int)fallback.size() - 1;
            replaced_any = true;
          }
          else
          {
            remove(tokens, i);
            i--;
            replaced_any = true;
          }
        }
      }
      else if (tok.is_component_value())
      {
        if (subst_var_nested(tok.value, el, used_vars))
        {
          replaced_any = true;
        }
      }
    }
    return replaced_any;
  }

  bool evaluate_light_dark(css_token_vector &tokens, document_container *container)
  {
    bool changed = false;
    for (int i = 0; i < (int)tokens.size(); i++)
    {
      auto &tok = tokens[i];
      if (tok.type == CV_FUNCTION && lowcase(tok.name) == "light-dark")
      {
        auto list = parse_comma_separated_list(tok.value);
        if (list.size() == 2)
        {
          color_scheme scheme = color_scheme_light;
          if (container)
          {
            media_features feat;
            container->get_media_features(feat);
            scheme = feat.scheme;
          }
          css_token_vector selected = (scheme == color_scheme_dark) ? list[1] : list[0];
          remove(tokens, i);
          insert(tokens, i, selected);
          changed = true;
          i--;
          continue;
        }
      }
      if (tok.is_component_value())
      {
        if (evaluate_light_dark(tok.value, container))
          changed = true;
      }
    }
    return changed;
  }

  bool evaluate_op(const css_token &left, char op, const css_token &right, css_token &result)
  {
    if (left.type == NUMBER && right.type == NUMBER)
    {
      result = left;
      if (op == '+') result.n.number = left.n.number + right.n.number;
      else if (op == '-') result.n.number = left.n.number - right.n.number;
      else if (op == '*') result.n.number = left.n.number * right.n.number;
      else if (op == '/' && right.n.number != 0) result.n.number = left.n.number / right.n.number;
      else return false;
      return true;
    }
    if ((left.type == DIMENSION || left.type == PERCENTAGE) && right.type == NUMBER)
    {
      result = left;
      if (op == '*') result.n.number = left.n.number * right.n.number;
      else if (op == '/' && right.n.number != 0) result.n.number = left.n.number / right.n.number;
      else return false;
      return true;
    }
    if (left.type == NUMBER && (right.type == DIMENSION || right.type == PERCENTAGE))
    {
      result = right;
      if (op == '*') result.n.number = right.n.number * left.n.number;
      else return false;
      return true;
    }
    if (left.type == right.type && (left.type == DIMENSION || left.type == PERCENTAGE))
    {
      if (left.type == DIMENSION && lowcase(left.unit) != lowcase(right.unit))
        return false;
      result = left;
      if (op == '+') result.n.number = left.n.number + right.n.number;
      else if (op == '-') result.n.number = left.n.number - right.n.number;
      else return false;
      return true;
    }
    return false;
  }

  bool simplify_calc_tokens(css_token_vector &val)
  {
    bool changed = false;
    for (int i = 1; i < (int)val.size() - 1; )
    {
      if (val[i].ch == '*' || val[i].ch == '/')
      {
        css_token res;
        if (evaluate_op(val[i - 1], val[i].ch, val[i + 1], res))
        {
          val[i - 1] = res;
          val.erase(val.begin() + i, val.begin() + i + 2);
          changed = true;
          continue;
        }
      }
      i++;
    }
    for (int i = 1; i < (int)val.size() - 1; )
    {
      if (val[i].ch == '+' || val[i].ch == '-')
      {
        css_token res;
        if (evaluate_op(val[i - 1], val[i].ch, val[i + 1], res))
        {
          val[i - 1] = res;
          val.erase(val.begin() + i, val.begin() + i + 2);
          changed = true;
          continue;
        }
      }
      i++;
    }
    return changed;
  }

  bool evaluate_calc(css_token_vector &tokens, const html_tag *el)
  {
    bool changed = false;
    for (int i = 0; i < (int)tokens.size(); i++)
    {
      auto &tok = tokens[i];
      if (tok.type == CV_FUNCTION && (lowcase(tok.name) == "calc" || lowcase(tok.name) == "min" || lowcase(tok.name) == "max" || lowcase(tok.name) == "clamp"))
      {
        evaluate_calc(tok.value, el);
        string func_name = lowcase(tok.name);
        if (func_name == "calc")
        {
          css_token_vector val;
          for (const auto& t : tok.value) if (t.type != WHITESPACE) val.push_back(t);
          
          while (simplify_calc_tokens(val))
          {
            changed = true;
          }
          if (val.size() == 1 && (val[0].type == DIMENSION || val[0].type == NUMBER || val[0].type == PERCENTAGE))
          {
            css_token inner = val[0];
            remove(tokens, i);
            insert(tokens, i, {inner});
            changed = true;
            continue;
          }
          if (changed)
          {
            tok.value = val;
          }
        }
      }
      if (tok.is_component_value())
      {
        if (evaluate_calc(tok.value, el)) changed = true;
      }
    }
    return changed;
  }

  void subst_vars_(string_id name, css_token_vector &tokens, const html_tag *el)
  {
    std::set<string_id> used_vars = {name};
    subst_var_nested(tokens, el, used_vars);
    evaluate_calc(tokens, el);
  }

  void style::subst_vars(const html_tag *el)
  {
    auto properties = m_properties;
    for (auto &prop : properties)
    {
      if (prop.second.m_has_var)
      {
        auto &value = prop.second.get<css_token_vector>();
        subst_vars_(prop.first, value, el);
        // Re-parse with the declaration's own cascade stamp: a var()
        // shorthand expands to longhands carrying the shorthand's position,
        // so it overwrites earlier declarations it beats and leaves later
        // ones (which add_parsed_property would previously have clobbered,
        // as they sit earlier in the properties vector) untouched.
        add_property(prop.first, value, "", prop.second.m_priority.important, el->get_document()->container(), prop.second.m_priority.layer_rank, prop.second.m_priority.specificity, prop.second.m_priority.order);
      }
    }
  }

  void style::parse_shadow(string_id name, const css_token_vector &tokens, bool important, document_container *container)
  {
    shadow_vector shadows;
    css_token_vector shadow_tokens;
    for (size_t i = 0; i <= tokens.size(); ++i)
    {
      if (i == tokens.size() || (tokens[i].type == COMMA))
      {
        if (!shadow_tokens.empty())
        {
          shadow s;
          int lengths_count = 0;
          for (const auto &tok : shadow_tokens)
          {
            if (tok.ident() == "inset")
              s.inset = true;
            else if (parse_color(tok, s.color, container))
            {
            }
            else
            {
              css_length len;
              if (len.from_token(tok, f_length))
              {
                if (lengths_count == 0)
                  s.x = len;
                else if (lengths_count == 1)
                  s.y = len;
                else if (lengths_count == 2)
                  s.blur = len;
                else if (lengths_count == 3)
                  s.spread = len;
                lengths_count++;
              }
            }
          }
          if (lengths_count >= 2)
            shadows.push_back(s);
          shadow_tokens.clear();
        }
      }
      else
        shadow_tokens.push_back(tokens[i]);
    }
    if (!shadows.empty())
      add_parsed_property(name, property_value(shadows, important, false, m_layer, m_specificity));
  }

  void style::parse_place_shorthand(string_id name, const css_token_vector& tokens, bool important)
  {
    css_token_vector values;
    for (const auto& tok : tokens)
    {
      if (tok.type != WHITESPACE)
        values.push_back(tok);
    }

    if (values.empty() || values.size() > 2) return;

    string_id align_id, justify_id;
    if (name == _place_items_) { align_id = _align_items_; justify_id = _justify_items_; }
    else if (name == _place_content_) { align_id = _align_content_; justify_id = _justify_content_; }
    else if (name == _place_self_) { align_id = _align_self_; justify_id = _justify_self_; }
    else return;

    add_property(align_id, {values[0]}, "", important);
    if (values.size() == 2)
      add_property(justify_id, {values[1]}, "", important);
    else
      add_property(justify_id, {values[0]}, "", important);
  }
} // namespace litehtml
