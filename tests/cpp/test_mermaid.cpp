// tests/cpp/test_mermaid.cpp — host unit tests for src/cpp/mermaid (flowchart parser, layered layout, SVG).
// Standard library only; a minimal runner like the other tests here.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "mermaid/layered.h"
#include "mermaid/mermaid.h"

namespace {

int g_pass = 0;
int g_fail = 0;

void check(bool ok, const std::string& name) {
    if (ok) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("FAIL %s\n", name.c_str());
    }
}

int count(const std::string& hay, const std::string& needle) {
    int n = 0;
    for (size_t p = hay.find(needle); p != std::string::npos; p = hay.find(needle, p + needle.size())) n++;
    return n;
}

bool render(const std::string& src, std::string& svg, std::string& err) {
    mermaid::Options o;
    return mermaid::render(src, o, svg, err);
}

std::string svg_of(const std::string& src) {
    std::string svg, err;
    if (!render(src, svg, err)) return "ERROR: " + err;
    return svg;
}

bool has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

using mermaid::Direction;
using mermaid::LayoutEdge;
using mermaid::LayoutNode;

LayoutNode box(double w, double h) {
    LayoutNode n;
    n.w = w;
    n.h = h;
    return n;
}

LayoutEdge edge(int a, int b) {
    LayoutEdge e;
    e.from = a;
    e.to = b;
    return e;
}

}  // namespace

