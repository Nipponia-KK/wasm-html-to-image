#ifndef SATORU_MERMAID_ERDIAGRAM_H
#define SATORU_MERMAID_ERDIAGRAM_H

#include <string>

#include "mermaid/mermaid.h"

namespace mermaid {

/// `erDiagram` source (first line included) to SVG.
bool render_er(const std::string& source, const Options& options, std::string& svg, std::string& error);

}  // namespace mermaid

#endif  // SATORU_MERMAID_ERDIAGRAM_H
