#include "mermaid/c4.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "mermaid/layered.h"

namespace mermaid {
namespace {

using litehtml::svgutil::trim;

enum class Kind { Person, System, Container, Component, Node };

struct Element {
    std::string id, label, type_text, descr;
    Kind kind = Kind::System;
    bool ext = false, db = false, queue = false;
    int group = -1;
    double w = 0, h = 0;
    std::vector<std::string> descr_lines;
};

struct Boundary {
    std::string label, type;
    int parent = -1;
};

struct Rel {
    int from, to;
    std::string label, techn;
    bool arrow_from = false, arrow_to = true;
};

bool starts(const std::string& s, const char* p) { return s.compare(0, std::char_traits<char>::length(p), p) == 0; }

bool ends(const std::string& s, const char* p) {
    size_t n = std::char_traits<char>::length(p);
    return s.size() >= n && s.compare(s.size() - n, n, p) == 0;
}

std::string unquote(std::string s) {
    s = trim(s);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
    return s;
}

// The comma separated arguments between the outer parentheses, quotes respected; `$name=value` options dropped.
std::vector<std::string> split_args(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    for (char c : s) {
        if (c == '"') quoted = !quoted;
        if (c == ',' && !quoted) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    std::vector<std::string> args;
    for (auto& a : out) {
        std::string t = trim(a);
        if (!t.empty() && t[0] == '$') continue;
        args.push_back(unquote(t));
    }
    return args;
}

std::string arg(const std::vector<std::string>& a, size_t k) { return k < a.size() ? a[k] : std::string(); }

// Greedy wrap by measured width; breaks at spaces, or anywhere for text without them (CJK).
std::vector<std::string> wrap(const Text& text, const std::string& s, double size, double max_w) {
    std::vector<std::string> lines;
    if (s.empty()) return lines;
    std::string cur;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        size_t len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        std::string ch = s.substr(i, len);
        i += len;
        if (!cur.empty() && text.width(cur + ch, size, 400) > max_w) {
            size_t sp = len == 1 && ch != " " ? cur.rfind(' ') : std::string::npos;
            if (sp != std::string::npos) {
                lines.push_back(cur.substr(0, sp));
                cur = cur.substr(sp + 1) + ch;
            } else {
                lines.push_back(cur);
                cur = ch == " " ? "" : ch;
            }
        } else {
            cur += ch;
        }
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

void draw_arrow(std::string& body, const Theme& th, Pt tip, Pt from) {
    double dx = tip.x - from.x, dy = tip.y - from.y, len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001) return;
    dx /= len;
    dy /= len;
    double nx = -dy * 4.5, ny = dx * 4.5;
    body += "<polygon points=\"" + num(tip.x) + "," + num(tip.y) + " " + num(tip.x - dx * 9 + nx) + "," + num(tip.y - dy * 9 + ny) + " " + num(tip.x - dx * 9 - nx) + "," + num(tip.y - dy * 9 - ny) +
            "\" fill=\"" + th.arrow + "\"/>\n";
}

}  // namespace

bool render_c4(const std::string& source, const Options& options, std::string& svg, std::string& error) {
    using namespace metrics;
    std::vector<Element> els;
    std::vector<Boundary> bounds;
    std::vector<Rel> rels;
    std::map<std::string, int> index;
    std::vector<int> stack;
    std::string title;
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
        if (line == "}") {
            if (!stack.empty()) stack.pop_back();
            continue;
        }
        size_t open = line.find('(');
        if (open == std::string::npos) {
            if (starts(line, "accTitle") || starts(line, "accDescr")) continue;
            error = "unrecognised line: " + line;
            return false;
        }
        std::string name = trim(line.substr(0, open));
        size_t close = line.rfind(')');
        if (close == std::string::npos || close < open) {
            error = "missing `)`: " + line;
            return false;
        }
        std::vector<std::string> a = split_args(line.substr(open + 1, close - open - 1));
        if (starts(name, "Update") || starts(name, "LAYOUT_") || name == "SHOW_LEGEND") continue;
        if (ends(name, "Boundary") || name == "Node_Boundary") {
            Boundary b;
            b.label = arg(a, 1).empty() ? arg(a, 0) : arg(a, 1);
            b.type = name == "Enterprise_Boundary" ? "ENTERPRISE" : name == "System_Boundary" ? "SYSTEM" : name == "Container_Boundary" ? "CONTAINER" : arg(a, 2).empty() ? "BOUNDARY" : arg(a, 2);
            b.parent = stack.empty() ? -1 : stack.back();
            bounds.push_back(b);
            stack.push_back((int)bounds.size() - 1);
            continue;
        }
        if (starts(name, "Rel") || name == "BiRel") {
            Rel r;
            size_t base = 0;
            if (name == "RelIndex") base = 1;
            std::string from = arg(a, base), to = arg(a, base + 1);
            if (!index.count(from) || !index.count(to)) {
                error = "unknown element in: " + line;
                return false;
            }
            r.from = index[from];
            r.to = index[to];
            r.label = arg(a, base + 2);
            r.techn = arg(a, base + 3);
            if (name == "RelIndex" && !arg(a, 0).empty()) r.label = arg(a, 0) + ": " + r.label;
            if (name == "BiRel") r.arrow_from = true;
            if (name == "Rel_Back") {
                std::swap(r.from, r.to);
            }
            rels.push_back(r);
            continue;
        }
        Element el;
        el.group = stack.empty() ? -1 : stack.back();
        std::string base = name;
        el.ext = ends(base, "_Ext");
        if (el.ext) base = base.substr(0, base.size() - 4);
        if (base == "Node_L" || base == "Node_R" || base == "Deployment_Node") base = "Node";
        el.db = base.find("Db") != std::string::npos;
        el.queue = base.find("Queue") != std::string::npos;
        if (el.db) base.erase(base.find("Db"), 2);
        if (el.queue) base.erase(base.find("Queue"), 5);
        size_t fields = 3;  // id, label, descr
        if (base == "Person") el.kind = Kind::Person;
        else if (base == "System") el.kind = Kind::System;
        else if (base == "Container") el.kind = Kind::Container, fields = 4;
        else if (base == "Component") el.kind = Kind::Component, fields = 4;
        else if (base == "Node") el.kind = Kind::Node, fields = 4;
        else {
            error = "unknown element: " + name;
            return false;
        }
        el.id = arg(a, 0);
        el.label = arg(a, 1);
        if (el.id.empty()) {
            error = "an element needs an id: " + line;
            return false;
        }
        std::string techn = fields == 4 ? arg(a, 2) : "";
        el.descr = fields == 4 ? arg(a, 3) : arg(a, 2);
        std::string what = el.kind == Kind::Person ? "Person" : el.kind == Kind::System ? "Software System" : el.kind == Kind::Container ? "Container" : el.kind == Kind::Component ? "Component" : "Node";
        if (el.ext && (el.kind == Kind::Person || el.kind == Kind::System)) what = "External " + what;
        if (el.kind == Kind::Person && !el.ext) what = "Person";
        el.type_text = "[" + what + (techn.empty() ? "" : ": " + techn) + "]";
        index[el.id] = (int)els.size();
        els.push_back(el);
    }
    if (!stack.empty()) {
        error = "unclosed boundary";
        return false;
    }
    if (els.empty()) {
        error = "a C4 diagram needs at least one element";
        return false;
    }

