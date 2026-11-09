#include "mermaid/sankey.h"

#include <algorithm>
#include <cstdlib>
#include <map>

#include "mermaid/mermaid_internal.h"

namespace mermaid {
namespace {

using litehtml::svgutil::trim;

struct Link {
    int from, to;
    double value;
    double y_from = 0, y_to = 0;  // top of the ribbon at each end
};

struct Node {
    std::string name;
    double in = 0, out = 0, value = 0;
    int layer = 0;
    double y = 0, h = 0;
    double next_out = 0, next_in = 0;  // where the next ribbon leaves / arrives
};

bool starts(const std::string& s, const char* p) { return s.compare(0, std::char_traits<char>::length(p), p) == 0; }

/// One CSV row; a field may be quoted, and `""` inside quotes is a quote.
std::vector<std::string> csv_row(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (quoted) {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') cur += '"', i++;
            else if (c == '"') quoted = false;
            else cur += c;
        } else if (c == '"') quoted = true;
        else if (c == ',') out.push_back(trim(cur)), cur.clear();
        else cur += c;
    }
    out.push_back(trim(cur));
    return out;
}

}  // namespace

bool render_sankey(const std::string& source, const Options& options, std::string& svg, std::string& error) {
    using namespace metrics;
    std::vector<Node> nodes;
    std::vector<Link> links;
    std::map<std::string, int> index;
    auto node_for = [&](const std::string& name) {
        auto it = index.find(name);
        if (it != index.end()) return it->second;
        Node n;
        n.name = name;
        nodes.push_back(n);
        return index[name] = (int)nodes.size() - 1;
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
        auto row = csv_row(line);
        char* end;
        double v = row.size() == 3 ? std::strtod(row[2].c_str(), &end) : 0;
        if (row.size() != 3 || row[0].empty() || row[1].empty() || row[2].empty() || *end != 0 || v <= 0) {
            error = "a flow is `source,target,value` with a positive value: " + line;
            return false;
        }
        int a = node_for(row[0]), b = node_for(row[1]);
        if (a == b) {
            error = "a flow cannot start and end at the same node: " + line;
            return false;
        }
        links.push_back({a, b, v});
        nodes[a].out += v;
        nodes[b].in += v;
    }
    if (links.empty()) {
        error = "a sankey diagram needs at least one flow";
        return false;
    }
    for (auto& n : nodes) n.value = std::max(n.in, n.out);

    // layers: the longest path from a source (a cycle stops the relaxation)
    int max_layer = 0;
    for (size_t pass = 0; pass < nodes.size(); pass++) {
        bool moved = false;
        for (auto& l : links)
            if (nodes[l.to].layer < nodes[l.from].layer + 1 && nodes[l.from].layer < (int)nodes.size()) nodes[l.to].layer = nodes[l.from].layer + 1, moved = true;
        if (!moved) break;
    }
    for (auto& n : nodes) max_layer = std::max(max_layer, std::min(n.layer, (int)nodes.size() - 1));

    Theme th = make_theme(options);
    Text text{options};
    static const char* kColors[] = {"#4e79a7", "#f28e2b", "#59a14f", "#e15759", "#76b7b2", "#b07aa1", "#edc948", "#9c755f"};
    const double kNodeW = 14, kPadY = 14, kColGap = 190, kPlotH = 340;
    double label_w = 0;
    for (auto& n : nodes) label_w = std::max(label_w, text.width(n.name, 12, 500));
    double left = kMargin, right_pad = label_w + 50;

    // the scale that makes the fullest column fit the plot
    std::vector<std::vector<int>> cols(max_layer + 1);
    for (size_t k = 0; k < nodes.size(); k++) cols[std::min(nodes[k].layer, max_layer)].push_back((int)k);
    double scale = 1e18;
    for (auto& c : cols) {
        double sum = 0;
        for (int k : c) sum += nodes[k].value;
        if (sum > 0) scale = std::min(scale, (kPlotH - kPadY * (c.size() > 0 ? c.size() - 1 : 0)) / sum);
    }
    // order each column by the average position of its sources, and stack it
    for (int l = 0; l <= max_layer; l++) {
        auto& c = cols[l];
        if (l > 0) {
            std::vector<std::pair<double, int>> keyed;
            for (int k : c) {
                double s = 0, w = 0;
                for (auto& lk : links)
                    if (lk.to == k) s += (nodes[lk.from].y + nodes[lk.from].h / 2) * lk.value, w += lk.value;
                keyed.push_back({w > 0 ? s / w : 0, k});
            }
            std::stable_sort(keyed.begin(), keyed.end(), [](auto& a, auto& b) { return a.first < b.first; });
            for (size_t j = 0; j < keyed.size(); j++) c[j] = keyed[j].second;
        }
        double sum = 0;
        for (int k : c) sum += nodes[k].value * scale;
        double y = (kPlotH - sum - kPadY * (c.empty() ? 0 : c.size() - 1)) / 2;
        for (int k : c) {
            nodes[k].h = std::max(2.0, nodes[k].value * scale);
            nodes[k].y = y;
            nodes[k].next_out = nodes[k].next_in = y;
            y += nodes[k].h + kPadY;
        }
    }
    double top = kMargin;
    double W = left + max_layer * kColGap + kNodeW + right_pad + kMargin / 2, H = top + kPlotH + kMargin;
    auto X = [&](int layer) { return left + layer * kColGap; };

    // ribbons: the ones that leave a node are stacked by the position of their target, and the same on arrival
    std::vector<size_t> order(links.size());
    for (size_t k = 0; k < links.size(); k++) order[k] = k;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return nodes[links[a].to].y < nodes[links[b].to].y; });
    for (size_t k : order) {
        links[k].y_from = nodes[links[k].from].next_out;
        nodes[links[k].from].next_out += links[k].value * scale;
    }
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return nodes[links[a].from].y < nodes[links[b].from].y; });
    for (size_t k : order) {
        links[k].y_to = nodes[links[k].to].next_in;
        nodes[links[k].to].next_in += links[k].value * scale;
    }
    std::string body, ribbons, boxes, labels;
    for (auto& l : links) {
        const Node &a = nodes[l.from], &b = nodes[l.to];
        double x0 = X(std::min(a.layer, max_layer)) + kNodeW, x1 = X(std::min(b.layer, max_layer)), h = l.value * scale, mx = (x0 + x1) / 2;
        double ya = top + l.y_from, yb = top + l.y_to;
        ribbons += "<path class=\"flow\" d=\"M" + num(x0) + "," + num(ya) + " C" + num(mx) + "," + num(ya) + " " + num(mx) + "," + num(yb) + " " + num(x1) + "," + num(yb) + " L" + num(x1) + "," + num(yb + h) +
                   " C" + num(mx) + "," + num(yb + h) + " " + num(mx) + "," + num(ya + h) + " " + num(x0) + "," + num(ya + h) + " Z\" fill=\"" + kColors[l.from % 8] + "\" fill-opacity=\"0.38\"/>\n";
    }
    for (size_t k = 0; k < nodes.size(); k++) {
        const Node& n = nodes[k];
        double x = X(std::min(n.layer, max_layer)), y = top + n.y;
        boxes += "<rect class=\"node\" x=\"" + num(x) + "\" y=\"" + num(y) + "\" width=\"" + num(kNodeW) + "\" height=\"" + num(n.h) + "\" fill=\"" + kColors[k % 8] + "\"/>\n";
        aligned_text(labels, th, {n.name + "  " + num(n.value)}, x + kNodeW + 6, y + n.h / 2, 12, 500, th.text, "start");
    }
    svg = svg_document(th, W, H, ribbons + boxes + labels);
    return true;
}

}  // namespace mermaid
