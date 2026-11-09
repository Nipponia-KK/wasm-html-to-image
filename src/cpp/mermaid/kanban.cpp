#include "mermaid/kanban.h"

#include <algorithm>

#include "mermaid/mermaid_internal.h"

namespace mermaid {
namespace {

using litehtml::svgutil::trim;

struct Card {
    std::string text, assigned, ticket, priority;
};

struct Column {
    std::string title;
    std::vector<Card> cards;
};

bool starts(const std::string& s, const char* p) { return s.compare(0, std::char_traits<char>::length(p), p) == 0; }

/// `id[Text]` or `[Text]` or just `Text`.
std::string label_of(const std::string& s) {
    size_t open = s.find('['), close = s.rfind(']');
    if (open != std::string::npos && close != std::string::npos && close > open) return trim(s.substr(open + 1, close - open - 1));
    return trim(s);
}

/// The value of `key: 'value'` inside an `@{ ... }` block.
std::string meta(const std::string& block, const char* key) {
    size_t p = block.find(std::string(key) + ":");
    if (p == std::string::npos) return "";
    p += std::char_traits<char>::length(key) + 1;
    while (p < block.size() && (block[p] == ' ' || block[p] == '\'' || block[p] == '"')) p++;
    size_t q = p;
    while (q < block.size() && block[q] != '\'' && block[q] != '"' && block[q] != ',' && block[q] != '}') q++;
    return trim(block.substr(p, q - p));
}

}  // namespace

bool render_kanban(const std::string& source, const Options& options, std::string& svg, std::string& error) {
    using namespace metrics;
    std::vector<Column> columns;
    int column_indent = -1;
    bool first = true;
    size_t i = 0;
    while (i <= source.size()) {
        size_t e = source.find('\n', i);
        std::string raw = source.substr(i, e == std::string::npos ? std::string::npos : e - i);
        i = e == std::string::npos ? source.size() + 1 : e + 1;
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        std::string line = trim(raw);
        if (line.empty() || starts(line, "%%")) continue;
        if (first) {
            first = false;
            continue;
        }
        int indent = 0;
        for (char c : raw) {
            if (c == ' ') indent++;
            else if (c == '\t') indent += 4;
            else break;
        }
        if (column_indent < 0) column_indent = indent;
        std::string block;
        size_t at = line.find("@{");
        if (at != std::string::npos) {
            block = line.substr(at);
            line = trim(line.substr(0, at));
        }
        if (indent <= column_indent) {
            columns.push_back({label_of(line), {}});
        } else {
            if (columns.empty()) {
                error = "a task needs a column before it: " + line;
                return false;
            }
            Card c;
            c.text = label_of(line);
            c.assigned = meta(block, "assigned");
            c.ticket = meta(block, "ticket");
            c.priority = meta(block, "priority");
            columns.back().cards.push_back(c);
        }
    }
    if (columns.empty()) {
        error = "a kanban board needs at least one column";
        return false;
    }

    Theme th = make_theme(options);
    Text text{options};
    static const char* kColors[] = {"#4e79a7", "#f28e2b", "#59a14f", "#b07aa1", "#76b7b2", "#e15759", "#edc948", "#9c755f"};
    const double kColW = 210, kGap = 14, kPad = 10, kFont = 12.5, kHead = 34;
    const double lh = kFont * kLineHeight;
    struct Laid {
        std::vector<std::vector<std::string>> lines;
        std::vector<double> heights;
        double total = 0;
    };
    std::vector<Laid> laid(columns.size());
    double tallest = 0;
    for (size_t c = 0; c < columns.size(); c++) {
        double y = kHead + kPad;
        for (auto& card : columns[c].cards) {
            // words wrap at the card width; text without spaces (CJK) wraps by characters
            std::vector<std::string> lines;
            std::string cur;
            size_t k = 0;
            const double max_w = kColW - 2 * kPad - 16;
            std::string t = card.text;
            while (k < t.size()) {
                size_t len = 1;
                unsigned char ch = (unsigned char)t[k];
                if (ch >= 0xF0) len = 4;
                else if (ch >= 0xE0) len = 3;
                else if (ch >= 0xC0) len = 2;
                std::string next = cur + t.substr(k, len);
                if (!cur.empty() && text.width(next, kFont, 400) > max_w) {
                    size_t sp = cur.rfind(' ');
                    if (sp != std::string::npos && ch < 0x80 && ch != ' ') {
                        lines.push_back(cur.substr(0, sp));
                        cur = cur.substr(sp + 1) + t.substr(k, len);
                    } else {
                        lines.push_back(cur);
                        cur = t.substr(k, len);
                    }
                } else cur = next;
                k += len;
            }
            if (!cur.empty() || lines.empty()) lines.push_back(cur);
            double h = lines.size() * lh + 16 + ((!card.assigned.empty() || !card.ticket.empty()) ? 18 : 0);
            laid[c].lines.push_back(lines);
            laid[c].heights.push_back(h);
            y += h + 8;
        }
        laid[c].total = y + kPad - 8;
        tallest = std::max(tallest, laid[c].total);
    }
    double W = kMargin * 2 + columns.size() * kColW + (columns.size() - 1) * kGap, H = kMargin * 2 + tallest;
    std::string body;
    for (size_t c = 0; c < columns.size(); c++) {
        double x = kMargin + c * (kColW + kGap), y0 = kMargin;
        std::string color = kColors[c % 8];
        body += "<g class=\"column\">\n<rect x=\"" + num(x) + "\" y=\"" + num(y0) + "\" width=\"" + num(kColW) + "\" height=\"" + num(tallest) + "\" rx=\"6\" fill=\"" + th.group_hdr + "\" stroke=\"" + th.border +
                "\" stroke-width=\"1\"/>\n";
        body += "<rect x=\"" + num(x) + "\" y=\"" + num(y0) + "\" width=\"" + num(kColW) + "\" height=\"" + num(kHead) + "\" rx=\"6\" fill=\"" + color + "\"/>\n";
        body += "<rect x=\"" + num(x) + "\" y=\"" + num(y0 + kHead - 8) + "\" width=\"" + num(kColW) + "\" height=\"8\" fill=\"" + color + "\"/>\n";
        centered_text(body, th, {columns[c].title}, x + kColW / 2, y0 + kHead / 2, 13, 600, "#ffffff");
        double y = y0 + kHead + kPad;
        for (size_t k = 0; k < columns[c].cards.size(); k++) {
            const Card& card = columns[c].cards[k];
            double h = laid[c].heights[k];
            body += "<g class=\"card\">\n<rect x=\"" + num(x + kPad) + "\" y=\"" + num(y) + "\" width=\"" + num(kColW - 2 * kPad) + "\" height=\"" + num(h) + "\" rx=\"4\" fill=\"" + th.bg + "\" stroke=\"" +
                    th.border + "\" stroke-width=\"1\"/>\n";
            if (card.priority == "Very High" || card.priority == "High")
                body += "<rect x=\"" + num(x + kPad) + "\" y=\"" + num(y) + "\" width=\"4\" height=\"" + num(h) + "\" rx=\"2\" fill=\"" + (card.priority == "High" ? "#f28e2b" : "#e5484d") + "\"/>\n";
            aligned_text(body, th, laid[c].lines[k], x + kPad + 10, y + 8 + laid[c].lines[k].size() * lh / 2, kFont, 400, th.text, "start");
            if (!card.assigned.empty() || !card.ticket.empty()) {
                if (!card.ticket.empty()) aligned_text(body, th, {card.ticket}, x + kPad + 10, y + h - 12, 11, 400, th.text_sec, "start");
                if (!card.assigned.empty()) aligned_text(body, th, {card.assigned}, x + kColW - kPad - 10, y + h - 12, 11, 600, th.text_sec, "end");
            }
            body += "</g>\n";
            y += h + 8;
        }
        body += "</g>\n";
    }
    svg = svg_document(th, W, H, body);
    return true;
}

}  // namespace mermaid