    Theme th = make_theme(options);
    Text text{options};
    const double kDescW = 190, kPadX = 14, kHead = 34;
    std::vector<LayoutNode> ln(els.size());
    for (size_t k = 0; k < els.size(); k++) {
        Element& el = els[k];
        el.descr_lines = wrap(text, el.descr, 11, kDescW);
        double w = std::max(text.width(el.label, 13, 600), text.width(el.type_text, 10, 400));
        for (auto& l : el.descr_lines) w = std::max(w, text.width(l, 11, 400));
        el.w = std::max(160.0, w + 2 * kPadX);
        el.h = 20 + 16 + 14 + (el.descr_lines.empty() ? 0 : 6 + el.descr_lines.size() * 14.5) + 12 + (el.kind == Kind::Person ? kHead : 0) + (el.db ? 14 : 0);
        ln[k].w = el.w;
        ln[k].h = el.h;
        ln[k].group = el.group;
    }
    std::vector<LayoutEdge> le(rels.size());
    std::vector<std::vector<std::string>> rel_lines(rels.size());
    for (size_t k = 0; k < rels.size(); k++) {
        le[k].from = rels[k].from;
        le[k].to = rels[k].to;
        double lw = 0;
        if (!rels[k].label.empty()) rel_lines[k].push_back(rels[k].label);
        if (!rels[k].techn.empty()) rel_lines[k].push_back("[" + rels[k].techn + "]");
        for (auto& l : rel_lines[k]) lw = std::max(lw, text.width(l, kEdgeFont, kEdgeWeight));
        if (!rel_lines[k].empty()) {
            le[k].label_w = lw + 14;
            le[k].label_h = rel_lines[k].size() * kEdgeFont * kLineHeight + 8;
        }
    }
    std::vector<int> group_parent(bounds.size());
    for (size_t g = 0; g < bounds.size(); g++) group_parent[g] = bounds[g].parent;
    LayoutResult lay = layout_layered(ln, le, Direction::TD, bounds.empty() ? std::vector<int>{} : group_parent);

