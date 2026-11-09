#include "mermaid/architecture.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "mermaid/mermaid_internal.h"

namespace mermaid {
namespace {

using litehtml::svgutil::trim;

struct Node {
    std::string id, title, icon;
    bool junction = false;
    int parent = -1;  // group index
    int cx = 0, cy = 0;
    bool placed = false;
};

struct Group {
    std::string id, title, icon;
    int parent = -1;
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool has_box = false;
};

struct Edge {
    int a, b;
    char side_a, side_b;
    bool group_a = false, group_b = false;
    bool arrow_a = false, arrow_b = false;
};

bool starts(const std::string& s, const char* p) { return s.compare(0, std::char_traits<char>::length(p), p) == 0; }

constexpr double kIcon = 80, kPitchX = 200, kPitchY = 180, kGroupPad = 26, kGroupHead = 22;

void dir_of(char side, int& dx, int& dy) {
    dx = side == 'R' ? 1 : side == 'L' ? -1 : 0;
    dy = side == 'B' ? 1 : side == 'T' ? -1 : 0;
}

// `id(icon)[Title] in parent`, every part but the id optional.
bool parse_decl(const std::string& rest, std::string& id, std::string& icon, std::string& title, std::string& parent) {
    std::string s = trim(rest);
    size_t in = s.rfind(" in ");
    parent.clear();
    if (in != std::string::npos && s.find(']', in) == std::string::npos) {
        parent = trim(s.substr(in + 4));
        s = trim(s.substr(0, in));
    }
    size_t end = s.find_first_of("([");
    id = trim(s.substr(0, end));
    if (id.empty()) return false;
    icon.clear();
    title = id;
    if (end == std::string::npos) return true;
    if (s[end] == '(') {
        size_t close = s.find(')', end);
        if (close == std::string::npos) return false;
        icon = trim(s.substr(end + 1, close - end - 1));
        end = s.find('[', close);
        if (end == std::string::npos) return true;
    }
    size_t close = s.rfind(']');
    if (close == std::string::npos || close < end) return false;
    title = trim(s.substr(end + 1, close - end - 1));
    if (title.size() >= 2 && title.front() == '"' && title.back() == '"') title = title.substr(1, title.size() - 2);
    return true;
}

// `id`, `id{group}` with the `:S` side split off by the caller.
void split_group_flag(std::string& id, bool& flag) {
    flag = false;
    if (id.size() > 7 && id.compare(id.size() - 7, 7, "{group}") == 0) {
        flag = true;
        id = trim(id.substr(0, id.size() - 7));
    }
}

std::string icon_glyph(const std::string& icon, double cx, double cy, const std::string& stroke) {
    auto attrs = " fill=\"none\" stroke=\"" + stroke + "\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"";
    std::string o;
    if (icon == "database") {
        double l = cx - 15, r = cx + 15, t = cy - 16, b = cy + 16;
        o += "<path d=\"M" + num(l) + "," + num(t) + " A15,6 0 0 1 " + num(r) + "," + num(t) + " A15,6 0 0 1 " + num(l) + "," + num(t) + " L" + num(l) + "," + num(b) + " A15,6 0 0 0 " + num(r) + "," + num(b) +
             " L" + num(r) + "," + num(t) + "\"" + attrs + "/>\n";
        o += "<path d=\"M" + num(l) + "," + num(cy) + " A15,6 0 0 0 " + num(r) + "," + num(cy) + "\"" + attrs + "/>\n";
    } else if (icon == "server") {
        for (int k = 0; k < 3; k++) {
            double y = cy - 19 + k * 14;
            o += "<rect x=\"" + num(cx - 17) + "\" y=\"" + num(y) + "\" width=\"34\" height=\"11\" rx=\"2\"" + attrs + "/>\n";
            o += "<circle cx=\"" + num(cx + 10) + "\" cy=\"" + num(y + 5.5) + "\" r=\"1.2\" fill=\"" + stroke + "\"/>\n";
        }
    } else if (icon == "disk") {
        o += "<circle cx=\"" + num(cx) + "\" cy=\"" + num(cy) + "\" r=\"18\"" + attrs + "/>\n";
        o += "<circle cx=\"" + num(cx) + "\" cy=\"" + num(cy) + "\" r=\"5\"" + attrs + "/>\n";
    } else if (icon == "internet") {
        o += "<circle cx=\"" + num(cx) + "\" cy=\"" + num(cy) + "\" r=\"18\"" + attrs + "/>\n";
        o += "<ellipse cx=\"" + num(cx) + "\" cy=\"" + num(cy) + "\" rx=\"8\" ry=\"18\"" + attrs + "/>\n";
        o += "<line x1=\"" + num(cx - 18) + "\" y1=\"" + num(cy) + "\" x2=\"" + num(cx + 18) + "\" y2=\"" + num(cy) + "\" stroke=\"" + stroke + "\" stroke-width=\"2\"/>\n";
    } else if (icon == "cloud") {
        o += "<path d=\"M" + num(cx - 17) + "," + num(cy + 11) + " A9,9 0 0 1 " + num(cx - 15) + "," + num(cy - 4) + " A12,12 0 0 1 " + num(cx + 9) + "," + num(cy - 9) + " A9,9 0 0 1 " + num(cx + 17) + "," +
             num(cy + 11) + " Z\"" + attrs + "/>\n";
    } else {
        o += "<rect x=\"" + num(cx - 15) + "\" y=\"" + num(cy - 15) + "\" width=\"30\" height=\"30\" rx=\"4\"" + attrs + "/>\n";
    }
    return o;
}

}  // namespace

bool render_architecture(const std::string& source, const Options& options, std::string& svg, std::string& error) {
    using namespace metrics;
    std::vector<Node> nodes;
    std::vector<Group> groups;
    std::vector<Edge> edges;
    std::map<std::string, int> node_index, group_index;
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
        if (starts(line, "title ") || starts(line, "accTitle") || starts(line, "accDescr")) continue;
        std::string id, icon, title, parent;
        if (starts(line, "group ")) {
            if (!parse_decl(line.substr(6), id, icon, title, parent)) {
                error = "cannot read the group: " + line;
                return false;
            }
            Group g;
            g.id = id;
            g.icon = icon;
            g.title = title;
            if (!parent.empty()) {
                auto p = group_index.find(parent);
                if (p == group_index.end()) {
                    error = "unknown group: " + parent;
                    return false;
                }
                g.parent = p->second;
            }
            group_index[id] = (int)groups.size();
            groups.push_back(g);
            continue;
        }
        bool junction = starts(line, "junction ");
        if (starts(line, "service ") || junction) {
            if (!parse_decl(line.substr(junction ? 9 : 8), id, icon, title, parent)) {
                error = "cannot read the service: " + line;
                return false;
            }
            Node n;
            n.id = id;
            n.icon = icon;
            n.title = title;
            n.junction = junction;
            if (!parent.empty()) {
                auto p = group_index.find(parent);
                if (p == group_index.end()) {
                    error = "unknown group: " + parent;
                    return false;
                }
                n.parent = p->second;
            }
            node_index[id] = (int)nodes.size();
            nodes.push_back(n);
            continue;
        }
        // edge: `a{group}:R -- L:b`, `-->` `<--` `<-->`
        static const char* kOps[] = {"<-->", "-->", "<--", "--"};
        size_t op_at = std::string::npos;
        const char* op = nullptr;
        for (const char* o : kOps) {
            size_t p = line.find(o);
            if (p != std::string::npos && (op_at == std::string::npos || p < op_at)) {
                op_at = p;
                op = o;
            }
        }
        if (!op) {
            error = "unrecognised line: " + line;
            return false;
        }
        std::string left = trim(line.substr(0, op_at)), right = trim(line.substr(op_at + std::char_traits<char>::length(op)));
        size_t lc = left.rfind(':'), rc = right.find(':');
        if (lc == std::string::npos || rc == std::string::npos || lc + 2 != left.size() || rc != 1) {
            error = "an edge is `a:R -- L:b`: " + line;
            return false;
        }
        Edge ed;
        ed.side_a = left[lc + 1];
        ed.side_b = right[0];
        std::string ida = trim(left.substr(0, lc)), idb = trim(right.substr(2));
        split_group_flag(ida, ed.group_a);
        split_group_flag(idb, ed.group_b);
        std::string sides = "LRTB";
        if (sides.find(ed.side_a) == std::string::npos || sides.find(ed.side_b) == std::string::npos) {
            error = "a side is L, R, T or B: " + line;
            return false;
        }
        if (!node_index.count(ida) || !node_index.count(idb)) {
            error = "unknown service in: " + line;
            return false;
        }
        ed.a = node_index[ida];
        ed.b = node_index[idb];
        std::string o = op;
        ed.arrow_a = o[0] == '<';
        ed.arrow_b = o.back() == '>';
        edges.push_back(ed);
    }
    if (nodes.empty()) {
        error = "an architecture diagram needs at least one service";
        return false;
    }

