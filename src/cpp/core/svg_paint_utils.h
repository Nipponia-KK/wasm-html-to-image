#ifndef SATORU_SVG_PAINT_UTILS_H
#define SATORU_SVG_PAINT_UTILS_H

// Standard-library-only helpers for inline SVG (host-testable, see tests/cpp/test_svg_paint_utils.cpp):
//  - eval_color_functions: replaces color-mix() with a plain colour, since SkSVGDOM only reads
//    plain colours and the cascade has already substituted var();
//  - marker geometry: SkSVGDOM draws no <marker>, so el_svg expands marker-start / marker-end into
//    a transformed group, which needs the vertex list and the placement transform computed here.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace litehtml {
namespace svgutil {

struct Rgba {
    double r = 0, g = 0, b = 0, a = 1;  // r, g, b in 0..255, a in 0..1
};

inline std::string trim(const std::string& v) {
    size_t b = v.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = v.find_last_not_of(" \t\r\n");
    return v.substr(b, e - b + 1);
}

inline std::string lower(std::string v) {
    for (auto& c : v) c = (char)std::tolower((unsigned char)c);
    return v;
}

inline int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/// Plain colours only: #rgb, #rgba, #rrggbb, #rrggbbaa, rgb()/rgba() and a few keywords.
inline bool parse_color(const std::string& in, Rgba& out) {
    std::string s = lower(trim(in));
    if (s.empty()) return false;
    if (s[0] == '#') {
        std::vector<int> d;
        for (size_t i = 1; i < s.size(); i++) {
            int h = hex_digit(s[i]);
            if (h < 0) return false;
            d.push_back(h);
        }
        if (d.size() == 3 || d.size() == 4) {
            out.r = d[0] * 17;
            out.g = d[1] * 17;
            out.b = d[2] * 17;
            out.a = d.size() == 4 ? d[3] * 17 / 255.0 : 1;
            return true;
        }
        if (d.size() == 6 || d.size() == 8) {
            out.r = d[0] * 16 + d[1];
            out.g = d[2] * 16 + d[3];
            out.b = d[4] * 16 + d[5];
            out.a = d.size() == 8 ? (d[6] * 16 + d[7]) / 255.0 : 1;
            return true;
        }
        return false;
    }
    if (s.rfind("rgb", 0) == 0) {
        size_t open = s.find('('), close = s.rfind(')');
        if (open == std::string::npos || close == std::string::npos || close < open) return false;
        std::string args = s.substr(open + 1, close - open - 1);
        for (auto& c : args)
            if (c == ',' || c == '/') c = ' ';
        std::vector<double> v;
        std::vector<bool> pct;
        size_t i = 0;
        while (i < args.size()) {
            while (i < args.size() && args[i] == ' ') i++;
            if (i >= args.size()) break;
            char* end = nullptr;
            double n = std::strtod(args.c_str() + i, &end);
            if (end == args.c_str() + i) return false;
            i = (size_t)(end - args.c_str());
            bool p = i < args.size() && args[i] == '%';
            if (p) i++;
            v.push_back(n);
            pct.push_back(p);
        }
        if (v.size() < 3 || v.size() > 4) return false;
        auto ch = [&](size_t k) { return pct[k] ? v[k] * 2.55 : v[k]; };
        out.r = ch(0);
        out.g = ch(1);
        out.b = ch(2);
        out.a = v.size() == 4 ? (pct[3] ? v[3] / 100 : v[3]) : 1;
        return true;
    }
    if (s == "white") return out = {255, 255, 255, 1}, true;
    if (s == "black") return out = {0, 0, 0, 1}, true;
    if (s == "transparent") return out = {0, 0, 0, 0}, true;
    return false;
}

inline std::string color_string(const Rgba& c) {
    auto clamp = [](double v) { return (int)std::lround(std::min(255.0, std::max(0.0, v))); };
    char buf[64];
    if (c.a >= 0.999) {
        std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", clamp(c.r), clamp(c.g), clamp(c.b));
    } else {
        std::snprintf(buf, sizeof(buf), "rgba(%d,%d,%d,%.3g)", clamp(c.r), clamp(c.g), clamp(c.b),
                      std::min(1.0, std::max(0.0, c.a)));
    }
    return buf;
}

// Split at top-level commas.
inline std::vector<std::string> split_args(const std::string& s) {
    std::vector<std::string> out;
    int depth = 0;
    size_t start = 0;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '(') depth++;
        else if (s[i] == ')') depth--;
        else if (s[i] == ',' && depth == 0) {
            out.push_back(trim(s.substr(start, i - start)));
            start = i + 1;
        }
    }
    out.push_back(trim(s.substr(start)));
    return out;
}

// "<color> [<percentage>]" or "[<percentage>] <color>"; the colour must be a plain one by now.
inline bool parse_mix_arg(const std::string& arg, Rgba& color, double& pct) {
    std::string s = trim(arg);
    pct = -1;
    if (s.empty()) return false;
    if (s.back() == '%') {
        size_t sp = s.find_last_of(" \t");
        if (sp == std::string::npos) return false;
        pct = std::strtod(s.c_str() + sp + 1, nullptr);
        s = trim(s.substr(0, sp));
    } else if (std::isdigit((unsigned char)s[0]) || s[0] == '.') {
        size_t sp = s.find_first_of(" \t");
        if (sp != std::string::npos && s[sp - 1] == '%') {
            pct = std::strtod(s.c_str(), nullptr);
            s = trim(s.substr(sp));
        }
    }
    return parse_color(s, color);
}

