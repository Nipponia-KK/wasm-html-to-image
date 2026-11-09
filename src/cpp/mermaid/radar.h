#ifndef SATORU_MERMAID_RADAR_H
#define SATORU_MERMAID_RADAR_H

#include <string>

#include "mermaid/mermaid.h"

namespace mermaid {

/// `radar-beta` / `radar` source (first line included): axes, curves, graticule and legend.
bool render_radar(const std::string& source, const Options& options, std::string& svg, std::string& error);

}  // namespace mermaid

#endif  // SATORU_MERMAID_RADAR_H
