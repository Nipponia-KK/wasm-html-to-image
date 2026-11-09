#include "mermaid/radar.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>

#include "mermaid/mermaid_internal.h"

namespace mermaid {
namespace {

constexpr double kPi = 3.14159265358979323846;

using litehtml::svgutil::trim;

struct Axis {
    std::string id, label;
};

struct Curve {
    std::string id, label;
    std::vector<double> values;
};

bool starts(const std::string& s, const char* p) { return s.compare(0, std::char_traits<char>::length(p), p) == 0; }

std::string unquote(std::string s) {
    s = trim(s);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
    return s;
}

// `id["Label"]` or `id`
void id_label(const std::string& raw, std::string& id, std::string& label) {
    std::string s = trim(raw);
    size_t b = s.find('[');
    if (b != std::string::npos && s.back() == ']') {
        id = trim(s.substr(0, b));
        label = unquote(s.substr(b + 1, s.size() - b - 2));
    } else {
        id = label = s;
    }
}

// Splits at top level commas (not inside [] or {}).
std::vector<std::string> split_top(const std::string& s) {
    std::vector<std::string> out;
    int depth = 0;
    std::string cur;
    for (char c : s) {
        if (c == '[' || c == '{') depth++;
        if (c == ']' || c == '}') depth--;
        if (c == ',' && depth == 0) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!trim(cur).empty()) out.push_back(cur);
    return out;
}

}  // namespace

bool render_radar(const std::string& source, const Options& options, std::string& svg, std::string& error) {
    std::vector<Axis> axes;
    std::vector<Curve> curves;
    std::string title;
    double vmin = 0, vmax = -1;
    int ticks = 5;
    bool circle = false, legend = true, first = true;
    size_t i = 0;
    while (i <= source.size()) {
        size_t e = source.find('\n', i);
        std::string line = trim(source.substr(i, e == std::string::npos ? std::string::npos : e - i));
        i = e == std::string::npos ? source.size() + 1 : e + 1;
        if (line.empty() || starts(line, "%%")) continue;
        if (first) {
            first = false;
            continue;
        }
        if (starts(line, "title ")) {
            title = trim(line.substr(6));
        } else if (starts(line, "axis ")) {
            for (const std::string& part : split_top(line.substr(5))) {
                Axis a;
                id_label(part, a.id, a.label);
                axes.push_back(a);
            }
        } else if (starts(line, "curve ")) {
            std::string rest = trim(line.substr(6));
            size_t open = rest.find('{'), close = rest.rfind('}');
            if (open == std::string::npos || close == std::string::npos || close < open) {
                error = "a curve is `curve id[\"Label\"]{1, 2, 3}`: " + line;
                return false;
            }
            Curve c;
            id_label(rest.substr(0, open), c.id, c.label);
            std::string vals = rest.substr(open + 1, close - open - 1);
            bool keyed = vals.find(':') != std::string::npos;
            c.values.assign(axes.size(), 0);
            size_t pos = 0;
            for (const std::string& part : split_top(vals)) {
                std::string v = trim(part);
                size_t idx = pos++;
                if (keyed) {
                    size_t colon = v.find(':');
                    if (colon == std::string::npos) {
                        error = "mixed value forms: " + line;
                        return false;
                    }
                    std::string key = trim(v.substr(0, colon));
                    v = trim(v.substr(colon + 1));
                    idx = axes.size();
                    for (size_t k = 0; k < axes.size(); k++)
                        if (axes[k].id == key) idx = k;
                    if (idx == axes.size()) {
                        error = "unknown axis: " + key;
                        return false;
                    }
                }
                char* end;
                double d = std::strtod(v.c_str(), &end);
                if (v.empty() || *end != 0) {
                    error = "not a number: " + v;
                    return false;
                }
                if (idx >= axes.size()) {
                    error = "more values than axes: " + line;
                    return false;
                }
                c.values[idx] = d;
            }
            curves.push_back(c);
        } else if (starts(line, "max ")) {
            vmax = std::atof(trim(line.substr(4)).c_str());
        } else if (starts(line, "min ")) {
            vmin = std::atof(trim(line.substr(4)).c_str());
        } else if (starts(line, "ticks ")) {
            ticks = std::max(1, std::atoi(trim(line.substr(6)).c_str()));
        } else if (starts(line, "graticule ")) {
            circle = trim(line.substr(10)) == "circle";
        } else if (starts(line, "showLegend ")) {
            legend = trim(line.substr(11)) != "false";
        } else if (starts(line, "accTitle") || starts(line, "accDescr")) {
            continue;
        } else {
            error = "unrecognised line: " + line;
            return false;
        }
    }
    if (axes.size() < 3) {
        error = "a radar chart needs at least three axes";
        return false;
    }
    if (curves.empty()) {
        error = "a radar chart needs at least one curve";
        return false;
    }
    if (vmax < 0) {
        vmax = vmin;
        for (auto& c : curves)
            for (double v : c.values) vmax = std::max(vmax, v);
    }
    if (vmax <= vmin) {
        error = "max must be greater than min";
        return false;
    }

    Theme th = make_theme(options);
    Text text{options};
    const double R = 150, pad = 24, kFont = 12;
    double label_w = 0;
    for (auto& a : axes) label_w = std::max(label_w, text.width(a.label, kFont, 500));
    double legend_w = 0;
    if (legend)
        for (auto& c : curves) legend_w = std::max(legend_w, text.width(c.label, kFont, 400) + 24);
    double side = R + label_w + 18;
    double top = title.empty() ? pad : pad + 30;
    double cx = pad + side, cy = top + R + 22;
    double W = cx + side + (legend ? legend_w + 24 : 0) + pad;
    if (!title.empty()) W = std::max(W, 2 * pad + text.width(title, 15, 600));
    double H = cy + R + 22 + pad;

    size_t n = axes.size();
    auto pt = [&](size_t k, double frac) {
        double a = -kPi / 2 + 2 * kPi * k / n;
        return Pt{cx + R * frac * std::cos(a), cy + R * frac * std::sin(a)};
    };
    std::string body;
    if (!title.empty()) centered_text(body, th, {title}, W / 2, pad + 8, 15, 600, th.text);
    for (int t = 1; t <= ticks; t++) {
        double frac = (double)t / ticks;
        if (circle) {
            body += "<circle cx=\"" + num(cx) + "\" cy=\"" + num(cy) + "\" r=\"" + num(R * frac) + "\" fill=\"none\" stroke=\"" + th.border + "\" stroke-width=\"1\"/>\n";
        } else {
            std::string pts;
            for (size_t k = 0; k < n; k++) pts += (pts.empty() ? "" : " ") + num(pt(k, frac).x) + "," + num(pt(k, frac).y);
            body += "<polygon points=\"" + pts + "\" fill=\"none\" stroke=\"" + th.border + "\" stroke-width=\"1\"/>\n";
        }
    }
    for (size_t k = 0; k < n; k++) {
        Pt p = pt(k, 1), l = pt(k, 1.0 + 14.0 / R);
        body += "<line x1=\"" + num(cx) + "\" y1=\"" + num(cy) + "\" x2=\"" + num(p.x) + "\" y2=\"" + num(p.y) + "\" stroke=\"" + th.border + "\" stroke-width=\"1\"/>\n";
        double dx = std::cos(-kPi / 2 + 2 * kPi * k / n);
        aligned_text(body, th, {axes[k].label}, l.x, l.y, kFont, 500, th.text, dx > 0.3 ? "start" : dx < -0.3 ? "end" : "middle");
    }
    for (size_t c = 0; c < curves.size(); c++) {
        int pct = curves.size() <= 1 ? 80 : 88 - (int)(60.0 * c / (curves.size() - 1));
        std::string color = mix(th.fg, th.bg, pct), pts;
        for (size_t k = 0; k < n; k++) {
            double frac = std::min(1.0, std::max(0.0, (curves[c].values[k] - vmin) / (vmax - vmin)));
            Pt p = pt(k, frac);
            pts += (pts.empty() ? "" : " ") + num(p.x) + "," + num(p.y);
        }
        body += "<polygon class=\"curve\" points=\"" + pts + "\" fill=\"" + color + "\" fill-opacity=\"0.25\" stroke=\"" + color + "\" stroke-width=\"2\" stroke-linejoin=\"round\"/>\n";
    }
    if (legend) {
        double lx = cx + side + 12;
        for (size_t c = 0; c < curves.size(); c++) {
            int pct = curves.size() <= 1 ? 80 : 88 - (int)(60.0 * c / (curves.size() - 1));
            double y = top + 14 + c * 22;
            body += "<rect x=\"" + num(lx) + "\" y=\"" + num(y - 6) + "\" width=\"12\" height=\"12\" rx=\"2\" fill=\"" + mix(th.fg, th.bg, pct) + "\"/>\n";
            aligned_text(body, th, {curves[c].label}, lx + 20, y, kFont, 400, th.text, "start");
        }
    }
    svg = svg_document(th, W, H, body);
    return true;
}

}  // namespace mermaid