    // grid placement: each edge puts the far end one cell away, in the direction of the side it leaves from
    std::set<std::pair<int, int>> taken;
    int next_col = 0;
    for (size_t root = 0; root < nodes.size(); root++) {
        if (nodes[root].placed) continue;
        auto place_at = [&](int idx, int cx, int cy, int dx, int dy) {
            while (taken.count({cx, cy})) {
                cx += dx ? dx : 0;
                cy += dy ? dy : 0;
                if (!dx && !dy) cx++;
            }
            nodes[idx].cx = cx;
            nodes[idx].cy = cy;
            nodes[idx].placed = true;
            taken.insert({cx, cy});
        };
        place_at((int)root, next_col, 0, 1, 0);
        std::vector<int> queue{(int)root};
        for (size_t q = 0; q < queue.size(); q++) {
            int cur = queue[q];
            for (const Edge& ed : edges) {
                int other = -1, dx = 0, dy = 0;
                if (ed.a == cur && !nodes[ed.b].placed) {
                    other = ed.b;
                    dir_of(ed.side_a, dx, dy);
                } else if (ed.b == cur && !nodes[ed.a].placed) {
                    other = ed.a;
                    dir_of(ed.side_b, dx, dy);
                }
                if (other < 0) continue;
                place_at(other, nodes[cur].cx + dx, nodes[cur].cy + dy, dx, dy);
                queue.push_back(other);
            }
        }
        int max_col = next_col;
        for (int idx : queue) max_col = std::max(max_col, nodes[idx].cx);
        next_col = max_col + 2;
    }
    int min_cx = nodes[0].cx, min_cy = nodes[0].cy;
    for (auto& n : nodes) {
        min_cx = std::min(min_cx, n.cx);
        min_cy = std::min(min_cy, n.cy);
    }

