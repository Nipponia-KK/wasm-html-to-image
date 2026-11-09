#pragma once
// Edges and their labels against the frames drawn around groups of nodes (subgraphs, composite states). The layout
// places edges and labels before the frames are known, so a bend or a label can land on a frame's border, where it
// reads as part of the frame. This moves them off, along the edge.

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <vector>

#include "mermaid/layered.h"

namespace mermaid {

/// @p F has x0, y0, x1, y1 and `used`. @p points and @p label_center are LayoutResult's; the label sizes are the
/// LayoutEdge's (empty or 0: no label).
template <class F>
void clear_frame_borders(std::vector<std::vector<Pt>>& points, std::vector<Pt>& label_center, const std::vector<LayoutEdge>& edges, const std::vector<F>& frames) {
    constexpr double kTol = 4, kAway = 8;
    // the border of a frame that a segment lying on axis position `pos` and spanning a0..a1 runs along (NaN: none)
    auto near_border = [&](bool horiz, double pos, double a0, double a1) {
        for (const F& f : frames) {
            if (!f.used) continue;
            double lo = horiz ? f.x0 : f.y0, hi = horiz ? f.x1 : f.y1;
            if (a1 < lo || a0 > hi) continue;
            for (double b : horiz ? std::initializer_list<double>{f.y0, f.y1} : std::initializer_list<double>{f.x0, f.x1})
                if (std::fabs(pos - b) < kTol) return b;
        }
        return std::nan("");
    };

    // a bend that runs along a border: kAway clear of it, outside the frame first, between the points it joins
    for (size_t i = 0; i < points.size(); i++) {
        std::vector<Pt>& p = points[i];
        for (size_t k = 1; k + 2 < p.size(); k++) {
            bool horiz = std::fabs(p[k].y - p[k + 1].y) < 1e-6 && std::fabs(p[k].x - p[k + 1].x) > 1e-6;
            bool vert = std::fabs(p[k].x - p[k + 1].x) < 1e-6 && std::fabs(p[k].y - p[k + 1].y) > 1e-6;
            if (!horiz && !vert) continue;
            auto coord = [&](Pt& q) -> double& { return horiz ? q.y : q.x; };
            double pos = coord(p[k]);
            double a0 = horiz ? std::min(p[k].x, p[k + 1].x) : std::min(p[k].y, p[k + 1].y);
            double a1 = horiz ? std::max(p[k].x, p[k + 1].x) : std::max(p[k].y, p[k + 1].y);
            double border = near_border(horiz, pos, a0, a1);
            if (std::isnan(border)) continue;
            double from = coord(p[k - 1]), to = coord(p[k + 2]);
            double lo = std::min(from, to) + 2, hi = std::max(from, to) - 2;
            for (double cand : {border - kAway, border + kAway, border - 2 * kAway, border + 2 * kAway}) {
                if (cand < lo || cand > hi || !std::isnan(near_border(horiz, cand, a0, a1))) continue;
                coord(p[k]) = coord(p[k + 1]) = cand;
                if (i < label_center.size()) {
                    double& lc = horiz ? label_center[i].y : label_center[i].x;
                    if (std::fabs(lc - pos) < 1.0) lc = cand;
                }
                break;
            }
        }
    }

    // a label whose box touches a border: slide it along the edge until it is kAway clear
    for (size_t i = 0; i < label_center.size() && i < edges.size() && i < points.size(); i++) {
        double w = edges[i].label_w, h = edges[i].label_h;
        if (w <= 0 || h <= 0 || points[i].size() < 2) continue;
        Pt c = label_center[i];
        double xmin = points[i][0].x, xmax = xmin, ymin = points[i][0].y, ymax = ymin;
        for (const Pt& q : points[i]) {
            xmin = std::min(xmin, q.x);
            xmax = std::max(xmax, q.x);
            ymin = std::min(ymin, q.y);
            ymax = std::max(ymax, q.y);
        }
        for (bool horiz : {true, false}) {  // a horizontal border (top, bottom) first, then a vertical one
            double half = horiz ? h / 2 : w / 2, centre = horiz ? c.y : c.x;
            double a0 = horiz ? c.x - w / 2 : c.y - h / 2, a1 = horiz ? c.x + w / 2 : c.y + h / 2;
            double border = std::nan("");
            for (const F& f : frames) {
                if (!f.used) continue;
                double lo = horiz ? f.x0 : f.y0, hi = horiz ? f.x1 : f.y1;
                if (a1 < lo || a0 > hi) continue;
                for (double b : horiz ? std::initializer_list<double>{f.y0, f.y1} : std::initializer_list<double>{f.x0, f.x1})
                    if (std::fabs(centre - b) < half + kTol) border = b;
            }
            if (std::isnan(border)) continue;
            double room_lo = (horiz ? ymin : xmin) + half, room_hi = (horiz ? ymax : xmax) - half;
            double best = std::nan("");
            for (double cand : {border - kAway - half, border + kAway + half}) {
                if (cand < room_lo || cand > room_hi) continue;
                if (std::isnan(best) || std::fabs(cand - centre) < std::fabs(best - centre)) best = cand;
            }
            if (std::isnan(best)) continue;
            (horiz ? label_center[i].y : label_center[i].x) = best;
            c = label_center[i];
        }
    }
}

}  // namespace mermaid
