#ifndef SATORU_MERMAID_XYCHART_H
#define SATORU_MERMAID_XYCHART_H

#include <string>

#include "mermaid/mermaid.h"

namespace mermaid {

/// `xychart-beta` / `xychart` source (first line included) to SVG: bar and line series on a category or numeric x axis.
bool render_xychart(const std::string& source, const Options& options, std::string& svg, std::string& error);

}  // namespace mermaid

#endif  // SATORU_MERMAID_XYCHART_H
