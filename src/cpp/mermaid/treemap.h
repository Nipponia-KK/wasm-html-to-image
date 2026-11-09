#ifndef SATORU_MERMAID_TREEMAP_H
#define SATORU_MERMAID_TREEMAP_H

#include <string>

#include "mermaid/mermaid.h"

namespace mermaid {

/// `treemap-beta` / `treemap` source (first line included): nested sections by indentation, leaves with values.
bool render_treemap(const std::string& source, const Options& options, std::string& svg, std::string& error);

}  // namespace mermaid

#endif  // SATORU_MERMAID_TREEMAP_H
