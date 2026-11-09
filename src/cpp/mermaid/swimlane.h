#ifndef SATORU_MERMAID_SWIMLANE_H
#define SATORU_MERMAID_SWIMLANE_H

// Swimlane layout: each lane is a band across the whole flow; a node sits in its lane at the position its rank gives,
// and edges run orthogonally between the nodes (no layered crossing reduction: the lanes already order the breadth).

#include <vector>

#include "mermaid/layered.h"

namespace mermaid {

struct SwimNode {
    double w = 0, h = 0;
    int lane = 0;
};

struct SwimLane {
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // the band, header included
    double header = 0;                      // the header's extent along the flow (a height with lanes side by side, a width with lanes stacked)
};

struct SwimResult {
    LayoutResult lay;  // node centres, edge points and label centres; width/height cover every lane
    std::vector<SwimLane> lanes;
    bool lanes_are_columns = false;  // top-down flow: lanes side by side with their headers on top
};

/// `header_h` is the height of a lane header when the lanes are columns; `header_w` the width of one when they are rows.
SwimResult layout_swimlane(const std::vector<SwimNode>& nodes, const std::vector<LayoutEdge>& edges, int lane_count, Direction dir, double header_h,
                           double header_w);

}  // namespace mermaid

#endif  // SATORU_MERMAID_SWIMLANE_H