    struct Frame {
        double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        bool used = false;
    };
    std::vector<Frame> frames(bounds.size());
    auto grow = [&](Frame& f, double x0, double y0, double x1, double y1) {
        if (!f.used) f = {x0, y0, x1, y1, true};
        else f = {std::min(f.x0, x0), std::min(f.y0, y0), std::max(f.x1, x1), std::max(f.y1, y1), true};
    };
    constexpr double kFramePad = 18, kHeader = 28;
    for (size_t k = 0; k < els.size(); k++)
        if (els[k].group >= 0) grow(frames[els[k].group], lay.center[k].x - els[k].w / 2, lay.center[k].y - els[k].h / 2, lay.center[k].x + els[k].w / 2, lay.center[k].y + els[k].h / 2);
    for (int gi = (int)bounds.size() - 1; gi >= 0; gi--) {
        if (!frames[gi].used) continue;
        Frame& f = frames[gi];
        double need = text.width(bounds[gi].label, 12, 600) + 24;
        f = {f.x0 - kFramePad, f.y0 - kFramePad - kHeader, std::max(f.x1 + kFramePad, f.x0 - kFramePad + need), f.y1 + kFramePad, true};
        if (bounds[gi].parent >= 0) grow(frames[bounds[gi].parent], f.x0, f.y0, f.x1, f.y1);
    }
    double x0 = 0, y0 = 0, x1 = lay.width, y1 = lay.height;
    for (const Frame& f : frames)
        if (f.used) x0 = std::min(x0, f.x0), y0 = std::min(y0, f.y0), x1 = std::max(x1, f.x1), y1 = std::max(y1, f.y1);
    double title_h = title.empty() ? 0 : 34;
    double W = (x1 - x0) + 2 * kMargin;
    if (!title.empty()) W = std::max(W, text.width(title, 15, 600) + 2 * kMargin);
    const double ox = kMargin - x0 + (W - ((x1 - x0) + 2 * kMargin)) / 2, oy = kMargin - y0 + title_h;
    auto at = [&](Pt p) { return Pt{p.x + ox, p.y + oy}; };