/// Replace every color-mix(in <space>, c1 [p1], c2 [p2]) in @p in with its result. The mix is done in
/// sRGB on premultiplied colours whatever space is named (exact for srgb, an approximation for the
/// others). Returns false and leaves @p out unset when a color-mix cannot be evaluated (a colour that is
/// not a plain one, e.g. an unresolved var()).
inline bool eval_color_functions(const std::string& in, std::string& out) {
    out.clear();
    size_t i = 0;
    while (i < in.size()) {
        size_t m = in.find("color-mix(", i);
        if (m == std::string::npos) {
            out.append(in, i, std::string::npos);
            return true;
        }
        out.append(in, i, m - i);
        size_t p = m + 10;
        int depth = 1;
        while (p < in.size() && depth > 0) {
            if (in[p] == '(') depth++;
            else if (in[p] == ')') depth--;
            if (depth > 0) p++;
        }
        if (depth != 0) return false;
        auto args = split_args(in.substr(m + 10, p - m - 10));
        if (args.size() != 3) return false;
        std::string a1, a2;
        if (!eval_color_functions(args[1], a1) || !eval_color_functions(args[2], a2)) return false;
        Rgba c1, c2;
        double p1, p2;
        if (!parse_mix_arg(a1, c1, p1) || !parse_mix_arg(a2, c2, p2)) return false;
        if (p1 < 0 && p2 < 0) p1 = p2 = 50;
        else if (p1 < 0) p1 = 100 - p2;
        else if (p2 < 0) p2 = 100 - p1;
        double sum = p1 + p2;
        if (sum <= 0) return false;
        double w1 = p1 / sum, w2 = p2 / sum;
        double alpha = c1.a * w1 + c2.a * w2;
        Rgba r;
        r.a = alpha * std::min(1.0, sum / 100);  // percentages under 100 leave the rest transparent
        if (alpha > 0) {
            r.r = (c1.r * c1.a * w1 + c2.r * c2.a * w2) / alpha;
            r.g = (c1.g * c1.a * w1 + c2.g * c2.a * w2) / alpha;
            r.b = (c1.b * c1.a * w1 + c2.b * c2.a * w2) / alpha;
        }
        out += color_string(r);
        i = p + 1;
    }
    return true;
}

// ─── markers ────────────────────────────────────────────────────────────────

struct Point {
    double x = 0, y = 0;
};

/// The numbers of a `points` attribute (comma and/or space separated) as pairs.
inline std::vector<Point> parse_points(const std::string& s) {
    std::vector<double> nums;
    const char* p = s.c_str();
    while (*p) {
        while (*p && (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
        if (!*p) break;
        char* end = nullptr;
        double v = std::strtod(p, &end);
        if (end == p) break;
        nums.push_back(v);
        p = end;
    }
    std::vector<Point> pts;
    for (size_t i = 0; i + 1 < nums.size(); i += 2) pts.push_back({nums[i], nums[i + 1]});
    return pts;
}

/// Direction in degrees of the first segment (at == 0) or of the last segment (at == 1) with length;
/// zero-length segments are skipped, as the spec asks.
inline double segment_angle(const std::vector<Point>& pts, bool at_end) {
    if (pts.size() < 2) return 0;
    const double kPi = 3.14159265358979323846;
    if (at_end) {
        for (size_t i = pts.size() - 1; i > 0; i--) {
            double dx = pts[i].x - pts[i - 1].x, dy = pts[i].y - pts[i - 1].y;
            if (dx != 0 || dy != 0) return std::atan2(dy, dx) * 180 / kPi;
        }
    } else {
        for (size_t i = 1; i < pts.size(); i++) {
            double dx = pts[i].x - pts[i - 1].x, dy = pts[i].y - pts[i - 1].y;
            if (dx != 0 || dy != 0) return std::atan2(dy, dx) * 180 / kPi;
        }
    }
    return 0;
}

struct MarkerDef {
    double refX = 0, refY = 0;
    double markerWidth = 3, markerHeight = 3;
    bool has_viewBox = false;
    double vbX = 0, vbY = 0, vbW = 0, vbH = 0;
    bool stroke_units = true;  // markerUnits="strokeWidth" (the default) or "userSpaceOnUse"
};

/// `transform` that puts a marker's content, drawn in its own coordinates, on a vertex: the reference
/// point lands on the vertex, the content turns by @p angle and scales by the stroke width (markerUnits
/// strokeWidth) and by the viewBox-to-viewport ratio. The viewport's own clip and alignment offset are
/// not modelled; arrowheads fit their viewport.
inline std::string marker_transform(const MarkerDef& m, Point vertex, double angle, double stroke_width) {
    double s = 1;
    if (m.has_viewBox && m.vbW > 0 && m.vbH > 0) s = std::min(m.markerWidth / m.vbW, m.markerHeight / m.vbH);
    if (m.stroke_units) s *= stroke_width;
    char buf[256];
    std::snprintf(buf, sizeof(buf), "translate(%g %g) rotate(%g) scale(%g) translate(%g %g)", vertex.x, vertex.y,
                  angle, s, -m.refX, -m.refY);
    return buf;
}

}  // namespace svgutil
}  // namespace litehtml

#endif  // SATORU_SVG_PAINT_UTILS_H
