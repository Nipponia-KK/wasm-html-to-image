#include "mermaid/flowchart.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>

#include "mermaid/layered.h"

namespace mermaid {

namespace {

using litehtml::svgutil::trim;

enum class Shape { Rect, Rounded, Stadium, Subroutine, Cylinder, Circle, DoubleCircle, Diamond, Hexagon, Asymmetric, Trapezoid, TrapezoidAlt, Parallel, ParallelAlt };

struct Style {
    std::string fill, stroke, color, stroke_width, dash;
};

struct Node {
    std::string id, label;
    Shape shape = Shape::Rect;
    Style style;
    int group = -1;
    double w = 0, h = 0;
    std::vector<std::string> lines;
};

enum class Kind { Solid, Dotted, Thick, Invisible };

struct Edge {
    int from = 0, to = 0;
    std::string label;
    Kind kind = Kind::Solid;
    bool arrow_end = true, arrow_start = false;
    std::string stroke, stroke_width;
};

struct Group {
    std::string id, title;
    int parent = -1;
    std::vector<int> nodes;
};

struct Graph {
    Direction dir = Direction::TD;
    std::vector<Node> nodes;
    std::vector<Edge> edges;
    std::vector<Group> groups;
    std::map<std::string, int> index;
    std::map<std::string, Style> classes;
    std::vector<std::pair<int, std::string>> class_use;  // node, class name
    std::map<int, Style> link_styles;                    // edge index, -1 = default
    std::map<int, std::string> link_stroke;
};

bool is_id_char(unsigned char c) { return std::isalnum(c) || c == '_' || c >= 0x80; }

Style parse_style(const std::string& decl) {
    Style s;
    size_t i = 0;
    while (i < decl.size()) {
        size_t e = decl.find(',', i);
        std::string part = trim(decl.substr(i, e == std::string::npos ? std::string::npos : e - i));
        size_t c = part.find(':');
        if (c != std::string::npos) {
            std::string k = trim(part.substr(0, c)), v = trim(part.substr(c + 1));
            if (k == "fill") s.fill = v;
            else if (k == "stroke") s.stroke = v;
            else if (k == "color") s.color = v;
            else if (k == "stroke-width") s.stroke_width = v;
            else if (k == "stroke-dasharray") s.dash = v;
        }
        if (e == std::string::npos) break;
        i = e + 1;
    }
    return s;
}

std::string px(const std::string& v) {
    return v.size() > 2 && v.compare(v.size() - 2, 2, "px") == 0 ? v.substr(0, v.size() - 2) : v;
}

struct Parser {
    Graph g;
    std::string error;
    std::vector<int> group_stack;

    int node_index(const std::string& id) {
        auto it = g.index.find(id);
        if (it != g.index.end()) return it->second;
        Node n;
        n.id = id;
        n.label = id;
        g.nodes.push_back(n);
        int idx = (int)g.nodes.size() - 1;
        g.index[id] = idx;
        return idx;
    }

    static bool closer_at(const std::string& s, size_t i, const std::string& closer) { return s.compare(i, closer.size(), closer) == 0; }

