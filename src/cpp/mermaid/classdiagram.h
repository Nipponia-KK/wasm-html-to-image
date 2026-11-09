#ifndef SATORU_MERMAID_CLASSDIAGRAM_H
#define SATORU_MERMAID_CLASSDIAGRAM_H

#include <string>

#include "mermaid/mermaid.h"

namespace mermaid {

/// `classDiagram` / `classDiagram-v2` source (first line included) to SVG.
bool render_class(const std::string& source, const Options& options, std::string& svg, std::string& error);

}  // namespace mermaid

#endif  // SATORU_MERMAID_CLASSDIAGRAM_H
