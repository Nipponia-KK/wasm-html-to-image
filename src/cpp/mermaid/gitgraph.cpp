#include "mermaid/gitgraph.h"

#include <algorithm>
#include <cstdio>

#include "mermaid/mermaid_internal.h"

namespace mermaid {
namespace {

using litehtml::svgutil::trim;

enum class Type { Normal, Highlight, Reverse };

struct Commit {
    std::string id, tag;
    int branch = 0;
    Type type = Type::Normal;
    std::vector<int> parents;
    bool merge = false;
};

struct Branch {
    std::string name;
    int fork = -1;  // the commit it was created from
    int head = -1;  // its latest commit
};

bool starts(const std::string& s, const char* p) { return s.compare(0, std::char_traits<char>::length(p), p) == 0; }

std::string unquote(std::string s) {
    s = trim(s);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
    return s;
}

/// The value of `key: value` in the options of a line (a quoted string or one word).
std::string option(const std::string& line, const char* key) {
    size_t p = line.find(std::string(key) + ":");
    if (p == std::string::npos) return "";
    p += std::char_traits<char>::length(key) + 1;
    while (p < line.size() && line[p] == ' ') p++;
    if (p < line.size() && line[p] == '"') {
        size_t q = line.find('"', p + 1);
        return line.substr(p + 1, q == std::string::npos ? std::string::npos : q - p - 1);
    }
    size_t q = line.find(' ', p);
    return line.substr(p, q == std::string::npos ? std::string::npos : q - p);
}

/// A short hex name for a commit that has no id, like the reference renderer's `0-1a2b3c4`.
std::string auto_id(int n) {
    unsigned h = 2166136261u ^ (unsigned)(n * 7919 + 13);
    h *= 16777619u;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d-%07x", n, h & 0xfffffff);
    return buf;
}

}  // namespace

bool render_gitgraph(const std::string& source, const Options& options, std::string& svg, std::string& error) {
    using namespace metrics;
    std::vector<Commit> commits;
    std::vector<Branch> branches = {{"main", -1, -1}};
    int current = 0;
    auto find_branch = [&](const std::string& name) {
        for (size_t b = 0; b < branches.size(); b++)
            if (branches[b].name == name) return (int)b;
        return -1;
    };
    auto add_commit = [&](Commit c) {
        c.branch = current;
        int parent = branches[current].head >= 0 ? branches[current].head : branches[current].fork;
        if (parent >= 0) c.parents.insert(c.parents.begin(), parent);
        if (c.id.empty()) c.id = auto_id((int)commits.size());
        commits.push_back(c);
        branches[current].head = (int)commits.size() - 1;
    };
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
        if (starts(line, "commit")) {
            Commit c;
            c.id = option(line, "id");
            c.tag = option(line, "tag");
            std::string type = option(line, "type");
            c.type = type == "HIGHLIGHT" ? Type::Highlight : type == "REVERSE" ? Type::Reverse : Type::Normal;
            add_commit(c);
        } else if (starts(line, "branch ")) {
            std::string name = trim(line.substr(7));
            size_t sp = name.find(' ');
            if (sp != std::string::npos) name = name.substr(0, sp);  // `order:` options are ignored
            name = unquote(name);
            if (find_branch(name) >= 0) {
                error = "the branch exists already: " + name;
                return false;
            }
            int head = branches[current].head >= 0 ? branches[current].head : branches[current].fork;
            branches.push_back({name, head, -1});
            current = (int)branches.size() - 1;
        } else if (starts(line, "checkout ") || starts(line, "switch ")) {
            std::string name = unquote(trim(line.substr(line.find(' ') + 1)));
            int b = find_branch(name);
            if (b < 0) {
                error = "unknown branch: " + name;
                return false;
            }
            current = b;
        } else if (starts(line, "merge ")) {
            std::string rest = trim(line.substr(6));
            size_t sp = rest.find(' ');
            std::string name = unquote(sp == std::string::npos ? rest : rest.substr(0, sp));
            int b = find_branch(name);
            if (b < 0) {
                error = "unknown branch: " + name;
                return false;
            }
            int head = branches[b].head >= 0 ? branches[b].head : branches[b].fork;
            if (b == current || head < 0) {
                error = "nothing to merge from: " + name;
                return false;
            }
            Commit c;
            c.id = option(line, "id");
            c.tag = option(line, "tag");
            c.merge = true;
            c.parents.push_back(head);
            // the first parent is added in front by add_commit
            add_commit(c);
        } else if (starts(line, "cherry-pick")) {
            std::string id = option(line, "id");
            int src = -1;
            for (size_t k = 0; k < commits.size(); k++)
                if (commits[k].id == id) src = (int)k;
            if (src < 0) {
                error = "unknown commit: " + id;
                return false;
            }
            Commit c;
            c.id = auto_id((int)commits.size());
            c.tag = "cherry-pick:" + id;
            add_commit(c);
        } else {
            error = "unrecognised line: " + line;
            return false;
        }
    }
    if (commits.empty()) {
        error = "a git graph needs at least one commit";
        return false;
    }