    // `A`, `A[text]`, `A((text))`, `A{text}` ... `A:::cls`. Returns the node index or -1.
    int parse_node(const std::string& s, size_t& i) {
        while (i < s.size() && s[i] == ' ') i++;
        size_t b = i;
        while (i < s.size() && is_id_char((unsigned char)s[i])) i++;
        if (i == b) return -1;
        std::string id = s.substr(b, i - b);
        struct Open {
            const char* open;
            std::vector<const char*> close;
            Shape shape;
        };
        static const Open opens[] = {
            {"(((", {")))"}, Shape::DoubleCircle}, {"((", {"))"}, Shape::Circle},      {"([", {"])"}, Shape::Stadium},
            {"(", {")"}, Shape::Rounded},          {"[[", {"]]"}, Shape::Subroutine}, {"[(", {")]"}, Shape::Cylinder},
            {"[/", {"\\]"}, Shape::Trapezoid},     {"[\\", {"/]"}, Shape::TrapezoidAlt},
            {"[", {"]"}, Shape::Rect},             {"{{", {"}}"}, Shape::Hexagon},     {"{", {"}"}, Shape::Diamond},
            {">", {"]"}, Shape::Asymmetric},
        };
        bool has_shape = false;
        Shape shape = Shape::Rect;
        std::string label;
        for (const Open& o : opens) {
            bool parallelogram = false;
            size_t ol = std::strlen(o.open);
            if (s.compare(i, ol, o.open) != 0) continue;
            size_t t = i + ol;
            std::string text;
            size_t end = std::string::npos;
            size_t close_len = 0;
            while (t < s.size() && s[t] == ' ') t++;
            if (t < s.size() && s[t] == '"') {
                size_t q = s.find('"', t + 1);
                if (q == std::string::npos) continue;
                text = s.substr(t + 1, q - t - 1);
                size_t k = q + 1;
                while (k < s.size() && s[k] == ' ') k++;
                for (const char* c : o.close)
                    if (closer_at(s, k, c)) {
                        end = k;
                        close_len = std::strlen(c);
                        break;
                    }
            } else {
                // the text runs to the first closer (the shape's own, or the other slash of a parallelogram)
                std::vector<std::string> closers(o.close.begin(), o.close.end());
                if (!std::strcmp(o.open, "[/")) closers.push_back("/]");
                if (!std::strcmp(o.open, "[\\")) closers.push_back("\\]");
                size_t best = std::string::npos;
                for (const std::string& c : closers) {
                    size_t f = s.find(c, t);
                    if (f != std::string::npos && f < best) {
                        best = f;
                        close_len = c.size();
                    }
                }
                if (best == std::string::npos) continue;
                end = best;
                text = s.substr(t, end - t);
                // `[/text/]` and `[\text\]` are parallelograms: drawn as a plain box
                if (!std::strcmp(o.open, "[/") && s.compare(end, 2, "/]") == 0) parallelogram = true;
                if (!std::strcmp(o.open, "[\\") && s.compare(end, 2, "\\]") == 0) parallelogram = true;
            }
            if (end == std::string::npos) continue;
            shape = parallelogram ? (o.open[1] == '/' ? Shape::Parallel : Shape::ParallelAlt) : o.shape;
            has_shape = true;
            label = trim(text);
            i = end + close_len;
            break;
        }
        int idx = node_index(id);
        if (has_shape) {
            g.nodes[idx].label = label;
            g.nodes[idx].shape = shape;
        }
        if (s.compare(i, 3, ":::") == 0) {
            size_t c = i + 3, e = c;
            while (e < s.size() && is_id_char((unsigned char)s[e])) e++;
            g.class_use.push_back({idx, s.substr(c, e - c)});
            i = e;
        }
        if (!group_stack.empty() && g.nodes[idx].group < 0) {
            g.nodes[idx].group = group_stack.back();
            g.groups[group_stack.back()].nodes.push_back(idx);
        }
        return idx;
    }

    struct Link {
        Kind kind = Kind::Solid;
        bool arrow_end = false, arrow_start = false;
        std::string label;
    };

