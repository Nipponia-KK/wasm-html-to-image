#include "mermaid/usecase.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>

#include "mermaid/frame_clear.h"
#include "mermaid/layered.h"
#include "mermaid/mermaid_internal.h"

namespace mermaid {
namespace {

using litehtml::svgutil::trim;

enum class Kind { Actor, Ellipse, Rect, Note, Json };

struct Style {
    std::string fill, stroke, color, stroke_width;
};

struct Node {
    std::string id, label, stereotype;
    Kind kind = Kind::Ellipse;
    std::string actor_type = "normal";  // normal, hollow, awesome
    bool business = false;
    int boundary = -1;
    Style style;
    std::vector<std::string> classes;
    std::vector<std::pair<std::string, std::string>> rows;  // Json
    std::vector<std::string> lines;
    double w = 0, h = 0;
    int note_for = -1;
};

enum class Op { Point, RevPoint, Line, Circle, RevCircle, Cross, RevCross, Include, Extend, Generalize };

struct Edge {
    int from = 0, to = 0;
    Op op = Op::Point;
    std::string label, id;
};

struct Boundary {
    std::string id, title, type = "rect";
    Style style;
    std::vector<std::string> classes;
};

bool starts(const std::string& s, const char* p) { return s.compare(0, std::char_traits<char>::length(p), p) == 0; }

bool id_char(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

/// `"text"`, `` "`text`" `` or unquoted text into its content.
std::string label_text(std::string s) {
    s = trim(s);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        s = s.substr(1, s.size() - 2);
        if (s.size() >= 2 && s.front() == '`' && s.back() == '`') s = s.substr(1, s.size() - 2);
    }
    // entity codes for the reserved characters
    for (auto& [code, ch] : std::vector<std::pair<std::string, std::string>>{{"#quot;", "\""}, {"#39;", "'"}, {"#40;", "("}, {"#41;", ")"}, {"#91;", "["}, {"#93;", "]"}, {"#96;", "`"}}) {
        size_t at = 0;
        while ((at = s.find(code, at)) != std::string::npos) {
            s.replace(at, code.size(), ch);
            at += ch.size();
        }
    }
    return s;
}

/// The id made of a quoted label (`"Reset password"` is `Reset_password`).
std::string id_of(const std::string& label) {
    std::string id;
    for (char c : label) id += id_char(c) ? c : '_';
    return id;
}

/// `css` of `classDef` / `style`: `fill:#f9f,stroke:#333,stroke-width:4px`.
Style parse_style(const std::string& css) {
    Style st;
    size_t at = 0;
    while (at <= css.size()) {
        size_t comma = css.find(',', at);
        while (comma != std::string::npos && comma > 0 && css[comma - 1] == '\\') comma = css.find(',', comma + 1);
        std::string part = trim(css.substr(at, comma == std::string::npos ? std::string::npos : comma - at));
        size_t colon = part.find(':');
        if (colon != std::string::npos) {
            std::string key = trim(part.substr(0, colon)), val = trim(part.substr(colon + 1));
            if (key == "fill") st.fill = val;
            else if (key == "stroke") st.stroke = val;
            else if (key == "color") st.color = val;
            else if (key == "stroke-width") st.stroke_width = val;
        }
        if (comma == std::string::npos) break;
        at = comma + 1;
    }
    return st;
}

std::string px(const std::string& v) { return v.size() > 2 && v.compare(v.size() - 2, 2, "px") == 0 ? v.substr(0, v.size() - 2) : v; }

void apply(Style& dst, const Style& src) {
    if (!src.fill.empty()) dst.fill = src.fill;
    if (!src.stroke.empty()) dst.stroke = src.stroke;
    if (!src.color.empty()) dst.color = src.color;
    if (!src.stroke_width.empty()) dst.stroke_width = src.stroke_width;
}

// ------------------------------------------------------------------------------------------ JSON tables

struct JsonReader {
    const std::string& s;
    size_t at = 0;
    std::vector<std::pair<std::string, std::string>>& rows;
    bool ok = true;

    void ws() {
        while (at < s.size() && std::isspace((unsigned char)s[at])) at++;
    }
    std::string string_value() {
        std::string out;
        at++;  // the opening quote
        while (at < s.size() && s[at] != '"') {
            if (s[at] == '\\' && at + 1 < s.size()) at++;
            out += s[at++];
        }
        at++;
        return out;
    }
    void value(const std::string& path) {
        ws();
        if (at >= s.size()) {
            ok = false;
            return;
        }
        if (s[at] == '{') {
            at++;
            ws();
            if (s[at] == '}') {
                rows.push_back({path, ""});
                at++;
                return;
            }
            while (ok && at < s.size()) {
                ws();
                if (s[at] != '"') {
                    ok = false;
                    return;
                }
                std::string key = string_value();
                ws();
                if (at >= s.size() || s[at] != ':') {
                    ok = false;
                    return;
                }
                at++;
                value(path.empty() ? key : path + "." + key);
                ws();
                if (at < s.size() && s[at] == ',') {
                    at++;
                    continue;
                }
                if (at < s.size() && s[at] == '}') {
                    at++;
                    return;
                }
                ok = false;
            }
        } else if (s[at] == '[') {
            at++;
            ws();
            if (s[at] == ']') {
                rows.push_back({path, ""});
                at++;
                return;
            }
            int index = 0;
            while (ok && at < s.size()) {
                value(path + "[" + std::to_string(index++) + "]");
                ws();
                if (at < s.size() && s[at] == ',') {
                    at++;
                    continue;
                }
                if (at < s.size() && s[at] == ']') {
                    at++;
                    return;
                }
                ok = false;
            }
        } else if (s[at] == '"') {
            rows.push_back({path, string_value()});
        } else {
            size_t end = at;
            while (end < s.size() && s[end] != ',' && s[end] != '}' && s[end] != ']' && !std::isspace((unsigned char)s[end])) end++;
            if (end == at) {
                ok = false;
                return;
            }
            rows.push_back({path, s.substr(at, end - at)});
            at = end;
        }
    }
};

// ------------------------------------------------------------------------------------------ parsing

struct Model {
    Direction dir = Direction::TD;
    std::vector<Node> nodes;
    std::map<std::string, int> index;
    std::vector<Edge> edges;
    std::vector<Boundary> boundaries;
    std::map<std::string, Style> class_defs;
    std::map<std::string, Style> styles;  // by id, for nodes, boundaries and edges
    std::string error;
    int open_boundary = -1;

