#ifndef SATORU_MERMAID_REQUIREMENT_H
#define SATORU_MERMAID_REQUIREMENT_H

#include <string>

#include "mermaid/mermaid.h"

namespace mermaid {

/// `requirementDiagram` source (first line included) to SVG.
bool render_requirement(const std::string& source, const Options& options, std::string& svg, std::string& error);

}  // namespace mermaid

#endif  // SATORU_MERMAID_REQUIREMENT_H
