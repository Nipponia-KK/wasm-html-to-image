#ifndef SATORU_MERMAID_GITGRAPH_H
#define SATORU_MERMAID_GITGRAPH_H

#include <string>

#include "mermaid/mermaid.h"

namespace mermaid {

/// `gitGraph` source (first line included) to SVG.
bool render_gitgraph(const std::string& source, const Options& options, std::string& svg, std::string& error);

}  // namespace mermaid

#endif  // SATORU_MERMAID_GITGRAPH_H