    // The connector after a node: `-->`, `---`, `-.->`, `==>`, `--x`, `<-->`, `-- text -->`, `-->|text|` ...
    bool parse_link(const std::string& s, size_t& i, Link& link) {
        while (i < s.size() && s[i] == ' ') i++;
        size_t j = i;
        if (j >= s.size()) return false;
        if (s[j] == '<') {
            link.arrow_start = true;
            j++;
        }
        char c = j < s.size() ? s[j] : 0;
        if (c != '-' && c != '=' && c != '~') return false;
        auto arrow = [&](size_t& k) {
            if (k < s.size() && (s[k] == '>' || s[k] == 'x' || s[k] == 'o')) {
                bool real = s[k] == '>' || k + 1 >= s.size() || !is_id_char((unsigned char)s[k + 1]);
                if (real) {
                    link.arrow_end = true;
                    k++;
                }
            }
        };
        if (c == '~') {
            size_t k = j;
            while (k < s.size() && s[k] == '~') k++;
            if (k - j < 3) return false;
            link.kind = Kind::Invisible;
            i = k;
        } else if (c == '-' && j + 1 < s.size() && s[j + 1] == '.') {
            // dotted: -.-  -.->  -. text .->
            size_t k = j + 2;
            if (k < s.size() && s[k] == ' ') {
                size_t e = s.find(".-", k);
                if (e == std::string::npos) return false;
                link.label = trim(s.substr(k, e - k));
                k = e;
            }
            while (k < s.size() && s[k] == '.') k++;
            if (k >= s.size() || s[k] != '-') return false;
            k++;
            link.kind = Kind::Dotted;
            arrow(k);
            i = k;
        } else {
            size_t k = j;
            while (k < s.size() && s[k] == c) k++;
            size_t run = k - j;
            if (run >= 2 && k < s.size() && s[k] == ' ') {
                // `-- text -->`: text up to the closing run of the same character
                size_t e = k;
                std::string closer(2, c);
                size_t f = s.find(closer, e);
                if (f == std::string::npos) return false;
                link.label = trim(s.substr(k, f - k));
                k = f;
                while (k < s.size() && s[k] == c) k++;
                arrow(k);
            } else {
                arrow(k);
                if (run < 2 || (run == 2 && !link.arrow_end)) return false;
            }
            link.kind = c == '=' ? Kind::Thick : Kind::Solid;
            i = k;
        }
        if (link.label.empty()) {
            size_t p = i;
            while (p < s.size() && s[p] == ' ') p++;
            if (p < s.size() && s[p] == '|') {
                size_t q = s.find('|', p + 1);
                if (q != std::string::npos) {
                    std::string text = trim(s.substr(p + 1, q - p - 1));
                    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') text = text.substr(1, text.size() - 2);
                    link.label = text;
                    i = q + 1;
                }
            }
        }
        return true;
    }

    bool parse_chain(const std::string& s) {
        size_t i = 0;
        std::vector<int> left;
        auto group = [&](std::vector<int>& out) {
            out.clear();
            for (;;) {
                int n = parse_node(s, i);
                if (n < 0) return false;
                out.push_back(n);
                size_t k = i;
                while (k < s.size() && s[k] == ' ') k++;
                if (k < s.size() && s[k] == '&') {
                    i = k + 1;
                    continue;
                }
                return true;
            }
        };
        if (!group(left)) return false;
        for (;;) {
            Link link;
            size_t save = i;
            if (!parse_link(s, i, link)) {
                i = save;
                break;
            }
            std::vector<int> right;
            if (!group(right)) return false;
            for (int a : left)
                for (int b : right) {
                    Edge e;
                    e.from = a;
                    e.to = b;
                    e.label = link.label;
                    e.kind = link.kind;
                    e.arrow_end = link.kind != Kind::Invisible && link.arrow_end;
                    e.arrow_start = link.arrow_start;
                    g.edges.push_back(e);
                }
            left = right;
        }
        while (i < s.size() && s[i] == ' ') i++;
        return i >= s.size();
    }