int main() {
    // --- parsing: edges and their variants ---
    check(count(svg_of("graph TD\nA-->B"), "class=\"edge\"") == 1, "edge/basic");
    check(count(svg_of("graph TD\nA-->B-->C"), "class=\"edge\"") == 2, "edge/chain");
    check(count(svg_of("graph TD\nA & B --> C & D"), "class=\"edge\"") == 4, "edge/groups");
    check(count(svg_of("graph TD\nA---B"), "<polygon") == 0, "edge/open_has_no_arrow");
    check(count(svg_of("graph TD\nA-->B"), "<polygon") == 1, "edge/arrow_head");
    check(count(svg_of("graph TD\nA<-->B"), "<polygon") == 2, "edge/both_arrows");
    check(has(svg_of("graph TD\nA-.->B"), "stroke-dasharray=\"4 4\""), "edge/dotted");
    check(has(svg_of("graph TD\nA==>B"), "stroke-width=\"2.5\""), "edge/thick");
    check(has(svg_of("graph TD\nA-->|yes|B"), ">yes<"), "edge/pipe_label");
    check(has(svg_of("graph TD\nA-- some text -->B"), ">some text<"), "edge/inline_label");
    check(count(svg_of("graph TD\nA~~~B"), "class=\"edge\"") == 0, "edge/invisible");
    check(count(svg_of("graph LR\nA-->B;B-->C"), "class=\"edge\"") == 2, "edge/semicolon");

    // --- nodes: shapes and labels ---
    check(has(svg_of("graph TD\nA((round))"), "<circle"), "shape/circle");
    check(has(svg_of("graph TD\nA{choice}"), "<polygon"), "shape/diamond");
    check(has(svg_of("graph TD\nA[(db)]"), "<path"), "shape/cylinder");
    check(has(svg_of("graph TD\nA([pill])"), "rx="), "shape/stadium");
    check(has(svg_of("graph TD\nA[\"quoted [text]\"]"), ">quoted [text]<"), "node/quoted_label");
    check(has(svg_of("graph TD\nA[line one<br/>line two]"), ">line two<"), "node/br_break");
    check(has(svg_of("graph TD\nA[a\xe6\x97\xa5\xe6\x9c\xac]-->B"), ">a\xe6\x97\xa5\xe6\x9c\xac<"), "node/cjk_label");
    check(count(svg_of("graph TD\nA[x] --> A"), "<g class=\"node\">") == 1, "node/redeclared_is_one_node");
    check(has(svg_of("graph TD\nA-->B\nstyle A fill:#ff0000"), "#ff0000"), "style/node_fill");
    check(has(svg_of("graph TD\nclassDef hot fill:#00ff00\nA:::hot-->B"), "#00ff00"), "style/class_def");
    check(has(svg_of("graph TD\nsubgraph one [First]\nA-->B\nend"), "class=\"subgraph\""), "subgraph/frame");
    check(has(svg_of("graph TD\nsubgraph one [First]\nA-->B\nend"), ">First<"), "subgraph/title");

    // --- frontmatter and init directives ---
    {
        std::string svg, err;
        mermaid::Options on;
        on.allow_directives = true;
        check(render("---\ntitle: T\n---\ngraph TD\nA-->B", svg, err), "front/skipped");
        check(render("%%{init: {\"theme\":\"dark\"}}%%\ngraph TD\nA-->B", svg, err) && !has(svg, "#0d1117"), "init/off_by_default");
        check(mermaid::render("%%{init: {\"theme\":\"dark\",\"themeVariables\":{\"primaryColor\":\"#123456\"}}}%%\ngraph TD\nA-->B", on, svg, err) && has(svg, "#123456"), "init/theme_variables");
        check(mermaid::render("---\nconfig:\n  theme: forest\n---\ngraph TD\nA-->B", on, svg, err) && has(svg, "#e8f5ec"), "front/config_theme");
    }

    // --- classDef / class / style on the other box diagrams ---
    check(has(svg_of("stateDiagram-v2\nclassDef hot fill:#aa0001\nA --> B\nclass A hot"), "#aa0001"), "style/state_class");
    check(has(svg_of("stateDiagram-v2\nA:::hot --> B\nclassDef hot fill:#aa0002"), "#aa0002"), "style/state_triple_colon");
    check(has(svg_of("stateDiagram-v2\nA --> B\nstyle B stroke:#aa0003,stroke-width:3px"), "stroke-width=\"3\""), "style/state_direct");
    check(has(svg_of("classDiagram\nclass A\nclass B\nA --> B\nstyle A fill:#aa0004"), "#aa0004"), "style/class_style");
    check(has(svg_of("classDiagram\nclass A\nclassDef hot fill:#aa0005\ncssClass \"A\" hot"), "#aa0005"), "style/class_css_class");
    check(has(svg_of("erDiagram\nA ||--o{ B : has\nstyle A fill:#aa0006"), "#aa0006"), "style/er_style");
    check(has(svg_of("block-beta\ncolumns 2\na b\nstyle a fill:#aa0007"), "#aa0007"), "style/block_style");

    // --- treemap, radar, info ---
    {
        std::string tm = svg_of("treemap-beta\n\"A\"\n  \"a1\": 10\n  \"a2\": 30\n\"B\"\n  \"b1\": 20\n");
        check(count(tm, "class=\"leaf\"") == 3 && count(tm, "class=\"section\"") == 2, "treemap/items");
        check(has(tm, ">a2<"), "treemap/label");
        check(has(svg_of("treemap\n\"Only\": 5"), "class=\"leaf\""), "treemap/leaf_at_top");
        check(has(svg_of("treemap-beta\n\"A\": 1\n  \"b\": 2"), "ERROR"), "treemap/leaf_with_children");
        check(has(svg_of("treemap-beta\n\"A\"\n  \"b\": x"), "ERROR"), "treemap/bad_value");
        std::string rd = svg_of("radar-beta\naxis m[\"Math\"], s[\"Sci\"], e[\"Eng\"]\ncurve a[\"Alice\"]{85, 90, 80}\ncurve b[\"Bob\"]{s: 70, m: 75, e: 85}\nmax 100\ngraticule circle\nticks 4");
        check(count(rd, "class=\"curve\"") == 2 && count(rd, "<circle") == 4, "radar/curves_and_graticule");
        check(has(rd, ">Math<") && has(rd, ">Alice<"), "radar/labels");
        check(has(svg_of("radar-beta\naxis a, b\ncurve c{1,2}"), "ERROR"), "radar/needs_three_axes");
        check(has(svg_of("radar-beta\naxis a, b, c\ncurve c{1,2,3,4}"), "ERROR"), "radar/too_many_values");
        check(has(svg_of("info\nshowInfo"), ">v11<"), "info/banner");
    }

    // --- architecture ---
    {
        std::string ar = svg_of("architecture-beta\ngroup api(cloud)[API]\nservice db(database)[Database] in api\nservice server(server)[Server] in api\nservice gw(internet)[Gateway]\njunction j\ndb:L -- R:server\nserver:T -- B:j\ngw:R --> L:server");
        check(count(ar, "class=\"node\"") == 4 && count(ar, "class=\"group\"") == 1 && count(ar, "class=\"edge\"") == 3, "architecture/items");
        check(has(ar, ">Database<") && has(ar, ">API<"), "architecture/labels");
        check(count(ar, "<polygon") == 1, "architecture/one_arrow");
        check(!has(svg_of("architecture-beta\nservice a[A]\nservice b[B]\nservice c[C]\na:R -- L:b\nb:B -- T:c\nc{group}:R -- L:a"), "ERROR"), "architecture/group_flag_parses");
        check(has(svg_of("architecture-beta\nservice a[A]\na:R -- L:b"), "ERROR"), "architecture/unknown_service");
        check(has(svg_of("architecture-beta\nservice a[A] in nope"), "ERROR"), "architecture/unknown_group");
        check(has(svg_of("architecture-beta\nservice a[A]\nservice b[B]\na:X -- L:b"), "ERROR"), "architecture/bad_side");
        check(has(svg_of("architecture-beta\nservice a[A]\nservice b[B]"), "class=\"node\""), "architecture/no_edges");
    }

    // --- C4 ---
    {
        std::string c4 = svg_of("C4Context\ntitle Banking\nPerson(cust, \"Customer\", \"A customer, with a, comma\")\nEnterprise_Boundary(b0, \"Bank\") {\nSystem(sys, \"Internet Banking\", \"Lets customers pay\")\nSystemDb(db, \"Ledger\")\n}\nSystem_Ext(mail, \"Mail\")\nRel(cust, sys, \"Uses\", \"HTTPS\")\nRel(sys, db, \"Reads\")\nBiRel(sys, mail, \"Sends\")\nUpdateRelStyle(cust, sys, $textColor=\"red\")");
        check(count(c4, "class=\"node\"") == 4 && count(c4, "class=\"boundary\"") == 1 && count(c4, "class=\"edge\"") == 3, "c4/items");
        check(has(c4, ">Internet Banking<") && has(c4, ">[Person]<") && has(c4, ">[Software System]<") && has(c4, ">[HTTPS]<") && has(c4, ">Banking<"), "c4/labels");
        check(has(c4, "[Bank]") == false && has(c4, ">[ENTERPRISE]<"), "c4/boundary_type");
        check(has(svg_of("C4Container\nContainer(web, \"Web\", \"React\", \"UI\")\nContainerDb(d, \"DB\", \"Postgres\")\nRel(web, d, \"Reads\")"), ">[Container: React]<"), "c4/container_techn");
        check(has(svg_of("C4Dynamic\nContainer(a, \"A\")\nContainer(b, \"B\")\nRelIndex(1, a, b, \"call\")"), ">1: call<"), "c4/dynamic_index");
        check(has(svg_of("C4Context\nSystem(a, \"A\")\nRel(a, nope, \"x\")"), "ERROR"), "c4/unknown_element");
        check(has(svg_of("C4Context\nEnterprise_Boundary(b, \"B\") {\nSystem(a, \"A\")"), "ERROR"), "c4/unclosed_boundary");
        check(has(svg_of("C4Context\nWidget(a, \"A\")"), "ERROR"), "c4/unknown_kind");
    }

    // --- treeView, ishikawa, venn ---
    {
        std::string tv = svg_of("treeView-beta\n  \"packages\"\n    \"core\"\n      \"src\"\n    \"cli\"\n  \"README.md\"");
        check(has(tv, ">packages<") && has(tv, ">src<") && has(tv, ">README.md<"), "treeview/labels");
        check(count(tv, "<path") == 5, "treeview/icons");
        check(has(svg_of("treeView-beta"), "ERROR"), "treeview/empty");
        std::string ik = svg_of("ishikawa-beta\n  Blurry photo\n    Process\n      Out of focus\n      Slow shutter\n    User\n      Shaky hands\n    Equipment");
        check(has(ik, ">Blurry photo<") && has(ik, ">Out of focus<") && has(ik, ">Equipment<"), "ishikawa/labels");
        check(count(ik, "stroke-width=\"2\" stroke-linecap") == 3, "ishikawa/bones");
        check(has(svg_of("ishikawa-beta\n  A\nB"), "ERROR"), "ishikawa/one_effect");
        check(has(svg_of("ishikawa-beta"), "ERROR"), "ishikawa/empty");
        std::string vn = svg_of("venn-beta\n  title Overlap\n  set A[\"Frontend\"]:20\n  set B[\"Backend\"]:30\n  union A,B[\"Fullstack\"]\n    text t1[\"TypeScript\"]");
        check(count(vn, "class=\"set\"") == 2 && has(vn, ">Fullstack<") && has(vn, ">TypeScript<") && has(vn, ">Overlap<"), "venn/two_sets");
        check(count(svg_of("venn-beta\n  set A\n  set B\n  set C\n  union A,B,C[\"All\"]"), "class=\"set\"") == 3, "venn/three_sets");
        check(has(svg_of("venn-beta\n  set A\n  union A,Z"), "ERROR"), "venn/unknown_set");
        check(has(svg_of("venn-beta\n  text x"), "ERROR"), "venn/text_without_owner");
    }

    // --- accTitle / accDescr ---
    {
        std::string a = svg_of("graph TD\naccTitle: Order flow\naccDescr: A leads to B\nA-->B");
        check(has(a, "<title>Order flow</title>") && has(a, "<desc>A leads to B</desc>") && count(a, "class=\"edge\"") == 1, "acc/single_line");
        std::string m = svg_of("sequenceDiagram\naccTitle Sign in\naccDescr {\nThe user signs in.\nThen sees the home page.\n}\nA->>B: hi");
        check(has(m, "<title>Sign in</title>") && has(m, "<desc>The user signs in.\nThen sees the home page.</desc>") && has(m, ">hi<"), "acc/multi_line");
        check(has(svg_of("pie\naccTitle: <Pets> & more\n\"a\": 1"), "<title>&lt;Pets&gt; &amp; more</title>"), "acc/escaped");
        check(!has(svg_of("graph TD\nA-->B"), "<title>"), "acc/absent_means_no_title");
        check(has(svg_of("stateDiagram-v2\naccDescr: only a description\nA --> B"), "<desc>only a description</desc>"), "acc/descr_only");
    }

    // --- named themes ---
    {
        std::string svg, err;
        mermaid::Options o;
        check(mermaid::apply_theme("Nord", o) && o.bg == "#2e3440" && o.accent == "#88c0d0", "theme/nord");
        check(mermaid::apply_theme("solarized_dark", o) && o.bg == "#002b36", "theme/underscore_name");
        check(mermaid::apply_theme("default", o) && o.bg == "#ffffff", "theme/default_is_light");
        mermaid::Options before = o;
        check(!mermaid::apply_theme("no-such-theme", o) && o.bg == before.bg, "theme/unknown_leaves_options");
        check(mermaid::theme_names().size() >= 10, "theme/names");
        mermaid::Options on;
        on.allow_directives = true;
        check(mermaid::render("%%{init: {\"theme\":\"dracula\"}}%%\ngraph TD\nA-->B", on, svg, err) && has(svg, "#343746"), "theme/init_directive_preset");
        check(mermaid::render("%%{init: {\"theme\":\"dracula\",\"themeVariables\":{\"primaryColor\":\"#abcdef\"}}}%%\ngraph TD\nA-->B", on, svg, err) && has(svg, "#abcdef") && !has(svg, "#343746"), "theme/variables_override_preset");
    }

    // --- modes: strict, compat, recover ---
    {
        std::string svg, err;
        std::vector<std::string> warn;
        mermaid::Options compat, recover;
        compat.mode = mermaid::Options::Mode::Compat;
        recover.mode = mermaid::Options::Mode::Recover;
        check(!render("flowchrt TD\nA-->B", svg, err) && has(err, "did you mean `flowchart`"), "mode/strict_hint");
        check(!render("Flowchart TD\nA-->B", svg, err), "mode/strict_case");
        check(mermaid::render("Flowchart TD\nA-->B", compat, svg, err, &warn) && has(svg, "recovered") && warn.size() == 1, "mode/compat_case");
        check(!mermaid::render("flowchrt TD\nA-->B", compat, svg, err), "mode/compat_no_typos");
        check(mermaid::render("flowchrt TD\nA-->B", recover, svg, err) && count(svg, "class=\"edge\"") == 1, "mode/recover_typo");
        check(mermaid::render("\xEF\xBB\xBFgraph TD\r\nA-->B\r\n", compat, svg, err), "mode/compat_bom_crlf");
        check(!render("stateDiagram-v2\nstate X {\nA --> B", svg, err) && has(err, "unclosed"), "mode/strict_unclosed");
        warn.clear();
        check(mermaid::render("stateDiagram-v2\nstate X {\nA --> B", compat, svg, err, &warn) && has(svg, "class=\"composite\"") && has(warn[0], "missing `}`"), "mode/compat_closes_block");
        warn.clear();
        check(!mermaid::render("pie\n\"a\" : 1\nnonsense\n\"b\" : 2", compat, svg, err), "mode/compat_keeps_bad_line_an_error");
        check(mermaid::render("pie\n\"a\" : 1\nnonsense\n\"b\" : 2", recover, svg, err, &warn) && count(svg, "class=\"slice\"") == 2 && has(warn[0], "skipped `nonsense`"), "mode/recover_skips_line");
        check(!mermaid::render("zzzzzz\nA-->B", recover, svg, err) && has(err, "unsupported"), "mode/recover_gives_up");
    }

    // --- errors ---
    {
        std::string svg, err;
        check(!render("zenuml", svg, err) && has(err, "unsupported"), "error/unsupported");
        check(!render("", svg, err), "error/empty");
        check(!render("graph TD\nA -->", svg, err), "error/dangling_edge");
    }

    // --- layout ---
    {
        auto r = mermaid::layout_layered({box(60, 30), box(60, 30)}, {edge(0, 1)}, Direction::TD);
        check(r.center[1].y > r.center[0].y + 30, "layout/td_goes_down");
        check(std::fabs(r.center[1].x - r.center[0].x) < 0.5, "layout/td_aligned");
        check(r.points[0].size() == 2, "layout/straight_edge_has_two_points");
        auto lr = mermaid::layout_layered({box(60, 30), box(60, 30)}, {edge(0, 1)}, Direction::LR);
        check(lr.center[1].x > lr.center[0].x + 60, "layout/lr_goes_right");
        auto bt = mermaid::layout_layered({box(60, 30), box(60, 30)}, {edge(0, 1)}, Direction::BT);
        check(bt.center[1].y < bt.center[0].y - 30, "layout/bt_goes_up");
        auto rl = mermaid::layout_layered({box(60, 30), box(60, 30)}, {edge(0, 1)}, Direction::RL);
        check(rl.center[1].x < rl.center[0].x - 60, "layout/rl_goes_left");
        check(r.center[0].x - 30 >= -0.01 && r.center[0].y - 15 >= -0.01, "layout/origin_is_top_left");
    }
    {
        // a -> b -> c and a -> c: the long edge must not run through b
        auto r = mermaid::layout_layered({box(60, 30), box(60, 30), box(60, 30)}, {edge(0, 1), edge(1, 2), edge(0, 2)}, Direction::TD);
        bool through = false;
        double bl = r.center[1].x - 30, br = r.center[1].x + 30, bt = r.center[1].y - 15, bb = r.center[1].y + 15;
        const auto& pts = r.points[2];
        for (size_t i = 0; i + 1 < pts.size(); i++) {
            double x = pts[i].x, y0 = std::min(pts[i].y, pts[i + 1].y), y1 = std::max(pts[i].y, pts[i + 1].y);
            if (std::fabs(pts[i].x - pts[i + 1].x) < 0.01 && x > bl && x < br && y1 > bt && y0 < bb) through = true;
        }
        check(!through, "layout/long_edge_avoids_node");
        check(r.center[2].y > r.center[1].y && r.center[1].y > r.center[0].y, "layout/three_layers");
    }
    {
        // a cycle: every edge is routed, the layout terminates, the nodes get different layers
        auto r = mermaid::layout_layered({box(60, 30), box(60, 30), box(60, 30)}, {edge(0, 1), edge(1, 2), edge(2, 0)}, Direction::TD);
        check(r.points[2].size() >= 2, "layout/cycle_edge_routed");
        check(r.center[0].y != r.center[2].y, "layout/cycle_layers");
        // the reversed edge still runs from its `from` node to its `to` node
        check(std::fabs(r.points[2].front().y - r.center[2].y) < 40 && std::fabs(r.points[2].back().y - r.center[0].y) < 40,
              "layout/cycle_edge_direction");
    }
    {
        auto r = mermaid::layout_layered({box(60, 30)}, {edge(0, 0)}, Direction::TD);
        check(r.points[0].size() == 4, "layout/self_loop");
        // siblings do not overlap
        auto s = mermaid::layout_layered({box(60, 30), box(80, 30), box(70, 30)}, {edge(0, 1), edge(0, 2)}, Direction::TD);
        check(std::fabs(s.center[1].x - s.center[2].x) >= (80 + 70) / 2.0, "layout/siblings_do_not_overlap");
        check(std::fabs(s.center[0].x - (s.center[1].x + s.center[2].x) / 2) < 30, "layout/parent_between_children");
    }
    {
        LayoutEdge e = edge(0, 1);
        e.label_w = 40;
        e.label_h = 30;
        auto lab = mermaid::layout_layered({box(60, 30), box(60, 30)}, {e}, Direction::TD);
        auto plain = mermaid::layout_layered({box(60, 30), box(60, 30)}, {edge(0, 1)}, Direction::TD);
        check(lab.center[1].y - lab.center[0].y > plain.center[1].y - plain.center[0].y + 29, "layout/label_gets_room");
    }
    {
        // a group keeps its members together, and nodes outside it stay out of its box
        std::vector<LayoutNode> ns(6, box(60, 30));
        ns[1].group = ns[2].group = ns[4].group = 0;
        auto r = mermaid::layout_layered(ns, {edge(0, 1), edge(0, 2), edge(0, 3), edge(1, 4), edge(3, 5), edge(2, 4)}, Direction::TD, {-1});
        double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
        for (int v : {1, 2, 4}) {
            x0 = std::min(x0, r.center[v].x - 30);
            x1 = std::max(x1, r.center[v].x + 30);
            y0 = std::min(y0, r.center[v].y - 15);
            y1 = std::max(y1, r.center[v].y + 15);
        }
        for (int v : {0, 3, 5}) {
            bool inside = r.center[v].x + 30 > x0 && r.center[v].x - 30 < x1 && r.center[v].y + 15 > y0 && r.center[v].y - 15 < y1;
            check(!inside, "layout/group_excludes_outsiders_" + std::to_string(v));
        }
    }
    {
        // labels in the same gap do not overlap
        LayoutEdge e1 = edge(0, 1), e2 = edge(0, 2);
        e1.label_w = e2.label_w = 80;
        e1.label_h = e2.label_h = 24;
        auto r = mermaid::layout_layered({box(60, 30), box(60, 30), box(60, 30)}, {e1, e2}, Direction::TD);
        double dx = std::fabs(r.label_center[0].x - r.label_center[1].x), dy = std::fabs(r.label_center[0].y - r.label_center[1].y);
        check(dx >= 80 || dy >= 24, "layout/labels_do_not_overlap");
    }
    {
        check(has(svg_of("graph TD\nA[\"a < b & c\"]"), "a &lt; b &amp; c"), "svg/escapes_text");
        std::string svg = svg_of("graph TD\nA-->B");
        check(!has(svg, "var(") && !has(svg, "color-mix"), "svg/plain_colours_only");
    }

    // --- sequence diagrams ---
    {
        const std::string seq = "sequenceDiagram\nparticipant A as Alice\nA->>B: hello\nB-->>A: back\n";
        std::string svg = svg_of(seq);
        check(has(svg, ">Alice<") && has(svg, ">B<"), "sequence/participants");
        check(count(svg, "<polygon") == 2, "sequence/arrow_heads");
        check(has(svg, ">hello<") && has(svg, "stroke-dasharray=\"6 4\""), "sequence/labels_and_dashed_reply");
        check(count(svg, ">Alice<") == 2, "sequence/boxes_top_and_bottom");
        check(count(svg_of("sequenceDiagram\nA->B: x"), "<polygon") == 0, "sequence/open_arrow_has_no_head");
        check(count(svg_of("sequenceDiagram\nA-xB: x"), "<line") >= 4, "sequence/cross");
        check(has(svg_of("sequenceDiagram\nA->>A: self"), "<polyline"), "sequence/self_message");
        check(has(svg_of("sequenceDiagram\nautonumber\nA->>B: x\nB->>A: y"), ">2. y<"), "sequence/autonumber");
        check(has(svg_of("sequenceDiagram\nA->>+B: x\nB-->>-A: y"), "width=\"8\""), "sequence/activation_bar");
        check(has(svg_of("sequenceDiagram\nNote over A,B: careful\nA->>B: x"), ">careful<"), "sequence/note");
        std::string blk = svg_of("sequenceDiagram\nloop every day\nA->>B: x\nalt ok\nB->>A: y\nelse bad\nB->>A: z\nend\nend");
        check(has(blk, ">loop<") && has(blk, ">[every day]<") && has(blk, ">alt<") && has(blk, ">[bad]<"), "sequence/blocks");
        check(has(svg_of("sequenceDiagram\nA->>B: \xe6\x97\xa5\xe6\x9c\xac"), ">\xe6\x97\xa5\xe6\x9c\xac<"), "sequence/cjk");
        check(!has(blk, "var(") && !has(blk, "color-mix"), "sequence/plain_colours_only");
        std::string e1, e2;
        check(!render("sequenceDiagram\nend", e1, e2) && has(e2, "without a block"), "sequence/error_stray_end");
        check(!render("sequenceDiagram", e1, e2), "sequence/error_empty");
    }

    // --- state diagrams ---
    {
        std::string s = svg_of("stateDiagram-v2\n[*] --> Idle\nIdle --> Busy : start\nBusy --> [*]");
        check(count(s, "class=\"node\"") == 4, "state/nodes");
        check(count(s, "class=\"edge\"") == 3 && has(s, ">start<"), "state/edges_and_label");
        check(has(s, ">Idle<") && has(s, ">Busy<"), "state/names");
        check(has(svg_of("stateDiagram\nstate \"Long name\" as L\n[*] --> L"), ">Long name<"), "state/alias");
        check(has(svg_of("stateDiagram-v2\nA : first\nA : second"), ">second<"), "state/description");
        check(has(svg_of("stateDiagram-v2\nstate X {\n[*] --> In\n}\n[*] --> X"), "class=\"composite\""), "state/composite");
        check(has(svg_of("stateDiagram-v2\nstate c <<choice>>\nA --> c\nc --> B"), "<polygon points"), "state/choice");
        check(has(svg_of("stateDiagram-v2\ndirection LR\nA --> B"), "<svg"), "state/direction");
        check(has(svg_of("stateDiagram-v2\nA --> B : \xe6\x97\xa5\xe6\x9c\xac"), ">\xe6\x97\xa5\xe6\x9c\xac<"), "state/cjk");
        std::string e1, e2;
        check(!render("stateDiagram-v2\nstate X {\nA --> B", e1, e2), "state/error_unclosed");
        check(!render("stateDiagram-v2", e1, e2), "state/error_empty");
    }

    // --- class diagrams ---
    {
        std::string s = svg_of("classDiagram\nclass Animal {\n  +String name\n  +eat() void\n}\nAnimal <|-- Dog\nDog : +bark()");
        check(count(s, "class=\"node\"") == 2, "class/nodes");
        check(has(s, ">+String name<") && has(s, ">+eat() void<") && has(s, ">+bark()<"), "class/members");
        check(count(s, "class=\"edge\"") == 1 && has(s, "<polygon"), "class/inheritance_triangle");
        check(has(svg_of("classDiagram\nA *-- B"), "<polygon") && has(svg_of("classDiagram\nA o-- B"), "<polygon"), "class/diamonds");
        check(has(svg_of("classDiagram\nA ..> B"), "stroke-dasharray=\"5 4\""), "class/dashed");
        check(has(svg_of("classDiagram\nA \"1\" --> \"*\" B : owns"), ">owns<") && has(svg_of("classDiagram\nA \"1\" --> \"*\" B"), ">*<"), "class/label_and_cardinality");
        check(has(svg_of("classDiagram\nclass I\n<<interface>> I"), ">&lt;&lt;interface&gt;&gt;<"), "class/annotation");
        check(has(svg_of("classDiagram\nA --|> B"), "<polygon"), "class/marker_at_end");
        check(has(svg_of("classDiagram\nclass \xe6\x97\xa5\xe6\x9c\xac"), ">\xe6\x97\xa5\xe6\x9c\xac<"), "class/cjk");
        std::string e1, e2;
        check(!render("classDiagram\nclass A {\n+x", e1, e2), "class/error_unclosed");
        check(!render("classDiagram\nnonsense here", e1, e2), "class/error_unrecognised");
    }

    // --- ER diagrams ---
    {
        std::string s = svg_of("erDiagram\nCUSTOMER ||--o{ ORDER : places\nCUSTOMER {\n  string name PK\n  string email UK \"login\"\n}");
        check(count(s, "class=\"node\"") == 2, "er/entities");
        check(count(s, "class=\"edge\"") == 1 && has(s, ">places<"), "er/relation_and_label");
        check(has(s, ">name<") && has(s, ">email<") && has(s, ">string<") && has(s, ">PK<") && has(s, ">UK<"), "er/attributes");
        check(has(s, "<circle") && count(s, "<line") >= 5, "er/crows_foot");
        check(has(svg_of("erDiagram\nA ||..|{ B : x"), "stroke-dasharray=\"5 4\""), "er/non_identifying_dashed");
        check(has(svg_of("erDiagram\nA }|--|| B"), "<line"), "er/many_to_one");
        check(has(svg_of("erDiagram\nA |o--o| B"), "<circle"), "er/zero_or_one");
        check(has(svg_of("erDiagram\nA[\"Alias\"] ||--|| B"), ">Alias<"), "er/alias");
        check(has(svg_of("erDiagram\nA ||--|| B : \"\xe6\x97\xa5\xe6\x9c\xac\""), ">\xe6\x97\xa5\xe6\x9c\xac<"), "er/cjk_label");
        std::string e1, e2;
        check(!render("erDiagram\nA {\nstring", e1, e2), "er/error_unclosed");
        check(!render("erDiagram\nA xx--|| B", e1, e2), "er/error_unknown_cardinality");
        check(!render("erDiagram", e1, e2), "er/error_empty");
    }

    // --- xychart ---
    {
        std::string s = svg_of("xychart-beta\n  title \"Sales\"\n  x-axis [jan, feb, mar]\n  y-axis \"Revenue\" 0 --> 100\n  bar [10, 50, 90]\n  line [20, 40, 60]");
        check(has(s, ">Sales<") && has(s, ">jan<") && has(s, ">mar<") && has(s, ">Revenue<"), "xychart/titles_and_categories");
        check(count(s, "<rect x=") >= 3 && has(s, "fill-opacity"), "xychart/bars");
        check(has(s, "class=\"series\"") && count(s, "<circle") == 3, "xychart/line_points");
        check(has(s, ">100<") && has(s, ">0<"), "xychart/explicit_range_ticks");
        check(has(svg_of("xychart-beta\nx-axis \"n\" 1 --> 3\nline [1, 2, 3]"), ">2<"), "xychart/numeric_x_axis");
        check(has(svg_of("xychart-beta\nx-axis [\xe6\x97\xa5, \xe6\x9c\x88]\nbar [1, 2]"), ">\xe6\x97\xa5<"), "xychart/cjk");
        check(has(svg_of("xychart-beta\nbar [3, -2]"), "<rect"), "xychart/negative_values");
        std::string e1, e2;
        check(!render("xychart-beta\nx-axis [a]", e1, e2), "xychart/error_no_series");
        check(!render("xychart-beta\nbar [a, b]", e1, e2), "xychart/error_not_a_number");
        check(!render("xychart-beta horizontal\nbar [1]", e1, e2), "xychart/error_horizontal");
    }

    // --- pie ---
    {
        std::string s = svg_of("pie showData\n  title Pets\n  \"Dogs\" : 386\n  \"Cats\" : 85.5\n  \"Rats\" : 15");
        check(has(s, ">Pets<") && count(s, "class=\"slice\"") == 3, "pie/slices_and_title");
        check(has(s, ">Dogs [386]<") && has(s, ">Cats [85.5]<"), "pie/show_data_legend");
        check(has(s, ">79.3%<"), "pie/percent_label");
        check(count(svg_of("pie\n\"A\" : 1"), "<circle") == 1, "pie/single_slice_is_a_circle");
        check(has(svg_of("pie title \xe6\x97\xa5\xe6\x9c\xac\n\"\xe7\x8c\xab\" : 1\n\"b\" : 1"), ">\xe7\x8c\xab<"), "pie/cjk");
        check(!has(s, "var(") && !has(s, "color-mix"), "pie/plain_colours_only");
        std::string e1, e2;
        check(!render("pie\n\"A\" : x", e1, e2), "pie/error_not_a_number");
        check(!render("pie\n\"A\" : 0", e1, e2), "pie/error_zero_total");
        check(!render("pie", e1, e2), "pie/error_empty");
    }
    check(has(svg_of("graph LR\nA[/para/]"), "<polygon") && has(svg_of("graph LR\nA[\\para\\]"), "<polygon"), "shape/parallelogram");

    // --- gantt ---
    {
        std::string s = svg_of("gantt\n  title Plan\n  dateFormat YYYY-MM-DD\n  section A\n  Design :a1, 2024-01-01, 10d\n  Build :after a1, 20d\n  Ship :milestone, m1, 2024-02-01, 0d\n  section B\n  Docs :crit, 2024-01-05, 2w");
        check(has(s, ">Plan<") && has(s, ">Design<") && has(s, ">Build<") && has(s, ">Docs<"), "gantt/title_and_tasks");
        check(has(s, ">A<") && has(s, ">B<"), "gantt/sections");
        check(count(s, "class=\"task\"") == 3, "gantt/bars");
        check(has(s, "#e5484d"), "gantt/crit_colour");
        check(has(s, "<polygon"), "gantt/milestone");
        check(has(s, ">2024-01-01<"), "gantt/axis_labels");
        check(has(svg_of("gantt\n  axisFormat %b %e\n  T :2024-03-01, 5d"), ">Mar 1<"), "gantt/axis_format");
        check(has(svg_of("gantt\n  A :a, 2024-01-01, 2024-01-10\n  B :after a, 3d"), ">B<"), "gantt/end_date");
        check(has(svg_of("gantt\n  \xe8\xa8\xad\xe8\xa8\x88 :2024-01-01, 3d"), ">\xe8\xa8\xad\xe8\xa8\x88<"), "gantt/cjk");
        std::string e1, e2;
        check(!render("gantt\n  A :after nope, 3d", e1, e2) && has(e2, "unknown task id"), "gantt/error_unknown_id");
        check(!render("gantt\n  A :3d", e1, e2), "gantt/error_first_task_without_start");
        check(!render("gantt\n  A :2024-01-01, soon", e1, e2), "gantt/error_bad_length");
        check(!render("gantt\n  title x", e1, e2), "gantt/error_no_tasks");
    }

    // --- mindmap ---
    {
        std::string s = svg_of("mindmap\n  root((Centre))\n    A\n      A1\n      A2\n    B[Square]\n    C(Round)\n    D{{Hex}}\n");
        check(count(s, "class=\"node\"") == 7, "mindmap/nodes");
        check(count(s, "class=\"edge\"") == 6, "mindmap/connectors");
        check(has(s, ">Centre<") && has(s, ">Square<") && has(s, ">Round<") && has(s, ">Hex<"), "mindmap/shapes_use_the_text_only");
        check(has(s, "<ellipse") && has(s, "<polygon"), "mindmap/circle_and_hexagon");
        check(has(svg_of("mindmap\n  r\n    \xe6\x97\xa5\xe6\x9c\xac"), ">\xe6\x97\xa5\xe6\x9c\xac<"), "mindmap/cjk");
        check(has(svg_of("mindmap\n  r\n    a\n    ::icon(fa fa-book)\n    b"), ">b<"), "mindmap/icon_lines_are_skipped");
        check(!has(s, "var(") && !has(s, "color-mix"), "mindmap/plain_colours_only");
        std::string e1, e2;
        check(!render("mindmap\n  a\n  b", e1, e2) && has(e2, "one root"), "mindmap/error_two_roots");
        check(!render("mindmap", e1, e2), "mindmap/error_empty");
    }

    // --- timeline ---
    {
        std::string s = svg_of("timeline\n  title History\n  section Early\n  2002 : LinkedIn\n  2004 : Facebook : Google\n  : Twitter\n  section Late\n  2010 : Instagram");
        check(has(s, ">History<") && has(s, ">Early<") && has(s, ">Late<"), "timeline/title_and_sections");
        check(count(s, "class=\"period\"") == 3, "timeline/periods");
        check(count(s, "class=\"event\"") == 5, "timeline/events_including_continuation");
        check(has(s, ">Twitter<") && has(s, ">Instagram<"), "timeline/event_text");
        check(count(svg_of("timeline\n  2000 : a\n  2001"), "class=\"period\"") == 2, "timeline/period_without_event");
        check(has(svg_of("timeline\n  \xe6\x97\xa5\xe6\x9c\xac : \xe6\x9d\xb1\xe4\xba\xac"), ">\xe6\x9d\xb1\xe4\xba\xac<"), "timeline/cjk");
        check(!has(s, "var(") && !has(s, "color-mix"), "timeline/plain_colours_only");
        std::string e1, e2;
        check(!render("timeline\n  : orphan", e1, e2), "timeline/error_event_without_period");
        check(!render("timeline", e1, e2), "timeline/error_empty");
    }

    // --- journey ---
    {
        std::string s = svg_of("journey\n  title Day\n  section Work\n    Make tea: 5: Me\n    Go upstairs: 3: Me, Cat\n  section Home\n    Sit down: 1: Me");
        check(has(s, ">Day<") && has(s, ">Work<") && has(s, ">Home<"), "journey/title_and_sections");
        check(count(s, "class=\"task\"") == 3 && count(s, "class=\"face\"") == 3, "journey/tasks_and_faces");
        check(has(s, ">Me<") && has(s, ">Cat<"), "journey/actor_legend");
        check(has(svg_of("journey\n  A: 4"), ">A<"), "journey/actors_are_optional");
        check(has(svg_of("journey\n  \xe4\xbd\x9c\xe6\xa5\xad: 3: \xe7\xa7\x81"), ">\xe4\xbd\x9c\xe6\xa5\xad<"), "journey/cjk");
        std::string e1, e2;
        check(!render("journey\n  A: 9", e1, e2), "journey/error_score_out_of_range");
        check(!render("journey\n  A", e1, e2), "journey/error_no_score");
        check(!render("journey", e1, e2), "journey/error_empty");
    }

    // --- quadrantChart ---
    {
        std::string s = svg_of("quadrantChart\n  title Reach\n  x-axis Low --> High\n  y-axis Cold --> Hot\n  quadrant-1 Expand\n  quadrant-3 Rethink\n  A: [0.3, 0.6]\n  B: [0.9, 0.1]");
        check(has(s, ">Reach<") && has(s, ">Low<") && has(s, ">High<") && has(s, ">Cold<") && has(s, ">Hot<"), "quadrant/titles_and_axes");
        check(has(s, ">Expand<") && has(s, ">Rethink<"), "quadrant/quadrant_names");
        check(count(s, "class=\"point\"") == 2 && has(s, ">A<") && has(s, ">B<"), "quadrant/points");
        check(has(svg_of("quadrantChart\n  \xe7\x82\xb9: [0.5, 0.5]"), ">\xe7\x82\xb9<"), "quadrant/cjk");
        std::string e1, e2;
        check(!render("quadrantChart\n  A: [2, 0.5]", e1, e2), "quadrant/error_out_of_range");
        check(!render("quadrantChart\n  nonsense", e1, e2), "quadrant/error_unrecognised");
    }

    // --- gitGraph ---
    {
        std::string s = svg_of("gitGraph\n  commit id: \"A\"\n  branch dev\n  checkout dev\n  commit id: \"B\" tag: \"v0\"\n  checkout main\n  merge dev id: \"M\"\n  commit type: HIGHLIGHT");
        check(count(s, "class=\"commit\"") == 4, "git/commits");
        check(count(s, "class=\"branch\"") == 2 && has(s, ">main<") && has(s, ">dev<"), "git/branches");
        check(has(s, ">A<") && has(s, ">B<") && has(s, ">M<") && has(s, ">v0<"), "git/ids_and_tags");
        check(has(s, "<path d=\""), "git/lane_change_is_a_curve");
        check(has(svg_of("gitGraph\n  commit\n  commit type: REVERSE"), "stroke=\"#ffffff\""), "git/reverse_cross");
        check(has(svg_of("gitGraph\n  commit id: \"\xe6\x97\xa5\xe6\x9c\xac\""), ">\xe6\x97\xa5\xe6\x9c\xac<"), "git/cjk");
        std::string e1, e2;
        check(!render("gitGraph\n  checkout nope", e1, e2), "git/error_unknown_branch");
        check(!render("gitGraph\n  commit\n  branch a\n  branch a", e1, e2), "git/error_duplicate_branch");
        check(!render("gitGraph\n  commit\n  merge main", e1, e2), "git/error_merge_into_itself");
        check(!render("gitGraph", e1, e2), "git/error_empty");
    }

    // --- kanban ---
    {
        std::string s = svg_of("kanban\n  Todo\n    [Write docs]\n    t1[Fix bug]@{ ticket: 'MC-1', assigned: 'ann', priority: 'High' }\n  [Done]\n    d1[Ship it]");
        check(count(s, "class=\"column\"") == 2 && has(s, ">Todo<") && has(s, ">Done<"), "kanban/columns");
        check(count(s, "class=\"card\"") == 3, "kanban/cards");
        check(has(s, ">Write docs<") && has(s, ">Fix bug<") && has(s, ">Ship it<"), "kanban/card_text");
        check(has(s, ">MC-1<") && has(s, ">ann<"), "kanban/ticket_and_assignee");
        check(has(s, "#f28e2b"), "kanban/priority_mark");
        check(has(svg_of("kanban\n  \xe5\x88\x97\n    \xe3\x82\xbf\xe3\x82\xb9\xe3\x82\xaf"), ">\xe5\x88\x97<"), "kanban/cjk");
        std::string e1, e2;
        check(!render("kanban", e1, e2), "kanban/error_empty");
    }

    // --- packet ---
    {
        std::string s = svg_of("packet-beta\n  title TCP\n  0-15: \"Source Port\"\n  16-31: \"Destination Port\"\n  32-63: \"Sequence Number\"\n  64-71: \"Flag\"");
        check(has(s, ">TCP<") && has(s, ">Source Port<") && has(s, ">Sequence Number<") && has(s, ">Flag<"), "packet/title_and_fields");
        check(count(s, "class=\"field\"") == 4, "packet/fields");
        check(count(svg_of("packet-beta\n  0-39: \"Wide\""), "class=\"field\"") == 2, "packet/field_wraps_over_rows");
        check(has(s, ">15<") && has(s, ">16<"), "packet/bit_numbers");
        check(has(svg_of("packet\n  0-7: \"\xe3\x83\x98\xe3\x83\x83\xe3\x83\x80\""), ">\xe3\x83\x98\xe3\x83\x83\xe3\x83\x80<"), "packet/cjk_and_plain_keyword");
        std::string e1, e2;
        check(!render("packet-beta\n  0-7: \"a\"\n  10-15: \"b\"", e1, e2) && has(e2, "without gaps"), "packet/error_gap");
        check(!render("packet-beta\n  x: \"a\"", e1, e2), "packet/error_bad_range");
        check(!render("packet-beta", e1, e2), "packet/error_empty");
    }

    // --- sankey ---
    {
        std::string s = svg_of("sankey-beta\n\nA,B,10\nA,C,5\nB,D,10\n\"x, y\",D,2");
        check(count(s, "class=\"flow\"") == 4, "sankey/flows");
        check(count(s, "class=\"node\"") == 5, "sankey/nodes");
        check(has(s, ">A  15<") && has(s, ">D  12<"), "sankey/node_values");
        check(has(s, ">x, y  2<"), "sankey/quoted_name");
        check(has(svg_of("sankey\n\xe5\x85\xa5,\xe5\x87\xba,3"), ">\xe5\x85\xa5  3<"), "sankey/cjk_and_plain_keyword");
        std::string e1, e2;
        check(!render("sankey-beta\nA,B", e1, e2), "sankey/error_missing_value");
        check(!render("sankey-beta\nA,B,-1", e1, e2), "sankey/error_negative");
        check(!render("sankey-beta\nA,A,1", e1, e2), "sankey/error_self_flow");
        check(!render("sankey-beta", e1, e2), "sankey/error_empty");
    }

    // --- requirementDiagram ---
    {
        std::string s = svg_of("requirementDiagram\n  requirement req {\n    id: 1\n    text: the test text.\n    risk: high\n    verifymethod: test\n  }\n  element ent {\n    type: simulation\n  }\n  ent - satisfies -> req\n  req <- contains - ent");
        check(count(s, "class=\"node\"") == 2 && has(s, ">req<") && has(s, ">ent<"), "requirement/nodes");
        check(has(s, ">&lt;&lt;Requirement&gt;&gt;<") && has(s, ">&lt;&lt;Element&gt;&gt;<"), "requirement/stereotypes");
        check(has(s, ">Id: 1<") && has(s, ">Text: the test text.<") && has(s, ">Type: simulation<"), "requirement/fields");
        check(has(s, ">Risk: High<") && has(s, ">Verification: Test<"), "requirement/capitalised_enum_values");
        check(count(s, "class=\"edge\"") == 2 && has(s, ">&lt;&lt;satisfies&gt;&gt;<") && has(s, ">&lt;&lt;contains&gt;&gt;<"), "requirement/relations");
        check(has(s, "stroke-dasharray=\"6 4\""), "requirement/dashed_except_contains");
        std::string e1, e2;
        check(!render("requirementDiagram\n  a - satisfies -> b", e1, e2), "requirement/error_unknown_item");
        check(!render("requirementDiagram\n  requirement r {\n id: 1", e1, e2), "requirement/error_unclosed");
        check(!render("requirementDiagram", e1, e2), "requirement/error_empty");
    }

    // --- block ---
    {
        std::string s = svg_of("block-beta\n  columns 3\n  a b c\n  d:2 e[\"Eee\"]\n  f(\"Round\") g((\"Circle\")) h{\"Dia\"}\n  a --> b\n  b -- \"link\" --> e");
        check(count(s, "class=\"node\"") == 8, "block/blocks");
        check(has(s, ">Eee<") && has(s, ">Round<") && has(s, ">Circle<") && has(s, ">Dia<"), "block/labels");
        check(has(s, "<ellipse") && has(s, "<polygon"), "block/shapes");
        check(count(s, "class=\"edge\"") == 2 && has(s, ">link<"), "block/links");
        check(count(svg_of("block-beta\n  columns 2\n  a space b"), "class=\"node\"") == 2, "block/space_is_empty");
        check(has(svg_of("block\n  \xe5\x9b\xb3[\"\xe6\x97\xa5\xe6\x9c\xac\"]"), ">\xe6\x97\xa5\xe6\x9c\xac<"), "block/cjk_and_plain_keyword");
        std::string e1, e2;
        check(!render("block-beta\n  a --> b", e1, e2), "block/error_unknown_block");
        check(!render("block-beta\n  block:g\n  a\n  end", e1, e2), "block/error_nested");
        check(!render("block-beta\n  columns 0", e1, e2), "block/error_columns");
        check(!render("block-beta", e1, e2), "block/error_empty");
    }

    std::printf("----\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