    std::string body;
    if (!title.empty()) centered_text(body, th, {title}, W / 2, kMargin / 2 + 12, 15, 600, th.text);
    for (size_t gi = 0; gi < bounds.size(); gi++) {
        const Frame& f = frames[gi];
        if (!f.used) continue;
        body += "<g class=\"boundary\">\n<rect x=\"" + num(f.x0 + ox) + "\" y=\"" + num(f.y0 + oy) + "\" width=\"" + num(f.x1 - f.x0) + "\" height=\"" + num(f.y1 - f.y0) +
                "\" rx=\"6\" fill=\"none\" stroke=\"" + th.line + "\" stroke-width=\"1.5\" stroke-dasharray=\"7 5\"/>\n";
        aligned_text(body, th, {bounds[gi].label}, f.x0 + ox + 12, f.y0 + oy + 13, 12, 600, th.text, "start");
        aligned_text(body, th, {"[" + bounds[gi].type + "]"}, f.x0 + ox + 12, f.y0 + oy + 26, 10, 400, th.text_sec, "start");
        body += "</g>\n";
    }
    for (size_t k = 0; k < rels.size(); k++) {
        if (lay.points[k].size() < 2) continue;
        std::vector<Pt> pts;
        std::string list;
        for (const Pt& p : lay.points[k]) {
            pts.push_back(at(p));
            list += (list.empty() ? "" : " ") + num(pts.back().x) + "," + num(pts.back().y);
        }
        body += "<polyline class=\"edge\" points=\"" + list + "\" fill=\"none\" stroke=\"" + th.line + "\" stroke-width=\"1.5\" stroke-linejoin=\"round\"/>\n";
        if (rels[k].arrow_to) draw_arrow(body, th, pts.back(), pts[pts.size() - 2]);
        if (rels[k].arrow_from) draw_arrow(body, th, pts.front(), pts[1]);
    }
    for (size_t k = 0; k < rels.size(); k++) {
        if (rel_lines[k].empty()) continue;
        Pt c = at(lay.label_center[k]);
        body += "<g class=\"edge-label\">\n<rect x=\"" + num(c.x - le[k].label_w / 2) + "\" y=\"" + num(c.y - le[k].label_h / 2) + "\" width=\"" + num(le[k].label_w) + "\" height=\"" + num(le[k].label_h) +
                "\" rx=\"2\" fill=\"" + th.bg + "\" stroke=\"" + th.inner_stroke + "\" stroke-width=\"1\"/>\n";
        centered_text(body, th, rel_lines[k], c.x, c.y, kEdgeFont, kEdgeWeight, th.text_sec);
        body += "</g>\n";
    }
    for (size_t k = 0; k < els.size(); k++) {
        const Element& el = els[k];
        Pt p = at(lay.center[k]);
        double x = p.x - el.w / 2, y = p.y - el.h / 2;
        std::string fill = el.ext ? mix(th.fg, th.bg, 14) : th.surface;
        std::string stroke = el.ext ? mix(th.fg, th.bg, 35) : th.border;
        std::string paint = " fill=\"" + fill + "\" stroke=\"" + stroke + "\" stroke-width=\"1\"";
        body += "<g class=\"node\">\n";
        double top = y;
        if (el.db) {
            double ry = 8, r = x + el.w, b = y + el.h;
            body += "<path d=\"M" + num(x) + "," + num(y + ry) + " A" + num(el.w / 2) + "," + num(ry) + " 0 0 1 " + num(r) + "," + num(y + ry) + " L" + num(r) + "," + num(b - ry) + " A" + num(el.w / 2) + "," +
                    num(ry) + " 0 0 1 " + num(x) + "," + num(b - ry) + " Z\"" + paint + "/>\n";
            body += "<path d=\"M" + num(x) + "," + num(y + ry) + " A" + num(el.w / 2) + "," + num(ry) + " 0 0 0 " + num(r) + "," + num(y + ry) + "\" fill=\"none\" stroke=\"" + stroke + "\" stroke-width=\"1\"/>\n";
            top = y + 8;
        } else {
            double rx = el.queue ? std::min(24.0, el.h / 2) : el.kind == Kind::Person ? 10 : 4;
            body += "<rect x=\"" + num(x) + "\" y=\"" + num(y) + "\" width=\"" + num(el.w) + "\" height=\"" + num(el.h) + "\" rx=\"" + num(rx) + "\"" + paint + "/>\n";
        }
        double ty = top + 20;
        if (el.kind == Kind::Person) {
            body += "<circle cx=\"" + num(p.x) + "\" cy=\"" + num(top + 17) + "\" r=\"11\" fill=\"" + th.bg + "\" stroke=\"" + th.text_sec + "\" stroke-width=\"1.5\"/>\n";
            body += "<path d=\"M" + num(p.x - 16) + "," + num(top + 38) + " Q" + num(p.x - 16) + "," + num(top + 30) + " " + num(p.x) + "," + num(top + 30) + " Q" + num(p.x + 16) + "," + num(top + 30) + " " + num(p.x + 16) + "," +
                    num(top + 38) + "\" fill=\"none\" stroke=\"" + th.text_sec + "\" stroke-width=\"1.5\"/>\n";
            ty = top + kHead + 20;
        }
        centered_text(body, th, {el.label}, p.x, ty, 13, 600, th.text);
        centered_text(body, th, {el.type_text}, p.x, ty + 16, 10, 400, th.text_sec);
        for (size_t l = 0; l < el.descr_lines.size(); l++) centered_text(body, th, {el.descr_lines[l]}, p.x, ty + 36 + l * 14.5, 11, 400, th.text);
        body += "</g>\n";
    }
    svg = svg_document(th, W, (y1 - y0) + 2 * kMargin + title_h, body);
    return true;
}

}  // namespace mermaid
