#include "mermaid/ishikawa.h"

#include <algorithm>

#include "mermaid/indent_tree.h"

namespace mermaid {
namespace {

struct CauseRow {
    std::string text;
    int depth;
};

void flatten(const TreeNode& n, int depth, std::vector<CauseRow>& out) {
    for (auto& k : n.kids) {
        out.push_back({k->text, depth});
        flatten(*k, depth + 1, out);
    }
}

struct Bone {
    std::string label;
    std::vector<CauseRow> rows;
    double row_w = 0, h = 0, slot = 0;
};

}  // namespace

bool render_ishikawa(const std::string& source, const Options& options, std::string& svg, std::string& error) {
    TreeNode tree;
    if (!parse_indent_tree(source, tree, error)) return false;
    if (tree.kids.empty()) {
        error = "an Ishikawa diagram needs the effect on the first line";
        return false;
    }
    const TreeNode& effect = *tree.kids[0];
    if (tree.kids.size() > 1) {
        error = "only one effect (the first line) is allowed: " + tree.kids[1]->text;
        return false;
    }
    Theme th = make_theme(options);
    Text text{options};
    const double kRow = 21, kSlant = 44, margin = 24, kFont = 12, kHead = 15;
    std::vector<Bone> bones;
    for (auto& c : effect.kids) {
        Bone b;
        b.label = c->text;
        flatten(*c, 0, b.rows);
        b.row_w = text.width(b.label, 13, 600) / 2;
        for (auto& r : b.rows) b.row_w = std::max(b.row_w, text.width(r.text, kFont, 400) + r.depth * 14 + 12);
        b.h = std::max(60.0, b.rows.size() * kRow + 30);
        b.slot = b.row_w + kSlant + 24;
        bones.push_back(b);
    }
    double top_h = 0, bot_h = 0, top_w = 0, bot_w = 0;
    for (size_t k = 0; k < bones.size(); k++) {
        if (k % 2 == 0) top_h = std::max(top_h, bones[k].h), top_w += bones[k].slot;
        else bot_h = std::max(bot_h, bones[k].h), bot_w += bones[k].slot;
    }
    double head_w = text.width(effect.text, kHead, 600) + 28, head_h = 46;
    double spine_y = margin + top_h + 34;
    double spine_len = std::max({top_w, bot_w, 120.0});
    double spine_x0 = margin, spine_x1 = margin + spine_len;
    double W = spine_x1 + head_w + margin, H = spine_y + bot_h + 34 + margin;
    std::string body;
    body += "<line x1=\"" + num(spine_x0) + "\" y1=\"" + num(spine_y) + "\" x2=\"" + num(spine_x1) + "\" y2=\"" + num(spine_y) + "\" stroke=\"" + th.line + "\" stroke-width=\"3\" stroke-linecap=\"round\"/>\n";
    double cursor[2] = {margin, margin};
    for (size_t k = 0; k < bones.size(); k++) {
        const Bone& b = bones[k];
        bool top = k % 2 == 0;
        double sx = cursor[top ? 0 : 1];
        cursor[top ? 0 : 1] += b.slot;
        double bx = sx + b.row_w + 8;  // x where the bone leaves its label
        double y_out = top ? spine_y - b.h : spine_y + b.h;
        body += "<line x1=\"" + num(bx) + "\" y1=\"" + num(y_out) + "\" x2=\"" + num(bx + kSlant) + "\" y2=\"" + num(spine_y) + "\" stroke=\"" + th.line + "\" stroke-width=\"2\" stroke-linecap=\"round\"/>\n";
        double lw = text.width(b.label, 13, 600) + 20;
        double ly = top ? y_out - 20 : y_out + 20;
        body += "<rect x=\"" + num(bx - lw / 2) + "\" y=\"" + num(ly - 13) + "\" width=\"" + num(lw) + "\" height=\"26\" rx=\"5\" fill=\"" + th.surface + "\" stroke=\"" + th.border + "\" stroke-width=\"1\"/>\n";
        centered_text(body, th, {b.label}, bx, ly, 13, 600, th.text);
        for (size_t j = 0; j < b.rows.size(); j++) {
            double y = top ? y_out + 26 + j * kRow : y_out - 26 - j * kRow;
            double t = (y - y_out) / (spine_y - y_out);
            double x_on = bx + kSlant * t;
            double tick = b.rows[j].depth * 14 + 10;
            body += "<line x1=\"" + num(x_on - tick - text.width(b.rows[j].text, kFont, 400) - 4) + "\" y1=\"" + num(y + 8) + "\" x2=\"" + num(x_on) + "\" y2=\"" + num(y + 8) + "\" stroke=\"" + th.border + "\" stroke-width=\"1\"/>\n";
            aligned_text(body, th, {b.rows[j].text}, x_on - 6 - b.rows[j].depth * 14, y - 2, kFont, 400, th.text, "end");
        }
    }
    body += "<rect x=\"" + num(spine_x1) + "\" y=\"" + num(spine_y - head_h / 2) + "\" width=\"" + num(head_w) + "\" height=\"" + num(head_h) + "\" rx=\"6\" fill=\"" + th.group_hdr + "\" stroke=\"" + th.border +
            "\" stroke-width=\"1.5\"/>\n";
    centered_text(body, th, {effect.text}, spine_x1 + head_w / 2, spine_y, kHead, 600, th.text);
    svg = svg_document(th, W, H, body);
    return true;
}

}  // namespace mermaid
