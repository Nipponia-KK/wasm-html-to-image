// tests/cpp/test_svg_paint_utils.cpp — host unit tests for core/svg_paint_utils.h
// (color-mix evaluation and marker geometry for inline SVG). Standard library only.

#include <cmath>
#include <cstdio>
#include <string>

#include "core/svg_paint_utils.h"

namespace {

int g_pass = 0;
int g_fail = 0;

void expect_eq(const std::string& actual, const std::string& expected, const char* name) {
    if (actual == expected) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("FAIL %s\n  expected: %s\n  actual:   %s\n", name, expected.c_str(), actual.c_str());
    }
}

void expect_true(bool v, const char* name) { expect_eq(v ? "true" : "false", "true", name); }

std::string mix(const std::string& in) {
    std::string out;
    return litehtml::svgutil::eval_color_functions(in, out) ? out : "<fail>";
}

}  // namespace

int main() {
    using namespace litehtml::svgutil;

    // --- color-mix ---
    expect_eq(mix("color-mix(in srgb, #000000 50%, #ffffff)"), "#808080", "mix/half");
    expect_eq(mix("color-mix(in srgb, #1f2328 60%, #ffffff)"), "#797B7E", "mix/percent");
    expect_eq(mix("color-mix(in srgb, #000 3%, #fff)"), "#F7F7F7", "mix/short_hex");
    expect_eq(mix("color-mix(in srgb, #ff0000, #0000ff)"), "#800080", "mix/no_percent");
    expect_eq(mix("color-mix(in srgb, #ff0000 25%, #0000ff)"), "#4000BF", "mix/second_implied");
    expect_eq(mix("color-mix(in srgb, white 10%, black)"), "#1A1A1A", "mix/keywords");
    expect_eq(mix("color-mix(in srgb, #000000 50%, transparent)"), "rgba(0,0,0,0.5)", "mix/alpha");
    expect_eq(mix("color-mix(in srgb, color-mix(in srgb, #000 50%, #fff) 50%, #fff)"), "#C0C0C0", "mix/nested");
    expect_eq(mix("1px solid color-mix(in srgb, #000 50%, #fff)"), "1px solid #808080", "mix/inside_text");
    expect_eq(mix("#123456"), "#123456", "mix/passthrough");
    expect_eq(mix("color-mix(in srgb, var(--x) 50%, #fff)"), "<fail>", "mix/unresolved_var");
    expect_eq(mix("color-mix(in srgb, #000"), "<fail>", "mix/unbalanced");

    // --- points and angles ---
    auto pts = parse_points("10,20 30,20 30,50");
    expect_true(pts.size() == 3 && pts[2].x == 30 && pts[2].y == 50, "points/parse");
    expect_true(parse_points("1 2 3").size() == 1, "points/odd_count");
    expect_true(std::fabs(segment_angle(pts, false) - 0) < 1e-9, "angle/start_right");
    expect_true(std::fabs(segment_angle(pts, true) - 90) < 1e-9, "angle/end_down");
    std::vector<Point> flat = {{0, 0}, {5, 0}, {5, 0}};
    expect_true(std::fabs(segment_angle(flat, true) - 0) < 1e-9, "angle/skips_zero_length");

    // --- marker placement: beautiful-mermaid's arrowhead (8x5 box, ref 7,2.5, strokeWidth units) ---
    MarkerDef arrow;
    arrow.markerWidth = 8;
    arrow.markerHeight = 5;
    arrow.refX = 7;
    arrow.refY = 2.5;
    expect_eq(marker_transform(arrow, {129.5, 124.9}, 90, 1), "translate(129.5 124.9) rotate(90) scale(1) translate(-7 -2.5)",
              "marker/basic");
    expect_eq(marker_transform(arrow, {0, 0}, 0, 2), "translate(0 0) rotate(0) scale(2) translate(-7 -2.5)", "marker/stroke_scale");
    arrow.stroke_units = false;
    expect_eq(marker_transform(arrow, {0, 0}, 0, 2), "translate(0 0) rotate(0) scale(1) translate(-7 -2.5)", "marker/user_space");
    MarkerDef vb;
    vb.has_viewBox = true;
    vb.vbW = 10;
    vb.vbH = 10;
    vb.markerWidth = 5;
    vb.markerHeight = 5;
    vb.refX = 10;
    vb.refY = 5;
    expect_eq(marker_transform(vb, {1, 2}, 180, 1), "translate(1 2) rotate(180) scale(0.5) translate(-10 -5)", "marker/viewbox_scale");

    std::printf("----\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