    bool parse_statement(const std::string& line) {
        std::string s = trim(line);
        if (s.empty()) return true;
        auto starts = [&](const char* kw) {
            size_t l = std::strlen(kw);
            return s.compare(0, l, kw) == 0 && (s.size() == l || s[l] == ' ');
        };
        if (starts("subgraph")) {
            std::string rest = trim(s.substr(8));
            Group gr;
            size_t br = rest.find('[');
            if (br != std::string::npos && rest.back() == ']') {
                gr.id = trim(rest.substr(0, br));
                gr.title = trim(rest.substr(br + 1, rest.size() - br - 2));
            } else {
                gr.id = rest;
                gr.title = rest;
            }
            if (gr.title.size() >= 2 && gr.title.front() == '"' && gr.title.back() == '"') gr.title = gr.title.substr(1, gr.title.size() - 2);
            if (gr.id.size() >= 2 && gr.id.front() == '"') gr.id = gr.title;
            gr.parent = group_stack.empty() ? -1 : group_stack.back();
            g.groups.push_back(gr);
            group_stack.push_back((int)g.groups.size() - 1);
            return true;
        }
        if (s == "end") {
            if (!group_stack.empty()) group_stack.pop_back();
            return true;
        }
        if (starts("direction") || starts("click") || starts("accTitle") || starts("accDescr") || starts("title")) return true;
        if (starts("classDef")) {
            std::string rest = trim(s.substr(8));
            size_t sp = rest.find(' ');
            if (sp == std::string::npos) return true;
            Style st = parse_style(rest.substr(sp + 1));
            std::string names = rest.substr(0, sp);
            size_t k = 0;
            while (k <= names.size()) {
                size_t e = names.find(',', k);
                g.classes[trim(names.substr(k, e == std::string::npos ? std::string::npos : e - k))] = st;
                if (e == std::string::npos) break;
                k = e + 1;
            }
            return true;
        }
        if (starts("class")) {
            std::string rest = trim(s.substr(5));
            size_t sp = rest.rfind(' ');
            if (sp == std::string::npos) return true;
            std::string names = rest.substr(0, sp), cls = trim(rest.substr(sp + 1));
            size_t k = 0;
            while (k <= names.size()) {
                size_t e = names.find(',', k);
                std::string id = trim(names.substr(k, e == std::string::npos ? std::string::npos : e - k));
                auto it = g.index.find(id);
                if (it != g.index.end()) g.class_use.push_back({it->second, cls});
                if (e == std::string::npos) break;
                k = e + 1;
            }
            return true;
        }
        if (starts("style")) {
            std::string rest = trim(s.substr(5));
            size_t sp = rest.find(' ');
            if (sp == std::string::npos) return true;
            auto it = g.index.find(rest.substr(0, sp));
            if (it == g.index.end()) return true;
            Style st = parse_style(rest.substr(sp + 1));
            Style& dst = g.nodes[it->second].style;
            if (!st.fill.empty()) dst.fill = st.fill;
            if (!st.stroke.empty()) dst.stroke = st.stroke;
            if (!st.color.empty()) dst.color = st.color;
            if (!st.stroke_width.empty()) dst.stroke_width = st.stroke_width;
            if (!st.dash.empty()) dst.dash = st.dash;
            return true;
        }
        if (starts("linkStyle")) {
            std::string rest = trim(s.substr(9));
            size_t sp = rest.find(' ');
            if (sp == std::string::npos) return true;
            std::string which = rest.substr(0, sp);
            Style st = parse_style(rest.substr(sp + 1));
            size_t k = 0;
            while (k <= which.size()) {
                size_t e = which.find(',', k);
                std::string tok = trim(which.substr(k, e == std::string::npos ? std::string::npos : e - k));
                int idx = tok == "default" ? -1 : std::atoi(tok.c_str());
                g.link_styles[idx] = st;
                if (e == std::string::npos) break;
                k = e + 1;
            }
            return true;
        }
        return parse_chain(s);
    }
};

bool parse(const std::string& source, Graph& g, std::string& error) {
    Parser p;
    std::vector<std::string> lines;
    std::string cur;
    bool in_quote = false;
    for (char c : source) {
        if (c == '"') in_quote = !in_quote;
        if (c == '\n' || (c == ';' && !in_quote)) {
            lines.push_back(cur);
            cur.clear();
        } else if (c != '\r') {
            cur += c;
        }
    }
    lines.push_back(cur);

    bool header = false;
    for (std::string line : lines) {
        size_t cm = line.find("%%");
        if (cm != std::string::npos) line = line.substr(0, cm);
        line = trim(line);
        if (line.empty()) continue;
        if (!header) {
            size_t sp = line.find(' ');
            std::string kw = line.substr(0, sp);
            if (kw != "graph" && kw != "flowchart" && kw != "flowchart-elk") {
                error = "not a flowchart: " + kw;
                return false;
            }
            std::string d = sp == std::string::npos ? "TD" : trim(line.substr(sp + 1));
            if (d == "LR") p.g.dir = Direction::LR;
            else if (d == "RL") p.g.dir = Direction::RL;
            else if (d == "BT") p.g.dir = Direction::BT;
            else p.g.dir = Direction::TD;
            header = true;
            continue;
        }
        if (!p.parse_statement(line)) {
            error = "cannot parse: " + line;
            return false;
        }
    }
    if (!header) {
        error = "empty diagram";
        return false;
    }
    for (auto& [idx, cls] : p.g.class_use) {
        auto it = p.g.classes.find(cls);
        if (it == p.g.classes.end()) continue;
        Style& dst = p.g.nodes[idx].style;
        const Style& st = it->second;
        if (dst.fill.empty()) dst.fill = st.fill;
        if (dst.stroke.empty()) dst.stroke = st.stroke;
        if (dst.color.empty()) dst.color = st.color;
        if (dst.stroke_width.empty()) dst.stroke_width = st.stroke_width;
        if (dst.dash.empty()) dst.dash = st.dash;
    }
    g = std::move(p.g);
    return true;
}

// ── sizes ──

void size_nodes(Graph& g, const Text& text) {
    using namespace metrics;
    for (Node& n : g.nodes) {
        n.lines = split_lines(n.label);
        double tw = text.block_width(n.lines, kNodeFont, kNodeWeight);
        double th = n.lines.size() * kNodeFont * kLineHeight;
        double w = tw + 2 * kPadX, h = th + 2 * kPadY;
        switch (n.shape) {
            case Shape::Diamond: {
                // the label has to fit the rhombus: tw / w + th / h <= 1
                w = std::max(w + 24, std::max(tw, th) + 62);
                h = std::max(w * 0.75, th * 2.2);
                break;
            }
            case Shape::Circle:
            case Shape::DoubleCircle: {
                double d = std::max(w, h) + (n.shape == Shape::DoubleCircle ? 12 : 4);
                w = h = d;
                break;
            }
            case Shape::Stadium: w += h / 2; break;
            case Shape::Subroutine: w += 16; break;
            case Shape::Cylinder: h += 16; break;
            case Shape::Hexagon: w += h * 0.6; break;
            case Shape::Asymmetric: w += 14; break;
            case Shape::Trapezoid:
            case Shape::TrapezoidAlt:
            case Shape::Parallel:
            case Shape::ParallelAlt: w += 28; break;
            default: break;
        }
        n.w = std::ceil(w * 100) / 100;
        n.h = std::ceil(h * 100) / 100;
    }
}

// ── drawing ──

std::string stroke_attrs(const std::string& color, const std::string& width, const std::string& dash) {
    std::string s = " stroke=\"" + color + "\" stroke-width=\"" + width + "\"";
    if (!dash.empty()) s += " stroke-dasharray=\"" + dash + "\"";
    return s;
}

void draw_node(std::string& out, const Theme& th, const Node& n, Pt c) {
    using namespace metrics;
    std::string fill = n.style.fill.empty() ? th.surface : n.style.fill;
    std::string stroke = n.style.stroke.empty() ? th.border : n.style.stroke;
    std::string sw = n.style.stroke_width.empty() ? num(kInnerStroke) : px(n.style.stroke_width);
    std::string st = " fill=\"" + fill + "\"" + stroke_attrs(stroke, sw, n.style.dash);
    double l = c.x - n.w / 2, r = c.x + n.w / 2, t = c.y - n.h / 2, b = c.y + n.h / 2;
    auto pts = [&](std::initializer_list<Pt> p) {
        std::string s;
        for (const Pt& q : p) s += (s.empty() ? "" : " ") + num(q.x) + "," + num(q.y);
        return s;
    };
    out += "<g class=\"node\">\n";
    switch (n.shape) {
        case Shape::Rect:
            out += "<rect x=\"" + num(l) + "\" y=\"" + num(t) + "\" width=\"" + num(n.w) + "\" height=\"" + num(n.h) + "\"" + st + "/>\n";
            break;
        case Shape::Rounded:
            out += "<rect x=\"" + num(l) + "\" y=\"" + num(t) + "\" width=\"" + num(n.w) + "\" height=\"" + num(n.h) + "\" rx=\"6\" ry=\"6\"" + st + "/>\n";
            break;
        case Shape::Stadium:
            out += "<rect x=\"" + num(l) + "\" y=\"" + num(t) + "\" width=\"" + num(n.w) + "\" height=\"" + num(n.h) + "\" rx=\"" + num(n.h / 2) + "\" ry=\"" + num(n.h / 2) + "\"" + st + "/>\n";
            break;
        case Shape::Subroutine:
            out += "<rect x=\"" + num(l) + "\" y=\"" + num(t) + "\" width=\"" + num(n.w) + "\" height=\"" + num(n.h) + "\"" + st + "/>\n";
            for (double x : {l + 8, r - 8})
                out += "<line x1=\"" + num(x) + "\" y1=\"" + num(t) + "\" x2=\"" + num(x) + "\" y2=\"" + num(b) + "\"" + stroke_attrs(stroke, sw, "") + "/>\n";
            break;
        case Shape::Circle:
            out += "<circle cx=\"" + num(c.x) + "\" cy=\"" + num(c.y) + "\" r=\"" + num(n.w / 2) + "\"" + st + "/>\n";
            break;
        case Shape::DoubleCircle:
            out += "<circle cx=\"" + num(c.x) + "\" cy=\"" + num(c.y) + "\" r=\"" + num(n.w / 2) + "\"" + st + "/>\n";
            out += "<circle cx=\"" + num(c.x) + "\" cy=\"" + num(c.y) + "\" r=\"" + num(n.w / 2 - 4) + "\" fill=\"none\"" + stroke_attrs(stroke, sw, "") + "/>\n";
            break;
        case Shape::Diamond:
            out += "<polygon points=\"" + pts({{c.x, t}, {r, c.y}, {c.x, b}, {l, c.y}}) + "\"" + st + "/>\n";
            break;
        case Shape::Hexagon: {
            double k = n.h * 0.3;
            out += "<polygon points=\"" + pts({{l + k, t}, {r - k, t}, {r, c.y}, {r - k, b}, {l + k, b}, {l, c.y}}) + "\"" + st + "/>\n";
            break;
        }
        case Shape::Asymmetric:
            out += "<polygon points=\"" + pts({{l, t}, {r, t}, {r, b}, {l, b}, {l + 12, c.y}}) + "\"" + st + "/>\n";
            break;
        case Shape::Trapezoid:
            out += "<polygon points=\"" + pts({{l + 14, t}, {r - 14, t}, {r, b}, {l, b}}) + "\"" + st + "/>\n";
            break;
        case Shape::TrapezoidAlt:
            out += "<polygon points=\"" + pts({{l, t}, {r, t}, {r - 14, b}, {l + 14, b}}) + "\"" + st + "/>\n";
            break;
        case Shape::Parallel:
            out += "<polygon points=\"" + pts({{l + 14, t}, {r, t}, {r - 14, b}, {l, b}}) + "\"" + st + "/>\n";
            break;
        case Shape::ParallelAlt:
            out += "<polygon points=\"" + pts({{l, t}, {r - 14, t}, {r, b}, {l + 14, b}}) + "\"" + st + "/>\n";
            break;
        case Shape::Cylinder: {
            double ry = std::min(10.0, n.h * 0.14), rx = n.w / 2;
            std::string d = "M" + num(l) + "," + num(t + ry) + " A" + num(rx) + "," + num(ry) + " 0 0 1 " + num(r) + "," + num(t + ry) + " L" + num(r) + "," +
                            num(b - ry) + " A" + num(rx) + "," + num(ry) + " 0 0 1 " + num(l) + "," + num(b - ry) + " Z";
            out += "<path d=\"" + d + "\"" + st + "/>\n";
            out += "<path d=\"M" + num(l) + "," + num(t + ry) + " A" + num(rx) + "," + num(ry) + " 0 0 0 " + num(r) + "," + num(t + ry) + "\" fill=\"none\"" +
                   stroke_attrs(stroke, sw, "") + "/>\n";
            break;
        }
    }
    double cy = n.shape == Shape::Cylinder ? c.y + n.h * 0.07 : c.y;
    centered_text(out, th, n.lines, c.x, cy, kNodeFont, kNodeWeight, n.style.color.empty() ? th.text : n.style.color);
    out += "</g>\n";
}

void draw_arrow(std::string& out, Pt tip, Pt from, const std::string& color) {
    using namespace metrics;
    double dx = tip.x - from.x, dy = tip.y - from.y, len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001) return;
    dx /= len;
    dy /= len;
    Pt base = {tip.x - dx * kArrowW, tip.y - dy * kArrowW};
    double nx = -dy * kArrowH / 2, ny = dx * kArrowH / 2;
    out += "<polygon points=\"" + num(tip.x) + "," + num(tip.y) + " " + num(base.x + nx) + "," + num(base.y + ny) + " " + num(base.x - nx) + "," +
           num(base.y - ny) + "\" fill=\"" + color + "\" stroke=\"" + color + "\" stroke-width=\"0.75\" stroke-linejoin=\"round\"/>\n";
}

}  // namespace

