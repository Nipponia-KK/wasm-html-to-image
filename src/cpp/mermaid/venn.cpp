#include "mermaid/venn.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>

#include "mermaid/mermaid_internal.h"

namespace mermaid {
namespace {

constexpr double kPi = 3.14159265358979323846;

using litehtml::svgutil::trim;

struct Item {
    std::string id, label;
    std::vector<int> sets;  // for a union: the sets it overlaps
    double size = 1;
    std::vector<std::string> texts;
    double cx = 0, cy = 0, r = 0;
};

bool starts(const std::string& s, const char* p) { return s.compare(0, std::char_traits<char>::length(p), p) == 0; }

std::string unquote(std::string s) {
    s = trim(s);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
    return s;
}

// `id["Label"]:size` with the label and size optional.
void parse_tail(std::string s, std::string& id, std::string& label, double& size, bool& has_size) {
    has_size = false;
    size_t colon = s.rfind(':');
    size_t bracket = s.rfind(']');
    if (colon != std::string::npos && (bracket == std::string::npos || colon > bracket)) {
        size = std::atof(trim(s.substr(colon + 1)).c_str());
        has_size = true;
        s = s.substr(0, colon);
    }
    s = trim(s);
    size_t b = s.find('[');
    if (b != std::string::npos && s.back() == ']') {
        id = trim(s.substr(0, b));
        label = unquote(s.substr(b + 1, s.size() - b - 2));
    } else {
        id = s;
        label = s;
    }
}

}  // namespace

bool render_venn(const std::string& source, const Options& options, std::string& svg, std::string& error) {
    std::vector<Item> sets, unions;
    std::map<std::string, int> index;
    std::string title;
    int last_set = -1, last_union = -1;
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
            title = unquote(line.substr(6));
        } else if (starts(line, "set ")) {
            Item it;
            bool has;
            parse_tail(line.substr(4), it.id, it.label, it.size, has);
            if (it.id.empty() || index.count(it.id)) {
                error = "a set needs a new name: " + line;
                return false;
            }
            if (!has) it.size = 10;
            index[it.id] = (int)sets.size();
            sets.push_back(it);
            last_set = (int)sets.size() - 1;
            last_union = -1;
        } else if (starts(line, "union ")) {
            std::string rest = line.substr(6);
            size_t b = rest.find('[');
            std::string head = trim(rest.substr(0, b == std::string::npos ? rest.find(':') : b));
            std::string tail = b == std::string::npos ? (rest.find(':') == std::string::npos ? "" : rest.substr(rest.find(':'))) : rest.substr(b);
            Item it;
            bool has;
            std::string dummy_id;
            parse_tail("u" + tail, dummy_id, it.label, it.size, has);
            size_t k = 0;
            while (k <= head.size()) {
                size_t c = head.find(',', k);
                std::string id = trim(head.substr(k, c == std::string::npos ? std::string::npos : c - k));
                auto f = index.find(id);
                if (f == index.end()) {
                    error = "unknown set: " + id;
                    return false;
                }
                it.sets.push_back(f->second);
                if (c == std::string::npos) break;
                k = c + 1;
            }
            if (it.sets.size() < 2) {
                error = "a union needs at least two sets: " + line;
                return false;
            }
            if (it.label == "u") it.label.clear();
            unions.push_back(it);
            last_union = (int)unions.size() - 1;
            last_set = -1;
        } else if (starts(line, "text ")) {
            if (last_set < 0 && last_union < 0) {
                error = "`text` follows a set or a union: " + line;
                return false;
            }
            std::string id, label;
            double sz;
            bool has;
            parse_tail(line.substr(5), id, label, sz, has);
            (last_set >= 0 ? sets[last_set] : unions[last_union]).texts.push_back(label);
        } else if (starts(line, "accTitle") || starts(line, "accDescr")) {
            continue;
        } else {
            error = "unrecognised line: " + line;
            return false;
        }
    }
    if (sets.empty()) {
        error = "a Venn diagram needs at least one set";
        return false;
    }

    Theme th = make_theme(options);
    Text text{options};
    size_t n = sets.size();
    double max_size = 0;
    for (auto& s : sets) max_size = std::max(max_size, s.size > 0 ? s.size : 1.0);
    for (auto& s : sets) s.r = std::max(48.0, 100.0 * std::sqrt(std::max(s.size, 0.0) / max_size));
    if (n == 1) {
        sets[0].cx = sets[0].cy = 0;
    } else if (n == 2) {
        double d = 0.6 * (sets[0].r + sets[1].r);
        sets[0].cx = -d / 2;
        sets[1].cx = d / 2;
    } else {
        double avg = 0;
        for (auto& s : sets) avg += s.r / n;
        double ring = avg * (n == 3 ? 0.62 : 0.8) / std::sin(kPi / n) * (n == 3 ? 0.95 : 0.75);
        for (size_t k = 0; k < n; k++) {
            double a = -kPi / 2 + 2 * kPi * k / n;
            sets[k].cx = ring * std::cos(a);
            sets[k].cy = ring * std::sin(a);
        }
    }
    double gx = 0, gy = 0;
    for (auto& s : sets) gx += s.cx / n, gy += s.cy / n;

    double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
    for (auto& s : sets) {
        x0 = std::min(x0, s.cx - s.r);
        y0 = std::min(y0, s.cy - s.r);
        x1 = std::max(x1, s.cx + s.r);
        y1 = std::max(y1, s.cy + s.r);
    }
    const double pad = 28;
    double top = title.empty() ? pad : pad + 30;
    double W = std::max(x1 - x0 + 2 * pad, title.empty() ? 0.0 : text.width(title, 15, 600) + 2 * pad);
    double ox = (W - (x1 - x0)) / 2 - x0, oy = top - y0;
    double H = top + (y1 - y0) + pad;

    std::string body;
    if (!title.empty()) centered_text(body, th, {title}, W / 2, pad + 6, 15, 600, th.text);
    for (size_t k = 0; k < n; k++) {
        int pct = n <= 1 ? 40 : 30 + (int)(40.0 * k / (n - 1));
        std::string color = mix(th.fg, th.bg, pct);
        body += "<circle class=\"set\" cx=\"" + num(sets[k].cx + ox) + "\" cy=\"" + num(sets[k].cy + oy) + "\" r=\"" + num(sets[k].r) + "\" fill=\"" + color + "\" fill-opacity=\"0.35\" stroke=\"" + color +
                "\" stroke-width=\"2\"/>\n";
    }
    auto write = [&](double cx, double cy, const std::string& label, const std::vector<std::string>& texts, int weight) {
        std::vector<std::string> lines;
        if (!label.empty()) lines.push_back(label);
        double y = cy - (lines.size() + texts.size() - 1) * 8;
        size_t row = 0;
        if (!label.empty()) centered_text(body, th, {label}, cx, y + 16 * row++, 13, weight, th.text);
        for (auto& t : texts) centered_text(body, th, {t}, cx, y + 16 * row++, 12, 400, th.text_sec);
    };
    for (size_t k = 0; k < n; k++) {
        double dx = sets[k].cx - gx, dy = sets[k].cy - gy, len = std::sqrt(dx * dx + dy * dy);
        double px = sets[k].cx, py = sets[k].cy;
        if (n > 1 && len > 0.01) {
            px += dx / len * sets[k].r * 0.45;
            py += dy / len * sets[k].r * 0.45;
        }
        write(px + ox, py + oy, sets[k].label, sets[k].texts, 600);
    }
    for (auto& u : unions) {
        double cx = 0, cy = 0;
        for (int s : u.sets) cx += sets[s].cx / u.sets.size(), cy += sets[s].cy / u.sets.size();
        write(cx + ox, cy + oy, u.label, u.texts, 500);
    }
    svg = svg_document(th, W, H, body);
    return true;
}

}  // namespace mermaid
