#ifndef SATORU_MERMAID_MINDMAP_H
#define SATORU_MERMAID_MINDMAP_H

#include <string>

#include "mermaid/mermaid.h"

namespace mermaid {

/// `mindmap` source (first line included) to SVG.
bool render_mindmap(const std::string& source, const Options& options, std::string& svg, std::string& error);

}  // namespace mermaid

#endif  // SATORU_MERMAID_MINDMAP_H