    Theme th = make_theme(options);
    Text text{options};
    static const char* kColors[] = {"#4e79a7", "#f28e2b", "#59a14f", "#e15759", "#76b7b2", "#b07aa1", "#edc948", "#9c755f"};
    const double kStep = 84, kLane = 64, kR = 9;
    double name_w = 0;
    for (auto& b : branches) name_w = std::max(name_w, text.width(b.name, 12, 600));
    double left = kMargin + name_w + 40, top = kMargin + 24;
    auto X = [&](size_t k) { return left + kStep * k + kStep / 2; };
    auto Y = [&](int lane) { return top + kLane * lane + kLane / 2; };
    double W = left + kStep * commits.size() + kMargin, H = top + kLane * branches.size() + kMargin + 10;

    std::string lanes, edges, nodes, labels;
    for (size_t b = 0; b < branches.size(); b++) {
        double y = Y((int)b);
        std::string color = kColors[b % 8];
        lanes += "<line x1=\"" + num(left) + "\" y1=\"" + num(y) + "\" x2=\"" + num(W - kMargin / 2) + "\" y2=\"" + num(y) + "\" stroke=\"" + th.inner_stroke + "\" stroke-width=\"1\" stroke-dasharray=\"4 4\"/>\n";
        double pw = text.width(branches[b].name, 12, 600) + 20;
        lanes += "<g class=\"branch\">\n<rect x=\"" + num(kMargin) + "\" y=\"" + num(y - 13) + "\" width=\"" + num(pw) + "\" height=\"26\" rx=\"13\" fill=\"" + color + "\"/>\n";
        centered_text(lanes, th, {branches[b].name}, kMargin + pw / 2, y, 12, 600, "#ffffff");
        lanes += "</g>\n";
    }
    for (size_t k = 0; k < commits.size(); k++) {
        const Commit& c = commits[k];
        double cx = X(k), cy = Y(c.branch);
        std::string color = kColors[c.branch % 8];
        for (int p : c.parents) {
            double px = X(p), py = Y(commits[p].branch);
            std::string pc = kColors[(commits[p].branch == c.branch ? c.branch : (c.merge && p != c.parents[0] ? commits[p].branch : c.branch)) % 8];
            if (py == cy) edges += "<line x1=\"" + num(px) + "\" y1=\"" + num(py) + "\" x2=\"" + num(cx) + "\" y2=\"" + num(cy) + "\" stroke=\"" + pc + "\" stroke-width=\"3\"/>\n";
            else {
                // between lanes: leave horizontally, bend, arrive horizontally
                double mx = (px + cx) / 2;
                edges += "<path d=\"M" + num(px) + "," + num(py) + " C" + num(mx) + "," + num(py) + " " + num(mx) + "," + num(cy) + " " + num(cx) + "," + num(cy) + "\" fill=\"none\" stroke=\"" + pc +
                         "\" stroke-width=\"3\"/>\n";
            }
        }
        nodes += "<g class=\"commit\">\n";
        if (c.type == Type::Highlight)
            nodes += "<rect x=\"" + num(cx - kR) + "\" y=\"" + num(cy - kR) + "\" width=\"" + num(2 * kR) + "\" height=\"" + num(2 * kR) + "\" rx=\"2\" fill=\"" + color + "\" stroke=\"" + th.bg + "\" stroke-width=\"2\"/>\n";
        else if (c.merge)
            nodes += "<circle cx=\"" + num(cx) + "\" cy=\"" + num(cy) + "\" r=\"" + num(kR) + "\" fill=\"" + th.bg + "\" stroke=\"" + color + "\" stroke-width=\"3.5\"/>\n<circle cx=\"" + num(cx) + "\" cy=\"" + num(cy) +
                     "\" r=\"3.5\" fill=\"" + color + "\"/>\n";
        else
            nodes += "<circle cx=\"" + num(cx) + "\" cy=\"" + num(cy) + "\" r=\"" + num(kR) + "\" fill=\"" + color + "\" stroke=\"" + th.bg + "\" stroke-width=\"2\"/>\n";
        if (c.type == Type::Reverse) {
            nodes += "<line x1=\"" + num(cx - 5) + "\" y1=\"" + num(cy - 5) + "\" x2=\"" + num(cx + 5) + "\" y2=\"" + num(cy + 5) + "\" stroke=\"#ffffff\" stroke-width=\"2\"/>\n";
            nodes += "<line x1=\"" + num(cx - 5) + "\" y1=\"" + num(cy + 5) + "\" x2=\"" + num(cx + 5) + "\" y2=\"" + num(cy - 5) + "\" stroke=\"#ffffff\" stroke-width=\"2\"/>\n";
        }
        nodes += "</g>\n";
        centered_text(labels, th, {c.id}, cx, cy + 24, 10.5, 400, th.text_sec);
        if (!c.tag.empty()) {
            double tw = text.width(c.tag, 10.5, 600) + 14;
            labels += "<g class=\"tag\">\n<rect x=\"" + num(cx - tw / 2) + "\" y=\"" + num(cy - 34) + "\" width=\"" + num(tw) + "\" height=\"18\" rx=\"4\" fill=\"" + th.group_hdr + "\" stroke=\"" + th.border + "\" stroke-width=\"1\"/>\n";
            centered_text(labels, th, {c.tag}, cx, cy - 25, 10.5, 600, th.text);
            labels += "</g>\n";
        }
    }
    svg = svg_document(th, W, H, lanes + edges + nodes + labels);
    return true;
}

}  // namespace mermaid
