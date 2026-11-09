#include "mermaid/journey.h"

#include <algorithm>
#include <cstdlib>

#include "mermaid/mermaid_internal.h"

namespace mermaid {
namespace {

using litehtml::svgutil::trim;

struct Task {
    std::string name;
    int score = 3;
    int section = -1;
    std::vector<int> actors;
};

bool starts(const std::string& s, const char* p) { return s.compare(0, std::char_traits<char>::length(p), p) == 0; }

}  // namespace

bool render_journey(const std::string& source, const Options& options, std::string& svg, std::string& error) {
    using namespace metrics;
    std::string title;
    std::vector<std::string> sections, actors;
    std::vector<Task> tasks;
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
        if (starts(line, "title ")) title = trim(line.substr(6));
        else if (starts(line, "section ")) sections.push_back(trim(line.substr(8)));
        else {
            size_t c1 = line.find(':');
            if (c1 == std::string::npos) {
                error = "a task is `name: score: actors`: " + line;
                return false;
            }
            Task t;
            t.name = trim(line.substr(0, c1));
            size_t c2 = line.find(':', c1 + 1);
            std::string score = trim(line.substr(c1 + 1, c2 == std::string::npos ? std::string::npos : c2 - c1 - 1));
            char* end;
            long v = std::strtol(score.c_str(), &end, 10);
            if (score.empty() || *end != 0 || v < 1 || v > 5) {
                error = "the score is a number from 1 to 5: " + line;
                return false;
            }
            t.score = (int)v;
            t.section = (int)sections.size() - 1;
            if (c2 != std::string::npos) {
                std::string rest = line.substr(c2 + 1), cur;
                rest += ',';
                for (char ch : rest) {
                    if (ch != ',') {
                        cur += ch;
                        continue;
                    }
                    std::string a = trim(cur);
                    cur.clear();
                    if (a.empty()) continue;
                    auto it = std::find(actors.begin(), actors.end(), a);
                    if (it == actors.end()) actors.push_back(a), it = actors.end() - 1;
                    t.actors.push_back((int)(it - actors.begin()));
                }
            }
            tasks.push_back(t);
        }
    }
    if (tasks.empty()) {
        error = "a journey needs at least one task";
        return false;
    }

    Theme th = make_theme(options);
    Text text{options};
    static const char* kActor[] = {"#4e79a7", "#f28e2b", "#59a14f", "#b07aa1", "#76b7b2", "#9c755f", "#edc948", "#e15759"};
    static const char* kScore[] = {"#e5484d", "#f28e2b", "#edc948", "#76b7b2", "#59a14f"};
    const double kColW = 140, kTaskH = 46, kLevel = 28, kFace = 13, kSectionH = 28;
    double left = kMargin;
    // legend
    double top = title.empty() ? kMargin : kMargin + 28;
    std::string body;
    if (!title.empty()) centered_text(body, th, {title}, left + tasks.size() * kColW / 2, kMargin, 16, 600, th.text);
    double ly = top;
    for (size_t a = 0; a < actors.size(); a++) {
        body += "<circle cx=\"" + num(left + 6) + "\" cy=\"" + num(ly + 6) + "\" r=\"5\" fill=\"" + kActor[a % 8] + "\"/>\n";
        aligned_text(body, th, {actors[a]}, left + 18, ly + 6, 12, 400, th.text, "start");
        ly += 20;
    }
    double chart_top = std::max(top, ly) + (actors.empty() ? 0 : 10);
    double section_y = chart_top, task_y = section_y + (sections.empty() ? 0 : kSectionH + 6);
    double plot_y = task_y + kTaskH + 34, W = left + tasks.size() * kColW + kMargin, H = plot_y + 5 * kLevel + kMargin;

