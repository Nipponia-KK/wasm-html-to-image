#ifndef SATORU_MERMAID_SANKEY_H
#define SATORU_MERMAID_SANKEY_H

#include <string>

#include "mermaid/mermaid.h"

namespace mermaid {

/// `sankey-beta` / `sankey` source (first line included: CSV rows of source,target,value) to SVG.
bool render_sankey(const std::string& source, const Options& options, std::string& svg, std::string& error);

}  // namespace mermaid

#endif  // SATORU_MERMAID_SANKEY_H