    Theme th = make_theme(options);
    Text text{options};
    auto px = [&](const Node& n) { return (n.cx - min_cx) * kPitchX; };
    auto py = [&](const Node& n) { return (n.cy - min_cy) * kPitchY; };
    auto half = [&](const Node& n) { return n.junction ? 5.0 : kIcon / 2; };

    // group frames: the members' boxes (labels included) grown by the padding, children before parents
    auto add = [&](Group& g, double x0, double y0, double x1, double y1) {
        if (!g.has_box) {
            g.x0 = x0, g.y0 = y0, g.x1 = x1, g.y1 = y1;
            g.has_box = true;
        } else {
            g.x0 = std::min(g.x0, x0);
            g.y0 = std::min(g.y0, y0);
            g.x1 = std::max(g.x1, x1);
            g.y1 = std::max(g.y1, y1);
        }
    };
    for (auto& n : nodes) {
        if (n.parent < 0) continue;
        double lw = n.junction ? 0 : text.width(n.title, 12, 500) / 2;
        double h = half(n);
        add(groups[n.parent], px(n) - std::max(h, lw) - kGroupPad, py(n) - h - kGroupPad - kGroupHead, px(n) + std::max(h, lw) + kGroupPad, py(n) + h + (n.junction ? 0 : 22) + kGroupPad);
    }
    std::vector<int> depth(groups.size(), 0);
    for (size_t g = 0; g < groups.size(); g++)
        for (int p = groups[g].parent; p >= 0; p = groups[p].parent) depth[g]++;
    std::vector<int> by_depth(groups.size());
    for (size_t g = 0; g < groups.size(); g++) by_depth[g] = (int)g;
    std::sort(by_depth.begin(), by_depth.end(), [&](int a, int b) { return depth[a] > depth[b]; });
    for (int g : by_depth) {
        Group& gr = groups[g];
        if (gr.has_box) {
            double need = text.width(gr.title, 12, 600) + 24;
            gr.x1 = std::max(gr.x1, gr.x0 + need);
        }
        if (gr.parent >= 0 && gr.has_box) add(groups[gr.parent], gr.x0 - kGroupPad / 2, gr.y0 - kGroupPad / 2 - kGroupHead / 2, gr.x1 + kGroupPad / 2, gr.y1 + kGroupPad / 2);
    }

    std::string body;
    double min_x = 1e9, min_y = 1e9, max_x = -1e9, max_y = -1e9;
    auto grow = [&](double x0, double y0, double x1, double y1) {
        min_x = std::min(min_x, x0);
        min_y = std::min(min_y, y0);
        max_x = std::max(max_x, x1);
        max_y = std::max(max_y, y1);
    };
    std::vector<int> outer_first(by_depth.rbegin(), by_depth.rend());
    for (int g : outer_first) {
        const Group& gr = groups[g];
        if (!gr.has_box) continue;
        grow(gr.x0, gr.y0, gr.x1, gr.y1);
        body += "<g class=\"group\">\n<rect x=\"" + num(gr.x0) + "\" y=\"" + num(gr.y0) + "\" width=\"" + num(gr.x1 - gr.x0) + "\" height=\"" + num(gr.y1 - gr.y0) +
                "\" rx=\"8\" fill=\"none\" stroke=\"" + th.border + "\" stroke-width=\"1.5\" stroke-dasharray=\"6 4\"/>\n";
        aligned_text(body, th, {gr.title}, gr.x0 + 12, gr.y0 + kGroupHead / 2 + 4, 12, 600, th.text_sec, "start");
        body += "</g>\n";
    }

