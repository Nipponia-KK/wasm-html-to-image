#ifndef SATORU_MERMAID_FLOWCHART_H
#define SATORU_MERMAID_FLOWCHART_H

#include <string>

#include "mermaid/mermaid.h"

namespace mermaid {

/// `graph` / `flowchart` source (first line included) to SVG.
bool render_flowchart(const std::string& source, const Options& options, std::string& svg, std::string& error);

}  // namespace mermaid

#endif  // SATORU_MERMAID_FLOWCHART_H
