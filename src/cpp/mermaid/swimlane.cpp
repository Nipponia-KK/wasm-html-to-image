#include "mermaid/swimlane.h"

#include <algorithm>
#include <cmath>

namespace mermaid {
namespace {

constexpr double kRankGap = 56, kStackGap = 18, kLanePad = 22, kMinLane = 96;

/// Ranks by longest path; an edge that closes a cycle (found by a depth-first walk) does not count.
std::vector<int> ranks_of(size_t n, const std::vector<LayoutEdge>& edges) {
    std::vector<std::vector<int>> out(n);
    for (const LayoutEdge& e : edges)
        if (e.from != e.to) out[e.from].push_back(e.to);
    std::vector<int> state(n, 0);  // 0 new, 1 on the stack, 2 done
    std::vector<std::vector<int>> dag(n);
    // iterative depth-first walk
    for (size_t root = 0; root < n; root++) {
        if (state[root]) continue;
        std::vector<std::pair<int, size_t>> stack{{(int)root, 0}};
        state[root] = 1;
        while (!stack.empty()) {
            auto& [v, k] = stack.back();
            if (k < out[v].size()) {
                int w = out[v][k++];
                if (state[w] == 1) continue;  // a back edge
                dag[v].push_back(w);
                if (state[w] == 0) {
                    state[w] = 1;
                    stack.push_back({w, 0});
                }
            } else {
                state[v] = 2;
                stack.pop_back();
            }
        }
    }
    std::vector<int> rank(n, 0), indeg(n, 0);
    for (size_t v = 0; v < n; v++)
        for (int w : dag[v]) indeg[w]++;
    std::vector<int> queue;
    for (size_t v = 0; v < n; v++)
        if (!indeg[v]) queue.push_back((int)v);
    for (size_t at = 0; at < queue.size(); at++) {
        int v = queue[at];
        for (int w : dag[v]) {
            rank[w] = std::max(rank[w], rank[v] + 1);
            if (--indeg[w] == 0) queue.push_back(w);
        }
    }
    return rank;
}

}  // namespace

SwimResult layout_swimlane(const std::vector<SwimNode>& nodes, const std::vector<LayoutEdge>& edges, int lane_count, Direction dir, double header_h, double header_w) {
    SwimResult res;
    const size_t n = nodes.size();
    const bool horizontal = dir == Direction::LR || dir == Direction::RL;  // the flow runs along x
    const bool reversed = dir == Direction::BT || dir == Direction::RL;
    res.lanes_are_columns = !horizontal;
    std::vector<int> rank = ranks_of(n, edges);
    int rank_count = 0;
    for (int r : rank) rank_count = std::max(rank_count, r + 1);
    if (rank_count == 0) rank_count = 1;

    auto flow_of = [&](size_t v) { return horizontal ? nodes[v].w : nodes[v].h; };     // extent along the flow
    auto breadth_of = [&](size_t v) { return horizontal ? nodes[v].h : nodes[v].w; };  // extent across it

    // the rank bands along the flow
    std::vector<double> rank_size(rank_count, 0), rank_start(rank_count, 0);
    for (size_t v = 0; v < n; v++) rank_size[rank[v]] = std::max(rank_size[rank[v]], flow_of(v));
    const double head = horizontal ? header_w : header_h;
    double at = head + kLanePad;
    for (int r = 0; r < rank_count; r++) {
        rank_start[r] = at;
        at += rank_size[r] + kRankGap;
    }
    const double flow_total = at - kRankGap + kLanePad;

    // the lanes across the flow: as thick as their fullest rank
    std::vector<std::vector<std::vector<int>>> cell(lane_count, std::vector<std::vector<int>>(rank_count));
    for (size_t v = 0; v < n; v++) cell[nodes[v].lane][rank[v]].push_back((int)v);
    std::vector<double> thick(lane_count, kMinLane), lane_start(lane_count, 0);
    for (int l = 0; l < lane_count; l++)
        for (int r = 0; r < rank_count; r++) {
            double sum = 0;
            for (int v : cell[l][r]) sum += breadth_of(v) + (sum > 0 ? kStackGap : 0);
            thick[l] = std::max(thick[l], sum + 2 * kLanePad);
        }
    double edge = 0;
    for (int l = 0; l < lane_count; l++) {
        lane_start[l] = edge;
        edge += thick[l];
    }
    const double breadth_total = edge;

    // node centres
    res.lay.center.assign(n, {});
    for (int l = 0; l < lane_count; l++)
        for (int r = 0; r < rank_count; r++) {
            double sum = 0;
            for (int v : cell[l][r]) sum += breadth_of(v) + (sum > 0 ? kStackGap : 0);
            double b = lane_start[l] + (thick[l] - sum) / 2;
            for (int v : cell[l][r]) {
                double f = rank_start[r] + rank_size[r] / 2;
                if (reversed) f = head + flow_total - f;
                double bc = b + breadth_of(v) / 2;
                res.lay.center[v] = horizontal ? Pt{f, bc} : Pt{bc, f};
                b += breadth_of(v) + kStackGap;
            }
        }

    // lanes
    res.lanes.resize(lane_count);
    for (int l = 0; l < lane_count; l++) {
        SwimLane& lane = res.lanes[l];
        lane.header = head;
        if (horizontal) lane = {0, lane_start[l], flow_total, lane_start[l] + thick[l], header_w};
        else lane = {lane_start[l], 0, lane_start[l] + thick[l], flow_total, header_h};
        if (reversed) {  // the header stays on the side the flow starts from visually: keep it at the left / top
            lane.header = head;
        }
    }
    res.lay.width = horizontal ? flow_total : breadth_total;
    res.lay.height = horizontal ? breadth_total : flow_total;

    // edges: out of the side facing the target, a bend half way, into the side facing the source
    res.lay.points.assign(edges.size(), {});
    res.lay.label_center.assign(edges.size(), {});
    for (size_t i = 0; i < edges.size(); i++) {
        int u = edges[i].from, v = edges[i].to;
        Pt pu = res.lay.center[u], pv = res.lay.center[v];
        std::vector<Pt> pts;
        if (u == v) {
            pts = {pu, pu};
        } else if (rank[u] != rank[v]) {
            double sign = (rank[v] > rank[u]) != reversed ? 1 : -1;
            if (horizontal) {
                Pt a{pu.x + sign * nodes[u].w / 2, pu.y}, b{pv.x - sign * nodes[v].w / 2, pv.y};
                double mid = (a.x + b.x) / 2;
                pts = {a, {mid, a.y}, {mid, b.y}, b};
            } else {
                Pt a{pu.x, pu.y + sign * nodes[u].h / 2}, b{pv.x, pv.y - sign * nodes[v].h / 2};
                double mid = (a.y + b.y) / 2;
                pts = {a, {a.x, mid}, {b.x, mid}, b};
            }
        } else {  // the same rank: across the breadth
            if (horizontal) {
                double sign = pv.y > pu.y ? 1 : -1;
                Pt a{pu.x, pu.y + sign * nodes[u].h / 2}, b{pv.x, pv.y - sign * nodes[v].h / 2};
                double mid = (a.y + b.y) / 2;
                pts = {a, {a.x, mid}, {b.x, mid}, b};
            } else {
                double sign = pv.x > pu.x ? 1 : -1;
                Pt a{pu.x + sign * nodes[u].w / 2, pu.y}, b{pv.x - sign * nodes[v].w / 2, pv.y};
                double mid = (a.x + b.x) / 2;
                pts = {a, {mid, a.y}, {mid, b.y}, b};
            }
        }
        res.lay.points[i] = pts;
        // the label sits on the middle segment
        size_t m = pts.size() / 2;
        res.lay.label_center[i] = {(pts[m - 1].x + pts[m].x) / 2, (pts[m - 1].y + pts[m].y) / 2};
    }
    return res;
}

}  // namespace mermaid
