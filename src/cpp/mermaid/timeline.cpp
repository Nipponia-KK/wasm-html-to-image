#include "mermaid/timeline.h"

#include <algorithm>

#include "mermaid/mermaid_internal.h"

namespace mermaid {
namespace {

using litehtml::svgutil::trim;

struct Period {
    std::string label;
    int section = -1;
    std::vector<std::string> events;
    double x = 0, w = 0;
};

bool starts(const std::string& s, const char* p) { return s.compare(0, std::char_traits<char>::length(p), p) == 0; }

}  // namespace

bool render_timeline(const std::string& source, const Options& options, std::string& svg, std::string& error) {
    using namespace metrics;
    std::string title;
    std::vector<std::string> sections;
    std::vector<Period> periods;
    bool first = true;
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
            continue;
        }
        if (starts(line, "section ")) {
            sections.push_back(trim(line.substr(8)));
            continue;
        }
        // `period : event : event`, or `: event` for one more event of the previous period
        std::vector<std::string> parts;
        std::string cur;
        for (char c : line) {
            if (c == ':') parts.push_back(trim(cur)), cur.clear();
            else cur += c;
        }
        parts.push_back(trim(cur));
        if (parts.size() == 1) {
            // a period with no event
            Period p;
            p.label = parts[0];
            p.section = (int)sections.size() - 1;
            periods.push_back(p);
            continue;
        }
        size_t from = 1;
        if (parts[0].empty()) {
            if (periods.empty()) {
                error = "an event needs a period before it: " + line;
                return false;
            }
        } else {
            Period p;
            p.label = parts[0];
            p.section = (int)sections.size() - 1;
            periods.push_back(p);
        }
        for (size_t k = from; k < parts.size(); k++)
            if (!parts[k].empty()) periods.back().events.push_back(parts[k]);
    }
    if (periods.empty()) {
        error = "a timeline needs at least one period";
        return false;
    }

    Theme th = make_theme(options);
    Text text{options};
    static const char* kColors[] = {"#4e79a7", "#f28e2b", "#59a14f", "#e15759", "#76b7b2", "#b07aa1", "#edc948", "#9c755f"};
    const double kFont = 12.5, kColGap = 16, kPadX = 12, kCardPad = 8, kCardGap = 8, kMinCol = 110, kPeriodH = 34, kSectionH = 28;
    const double lh = kFont * kLineHeight;

    // columns
    double x = kMargin;
    for (auto& p : periods) {
        double w = text.width(p.label, kNodeFont, 600);
        for (auto& ev : p.events) w = std::max(w, text.block_width(split_lines(ev), kFont, 400));
        p.w = std::max(kMinCol, w + 2 * kPadX);
        p.x = x;
        x += p.w + kColGap;
    }
    double right = x - kColGap + kMargin;
    bool has_sections = !sections.empty();
    double y = title.empty() ? kMargin : kMargin + 26;
    double section_y = y;
    if (has_sections) y += kSectionH + 10;
    double period_y = y, axis_y = period_y + kPeriodH + 18, events_y = axis_y + 20;
    double bottom = events_y;
    for (auto& p : periods) {
        double h = events_y;
        for (auto& ev : p.events) h += split_lines(ev).size() * lh + 2 * kCardPad + kCardGap;
        bottom = std::max(bottom, h);
    }
    double H = bottom + kMargin - kCardGap;
    auto color_of = [&](const Period& p, size_t k) { return std::string(kColors[(has_sections && p.section >= 0 ? (size_t)p.section : k) % 8]); };

    std::string body;
    if (!title.empty()) centered_text(body, th, {title}, right / 2, kMargin, 16, 600, th.text);
    // sections span their periods
    if (has_sections) {
        for (size_t s = 0; s < sections.size(); s++) {
            double x0 = 1e18, x1 = -1e18;
            for (auto& p : periods)
                if (p.section == (int)s) x0 = std::min(x0, p.x), x1 = std::max(x1, p.x + p.w);
            if (x0 > x1) continue;
            body += "<rect x=\"" + num(x0) + "\" y=\"" + num(section_y) + "\" width=\"" + num(x1 - x0) + "\" height=\"" + num(kSectionH) + "\" rx=\"4\" fill=\"" + kColors[s % 8] + "\"/>\n";
            centered_text(body, th, {sections[s]}, (x0 + x1) / 2, section_y + kSectionH / 2, kNodeFont, 600, "#ffffff");
        }
    }
    // the axis, with an arrow head
    body += "<line x1=\"" + num(kMargin - 12) + "\" y1=\"" + num(axis_y) + "\" x2=\"" + num(right - kMargin + 8) + "\" y2=\"" + num(axis_y) + "\" stroke=\"" + th.line + "\" stroke-width=\"2\"/>\n";
    body += "<polygon points=\"" + num(right - kMargin + 16) + "," + num(axis_y) + " " + num(right - kMargin + 6) + "," + num(axis_y - 5) + " " + num(right - kMargin + 6) + "," + num(axis_y + 5) + "\" fill=\"" + th.line + "\"/>\n";
    for (size_t k = 0; k < periods.size(); k++) {
        const Period& p = periods[k];
        std::string color = color_of(p, k);
        double cx = p.x + p.w / 2;
        body += "<g class=\"period\">\n<rect x=\"" + num(p.x) + "\" y=\"" + num(period_y) + "\" width=\"" + num(p.w) + "\" height=\"" + num(kPeriodH) + "\" rx=\"4\" fill=\"" + mix(color, th.bg, 22) +
                "\" stroke=\"" + color + "\" stroke-width=\"1.5\"/>\n";
        centered_text(body, th, {p.label}, cx, period_y + kPeriodH / 2, kNodeFont, 600, th.text);
        body += "</g>\n";
        body += "<line x1=\"" + num(cx) + "\" y1=\"" + num(period_y + kPeriodH) + "\" x2=\"" + num(cx) + "\" y2=\"" + num(axis_y) + "\" stroke=\"" + color + "\" stroke-width=\"1.5\"/>\n";
        body += "<circle cx=\"" + num(cx) + "\" cy=\"" + num(axis_y) + "\" r=\"5\" fill=\"" + color + "\" stroke=\"" + th.bg + "\" stroke-width=\"2\"/>\n";
        double ey = events_y;
        for (auto& ev : p.events) {
            auto lines = split_lines(ev);
            double h = lines.size() * lh + 2 * kCardPad;
            body += "<g class=\"event\">\n<rect x=\"" + num(p.x) + "\" y=\"" + num(ey) + "\" width=\"" + num(p.w) + "\" height=\"" + num(h) + "\" rx=\"4\" fill=\"" + th.surface + "\" stroke=\"" + color +
                    "\" stroke-width=\"1\"/>\n";
            centered_text(body, th, lines, cx, ey + h / 2, kFont, 400, th.text);
            body += "</g>\n";
            ey += h + kCardGap;
        }
    }
    svg = svg_document(th, right, H, body);
    return true;
}

}  // namespace mermaid
