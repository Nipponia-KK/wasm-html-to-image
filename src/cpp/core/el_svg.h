#ifndef EL_SVG_H
#define EL_SVG_H

#include <cstdio>
#include <sstream>

#include "libs/litehtml/include/litehtml/html_tag.h"

class container_skia;

namespace litehtml {

// ─────────────────────────────────────────────────
// Pure utility functions (extracted for testability)
// ─────────────────────────────────────────────────
// These are inline so they don't require el_svg.cpp
// (which depends on Skia infrastructure) to be compiled.

/// Convert a web_color to SVG-compatible string format:
///   opaque → "#RRGGBB"
///   alpha < 255 → "rgba(r,g,b,a)"
inline std::string color_to_string(const web_color& color) {
    if (color.alpha < 255) {
        std::stringstream ss;
        ss << "rgba(" << (int)color.red << "," << (int)color.green << "," << (int)color.blue << ","
           << (float)color.alpha / 255.0f << ")";
        return ss.str();
    } else {
        char hex[8];
        snprintf(hex, sizeof(hex), "#%02X%02X%02X", color.red, color.green, color.blue);
        return std::string(hex);
    }
}

/// Replace every occurrence of "currentColor" in @p xml with the
/// string representation of @p color.
/// Returns the modified copy.
inline std::string replace_current_color(const std::string& xml, const web_color& color) {
    std::string result = xml;
    std::string color_str = color_to_string(color);
    size_t start_pos = 0;
    while ((start_pos = result.find("currentColor", start_pos)) != std::string::npos) {
        result.replace(start_pos, 12, color_str);
        start_pos += color_str.length();
    }
    return result;
}

/// Replace every CSS var(--name, fallback) in @p xml with its fallback (trimmed). The SVG
/// renderer does not know custom properties, so e.g. fill="var( --color-base, #000 )" would
/// otherwise be dropped and the shape or text left unpainted. Without a fallback the function is
/// replaced by "currentColor".
inline std::string replace_css_vars(const std::string& xml) {
    std::string out;
    size_t i = 0;
    while (i < xml.size()) {
        size_t v = xml.find("var(", i);
        if (v == std::string::npos) {
            out.append(xml, i, std::string::npos);
            break;
        }
        out.append(xml, i, v - i);
        size_t p = v + 4;
        int depth = 1;
        size_t comma = std::string::npos;
        while (p < xml.size() && depth > 0) {
            char c = xml[p];
            if (c == '(') depth++;
            else if (c == ')') depth--;
            else if (c == ',' && depth == 1 && comma == std::string::npos) comma = p;
            if (depth > 0) p++;
        }
        if (depth != 0) {  // unbalanced: leave the rest untouched
            out.append(xml, v, std::string::npos);
            break;
        }
        std::string fallback = comma == std::string::npos ? "currentColor" : xml.substr(comma + 1, p - comma - 1);
        size_t b = fallback.find_first_not_of(" 	");
        size_t e = fallback.find_last_not_of(" 	");
        fallback = b == std::string::npos ? "currentColor" : fallback.substr(b, e - b + 1);
        out += replace_css_vars(fallback);
        i = p + 1;
    }
    return out;
}

// ─────────────────────────────────────────────────
// el_mermaid: <pre class="mermaid"> / <div class="mermaid"> drawn as a diagram (el_mermaid.cpp)
// ─────────────────────────────────────────────────

void draw_svg_xml(::container_skia* container, const std::string& xml, const position& pos);

class el_mermaid : public html_tag {
   public:
    el_mermaid(const std::shared_ptr<document>& doc);
    virtual ~el_mermaid();

    void draw(uint_ptr hdc, pixel_t x, pixel_t y, const position* clip,
              const std::shared_ptr<render_item>& ri) override;
    bool is_replaced() const override { return true; }
    void get_content_size(size& sz, pixel_t max_width) override;
    std::shared_ptr<render_item> create_render_item(
        const std::shared_ptr<render_item>& parent_ri) override;

   private:
    void ensure_svg();
    bool m_ready = false;
    std::string m_inner;  // the drawing inside the root <svg>
    double m_w = 0, m_h = 0;
};

// ─────────────────────────────────────────────────
// el_svg class
// ─────────────────────────────────────────────────

class el_svg : public html_tag {
   public:
    el_svg(const std::shared_ptr<document>& doc);
    virtual ~el_svg();

    void draw(uint_ptr hdc, pixel_t x, pixel_t y, const position* clip,
              const std::shared_ptr<render_item>& ri) override;
    void parse_attributes() override;
    void parse_presentational_hints() override;
    bool is_replaced() const override { return true; }
    bool is_ratio_only_replaced() const override;
    void get_content_size(size& sz, pixel_t max_width) override;
    std::shared_ptr<render_item> create_render_item(
        const std::shared_ptr<render_item>& parent_ri) override;

   private:
    std::string reconstruct_xml(int x, int y, int width, int height) const;
    void write_element(std::ostream& os, const element::ptr& el) const;
    void write_markers(std::ostream& os, const element::ptr& el, const std::string& tag_name) const;
    using element_list_t = std::list<std::shared_ptr<element>>;
    const element* find_marker(const element_list_t& list, const std::string& id) const;
};
}  // namespace litehtml

#endif