    // edges
    auto group_box = [&](const Node& n) -> const Group* {
        for (int g = n.parent; g >= 0; g = groups[g].parent)
            if (groups[g].has_box) return &groups[g];
        return nullptr;
    };
    auto port = [&](const Node& n, char side, bool use_group) {
        double cx = px(n), cy = py(n), h = half(n);
        const Group* g = use_group ? group_box(n) : nullptr;
        double l = g ? g->x0 : cx - h, r = g ? g->x1 : cx + h, t = g ? g->y0 : cy - h, b = g ? g->y1 : cy + h;
        return side == 'L' ? Pt{l, cy} : side == 'R' ? Pt{r, cy} : side == 'T' ? Pt{cx, t} : Pt{cx, b};
    };
    for (const Edge& ed : edges) {
        Pt p1 = port(nodes[ed.a], ed.side_a, ed.group_a), p2 = port(nodes[ed.b], ed.side_b, ed.group_b);
        bool h1 = ed.side_a == 'L' || ed.side_a == 'R', h2 = ed.side_b == 'L' || ed.side_b == 'R';
        std::vector<Pt> pts{p1};
        if (h1 && h2) {
            double mx = (p1.x + p2.x) / 2;
            if (std::fabs(p1.y - p2.y) > 0.5) {
                pts.push_back({mx, p1.y});
                pts.push_back({mx, p2.y});
            }
        } else if (!h1 && !h2) {
            double my = (p1.y + p2.y) / 2;
            if (std::fabs(p1.x - p2.x) > 0.5) {
                pts.push_back({p1.x, my});
                pts.push_back({p2.x, my});
            }
        } else if (h1) {
            pts.push_back({p2.x, p1.y});
        } else {
            pts.push_back({p1.x, p2.y});
        }
        pts.push_back(p2);
        std::string list;
        for (const Pt& p : pts) {
            list += (list.empty() ? "" : " ") + num(p.x) + "," + num(p.y);
            grow(p.x, p.y, p.x, p.y);
        }
        body += "<polyline class=\"edge\" points=\"" + list + "\" fill=\"none\" stroke=\"" + th.line + "\" stroke-width=\"2\" stroke-linejoin=\"round\"/>\n";
        auto head = [&](Pt tip, Pt from) {
            double dx = tip.x - from.x, dy = tip.y - from.y, len = std::sqrt(dx * dx + dy * dy);
            if (len < 0.001) return;
            dx /= len;
            dy /= len;
            double nx = -dy * 5, ny = dx * 5;
            body += "<polygon points=\"" + num(tip.x) + "," + num(tip.y) + " " + num(tip.x - dx * 10 + nx) + "," + num(tip.y - dy * 10 + ny) + " " + num(tip.x - dx * 10 - nx) + "," +
                    num(tip.y - dy * 10 - ny) + "\" fill=\"" + th.arrow + "\"/>\n";
        };
        if (ed.arrow_b) head(pts.back(), pts[pts.size() - 2]);
        if (ed.arrow_a) head(pts.front(), pts[1]);
    }

    for (const Node& n : nodes) {
        double cx = px(n), cy = py(n);
        body += "<g class=\"node\">\n";
        if (n.junction) {
            body += "<circle cx=\"" + num(cx) + "\" cy=\"" + num(cy) + "\" r=\"5\" fill=\"" + th.line + "\"/>\n";
            grow(cx - 5, cy - 5, cx + 5, cy + 5);
        } else {
            body += "<rect x=\"" + num(cx - kIcon / 2) + "\" y=\"" + num(cy - kIcon / 2) + "\" width=\"" + num(kIcon) + "\" height=\"" + num(kIcon) + "\" rx=\"10\" fill=\"" + th.surface +
                    "\" stroke=\"" + th.border + "\" stroke-width=\"1\"/>\n";
            body += icon_glyph(n.icon, cx, cy, th.text_sec);
            double lw = text.width(n.title, 12, 500) / 2;
            body += "<rect x=\"" + num(cx - lw - 4) + "\" y=\"" + num(cy + kIcon / 2 + 4) + "\" width=\"" + num(2 * lw + 8) + "\" height=\"20\" fill=\"" + th.bg + "\"/>\n";
            centered_text(body, th, {n.title}, cx, cy + kIcon / 2 + 14, 12, 500, th.text);
            grow(cx - std::max(kIcon / 2, lw), cy - kIcon / 2, cx + std::max(kIcon / 2, lw), cy + kIcon / 2 + 24);
        }
        body += "</g>\n";
    }

    const double margin = 24;
    double ox = margin - min_x, oy = margin - min_y;
    svg = svg_document(th, max_x - min_x + 2 * margin, max_y - min_y + 2 * margin, "<g transform=\"translate(" + num(ox) + "," + num(oy) + ")\">\n" + body + "</g>\n");
    return true;
}

}  // namespace mermaid
