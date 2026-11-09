#include "mermaid/xychart.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "mermaid/mermaid_internal.h"

namespace mermaid {
namespace {

using litehtml::svgutil::trim;

struct Series {
    bool bar;
    std::string name;
    std::vector<double> values;
};

struct Chart {
    std::string title, x_title, y_title;
    std::vector<std::string> categories;
    bool has_x_range = false, has_y_range = false;
    double x_min = 0, x_max = 0, y_min = 0, y_max = 0;
    std::vector<Series> series;
};

bool starts(const std::string& s, const char* p) { return s.compare(0, std::char_traits<char>::length(p), p) == 0; }

std::string unquote(std::string s) {
    s = trim(s);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
    return s;
}

/// The items of `[a, "b c", 3]`.
bool parse_list(const std::string& text, std::vector<std::string>& out) {
    size_t open = text.find('['), close = text.rfind(']');
    if (open == std::string::npos || close == std::string::npos || close < open) return false;
    std::string cur;
    bool quoted = false;
    for (size_t i = open + 1; i < close; i++) {
        char c = text[i];
        if (c == '"') quoted = !quoted;
        else if (c == ',' && !quoted) {
            out.push_back(unquote(cur));
            cur.clear();
        } else cur += c;
    }
    if (!trim(cur).empty() || !out.empty()) out.push_back(unquote(cur));
    return true;
}

/// `min --> max` after an optional quoted title.
bool parse_range(const std::string& text, double& lo, double& hi) {
    size_t arrow = text.find("-->");
    if (arrow == std::string::npos) return false;
    std::string a = trim(text.substr(0, arrow)), b = trim(text.substr(arrow + 3));
    size_t sp = a.find_last_of(" \t");
    if (sp != std::string::npos) a = a.substr(sp + 1);
    char *e1, *e2;
    lo = std::strtod(a.c_str(), &e1);
    hi = std::strtod(b.c_str(), &e2);
    return *e1 == 0 && *e2 == 0 && !a.empty() && !b.empty();
}

std::string leading_title(const std::string& text) {
    std::string t = trim(text);
    if (t.empty() || t[0] != '"') return "";
    size_t q = t.find('"', 1);
    return q == std::string::npos ? "" : t.substr(1, q - 1);
}

bool parse(const std::string& source, Chart& c, std::string& error) {
    bool first = true;
    size_t i = 0;
    while (i <= source.size()) {
        size_t e = source.find('\n', i);
        std::string line = trim(source.substr(i, e == std::string::npos ? std::string::npos : e - i));
        i = e == std::string::npos ? source.size() + 1 : e + 1;
        if (line.empty() || starts(line, "%%")) continue;
        if (first) {
            first = false;
            if (line.find("horizontal") != std::string::npos) {
                error = "horizontal xychart is not supported";
                return false;
            }
            continue;
        }
        if (starts(line, "title")) {
            c.title = unquote(line.substr(5));
        } else if (starts(line, "x-axis")) {
            std::string rest = trim(line.substr(6));
            c.x_title = leading_title(rest);
            if (rest.find('[') != std::string::npos) {
                if (!parse_list(rest, c.categories)) {
                    error = "bad x-axis list: " + line;
                    return false;
                }
            } else if (parse_range(rest, c.x_min, c.x_max)) {
                c.has_x_range = true;
            } else if (c.x_title.empty()) {
                c.x_title = unquote(rest);
            }
        } else if (starts(line, "y-axis")) {
            std::string rest = trim(line.substr(6));
            c.y_title = leading_title(rest);
            if (parse_range(rest, c.y_min, c.y_max)) c.has_y_range = true;
            else if (c.y_title.empty()) c.y_title = unquote(rest);
        } else if (starts(line, "bar") || starts(line, "line")) {
            Series s;
            s.bar = starts(line, "bar");
            std::string rest = trim(line.substr(s.bar ? 3 : 4));
            s.name = leading_title(rest);
            std::vector<std::string> items;
            if (!parse_list(rest, items)) {
                error = "a series needs a [list]: " + line;
                return false;
            }
            for (auto& it : items) {
                char* end;
                double v = std::strtod(it.c_str(), &end);
                if (it.empty() || *end != 0) {
                    error = "not a number: " + it;
                    return false;
                }
                s.values.push_back(v);
            }
            c.series.push_back(s);
        } else {
            error = "unrecognised line: " + line;
            return false;
        }
    }
    if (c.series.empty()) {
        error = "an xychart needs a bar or line series";
        return false;
    }
    return true;
}

double nice_step(double range, int target) {
    double raw = range / target, base = std::pow(10.0, std::floor(std::log10(raw))), f = raw / base;
    return (f <= 1 ? 1 : f <= 2 ? 2 : f <= 5 ? 5 : 10) * base;
}

}  // namespace

bool render_xychart(const std::string& source, const Options& options, std::string& svg, std::string& error) {
    Chart c;
    if (!parse(source, c, error)) return false;
    Theme th = make_theme(options);
    Text text{options};

    size_t n = 0;
    for (auto& s : c.series) n = std::max(n, s.values.size());
    if (n == 0) {
        error = "a series has no values";
        return false;
    }
    std::vector<std::string> labels = c.categories;
    if (labels.empty()) {
        for (size_t k = 0; k < n; k++) {
            double v = c.has_x_range ? (n > 1 ? c.x_min + (c.x_max - c.x_min) * k / (n - 1) : c.x_min) : (double)(k + 1);
            labels.push_back(num(v));
        }
    }
    n = std::max(n, labels.size());

    double lo = 0, hi = 0;
    bool any = false;
    for (auto& s : c.series)
        for (double v : s.values) {
            lo = any ? std::min(lo, v) : v;
            hi = any ? std::max(hi, v) : v;
            any = true;
        }
    bool has_bar = std::any_of(c.series.begin(), c.series.end(), [](const Series& s) { return s.bar; });
    if (has_bar) lo = std::min(lo, 0.0), hi = std::max(hi, 0.0);
    if (hi - lo < 1e-9) hi = lo + 1;
    double step, y0, y1;
    if (c.has_y_range) {
        y0 = c.y_min;
        y1 = c.y_max;
        step = nice_step(y1 - y0, 5);
    } else {
        step = nice_step(hi - lo, 5);
        y0 = std::floor(lo / step) * step;
        y1 = std::ceil(hi / step) * step;
    }
    if (y1 - y0 < 1e-9) y1 = y0 + step;
    std::vector<double> ticks;
    for (double t = std::ceil(y0 / step - 1e-9) * step; t <= y1 + step * 1e-6; t += step) ticks.push_back(t);

    const double W = 640, H = 360, kTick = 11, kLabel = 12;
    double tick_w = 0;
    for (double t : ticks) tick_w = std::max(tick_w, text.width(num(t), kTick, 400));
    double left = 16 + (c.y_title.empty() ? 0 : 18) + tick_w + 10;
    double right = 24, top = c.title.empty() ? 20 : 44;
    double bottom = 36 + (c.x_title.empty() ? 0 : 20);
    double pw = W - left - right, ph = H - top - bottom;
    auto X = [&](size_t k) { return left + pw * (k + 0.5) / n; };
    auto Y = [&](double v) { return top + ph * (1 - (v - y0) / (y1 - y0)); };

    std::string body;
    if (!c.title.empty()) {
        std::vector<std::string> t = {c.title};
        centered_text(body, th, t, W / 2, 22, 15, 600, th.text);
    }
    for (double t : ticks) {
        double y = Y(t);
        body += "<line x1=\"" + num(left) + "\" y1=\"" + num(y) + "\" x2=\"" + num(left + pw) + "\" y2=\"" + num(y) + "\" stroke=\"" + th.inner_stroke + "\" stroke-width=\"1\"/>\n";
        aligned_text(body, th, {num(t)}, left - 8, y, kTick, 400, th.text_sec, "end");
    }
    // the axes
    body += "<line x1=\"" + num(left) + "\" y1=\"" + num(top) + "\" x2=\"" + num(left) + "\" y2=\"" + num(top + ph) + "\" stroke=\"" + th.border + "\" stroke-width=\"1\"/>\n";
    double base = Y(std::max(y0, std::min(0.0, y1)));
    body += "<line x1=\"" + num(left) + "\" y1=\"" + num(base) + "\" x2=\"" + num(left + pw) + "\" y2=\"" + num(base) + "\" stroke=\"" + th.border + "\" stroke-width=\"1\"/>\n";

    // category labels: thinned out when they would touch
    double widest = 0;
    for (auto& l : labels) widest = std::max(widest, text.width(l, kTick, 400));
    size_t every = std::max<size_t>(1, (size_t)std::ceil((widest + 8) / (pw / n)));
    for (size_t k = 0; k < labels.size() && k < n; k += every)
        centered_text(body, th, {labels[k]}, X(k), top + ph + 16, kTick, 400, th.text_sec);
    if (!c.x_title.empty()) centered_text(body, th, {c.x_title}, left + pw / 2, H - 12, kLabel, 500, th.text);
    if (!c.y_title.empty()) {
        double cy = top + ph / 2;
        body += "<text transform=\"rotate(-90 14 " + num(cy) + ")\" x=\"14\" y=\"" + num(cy + kLabel * 0.35) + "\" text-anchor=\"middle\" font-size=\"" + num(kLabel) +
                "\" font-weight=\"500\" font-family=\"" + esc(th.font) + "\" fill=\"" + th.text + "\">" + esc(c.y_title) + "</text>\n";
    }

    // bars side by side inside each category, then the lines on top
    size_t bar_count = 0;
    for (auto& s : c.series) bar_count += s.bar ? 1 : 0;
    double slot = pw / n, group = slot * 0.7, bw = bar_count ? group / bar_count : 0;
    // a given accent colours the series; without one the bars are a mid grey so the line stays readable on them
    const std::string bar_color = options.accent.empty() ? th.line : th.accent;
    static const double kBarOpacity[] = {1.0, 0.6, 0.35, 0.8};
    size_t bi = 0, li = 0;
    for (auto& s : c.series) {
        if (!s.bar) continue;
        double op = kBarOpacity[bi % 4];
        for (size_t k = 0; k < s.values.size(); k++) {
            double x = X(k) - group / 2 + bw * bi, y = Y(s.values[k]), h = std::fabs(base - y);
            body += "<rect x=\"" + num(x + 1) + "\" y=\"" + num(std::min(y, base)) + "\" width=\"" + num(std::max(1.0, bw - 2)) + "\" height=\"" + num(h) + "\" rx=\"2\" fill=\"" + bar_color +
                    "\" fill-opacity=\"" + num(op) + "\"/>\n";
        }
        bi++;
    }
    static const char* kDash[] = {"", " stroke-dasharray=\"6 4\"", " stroke-dasharray=\"2 3\""};
    for (auto& s : c.series) {
        if (s.bar) continue;
        std::string color = has_bar ? th.text : (options.accent.empty() ? th.text : th.accent), pts;
        for (size_t k = 0; k < s.values.size(); k++) pts += (pts.empty() ? "" : " ") + num(X(k)) + "," + num(Y(s.values[k]));
        body += "<polyline class=\"series\" points=\"" + pts + "\" fill=\"none\" stroke=\"" + color + "\" stroke-width=\"2\" stroke-linejoin=\"round\"" + kDash[li % 3] + "/>\n";
        for (size_t k = 0; k < s.values.size(); k++)
            body += "<circle cx=\"" + num(X(k)) + "\" cy=\"" + num(Y(s.values[k])) + "\" r=\"3.5\" fill=\"" + th.bg + "\" stroke=\"" + color + "\" stroke-width=\"2\"/>\n";
        li++;
    }
    svg = svg_document(th, W, H, body);
    return true;
}

}  // namespace mermaid