bool render_flowchart(const std::string& source, const Options& options, std::string& svg, std::string& error) {
    using namespace metrics;
    Graph g;
    if (!parse(source, g, error)) return false;
    if (g.nodes.empty()) {
        error = "empty diagram";
        return false;
    }
    Theme th = make_theme(options);
    Text text{options};
    size_nodes(g, text);

    std::vector<LayoutNode> ln(g.nodes.size());
    for (size_t i = 0; i < g.nodes.size(); i++) {
        ln[i].w = g.nodes[i].w;
        ln[i].h = g.nodes[i].h;
        ln[i].group = g.nodes[i].group;
        Shape s = g.nodes[i].shape;
        ln[i].outline = s == Shape::Diamond ? 1 : (s == Shape::Circle || s == Shape::DoubleCircle ? 2 : 0);
    }
    std::vector<LayoutEdge> le(g.edges.size());
    std::vector<std::vector<std::string>> label_lines(g.edges.size());
    for (size_t i = 0; i < g.edges.size(); i++) {
        le[i].from = g.edges[i].from;
        le[i].to = g.edges[i].to;
        if (!g.edges[i].label.empty()) {
            label_lines[i] = split_lines(g.edges[i].label);
            le[i].label_w = text.block_width(label_lines[i], kEdgeFont, kEdgeWeight) + 16;
            le[i].label_h = label_lines[i].size() * kEdgeFont * kLineHeight + 16;
        }
    }
    std::vector<int> group_parent;
    for (const Group& gr : g.groups) group_parent.push_back(gr.parent);
    LayoutResult lay = layout_layered(ln, le, g.dir, group_parent);

    // subgraph frames around their members (children first so a parent covers them)
    struct Frame {
        double x0, y0, x1, y1;
        bool used = false;
    };
    std::vector<Frame> frames(g.groups.size());
    auto grow = [&](Frame& f, double x0, double y0, double x1, double y1) {
        if (!f.used) f = {x0, y0, x1, y1, true};
        else f = {std::min(f.x0, x0), std::min(f.y0, y0), std::max(f.x1, x1), std::max(f.y1, y1), true};
    };
    constexpr double kFramePad = 16, kHeader = 26;
    for (size_t gi = 0; gi < g.groups.size(); gi++)
        for (int v : g.groups[gi].nodes)
            grow(frames[gi], lay.center[v].x - g.nodes[v].w / 2, lay.center[v].y - g.nodes[v].h / 2, lay.center[v].x + g.nodes[v].w / 2,
                 lay.center[v].y + g.nodes[v].h / 2);
    for (int gi = (int)g.groups.size() - 1; gi >= 0; gi--) {
        if (!frames[gi].used) continue;
        Frame& f = frames[gi];
        f = {f.x0 - kFramePad, f.y0 - kFramePad - kHeader, f.x1 + kFramePad, f.y1 + kFramePad, true};
        int p = g.groups[gi].parent;
        if (p >= 0) grow(frames[p], f.x0, f.y0, f.x1, f.y1);
    }
    // the drawing's box: the layout plus the frames, with a margin
    double x0 = 0, y0 = 0, x1 = lay.width, y1 = lay.height;
    for (const Frame& f : frames)
        if (f.used) {
            x0 = std::min(x0, f.x0);
            y0 = std::min(y0, f.y0);
            x1 = std::max(x1, f.x1);
            y1 = std::max(y1, f.y1);
        }
    const double ox = kMargin - x0, oy = kMargin - y0;
    auto at = [&](Pt p) { return Pt{p.x + ox, p.y + oy}; };

    std::string body;
    for (size_t gi = 0; gi < g.groups.size(); gi++) {
        const Frame& f = frames[gi];
        if (!f.used) continue;
        body += "<g class=\"subgraph\">\n<rect x=\"" + num(f.x0 + ox) + "\" y=\"" + num(f.y0 + oy) + "\" width=\"" + num(f.x1 - f.x0) + "\" height=\"" + num(f.y1 - f.y0) +
                "\" rx=\"2\" ry=\"2\" fill=\"" + th.group_fill + "\" stroke=\"" + th.border + "\" stroke-width=\"1\"/>\n";
        body += "<rect x=\"" + num(f.x0 + ox) + "\" y=\"" + num(f.y0 + oy) + "\" width=\"" + num(f.x1 - f.x0) + "\" height=\"" + num(kHeader) +
                "\" rx=\"2\" ry=\"2\" fill=\"" + th.group_hdr + "\" stroke=\"" + th.border + "\" stroke-width=\"1\"/>\n";
        body += "<text x=\"" + num(f.x0 + ox + 12) + "\" y=\"" + num(f.y0 + oy + kHeader / 2 + kGroupFont * 0.35) + "\" font-size=\"" + num(kGroupFont) +
                "\" font-weight=\"" + std::to_string(kGroupWeight) + "\" font-family=\"" + esc(th.font) + "\" fill=\"" + th.text + "\">" + esc(g.groups[gi].title) + "</text>\n</g>\n";
    }
    // edges
    for (size_t i = 0; i < g.edges.size(); i++) {
        const Edge& e = g.edges[i];
        if (e.kind == Kind::Invisible || lay.points[i].size() < 2) continue;
        std::string color = th.line, width = e.kind == Kind::Thick ? "2.5" : num(kStroke), dash = e.kind == Kind::Dotted ? "4 4" : "";
        for (int key : {-1, (int)i}) {
            auto it = g.link_styles.find(key);
            if (it == g.link_styles.end()) continue;
            if (!it->second.stroke.empty()) color = it->second.stroke;
            if (!it->second.stroke_width.empty()) width = px(it->second.stroke_width);
        }
        std::vector<Pt> pts;
        for (const Pt& p : lay.points[i]) pts.push_back(at(p));
        std::string list;
        for (const Pt& p : pts) list += (list.empty() ? "" : " ") + num(p.x) + "," + num(p.y);
        body += "<polyline class=\"edge\" points=\"" + list + "\" fill=\"none\"" + stroke_attrs(color, width, dash) + " stroke-linejoin=\"round\"/>\n";
        if (e.arrow_end) draw_arrow(body, pts.back(), pts[pts.size() - 2], color == th.line ? th.arrow : color);
        if (e.arrow_start) draw_arrow(body, pts.front(), pts[1], color == th.line ? th.arrow : color);
    }
    for (size_t i = 0; i < g.edges.size(); i++) {
        if (label_lines[i].empty() || g.edges[i].kind == Kind::Invisible) continue;
        Pt c = at(lay.label_center[i]);
        body += "<g class=\"edge-label\">\n<rect x=\"" + num(c.x - le[i].label_w / 2) + "\" y=\"" + num(c.y - le[i].label_h / 2) + "\" width=\"" + num(le[i].label_w) +
                "\" height=\"" + num(le[i].label_h) + "\" rx=\"2\" ry=\"2\" fill=\"" + th.bg + "\" stroke=\"" + th.inner_stroke + "\" stroke-width=\"1\"/>\n";
        centered_text(body, th, label_lines[i], c.x, c.y, kEdgeFont, kEdgeWeight, th.text_sec);
        body += "</g>\n";
    }
    for (size_t i = 0; i < g.nodes.size(); i++) draw_node(body, th, g.nodes[i], at(lay.center[i]));

    svg = svg_document(th, (x1 - x0) + 2 * kMargin, (y1 - y0) + 2 * kMargin, body);
    return true;
}

}  // namespace mermaid
