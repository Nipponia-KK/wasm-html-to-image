#include "mermaid/block.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>

#include "mermaid/mermaid_internal.h"
#include "mermaid/nodestyle.h"

namespace mermaid {
namespace {

using litehtml::svgutil::trim;

enum class Shape { Rect, Rounded, Circle, Diamond, Stadium, Cylinder };

struct Block {
    std::string id, label;
    Shape shape = Shape::Rect;
    int span = 1;
    bool space = false;
    int col = 0, row = 0;
    double x = 0, y = 0, w = 0, h = 0;
    std::vector<std::string> lines;
};

struct Link {
    int from, to;
    std::string label;
};

bool starts(const std::string& s, const char* p) { return s.compare(0, std::char_traits<char>::length(p), p) == 0; }

std::string unquote(std::string s) {
    s = trim(s);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
    return s;
}

/// Splits on spaces that are outside brackets and quotes.
std::vector<std::string> tokens(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    bool quoted = false;
    for (char c : line) {
        if (c == '"') quoted = !quoted;
        if (!quoted) {
            if (c == '[' || c == '(' || c == '{') depth++;
            if (c == ']' || c == ')' || c == '}') depth--;
        }
        if (c == ' ' && depth <= 0 && !quoted) {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

/// `id`, `id:2`, `id["text"]`, `id("text")`, `id(("text"))`, `id{"text"}`, `id(["text"])`, each with an optional `:span`.
bool parse_block(std::string tok, Block& b) {
    size_t colon = std::string::npos;
    // the span is the `:n` after the last closing bracket (or after a bare id)
    size_t close = tok.find_last_of("])}");
    size_t from = close == std::string::npos ? 0 : close;
    colon = tok.find(':', from);
    if (colon != std::string::npos) {
        char* end;
        long n = std::strtol(tok.c_str() + colon + 1, &end, 10);
        if (*end != 0 || n < 1) return false;
        b.span = (int)n;
        tok = tok.substr(0, colon);
    }
    size_t open = tok.find_first_of("[({");
    if (open == std::string::npos) {
        b.id = tok;
        b.label = tok;
        return !tok.empty();
    }
    b.id = trim(tok.substr(0, open));
    std::string rest = tok.substr(open);
    struct Form {
        const char* open;
        const char* close;
        Shape shape;
    };
    static const Form forms[] = {{"((", "))", Shape::Circle}, {"([", "])", Shape::Stadium}, {"[(", ")]", Shape::Cylinder}, {"[", "]", Shape::Rect}, {"(", ")", Shape::Rounded}, {"{", "}", Shape::Diamond}};
    for (auto& f : forms) {
        size_t ol = std::char_traits<char>::length(f.open), cl = std::char_traits<char>::length(f.close);
        if (rest.size() >= ol + cl && rest.compare(0, ol, f.open) == 0 && rest.compare(rest.size() - cl, cl, f.close) == 0) {
            b.label = unquote(rest.substr(ol, rest.size() - ol - cl));
            b.shape = f.shape;
            return !b.id.empty();
        }
    }
    return false;
}

}  // namespace

bool render_block(const std::string& source, const Options& options, std::string& svg, std::string& error) {
    using namespace metrics;
    std::vector<Block> blocks;
    StyleSheet sheet;
    std::vector<Link> links;
    std::map<std::string, int> index;
    int columns = 0;
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
        if (starts(line, "columns ")) {
            columns = std::atoi(line.c_str() + 8);
            if (columns < 1) {
                error = "columns needs a positive number: " + line;
                return false;
            }
            continue;
        }
        if (starts(line, "block:") || line == "end" || starts(line, "block ")) {
            error = "nested blocks are not supported";
            return false;
        }
        if (sheet.take(line)) continue;
        size_t arrow = line.find("-->");
        if (arrow != std::string::npos) {
            // `a --> b` or `a -- "text" --> b`
            std::string left = trim(line.substr(0, arrow)), right = trim(line.substr(arrow + 3)), label;
            size_t dd = left.find("--");
            if (dd != std::string::npos) {
                label = unquote(left.substr(dd + 2));
                left = trim(left.substr(0, dd));
            }
            if (!index.count(left) || !index.count(right)) {
                error = "unknown block in: " + line;
                return false;
            }
            links.push_back({index[left], index[right], label});
            continue;
        }
        for (auto& tok : tokens(line)) {
            Block b;
            if (starts(tok, "space")) {
                b.space = true;
                b.id = "";
                size_t c = tok.find(':');
                b.span = c == std::string::npos ? 1 : std::max(1, std::atoi(tok.c_str() + c + 1));
            } else if (!parse_block(tok, b)) {
                error = "cannot read the block: " + tok;
                return false;
            }
            blocks.push_back(b);
            if (!b.space) index[b.id] = (int)blocks.size() - 1;
        }
    }
    size_t real = 0;
    for (auto& b : blocks) real += !b.space;
    if (real == 0) {
        error = "a block diagram needs at least one block";
        return false;
    }
    if (columns == 0) {
        for (auto& b : blocks) columns += b.span;
    }
    Theme th = make_theme(options);
    Text text{options};
    const double kGap = 16, kH = 52, kFont = 13;
    // one cell width for the whole grid, wide enough for the widest label per column it spans
    double cell = 80;
    for (auto& b : blocks) {
        if (b.space) continue;
        b.lines = split_lines(b.label);
        double w = text.block_width(b.lines, kFont, 500) + 32 + (b.shape == Shape::Diamond ? 40 : 0);
        cell = std::max(cell, (w - (b.span - 1) * kGap) / std::max(1, std::min(b.span, columns)));
    }
    // placement: left to right, a block that does not fit in the row starts the next one
    int col = 0, row = 0;
    std::vector<double> row_h;
    for (auto& b : blocks) {
        int span = std::min(b.span, columns);
        if (col + span > columns) col = 0, row++;
        b.col = col;
        b.row = row;
        b.span = span;
        col += span;
        if (col >= columns) col = 0, row++;
    }
    int rows = 0;
    for (auto& b : blocks) rows = std::max(rows, b.row + 1);
    for (auto& b : blocks) {
        b.w = b.span * cell + (b.span - 1) * kGap;
        b.h = std::max(kH, b.lines.size() * kFont * kLineHeight + 24);
        if (b.shape == Shape::Circle) b.h = std::max(b.h, 64.0);
    }
    double row_height = kH;
    for (auto& b : blocks) row_height = std::max(row_height, b.h);
    for (auto& b : blocks) {
        b.x = kMargin + b.col * (cell + kGap);
        b.y = kMargin + b.row * (row_height + kGap + 8) + (row_height - b.h) / 2;
    }
    double W = kMargin * 2 + columns * cell + (columns - 1) * kGap, H = kMargin * 2 + rows * row_height + (rows - 1) * (kGap + 8);

    std::string body, edges;
    for (auto& l : links) {
        const Block &a = blocks[l.from], &b = blocks[l.to];
        double ax = a.x + a.w / 2, ay = a.y + a.h / 2, bx = b.x + b.w / 2, by = b.y + b.h / 2, dx = bx - ax, dy = by - ay;
        auto edge_point = [&](const Block& k, double cx, double cy, double ux, double uy) {
            double tx = std::fabs(ux) > 1e-9 ? (k.w / 2) / std::fabs(ux) : 1e18, ty = std::fabs(uy) > 1e-9 ? (k.h / 2) / std::fabs(uy) : 1e18;
            double t = std::min(tx, ty);
            return Pt{cx + ux * t, cy + uy * t};
        };
        double len = std::sqrt(dx * dx + dy * dy);
        if (len < 1) continue;
        // a link between two blocks of one row with another block in between arcs over the row
        bool blocked = false;
        if (std::fabs(dy) < 1) {
            for (size_t k = 0; k < blocks.size(); k++) {
                const Block& o = blocks[k];
                if (o.space || &o == &a || &o == &b) continue;
                if (o.x + o.w > std::min(ax, bx) && o.x < std::max(ax, bx) && std::fabs(o.y + o.h / 2 - ay) < 1) blocked = true;
            }
        }
        if (blocked) {
            double ty = a.y, ctrl = ty - 30, mid = (ax + bx) / 2, dir = bx > ax ? 1 : -1;
            edges += "<path class=\"edge\" d=\"M" + num(ax) + "," + num(ty) + " Q" + num(mid) + "," + num(ctrl) + " " + num(bx - dir * 0) + "," + num(b.y - 9) + "\" fill=\"none\" stroke=\"" + th.line +
                     "\" stroke-width=\"1.5\"/>\n";
            edges += "<polygon points=\"" + num(bx) + "," + num(b.y) + " " + num(bx - 5) + "," + num(b.y - 9) + " " + num(bx + 5) + "," + num(b.y - 9) + "\" fill=\"" + th.arrow + "\"/>\n";
            if (!l.label.empty()) {
                double lw = text.width(l.label, kEdgeFont, kEdgeWeight) + 14, lh = kEdgeFont * kLineHeight + 8, my = (ty + ctrl) / 2 - 2;
                edges += "<g class=\"edge-label\">\n<rect x=\"" + num(mid - lw / 2) + "\" y=\"" + num(my - lh / 2) + "\" width=\"" + num(lw) + "\" height=\"" + num(lh) + "\" rx=\"2\" fill=\"" + th.bg +
                         "\" stroke=\"" + th.inner_stroke + "\" stroke-width=\"1\"/>\n";
                centered_text(edges, th, {l.label}, mid, my, kEdgeFont, kEdgeWeight, th.text_sec);
                edges += "</g>\n";
            }
            continue;
        }
        double ux = dx / len, uy = dy / len;
        Pt p = edge_point(a, ax, ay, ux, uy), q = edge_point(b, bx, by, -ux, -uy);
        double hx = q.x - ux * 9, hy = q.y - uy * 9, nx = -uy * 5, ny = ux * 5;
        edges += "<line class=\"edge\" x1=\"" + num(p.x) + "\" y1=\"" + num(p.y) + "\" x2=\"" + num(hx) + "\" y2=\"" + num(hy) + "\" stroke=\"" + th.line + "\" stroke-width=\"1.5\"/>\n";
        edges += "<polygon points=\"" + num(q.x) + "," + num(q.y) + " " + num(hx + nx) + "," + num(hy + ny) + " " + num(hx - nx) + "," + num(hy - ny) + "\" fill=\"" + th.arrow + "\"/>\n";
        if (!l.label.empty()) {
            double lw = text.width(l.label, kEdgeFont, kEdgeWeight) + 14, lh = kEdgeFont * kLineHeight + 8, mx = (p.x + q.x) / 2, my = (p.y + q.y) / 2;
            edges += "<g class=\"edge-label\">\n<rect x=\"" + num(mx - lw / 2) + "\" y=\"" + num(my - lh / 2) + "\" width=\"" + num(lw) + "\" height=\"" + num(lh) + "\" rx=\"2\" fill=\"" + th.bg + "\" stroke=\"" +
                     th.inner_stroke + "\" stroke-width=\"1\"/>\n";
            centered_text(edges, th, {l.label}, mx, my, kEdgeFont, kEdgeWeight, th.text_sec);
            edges += "</g>\n";
        }
    }
    for (auto& b : blocks) {
        if (b.space) continue;
        double cx = b.x + b.w / 2, cy = b.y + b.h / 2;
        std::string style = sheet.paint(b.id, th.surface, th.border);
        body += "<g class=\"node\">\n";
        if (b.shape == Shape::Circle) body += "<ellipse cx=\"" + num(cx) + "\" cy=\"" + num(cy) + "\" rx=\"" + num(b.w / 2) + "\" ry=\"" + num(b.h / 2) + "\"" + style + "/>\n";
        else if (b.shape == Shape::Cylinder) {
            double ry = 8, l = b.x, r = b.x + b.w, t = b.y, bt = b.y + b.h;
            body += "<path d=\"M" + num(l) + "," + num(t + ry) + " A" + num(b.w / 2) + "," + num(ry) + " 0 0 1 " + num(r) + "," + num(t + ry) + " L" + num(r) + "," + num(bt - ry) + " A" + num(b.w / 2) + "," + num(ry) +
                    " 0 0 1 " + num(l) + "," + num(bt - ry) + " Z\"" + style + "/>\n";
            body += "<path d=\"M" + num(l) + "," + num(t + ry) + " A" + num(b.w / 2) + "," + num(ry) + " 0 0 0 " + num(r) + "," + num(t + ry) + "\" fill=\"none\" stroke=\"" + th.border + "\" stroke-width=\"1\"/>\n";
        } else if (b.shape == Shape::Diamond)
            body += "<polygon points=\"" + num(cx) + "," + num(b.y) + " " + num(b.x + b.w) + "," + num(cy) + " " + num(cx) + "," + num(b.y + b.h) + " " + num(b.x) + "," + num(cy) + "\"" + style + "/>\n";
        else {
            double rx = b.shape == Shape::Stadium ? b.h / 2 : b.shape == Shape::Rounded ? 10 : 2;
            body += "<rect x=\"" + num(b.x) + "\" y=\"" + num(b.y) + "\" width=\"" + num(b.w) + "\" height=\"" + num(b.h) + "\" rx=\"" + num(rx) + "\"" + style + "/>\n";
        }
        centered_text(body, th, b.lines, cx, cy, kFont, 500, sheet.text_color(b.id, th.text));
        body += "</g>\n";
    }
    svg = svg_document(th, W, H, body + edges);
    return true;
}

}  // namespace mermaid
