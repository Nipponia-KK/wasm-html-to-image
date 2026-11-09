#ifndef SATORU_MERMAID_INTERNAL_H
#define SATORU_MERMAID_INTERNAL_H

// Shared by the diagram types: resolved theme colours, text measuring and small SVG helpers.

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "core/svg_paint_utils.h"
#include "mermaid/mermaid.h"

namespace mermaid {

struct Pt {
    double x = 0, y = 0;
};

/// Fixed sizes of the drawing (px), the same scale as the reference renderer so diagrams look alike.
namespace metrics {
constexpr double kNodeFont = 13;
constexpr int kNodeWeight = 500;
constexpr double kEdgeFont = 11;
constexpr int kEdgeWeight = 400;
constexpr double kGroupFont = 12;
constexpr int kGroupWeight = 600;
constexpr double kLineHeight = 1.3;
constexpr double kPadX = 20;
constexpr double kPadY = 10;
constexpr double kStroke = 1;
constexpr double kInnerStroke = 0.75;
constexpr double kArrowW = 8;
constexpr double kArrowH = 5;
constexpr double kMargin = 40;
}  // namespace metrics

/// Colours with every derived value computed (no var() / color-mix() left in the output).
struct Theme {
    std::string bg, fg, accent, line, muted, surface, border;
    std::string text, text_sec, text_faint, arrow, group_fill, group_hdr, inner_stroke, key_badge;
    bool paint_background = false;
    std::string font, mono;
};

inline std::string mix(const std::string& fg, const std::string& bg, int pct) {
    std::string expr = "color-mix(in srgb, " + fg + " " + std::to_string(pct) + "%, " + bg + ")", out;
    return litehtml::svgutil::eval_color_functions(expr, out) ? out : fg;
}

inline Theme make_theme(const Options& o) {
    Theme t;
    t.bg = o.bg;
    t.fg = o.fg;
    t.accent = o.accent.empty() ? mix(o.fg, o.bg, 85) : o.accent;
    t.line = o.line.empty() ? mix(o.fg, o.bg, 50) : o.line;
    t.muted = o.muted;
    t.surface = o.surface.empty() ? mix(o.fg, o.bg, 3) : o.surface;
    t.border = o.border.empty() ? mix(o.fg, o.bg, 20) : o.border;
    t.text = o.fg;
    t.text_sec = o.muted.empty() ? mix(o.fg, o.bg, 60) : o.muted;
    t.text_faint = mix(o.fg, o.bg, 25);
    t.arrow = t.accent;
    t.group_fill = o.bg;
    t.group_hdr = mix(o.fg, o.bg, 5);
    t.inner_stroke = mix(o.fg, o.bg, 12);
    t.key_badge = mix(o.fg, o.bg, 10);
    t.paint_background = o.paint_background;
    t.font = o.font_family;
    t.mono = o.mono_family;
    return t;
}

inline std::string num(double v) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.2f", v);
    std::string s = buf;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s == "-0" ? "0" : s;
}

inline std::string esc(const std::string& v) {
    std::string out;
    for (char c : v) {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '"') out += "&quot;";
        else out += c;
    }
    return out;
}

inline std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] == '\n') {
            out.push_back(cur);
            cur.clear();
        } else if (text[i] == '<' && text.compare(i, 4, "<br>") == 0) {
            out.push_back(cur);
            cur.clear();
            i += 3;
        } else if (text[i] == '<' && (text.compare(i, 5, "<br/>") == 0)) {
            out.push_back(cur);
            cur.clear();
            i += 4;
        } else if (text[i] == '<' && (text.compare(i, 6, "<br />") == 0)) {
            out.push_back(cur);
            cur.clear();
            i += 5;
        } else if (text[i] == '\\' && i + 1 < text.size() && text[i + 1] == 'n') {
            out.push_back(cur);
            cur.clear();
            i += 1;
        } else {
            cur += text[i];
        }
    }
    out.push_back(cur);
    return out;
}

/// Measures through the options' callback, or the estimate.
struct Text {
    const Options& opt;
    double width(const std::string& s, double size, int weight, bool mono = false) const {
        return opt.measure ? opt.measure(s, size, weight, mono) : estimate_text_width(s, size, weight, mono);
    }
    double block_width(const std::vector<std::string>& lines, double size, int weight, bool mono = false) const {
        double w = 0;
        for (auto& l : lines) w = std::max(w, width(l, size, weight, mono));
        return w;
    }
};

/// Escaped text for a `<text>`.
inline std::string text_content(const std::string& s) { return esc(s); }

/// A `<text>` that is centred on (cx, cy); one `<text>` per line so no tspan support is needed.
inline void centered_text(std::string& out, const Theme& th, const std::vector<std::string>& lines, double cx, double cy,
                          double size, int weight, const std::string& fill, bool mono = false) {
    double lh = size * metrics::kLineHeight;
    double y0 = cy - lh * (lines.size() - 1) / 2.0;
    for (size_t i = 0; i < lines.size(); i++) {
        out += "<text x=\"" + num(cx) + "\" y=\"" + num(y0 + lh * i + size * 0.35) + "\" text-anchor=\"middle\" font-size=\"" +
               num(size) + "\" font-weight=\"" + std::to_string(weight) + "\" font-family=\"" + esc(mono ? th.mono : th.font) +
               "\" fill=\"" + fill + "\">" + text_content(lines[i]) + "</text>\n";
    }
}

/// Like centered_text, anchored at x: "start" (left aligned), "middle" or "end".
inline void aligned_text(std::string& out, const Theme& th, const std::vector<std::string>& lines, double x, double cy, double size,
                         int weight, const std::string& fill, const char* anchor, bool mono = false) {
    double lh = size * metrics::kLineHeight;
    double y0 = cy - lh * (lines.size() - 1) / 2.0;
    for (size_t i = 0; i < lines.size(); i++) {
        out += "<text x=\"" + num(x) + "\" y=\"" + num(y0 + lh * i + size * 0.35) + "\" text-anchor=\"" + anchor + "\" font-size=\"" + num(size) +
               "\" font-weight=\"" + std::to_string(weight) + "\" font-family=\"" + esc(mono ? th.mono : th.font) + "\" fill=\"" + fill + "\">" +
               text_content(lines[i]) + "</text>\n";
    }
}

/// Wrap a drawing: viewBox and size are the content box plus margin, the origin is (0, 0).
inline std::string svg_document(const Theme& th, double w, double h, const std::string& body) {
    std::string s = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " + num(w) + " " + num(h) + "\" width=\"" + num(w) +
                    "\" height=\"" + num(h) + "\">\n";
    if (th.paint_background) s += "<rect width=\"" + num(w) + "\" height=\"" + num(h) + "\" fill=\"" + th.bg + "\"/>\n";
    return s + body + "</svg>\n";
}

}  // namespace mermaid

#endif  // SATORU_MERMAID_INTERNAL_H