    // sections
    for (size_t s = 0; s < sections.size(); s++) {
        size_t a = tasks.size(), b = 0;
        for (size_t k = 0; k < tasks.size(); k++)
            if (tasks[k].section == (int)s) a = std::min(a, k), b = std::max(b, k);
        if (a > b) continue;
        body += "<rect x=\"" + num(left + a * kColW + 2) + "\" y=\"" + num(section_y) + "\" width=\"" + num((b - a + 1) * kColW - 4) + "\" height=\"" + num(kSectionH) + "\" rx=\"4\" fill=\"" + kActor[s % 8] + "\"/>\n";
        centered_text(body, th, {sections[s]}, left + (a + b + 1) * kColW / 2, section_y + kSectionH / 2, 12.5, 600, "#ffffff");
    }
    // the plot: a faint line per score, the path through the faces, then the tasks
    for (int l = 0; l < 5; l++) {
        double y = plot_y + l * kLevel;
        body += dashed_line(left, y + kLevel / 2, W - kMargin, y + kLevel / 2, th.inner_stroke, 1, "3 4");
    }
    std::string path;
    for (size_t k = 0; k < tasks.size(); k++) {
        double cx = left + k * kColW + kColW / 2, cy = plot_y + (5 - tasks[k].score) * kLevel + kLevel / 2;
        path += (path.empty() ? "" : " ") + num(cx) + "," + num(cy);
    }
    body += "<polyline points=\"" + path + "\" fill=\"none\" stroke=\"" + th.line + "\" stroke-width=\"2\" stroke-linejoin=\"round\"/>\n";
    for (size_t k = 0; k < tasks.size(); k++) {
        const Task& t = tasks[k];
        double x0 = left + k * kColW, cx = x0 + kColW / 2;
        std::string color = t.section >= 0 ? kActor[t.section % 8] : kActor[0];
        body += "<g class=\"task\">\n<rect x=\"" + num(x0 + 6) + "\" y=\"" + num(task_y) + "\" width=\"" + num(kColW - 12) + "\" height=\"" + num(kTaskH) + "\" rx=\"4\" fill=\"" + mix(color, th.bg, 22) +
                "\" stroke=\"" + color + "\" stroke-width=\"1.5\"/>\n";
        centered_text(body, th, split_lines(t.name), cx, task_y + kTaskH / 2, 12.5, 500, th.text);
        body += "</g>\n";
        for (size_t a = 0; a < t.actors.size(); a++) {
            double ax = cx - (t.actors.size() - 1) * 7 + a * 14;
            body += "<circle cx=\"" + num(ax) + "\" cy=\"" + num(task_y + kTaskH + 12) + "\" r=\"5\" fill=\"" + kActor[t.actors[a] % 8] + "\"/>\n";
        }
        double cy = plot_y + (5 - t.score) * kLevel + kLevel / 2;
        body += "<g class=\"face\">\n<circle cx=\"" + num(cx) + "\" cy=\"" + num(cy) + "\" r=\"" + num(kFace) + "\" fill=\"" + kScore[t.score - 1] + "\" stroke=\"" + th.bg + "\" stroke-width=\"2\"/>\n";
        body += "<circle cx=\"" + num(cx - 4.5) + "\" cy=\"" + num(cy - 3) + "\" r=\"1.6\" fill=\"#333333\"/>\n<circle cx=\"" + num(cx + 4.5) + "\" cy=\"" + num(cy - 3) + "\" r=\"1.6\" fill=\"#333333\"/>\n";
        // the mouth: a smile for 4-5, flat for 3, a frown for 1-2
        double bend = (t.score - 3) * 3.0;
        body += "<path d=\"M" + num(cx - 5) + "," + num(cy + 5) + " Q" + num(cx) + "," + num(cy + 5 + bend) + " " + num(cx + 5) + "," + num(cy + 5) + "\" fill=\"none\" stroke=\"#333333\" stroke-width=\"1.5\" stroke-linecap=\"round\"/>\n</g>\n";
    }
    svg = svg_document(th, W, H, body);
    return true;
}

}  // namespace mermaid
