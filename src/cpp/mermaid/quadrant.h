#ifndef SATORU_MERMAID_QUADRANT_H
#define SATORU_MERMAID_QUADRANT_H

#include <string>

#include "mermaid/mermaid.h"

namespace mermaid {

/// `quadrantChart` source (first line included) to SVG.
bool render_quadrant(const std::string& source, const Options& options, std::string& svg, std::string& error);

}  // namespace mermaid

#endif  // SATORU_MERMAID_QUADRANT_H