    int node(const std::string& id) {
        auto it = index.find(id);
        if (it != index.end()) return it->second;
        Node n;
        n.id = id;
        n.label = id;
        index[id] = (int)nodes.size();
        nodes.push_back(std::move(n));
        return (int)nodes.size() - 1;
    }
};

/// Pulls `@{ ... }`, `<<stereotype>>` and `:::class` off the end of a declaration; returns the head.
std::string take_suffixes(std::string s, std::string& meta, std::string& stereotype, std::vector<std::string>& classes) {
    s = trim(s);
    while (true) {
        size_t cls = s.rfind(":::");
        if (cls != std::string::npos && s.find_first_of(" \t", cls) == std::string::npos) {
            std::string list = s.substr(cls + 3);
            size_t at = 0;
            while (at <= list.size()) {
                size_t comma = list.find(',', at);
                std::string c = trim(list.substr(at, comma == std::string::npos ? std::string::npos : comma - at));
                if (!c.empty()) classes.push_back(c);
                if (comma == std::string::npos) break;
                at = comma + 1;
            }
            s = trim(s.substr(0, cls));
            continue;
        }
        if (s.size() > 4 && s.compare(s.size() - 2, 2, ">>") == 0) {
            size_t open = s.rfind("<<");
            if (open != std::string::npos) {
                stereotype = trim(s.substr(open + 2, s.size() - open - 4));
                s = trim(s.substr(0, open));
                continue;
            }
        }
        if (!s.empty() && s.back() == '}') {
            size_t open = s.rfind("@{");
            if (open != std::string::npos) {
                meta = s.substr(open + 2, s.size() - open - 3);
                s = trim(s.substr(0, open));
                continue;
            }
        }
        break;
    }
    return s;
}

/// `key: value` pairs of a metadata body.
std::map<std::string, std::string> meta_pairs(const std::string& body) {
    std::map<std::string, std::string> out;
    size_t at = 0;
    while (at <= body.size()) {
        size_t comma = body.find(',', at);
        std::string part = trim(body.substr(at, comma == std::string::npos ? std::string::npos : comma - at));
        size_t colon = part.find(':');
        if (colon != std::string::npos) {
            std::string v = trim(part.substr(colon + 1));
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
            out[trim(part.substr(0, colon))] = v;
        }
        if (comma == std::string::npos) break;
        at = comma + 1;
    }
    return out;
}

/// `ID`, `ID("label")`, `ID[label]` or `"Quoted"` into id, label and shape.
bool take_head(const std::string& head, std::string& id, std::string& label, Kind& kind, bool& has_label) {
    std::string h = trim(head);
    has_label = false;
    if (h.empty()) return false;
    if (h[0] == '"') {
        size_t close = h.find('"', 1);
        if (close == std::string::npos || close + 1 != h.size()) return false;
        label = label_text(h);
        id = id_of(label);
        has_label = true;
        return !id.empty();
    }
    size_t k = 0;
    while (k < h.size() && id_char(h[k])) k++;
    if (k == 0) return false;
    id = h.substr(0, k);
    label = id;
    if (k == h.size()) return true;
    char open = h[k], close = open == '(' ? ')' : open == '[' ? ']' : 0;
    if (!close || h.back() != close) return false;
    label = label_text(h.substr(k + 1, h.size() - k - 2));
    kind = open == '[' ? Kind::Rect : Kind::Ellipse;
    has_label = true;
    return true;
}

// `A-->B` is `A --> B`: put spaces around a run of dashes (`--`, `-->`, `--|>`) or `..>` that touches a name
std::string space_operators(const std::string& line) {
    bool quote = false;
    std::string spaced;
    for (size_t k = 0; k < line.size(); k++) {
        char c = line[k];
        if (c == '"') quote = !quote;
        bool run = !quote && ((c == '-' && k + 1 < line.size() && line[k + 1] == '-' && (k == 0 || line[k - 1] != '-')) ||
                              (c == '.' && line.compare(k, 3, "..>") == 0));
        if (run && (k == 0 || line[k - 1] != '@')) {
            size_t e = k;
            if (c == '.') e += 3;
            else {
                while (e < line.size() && line[e] == '-') e++;
                if (line.compare(e, 2, "|>") == 0) e += 2;
                else if (e < line.size() && line[e] == '>') e++;
            }
            if (!spaced.empty() && spaced.back() != ' ') spaced += ' ';
            spaced += line.substr(k, e - k);
            if (e < line.size() && line[e] != ' ') spaced += ' ';
            k = e - 1;
            continue;
        }
        spaced += c;
    }
    return spaced;
}

bool parse_relationship_words(Model& m, const std::string& line) {
    // split into words, keeping quoted text whole
    std::vector<std::string> w;
    std::string cur;
    bool quote = false;
    quote = false;
    for (char c : line) {
        if (c == '"') quote = !quote;
        if (std::isspace((unsigned char)c) && !quote) {
            if (!cur.empty()) w.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) w.push_back(cur);
    auto is_op = [](std::string t, std::string& id, Op& op) {
        size_t at = t.find('@');
        id.clear();
        if (at != std::string::npos && at > 0 && at + 1 < t.size()) {
            id = t.substr(0, at);
            t = t.substr(at + 1);
        }
        if (t == "..>") return op = Op::Include, true;
        auto dashes = [](const std::string& x, size_t from, size_t to) {
            for (size_t i = from; i < to; i++)
                if (x[i] != '-') return false;
            return to > from;
        };
        size_t n = t.size();
        if (n >= 3 && t[n - 1] == '>' && dashes(t, 0, n - 1) && n - 1 >= 2) return op = Op::Point, true;
        if (n >= 3 && t.compare(0, 2, "<-") == 0 && dashes(t, 1, n)) return op = Op::RevPoint, true;
        if (n >= 4 && t.compare(n - 2, 2, "|>") == 0 && dashes(t, 0, n - 2)) return op = Op::Generalize, true;
        if (n >= 3 && t.back() == 'o' && dashes(t, 0, n - 1)) return op = Op::Circle, true;
        if (n >= 3 && t.front() == 'o' && dashes(t, 1, n)) return op = Op::RevCircle, true;
        if (n >= 3 && t.back() == 'x' && dashes(t, 0, n - 1)) return op = Op::Cross, true;
        if (n >= 3 && t.front() == 'x' && dashes(t, 1, n)) return op = Op::RevCross, true;
        if (dashes(t, 0, n) && n >= 2) return op = Op::Line, true;  // `--`, `---`
        return false;
    };
    Edge e;
    std::string left, right, label;
    for (size_t i = 1; i < w.size() && left.empty(); i++) {
        std::string id;
        Op op;
        if (!is_op(w[i], id, op)) continue;
        std::string id2;
        Op op2;
        if (op == Op::Line && i + 4 == w.size() && w[i + 1][0] == '"' && is_op(w[i + 2], id2, op2) && op2 != Op::Line && op2 != Op::Include) {  // A -- "label" --> B
            label = label_text(w[i + 1]);
            e.id = id2.empty() ? id : id2;
            e.op = op2;
            left = w[i - 1];
            right = w[i + 3];
        } else if (op == Op::Include && i + 4 == w.size() && w[i + 1] == ":" && (w[i + 2] == "include" || w[i + 2] == "extend")) {  // A ..> : include B
            e.op = w[i + 2] == "include" ? Op::Include : Op::Extend;
            e.id = id;
            left = w[i - 1];
            right = w[i + 3];
        } else if (op != Op::Include && i + 2 == w.size()) {  // A --> B
            e.op = op;
            e.id = id;
            left = w[i - 1];
            right = w[i + 1];
        }
        break;
    }
    if (left.empty() || right.empty()) return false;
    auto endpoint = [&](const std::string& s) {
        std::string id = s[0] == '"' ? id_of(label_text(s)) : s;
        int n = m.node(id);
        if (s[0] == '"' && m.nodes[n].label == m.nodes[n].id) m.nodes[n].label = label_text(s);
        return n;
    };
    e.from = endpoint(left);
    e.to = endpoint(right);
    e.label = label;
    m.edges.push_back(e);
    return true;
}

bool parse_relationship(Model& m, const std::string& line) {
    if (parse_relationship_words(m, line)) return true;
    std::string spaced = space_operators(line);
    return spaced != line && parse_relationship_words(m, spaced);
}

bool parse(const std::string& source, Model& m, std::string& error) {
    std::vector<std::string> lines;
    size_t i = 0;
    while (i <= source.size()) {
        size_t e = source.find('\n', i);
        lines.push_back(source.substr(i, e == std::string::npos ? std::string::npos : e - i));
        i = e == std::string::npos ? source.size() + 1 : e + 1;
    }
    bool first = true;
    std::vector<std::pair<std::string, std::string>> classes_to_apply;  // id list, class list
    std::vector<std::pair<std::string, std::string>> styles_to_apply;
    for (size_t li = 0; li < lines.size(); li++) {
        std::string line = trim(lines[li]);
        if (line.empty() || starts(line, "%%")) continue;
        if (first) {
            first = false;
            continue;
        }
        auto fail = [&](const std::string& why) {
            error = why + ": " + line;
            return false;
        };
        if (starts(line, "direction ")) {
            std::string d = trim(line.substr(10));
            if (d == "LR") m.dir = Direction::LR;
            else if (d == "RL") m.dir = Direction::RL;
            else if (d == "BT") m.dir = Direction::BT;
            else if (d == "TD" || d == "TB") m.dir = Direction::TD;
            else return fail("unknown direction");
        } else if (starts(line, "accTitle")) {
            continue;
        } else if (starts(line, "accDescr")) {
            if (line.find('{') != std::string::npos && line.find('}') == std::string::npos)
                while (++li < lines.size() && trim(lines[li]) != "}") {
                }
        } else if (starts(line, "systemBoundary ")) {
            if (m.open_boundary >= 0) return fail("a system boundary cannot be nested");
            std::string meta, ster;
            std::vector<std::string> cls;
            std::string head = take_suffixes(line.substr(15), meta, ster, cls);
            Boundary b;
            if (!head.empty() && head[0] == '"') {
                b.title = label_text(head);
                b.id = id_of(b.title);
            } else {
                size_t k = 0;
                while (k < head.size() && id_char(head[k])) k++;
                if (k == 0) return fail("a system boundary needs a name");
                b.id = head.substr(0, k);
                std::string rest = trim(head.substr(k));
                b.title = b.id;
                if (!rest.empty()) {
                    if ((rest.front() == '[' && rest.back() == ']') || (rest.front() == '(' && rest.back() == ')')) b.title = label_text(rest.substr(1, rest.size() - 2));
                    else return fail("a system boundary is `systemBoundary id[\"Title\"]`");
                }
            }
            auto pairs = meta_pairs(meta);
            if (pairs.count("type")) {
                if (pairs["type"] != "rect" && pairs["type"] != "package") return fail("a boundary type is rect or package");
                b.type = pairs["type"];
            }
            b.classes = cls;
            m.boundaries.push_back(b);
            m.open_boundary = (int)m.boundaries.size() - 1;
        } else if (line == "end") {
            if (m.open_boundary < 0) return fail("`end` without a system boundary");
            m.open_boundary = -1;
        } else if (starts(line, "actor ")) {
            std::string meta, ster;
            std::vector<std::string> cls;
            std::string head = take_suffixes(line.substr(6), meta, ster, cls);
            std::string id, label;
            Kind kind = Kind::Actor;
            bool has_label = false;
            if (!take_head(head, id, label, kind, has_label)) return fail("an actor is `actor Name` or `actor Name(\"Label\")`");
            int n = m.node(id);
            Node& node = m.nodes[n];
            node.kind = Kind::Actor;
            if (has_label) node.label = label;
            node.stereotype = ster;
            node.classes.insert(node.classes.end(), cls.begin(), cls.end());
            auto pairs = meta_pairs(meta);
            for (auto& [k, v] : pairs) {
                if (k == "type") {
                    if (v != "normal" && v != "hollow" && v != "awesome") return fail("an actor type is normal, hollow or awesome");
                    node.actor_type = v;
                } else if (k == "business") node.business = v == "true";
                else if (k != "icon") return fail("unknown actor key `" + k + "`");
            }
            if (m.open_boundary >= 0) node.boundary = m.open_boundary;
        } else if (starts(line, "note for ")) {
            std::string t = trim(line.substr(9));
            size_t k = 0;
            while (k < t.size() && id_char(t[k])) k++;
            std::string target = t.substr(0, k), text = trim(t.substr(k));
            if (target.empty() || text.empty() || text[0] != '"') return fail("a note is `note for Target \"text\"`");
            Node note;
            note.id = "note_" + std::to_string(m.nodes.size());
            note.kind = Kind::Note;
            note.label = label_text(text);
            note.note_for = m.node(target);
            m.index[note.id] = (int)m.nodes.size();
            m.nodes.push_back(note);
        } else if (starts(line, "json ")) {
            std::string meta, ster;
            std::vector<std::string> cls;
            std::string text = line.substr(5);
            size_t brace = text.find("@{");
            if (brace == std::string::npos) return fail("a JSON table is `json Name@{ ... }`");
            std::string name = trim(text.substr(0, brace));
            std::string body = text.substr(brace + 2);
            // the object may run over several lines: until its braces balance
            auto balanced = [](const std::string& b, size_t& end) {
                int depth = 1;
                bool q = false;
                for (size_t k = 0; k < b.size(); k++) {
                    if (b[k] == '"' && (k == 0 || b[k - 1] != '\\')) q = !q;
                    if (q) continue;
                    if (b[k] == '{') depth++;
                    else if (b[k] == '}' && --depth == 0) {
                        end = k;
                        return true;
                    }
                }
                return false;
            };
            size_t end = 0;
            while (!balanced(body, end)) {
                if (++li >= lines.size()) return fail("a JSON table is not closed");
                body += "\n" + lines[li];
            }
            std::string json = "{" + body.substr(0, end) + "}";
            std::string tail = trim(body.substr(end + 1));
            if (starts(tail, ":::")) cls.push_back(trim(tail.substr(3)));
            Node n;
            n.id = name;
            n.label = name;
            n.kind = Kind::Json;
            JsonReader jr{json, 0, n.rows};
            jr.value("");
            if (!jr.ok) return fail("the JSON is not valid");
            n.classes = cls;
            m.index[n.id] = (int)m.nodes.size();
            m.nodes.push_back(n);
        } else if (starts(line, "classDef ")) {
            std::string t = trim(line.substr(9));
            size_t sp = t.find_first_of(" \t");
            if (sp == std::string::npos) return fail("a classDef is `classDef names css`");
            Style st = parse_style(trim(t.substr(sp)));
            std::string names = t.substr(0, sp);
            size_t at = 0;
            while (at <= names.size()) {
                size_t comma = names.find(',', at);
                m.class_defs[names.substr(at, comma == std::string::npos ? std::string::npos : comma - at)] = st;
                if (comma == std::string::npos) break;
                at = comma + 1;
            }
        } else if (starts(line, "class ")) {
            std::string t = trim(line.substr(6));
            size_t sp = t.find_first_of(" \t");
            if (sp == std::string::npos) return fail("a class assignment is `class ids names`");
            classes_to_apply.push_back({t.substr(0, sp), trim(t.substr(sp))});
        } else if (starts(line, "style ")) {
            std::string t = trim(line.substr(6));
            size_t sp = t.find_first_of(" \t");
            if (sp == std::string::npos) return fail("a style is `style id css`");
            styles_to_apply.push_back({t.substr(0, sp), trim(t.substr(sp))});
        } else if (parse_relationship(m, line)) {
            continue;
        } else {
            // a use case: ID, ID("label"), ID[label], "Quoted", with metadata, a stereotype and classes
            std::string meta, ster;
            std::vector<std::string> cls;
            std::string head = take_suffixes(line, meta, ster, cls);
            // `id@{ ... }` alone is metadata of an edge or a boundary: nothing to draw
            if (head.empty()) continue;
            std::string id, label;
            Kind kind = Kind::Ellipse;
            bool has_label = false;
            if (!take_head(head, id, label, kind, has_label)) return fail("unrecognised line");
            int n = m.node(id);
            Node& node = m.nodes[n];
            if (node.kind != Kind::Actor) node.kind = kind;
            if (has_label) node.label = label;
            node.stereotype = ster;
            node.classes.insert(node.classes.end(), cls.begin(), cls.end());
            for (auto& [k, v] : meta_pairs(meta))
                if (k == "business") node.business = v == "true";
            if (m.open_boundary >= 0) node.boundary = m.open_boundary;
        }
    }
    if (m.open_boundary >= 0) {
        error = "a system boundary is not closed with `end`";
        return false;
    }
    for (auto& [ids, names] : classes_to_apply) {
        std::vector<std::string> nlist;
        size_t at = 0;
        while (at <= names.size()) {
            size_t comma = names.find(',', at);
            nlist.push_back(trim(names.substr(at, comma == std::string::npos ? std::string::npos : comma - at)));
            if (comma == std::string::npos) break;
            at = comma + 1;
        }
        at = 0;
        while (at <= ids.size()) {
            size_t comma = ids.find(',', at);
            std::string id = trim(ids.substr(at, comma == std::string::npos ? std::string::npos : comma - at));
            auto it = m.index.find(id);
            if (it != m.index.end()) m.nodes[it->second].classes.insert(m.nodes[it->second].classes.end(), nlist.begin(), nlist.end());
            for (auto& b : m.boundaries)
                if (b.id == id) b.classes.insert(b.classes.end(), nlist.begin(), nlist.end());
            if (comma == std::string::npos) break;
            at = comma + 1;
        }
    }
    for (auto& [id, css] : styles_to_apply) m.styles[id] = parse_style(css);
    return true;
}

// ------------------------------------------------------------------------------------------ drawing

std::string stroke_attrs(const std::string& color, const std::string& width) { return " stroke=\"" + color + "\" stroke-width=\"" + width + "\""; }

void draw_actor(std::string& out, const Theme& th, const Node& n, Pt c, const Style& st) {
    using namespace metrics;
    std::string stroke = st.stroke.empty() ? th.border : st.stroke, sw = st.stroke_width.empty() ? "1.5" : px(st.stroke_width);
    double top = c.y - n.h / 2 + 4, cx = c.x;
    std::string fill = n.actor_type == "hollow" ? "none" : (st.fill.empty() ? th.bg : st.fill);
    out += "<g class=\"actor\">\n";
    out += "<circle cx=\"" + num(cx) + "\" cy=\"" + num(top + 7) + "\" r=\"7\" fill=\"" + fill + "\"" + stroke_attrs(stroke, sw) + "/>\n";
    out += "<path d=\"M" + num(cx) + " " + num(top + 14) + " L" + num(cx) + " " + num(top + 34) + " M" + num(cx - 12) + " " + num(top + 22) + " L" + num(cx + 12) + " " + num(top + 22) + " M" + num(cx) + " " + num(top + 34) + " L" + num(cx - 10) +
           " " + num(top + 48) + " M" + num(cx) + " " + num(top + 34) + " L" + num(cx + 10) + " " + num(top + 48) + "\" fill=\"none\"" + stroke_attrs(stroke, sw) + " stroke-linecap=\"round\"/>\n";
    if (n.actor_type == "awesome") out += "<circle cx=\"" + num(cx) + "\" cy=\"" + num(top + 7) + "\" r=\"3\" fill=\"" + stroke + "\"/>\n";
    if (n.business) out += "<path d=\"M" + num(cx - 4) + " " + num(top + 5) + " L" + num(cx + 4) + " " + num(top + 9) + "\" fill=\"none\"" + stroke_attrs(stroke, "1") + "/>\n";
    std::vector<std::string> lines = n.lines;
    centered_text(out, th, lines, c.x, c.y + n.h / 2 - 10 - (lines.size() - 1) * kNodeFont * kLineHeight / 2, kNodeFont, 500, st.color.empty() ? th.label : st.color);
    out += "</g>\n";
}

}  // namespace

bool render_usecase(const std::string& source, const Options& options, std::string& svg, std::string& error) {
    using namespace metrics;
    Model m;
    if (!parse(source, m, error)) return false;
    if (m.nodes.empty()) {
        error = "empty diagram";
        return false;
    }
    Theme th = make_theme(options);
    Text text{options};

    // the style of each element: the default class, its named classes in order, then its own `style`
    auto style_of = [&](const std::vector<std::string>& classes, const std::string& id) {
        Style st;
        auto d = m.class_defs.find("default");
        if (d != m.class_defs.end()) apply(st, d->second);
        for (auto& c : classes) {
            auto it = m.class_defs.find(c);
            if (it != m.class_defs.end()) apply(st, it->second);
        }
        auto own = m.styles.find(id);
        if (own != m.styles.end()) apply(st, own->second);
        return st;
    };

    // sizes
    for (auto& n : m.nodes) {
        n.style = style_of(n.classes, n.id);
        std::vector<std::string> lines = split_lines(n.label);
        if (!n.stereotype.empty() && n.kind != Kind::Actor) lines.insert(lines.begin(), "\xC2\xAB" + n.stereotype + "\xC2\xBB");
        else if (!n.stereotype.empty()) lines.push_back("\xC2\xAB" + n.stereotype + "\xC2\xBB");
        n.lines = lines;
        double tw = text.block_width(lines, kNodeFont, 500), th_ = lines.size() * kNodeFont * kLineHeight;
        switch (n.kind) {
            case Kind::Actor:
                n.w = std::max(tw + 16, 56.0);
                n.h = 58 + th_ + 8;
                break;
            case Kind::Ellipse:
                n.w = tw + 56;
                n.h = std::max(46.0, th_ + 28);
                break;
            case Kind::Rect:
                n.w = tw + 36;
                n.h = std::max(40.0, th_ + 22);
                break;
            case Kind::Note:
                n.w = tw + 28;
                n.h = th_ + 22;
                break;
            case Kind::Json: {
                double kw = 0, vw = 0;
                for (auto& [k, v] : n.rows) {
                    kw = std::max(kw, text.width(k, 12, 500));
                    vw = std::max(vw, text.width(v, 12, 400));
                }
                n.w = std::max(text.width(n.label, 13, 700) + 28, kw + vw + 40);
                n.h = 30 + 22.0 * std::max<size_t>(1, n.rows.size());
                break;
            }
        }
    }
    std::vector<LayoutNode> ln(m.nodes.size());
    for (size_t i = 0; i < m.nodes.size(); i++) {
        ln[i].w = m.nodes[i].w;
        ln[i].h = m.nodes[i].h;
        ln[i].group = m.nodes[i].boundary;
        ln[i].outline = m.nodes[i].kind == Kind::Ellipse ? 2 : 0;
    }
    std::vector<LayoutEdge> le;
    std::vector<int> edge_of;  // layout edge -> model edge (notes come first)
    std::vector<std::vector<std::string>> label_lines;
    std::vector<std::pair<int, int>> note_links;
    for (size_t i = 0; i < m.nodes.size(); i++)
        if (m.nodes[i].kind == Kind::Note) {
            LayoutEdge e;
            e.from = m.nodes[i].note_for;
            e.to = (int)i;
            le.push_back(e);
            edge_of.push_back(-1);
            label_lines.push_back({});
        }
    for (size_t i = 0; i < m.edges.size(); i++) {
        const Edge& e = m.edges[i];
        LayoutEdge l;
        l.from = e.from;
        l.to = e.to;
        std::string label = e.label;
        if (e.op == Op::Include) label = std::string("\xC2\xAB") + "include" + "\xC2\xBB";
        if (e.op == Op::Extend) label = std::string("\xC2\xAB") + "extend" + "\xC2\xBB";
        std::vector<std::string> lines;
        if (!label.empty()) {
            lines = split_lines(label);
            l.label_w = text.block_width(lines, kEdgeFont, kEdgeWeight) + 16;
            l.label_h = lines.size() * kEdgeFont * kLineHeight + 12;
        }
        le.push_back(l);
        edge_of.push_back((int)i);
        label_lines.push_back(lines);
    }
    std::vector<int> group_parent(m.boundaries.size(), -1);
    LayoutResult lay = layout_layered(ln, le, m.dir, group_parent);

    // boundary frames
    struct Frame {
        double x0, y0, x1, y1;
        bool used = false;
    };
    std::vector<Frame> frames(m.boundaries.size());
    constexpr double kPad = 18, kHeader = 28;
    for (size_t i = 0; i < m.nodes.size(); i++) {
        int b = m.nodes[i].boundary;
        if (b < 0) continue;
        double x0 = lay.center[i].x - m.nodes[i].w / 2, y0 = lay.center[i].y - m.nodes[i].h / 2, x1 = lay.center[i].x + m.nodes[i].w / 2, y1 = lay.center[i].y + m.nodes[i].h / 2;
        Frame& f = frames[b];
        f = f.used ? Frame{std::min(f.x0, x0), std::min(f.y0, y0), std::max(f.x1, x1), std::max(f.y1, y1), true} : Frame{x0, y0, x1, y1, true};
    }
    for (auto& f : frames)
        if (f.used) f = {f.x0 - kPad, f.y0 - kPad - kHeader, f.x1 + kPad, f.y1 + kPad, true};
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
    for (size_t b = 0; b < m.boundaries.size(); b++) {
        const Frame& f = frames[b];
        if (!f.used) continue;
        Style st = style_of(m.boundaries[b].classes, m.boundaries[b].id);
        std::string fill = st.fill.empty() ? th.group_fill : st.fill, stroke = st.stroke.empty() ? th.border : st.stroke, sw = st.stroke_width.empty() ? "1" : px(st.stroke_width);
        body += "<g class=\"boundary\">\n<rect x=\"" + num(f.x0 + ox) + "\" y=\"" + num(f.y0 + oy) + "\" width=\"" + num(f.x1 - f.x0) + "\" height=\"" + num(f.y1 - f.y0) + "\" rx=\"2\" fill=\"" + fill + "\"" + stroke_attrs(stroke, sw) + "/>\n";
        double tw = text.width(m.boundaries[b].title, kGroupFont, kGroupWeight) + 24;
        if (m.boundaries[b].type == "package") {
            body += "<rect x=\"" + num(f.x0 + ox) + "\" y=\"" + num(f.y0 + oy) + "\" width=\"" + num(tw) + "\" height=\"" + num(kHeader) + "\" fill=\"" + (st.fill.empty() ? th.cluster_hdr : st.fill) + "\"" + stroke_attrs(stroke, sw) + "/>\n";
            aligned_text(body, th, {m.boundaries[b].title}, f.x0 + ox + 12, f.y0 + oy + kHeader / 2, kGroupFont, kGroupWeight, st.color.empty() ? th.label : st.color, "start");
        } else {
            centered_text(body, th, {m.boundaries[b].title}, (f.x0 + f.x1) / 2 + ox, f.y0 + oy + kHeader / 2, kGroupFont, kGroupWeight, st.color.empty() ? th.label : st.color);
        }
        body += "</g>\n";
    }
    {
        std::vector<LayoutEdge> frame_edges = le;
        std::vector<Frame> fcopy = frames;
        clear_frame_borders(lay.points, lay.label_center, frame_edges, fcopy);
    }

    // edges
    auto arrow_head = [&](Pt tip, Pt from, const std::string& color, bool hollow) {
        double dx = tip.x - from.x, dy = tip.y - from.y, len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-6) return;
        dx /= len;
        dy /= len;
        double aw = hollow ? 12 : kArrowW, ah = hollow ? 11 : kArrowH;
        Pt base{tip.x - dx * aw, tip.y - dy * aw};
        double nx = -dy * ah / 2, ny = dx * ah / 2;
        body += "<polygon points=\"" + num(tip.x) + "," + num(tip.y) + " " + num(base.x + nx) + "," + num(base.y + ny) + " " + num(base.x - nx) + "," + num(base.y - ny) + "\" fill=\"" + (hollow ? th.bg : color) + "\" stroke=\"" + color +
                "\" stroke-width=\"1\" stroke-linejoin=\"round\"/>\n";
    };
    auto end_mark = [&](Pt tip, Pt from, bool circle, const std::string& color) {
        double dx = tip.x - from.x, dy = tip.y - from.y, len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-6) return;
        dx /= len;
        dy /= len;
        if (circle) {
            body += "<circle cx=\"" + num(tip.x - dx * 5) + "\" cy=\"" + num(tip.y - dy * 5) + "\" r=\"5\" fill=\"" + th.bg + "\" stroke=\"" + color + "\" stroke-width=\"1.2\"/>\n";
        } else {
            Pt c{tip.x - dx * 6, tip.y - dy * 6};
            body += "<path d=\"M" + num(c.x - 4) + " " + num(c.y - 4) + " L" + num(c.x + 4) + " " + num(c.y + 4) + " M" + num(c.x + 4) + " " + num(c.y - 4) + " L" + num(c.x - 4) + " " + num(c.y + 4) + "\" fill=\"none\" stroke=\"" + color + "\" stroke-width=\"1.5\"/>\n";
        }
    };
    for (size_t li = 0; li < le.size(); li++) {
        if (lay.points[li].size() < 2) continue;
        int mi = edge_of[li];
        std::vector<Pt> pts;
        for (const Pt& p : lay.points[li]) pts.push_back(at(p));
        std::string list;
        for (const Pt& p : pts) list += (list.empty() ? "" : " ") + num(p.x) + "," + num(p.y);
        if (mi < 0) {  // a note's dotted line
            body += "<polyline class=\"note-link\" points=\"" + list + "\" fill=\"none\" stroke=\"" + th.line + "\" stroke-width=\"1\" stroke-dasharray=\"3 3\"/>\n";
            continue;
        }
        const Edge& e = m.edges[mi];
        std::string color = th.line, width = "1.2";
        Style st = e.id.empty() ? Style{} : style_of({}, e.id);
        if (!st.stroke.empty()) color = st.stroke;
        if (!st.stroke_width.empty()) width = px(st.stroke_width);
        bool dashed = e.op == Op::Include || e.op == Op::Extend;
        if (dashed) {  // a dashed polyline is drawn as a path: Skia draws a dashed <polyline> as a solid one plus black dashes
            std::string d;
            for (size_t k = 0; k < pts.size(); k++) d += std::string(k ? "L" : "M") + num(pts[k].x) + " " + num(pts[k].y) + " ";
            body += "<path class=\"edge\" d=\"" + d + "\" fill=\"none\"" + stroke_attrs(color, width) + " stroke-dasharray=\"5 4\" stroke-linejoin=\"round\"/>\n";
        } else {
            body += "<polyline class=\"edge\" points=\"" + list + "\" fill=\"none\"" + stroke_attrs(color, width) + " stroke-linejoin=\"round\"/>\n";
        }
        Pt tip = pts.back(), prev = pts[pts.size() - 2], head = pts.front(), next = pts[1];
        switch (e.op) {
            case Op::Point:
            case Op::Include:
            case Op::Extend: arrow_head(tip, prev, color == th.line ? th.arrow : color, false); break;
            case Op::RevPoint: arrow_head(head, next, color == th.line ? th.arrow : color, false); break;
            case Op::Generalize: arrow_head(tip, prev, color, true); break;
            case Op::Circle: end_mark(tip, prev, true, color); break;
            case Op::RevCircle: end_mark(head, next, true, color); break;
            case Op::Cross: end_mark(tip, prev, false, color); break;
            case Op::RevCross: end_mark(head, next, false, color); break;
            case Op::Line: break;
        }
    }
    for (size_t li = 0; li < le.size(); li++) {
        if (label_lines[li].empty()) continue;
        Pt c = at(lay.label_center[li]);
        body += "<g class=\"edge-label\">\n<rect x=\"" + num(c.x - le[li].label_w / 2) + "\" y=\"" + num(c.y - le[li].label_h / 2) + "\" width=\"" + num(le[li].label_w) + "\" height=\"" + num(le[li].label_h) + "\" rx=\"2\" fill=\"" + th.label_bg +
                "\" stroke=\"" + th.label_stroke + "\" stroke-width=\"1\"/>\n";
        centered_text(body, th, label_lines[li], c.x, c.y, kEdgeFont, kEdgeWeight, th.text_sec);
        body += "</g>\n";
    }
    // nodes
    for (size_t i = 0; i < m.nodes.size(); i++) {
        const Node& n = m.nodes[i];
        Pt c = at(lay.center[i]);
        const Style& st = n.style;
        std::string fill = st.fill.empty() ? th.surface : st.fill, stroke = st.stroke.empty() ? th.border : st.stroke, sw = st.stroke_width.empty() ? num(kInnerStroke) : px(st.stroke_width);
        std::string color = st.color.empty() ? th.label : st.color;
        switch (n.kind) {
            case Kind::Actor: draw_actor(body, th, n, c, st); break;
            case Kind::Ellipse:
                body += "<g class=\"usecase\">\n<ellipse cx=\"" + num(c.x) + "\" cy=\"" + num(c.y) + "\" rx=\"" + num(n.w / 2) + "\" ry=\"" + num(n.h / 2) + "\" fill=\"" + fill + "\"" + stroke_attrs(stroke, n.business ? "2" : sw) + "/>\n";
                centered_text(body, th, n.lines, c.x, c.y, kNodeFont, kNodeWeight, color);
                body += "</g>\n";
                break;
            case Kind::Rect:
                body += "<g class=\"usecase\">\n<rect x=\"" + num(c.x - n.w / 2) + "\" y=\"" + num(c.y - n.h / 2) + "\" width=\"" + num(n.w) + "\" height=\"" + num(n.h) + "\" rx=\"3\" fill=\"" + fill + "\"" + stroke_attrs(stroke, sw) + "/>\n";
                centered_text(body, th, n.lines, c.x, c.y, kNodeFont, kNodeWeight, color);
                body += "</g>\n";
                break;
            case Kind::Note: {
                double l = c.x - n.w / 2, t = c.y - n.h / 2, r = c.x + n.w / 2, b = c.y + n.h / 2, k = 9;
                body += "<g class=\"note\">\n<path d=\"M" + num(l) + " " + num(t) + " L" + num(r - k) + " " + num(t) + " L" + num(r) + " " + num(t + k) + " L" + num(r) + " " + num(b) + " L" + num(l) + " " + num(b) + " Z\" fill=\"" + th.note_fill + "\"" + stroke_attrs(th.note_border, "1") + "/>\n";
                centered_text(body, th, n.lines, c.x, c.y, 12, 400, th.note_text);
                body += "</g>\n";
                break;
            }
            case Kind::Json: {
                double l = c.x - n.w / 2, t = c.y - n.h / 2;
                body += "<g class=\"json\">\n<rect x=\"" + num(l) + "\" y=\"" + num(t) + "\" width=\"" + num(n.w) + "\" height=\"" + num(n.h) + "\" fill=\"" + fill + "\"" + stroke_attrs(stroke, "1") + "/>\n";
                body += "<rect x=\"" + num(l) + "\" y=\"" + num(t) + "\" width=\"" + num(n.w) + "\" height=\"30\" fill=\"" + th.cluster_hdr + "\"" + stroke_attrs(stroke, "1") + "/>\n";
                centered_text(body, th, {n.label}, c.x, t + 15, 13, 700, color);
                for (size_t r = 0; r < n.rows.size(); r++) {
                    double y = t + 30 + 22.0 * r + 11;
                    aligned_text(body, th, {n.rows[r].first}, l + 12, y, 12, 500, color, "start");
                    aligned_text(body, th, {n.rows[r].second}, l + n.w - 12, y, 12, 400, th.text_sec, "end");
                }
                body += "</g>\n";
                break;
            }
        }
    }
    svg = svg_document(th, (x1 - x0) + 2 * kMargin, (y1 - y0) + 2 * kMargin, body);
    return true;
}

}  // namespace mermaid
