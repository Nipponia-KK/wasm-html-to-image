#include "mermaid/eventmodeling.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>

#include "mermaid/mermaid_internal.h"

namespace mermaid {
namespace {

using litehtml::svgutil::trim;

enum class Type { Ui, Processor, Command, ReadModel, Event };

struct Frame {
    int number = 0;
    bool reset = false;
    Type type = Type::Ui;
    std::string entity, ns, name;
    std::vector<int> relates;          // explicit `->> n`
    std::string data_ref;              // [[Id]]
    std::vector<std::string> data;     // the lines shown in the box
    // layout
    int lane = 0;
    double w = 0, h = 0;
};

bool starts(const std::string& s, const char* p) { return s.compare(0, std::char_traits<char>::length(p), p) == 0; }

bool parse_type(const std::string& t, Type& out) {
    if (t == "ui") out = Type::Ui;
    else if (t == "pcr" || t == "processor") out = Type::Processor;
    else if (t == "cmd" || t == "command") out = Type::Command;
    else if (t == "rmo" || t == "readmodel") out = Type::ReadModel;
    else if (t == "evt" || t == "event") out = Type::Event;
    else return false;
    return true;
}

/// `key: value` lines (separated by newlines or commas outside quotes) of a data body.
std::vector<std::string> data_lines(const std::string& body) {
    std::vector<std::string> out;
    std::string cur;
    bool quote = false;
    char q = 0;
    for (char c : body) {
        if (quote) {
            if (c == q) quote = false;
            cur += c;
        } else if (c == '\'' || c == '"') {
            quote = true;
            q = c;
            cur += c;
        } else if (c == '\n' || c == ',') {
            if (!trim(cur).empty()) out.push_back(trim(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!trim(cur).empty()) out.push_back(trim(cur));
    return out;
}

const char* kind_name(Type t) {
    switch (t) {
        case Type::Ui: return "ui";
        case Type::Processor: return "processor";
        case Type::Command: return "command";
        case Type::ReadModel: return "readmodel";
        case Type::Event: return "event";
    }
    return "";
}

std::string kind_color(Type t) {
    switch (t) {
        case Type::Ui: return "#8b95a5";
        case Type::Processor: return "#7c828f";
        case Type::Command: return "#4f86e6";
        case Type::ReadModel: return "#3fae72";
        case Type::Event: return "#e9992f";
    }
    return "#888888";
}

}  // namespace

bool render_eventmodeling(const std::string& source, const Options& options, std::string& svg, std::string& error) {
    using namespace metrics;
    std::vector<Frame> frames;
    std::map<std::string, std::vector<std::string>> blocks;
    std::set<int> numbers;
    bool first = true;

    std::vector<std::string> lines;
    {
        size_t i = 0;
        while (i <= source.size()) {
            size_t e = source.find('\n', i);
            lines.push_back(source.substr(i, e == std::string::npos ? std::string::npos : e - i));
            i = e == std::string::npos ? source.size() + 1 : e + 1;
        }
    }
    for (size_t li = 0; li < lines.size(); li++) {
        std::string line = trim(lines[li]);
        if (line.empty() || starts(line, "%%")) continue;
        if (first) {
            first = false;
            continue;
        }
        if (starts(line, "data ")) {  // data Id [`type`]{ ... } over several lines
            std::string head = line.substr(5);
            size_t brace = head.find('{');
            std::string id = trim(head.substr(0, head.find_first_of(" `{")));
            if (id.empty() || brace == std::string::npos) {
                error = "a data block is `data Id { ... }`: " + line;
                return false;
            }
            std::string body = head.substr(brace + 1);
            int depth = 1;
            std::string text;
            size_t scan = 0;
            while (true) {
                for (; scan < body.size(); scan++) {
                    if (body[scan] == '{') depth++;
                    else if (body[scan] == '}' && --depth == 0) break;
                }
                if (depth == 0) {
                    text += body.substr(0, scan);
                    break;
                }
                text += body + "\n";
                if (++li >= lines.size()) {
                    error = "a data block is not closed with `}`: " + id;
                    return false;
                }
                body = lines[li];
                scan = 0;
            }
            blocks[id] = data_lines(text);
            continue;
        }
        bool tf = starts(line, "tf ") || starts(line, "timeframe "), rf = starts(line, "rf ") || starts(line, "resetframe ");
        if (!tf && !rf) {
            error = "unrecognised line: " + line;
            return false;
        }
        std::string rest = trim(line.substr(line.find(' ') + 1));
        Frame f;
        f.reset = rf;
        // number, type, entity
        std::vector<std::string> words;
        size_t at = 0;
        auto next_word = [&](std::string& w) {
            while (at < rest.size() && std::isspace((unsigned char)rest[at])) at++;
            if (at >= rest.size()) return false;
            size_t end = at;
            while (end < rest.size() && !std::isspace((unsigned char)rest[end])) end++;
            w = rest.substr(at, end - at);
            at = end;
            return true;
        };
        std::string w;
        if (!next_word(w) || w.find_first_not_of("0123456789") != std::string::npos) {
            error = "a frame starts with its number: " + line;
            return false;
        }
        f.number = std::atoi(w.c_str());
        if (!numbers.insert(f.number).second) {
            error = "frame number " + w + " is used twice: " + line;
            return false;
        }
        if (!next_word(w) || !parse_type(w, f.type)) {
            error = "a frame needs a type (ui, pcr, cmd, rmo, evt): " + line;
            return false;
        }
        if (!next_word(f.entity)) {
            error = "a frame needs an entity name: " + line;
            return false;
        }
        size_t dot = f.entity.find('.');
        f.ns = dot == std::string::npos ? "" : f.entity.substr(0, dot);
        f.name = dot == std::string::npos ? f.entity : f.entity.substr(dot + 1);
        // the rest: ->> n, [[Data]], `type`{ inline }
        while (true) {
            while (at < rest.size() && std::isspace((unsigned char)rest[at])) at++;
            if (at >= rest.size()) break;
            if (rest.compare(at, 3, "->>") == 0) {
                at += 3;
                if (!next_word(w) || w.find_first_not_of("0123456789") != std::string::npos) {
                    error = "`->>` is followed by a frame number: " + line;
                    return false;
                }
                f.relates.push_back(std::atoi(w.c_str()));
            } else if (rest.compare(at, 2, "[[") == 0) {
                size_t end = rest.find("]]", at);
                if (end == std::string::npos) {
                    error = "an unclosed [[data]] reference: " + line;
                    return false;
                }
                f.data_ref = trim(rest.substr(at + 2, end - at - 2));
                at = end + 2;
            } else if (rest[at] == '`' || rest[at] == '{') {
                size_t brace = rest.find('{', at);
                size_t close = rest.rfind('}');
                if (brace == std::string::npos || close == std::string::npos || close < brace) {
                    error = "an unclosed inline data block: " + line;
                    return false;
                }
                f.data = data_lines(rest.substr(brace + 1, close - brace - 1));
                at = close + 1;
            } else {
                error = "unrecognised text in the frame: " + line;
                return false;
            }
        }
        frames.push_back(std::move(f));
    }
    if (frames.empty()) {
        error = "no frames";
        return false;
    }
    for (auto& f : frames) {
        if (!f.data_ref.empty()) {
            auto it = blocks.find(f.data_ref);
            if (it == blocks.end()) {
                error = "frame " + std::to_string(f.number) + " refers to data `" + f.data_ref + "` that is not defined";
                return false;
            }
            f.data = it->second;
        }
        for (int n : f.relates)
            if (!numbers.count(n)) {
                error = "frame " + std::to_string(f.number) + " relates to frame " + std::to_string(n) + ", which does not exist";
                return false;
            }
    }

    // swimlanes: UI / Automation, Command / Read Model, then the events (one lane per namespace, in order of appearance)
    std::vector<std::string> lane_names = {"UI / Automation", "Command / Read Model"};
    std::map<std::string, int> event_lane;
    for (auto& f : frames) {
        if (f.type == Type::Ui || f.type == Type::Processor) f.lane = 0;
        else if (f.type == Type::Command || f.type == Type::ReadModel) f.lane = 1;
        else {
            auto it = event_lane.find(f.ns);
            if (it == event_lane.end()) {
                it = event_lane.emplace(f.ns, (int)lane_names.size()).first;
                lane_names.push_back(f.ns.empty() ? "Events" : f.ns);
            }
            f.lane = it->second;
        }
    }

    Theme th = make_theme(options);
    Text text{options};
    const double kLabelW = 150, kRowPad = 22, kColGap = 36, kData = 11;
    double col_w = 0;
    std::vector<double> lane_h(lane_names.size(), 0);
    for (auto& f : frames) {
        std::vector<std::string> body = f.data;
        if (body.size() > 4) body.resize(4);
        double w = text.width(f.name, 13, 600) + 28;
        for (auto& l : body) w = std::max(w, text.width(l, kData, 400) + 28);
        f.w = std::max(w, 112.0);
        f.h = 38 + (double)body.size() * 15 + (body.empty() ? 0 : 4);
        col_w = std::max(col_w, f.w);
    }
    for (auto& f : frames) lane_h[f.lane] = std::max(lane_h[f.lane], f.h);
    for (auto& h : lane_h) h += 2 * kRowPad;
    const double colstep = col_w + kColGap, top = kMargin, left = kMargin + kLabelW;
    std::vector<double> lane_y(lane_names.size());
    double y = top;
    for (size_t k = 0; k < lane_names.size(); k++) {
        lane_y[k] = y;
        y += lane_h[k];
    }
    const double W = left + frames.size() * colstep + kColGap / 2 + kMargin, H = y + kMargin;
    auto cx = [&](size_t idx) { return left + idx * colstep + colstep / 2; };
    auto cy = [&](const Frame& f) { return lane_y[f.lane] + lane_h[f.lane] / 2; };
    std::map<int, size_t> by_number;
    for (size_t k = 0; k < frames.size(); k++) by_number[frames[k].number] = k;

    std::string body;
    // lane bands and their names
    for (size_t k = 0; k < lane_names.size(); k++) {
        body += "<rect x=\"" + num(kMargin) + "\" y=\"" + num(lane_y[k]) + "\" width=\"" + num(W - 2 * kMargin) + "\" height=\"" + num(lane_h[k]) + "\" fill=\"" + (k % 2 ? th.bg : th.surface) + "\" stroke=\"" + th.border + "\" stroke-width=\"1\"/>\n";
        body += "<rect x=\"" + num(kMargin) + "\" y=\"" + num(lane_y[k]) + "\" width=\"" + num(kLabelW) + "\" height=\"" + num(lane_h[k]) + "\" fill=\"" + th.cluster_hdr + "\" stroke=\"" + th.border + "\" stroke-width=\"1\"/>\n";
        centered_text(body, th, split_lines(lane_names[k]), kMargin + kLabelW / 2, lane_y[k] + lane_h[k] / 2, 12, 600, th.text);
    }
    // relations: each frame follows the one before it (a reset frame does not), and `->> n` adds more
    auto arrow = [&](size_t from, size_t to) {
        const Frame &a = frames[from], &b = frames[to];
        Pt p{cx(from), cy(a)}, q{cx(to), cy(b)};
        bool same_lane = a.lane == b.lane;
        Pt s = p, t = q;
        std::string d;
        if (same_lane) {
            double dir = q.x > p.x ? 1 : -1;
            s = {p.x + dir * a.w / 2, p.y};
            t = {q.x - dir * b.w / 2, q.y};
            d = "M" + num(s.x) + " " + num(s.y) + " L" + num(t.x) + " " + num(t.y);
        } else {
            double dir = b.lane > a.lane ? 1 : -1;
            s = {p.x, p.y + dir * a.h / 2};
            t = {q.x, q.y - dir * b.h / 2};
            double mid = (s.y + t.y) / 2;
            d = "M" + num(s.x) + " " + num(s.y) + " L" + num(s.x) + " " + num(mid) + " L" + num(t.x) + " " + num(mid) + " L" + num(t.x) + " " + num(t.y);
        }
        body += "<path class=\"relation\" d=\"" + d + "\" fill=\"none\" stroke=\"" + th.line + "\" stroke-width=\"1.5\" stroke-linejoin=\"round\"/>\n";
        double dx = t.x - (same_lane ? s.x : t.x), dy = t.y - (same_lane ? t.y : (s.y + t.y) / 2);
        Pt from_pt = same_lane ? s : Pt{t.x, (s.y + t.y) / 2};
        (void)dx;
        (void)dy;
        double hx = t.x - from_pt.x, hy = t.y - from_pt.y, hl = std::sqrt(hx * hx + hy * hy);
        if (hl < 1e-6) return;
        hx /= hl;
        hy /= hl;
        Pt b0{t.x - hx * kArrowW, t.y - hy * kArrowW};
        body += "<polygon points=\"" + num(t.x) + "," + num(t.y) + " " + num(b0.x - hy * kArrowH * 0.6) + "," + num(b0.y + hx * kArrowH * 0.6) + " " + num(b0.x + hy * kArrowH * 0.6) + "," + num(b0.y - hx * kArrowH * 0.6) + "\" fill=\"" + th.line + "\"/>\n";
    };
    for (size_t k = 0; k < frames.size(); k++) {
        if (k > 0 && !frames[k].reset) arrow(k - 1, k);
        for (int n : frames[k].relates) {
            size_t other = by_number[n];
            if (other == k) continue;
            if (frames[k].type == Type::ReadModel) arrow(other, k);  // a read model is built from what it lists
            else arrow(k, other);
        }
    }
    // the frames
    for (size_t k = 0; k < frames.size(); k++) {
        const Frame& f = frames[k];
        std::string color = kind_color(f.type), fill = mix(color, th.bg, 24);
        double x = cx(k) - f.w / 2, fy = cy(f) - f.h / 2;
        body += "<g class=\"frame " + std::string(kind_name(f.type)) + "\">\n";
        body += "<rect x=\"" + num(x) + "\" y=\"" + num(fy) + "\" width=\"" + num(f.w) + "\" height=\"" + num(f.h) + "\" rx=\"" + (f.type == Type::Event || f.type == Type::Command ? "3" : "8") + "\" fill=\"" + fill + "\" stroke=\"" + color + "\" stroke-width=\"" +
                (f.reset ? "2.5" : "1.5") + "\"" + (f.type == Type::Processor ? " stroke-dasharray=\"5 3\"" : "") + "/>\n";
        aligned_text(body, th, {f.name}, x + f.w / 2, fy + 14, 13, 600, th.text, "middle");
        aligned_text(body, th, {std::string(f.number < 10 ? "0" : "") + std::to_string(f.number)}, x + 6, fy + 10, 9, 600, th.text_sec, "start");
        for (size_t d = 0; d < std::min<size_t>(f.data.size(), 4); d++) aligned_text(body, th, {f.data[d]}, x + 10, fy + 32 + 15 * d, kData, 400, th.text_sec, "start");
        body += "</g>\n";
    }
    svg = svg_document(th, W, H, body);
    return true;
}

}  // namespace mermaid
