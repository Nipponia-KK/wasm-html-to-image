#ifndef SATORU_MERMAID_C4_H
#define SATORU_MERMAID_C4_H

#include <string>

#include "mermaid/mermaid.h"

namespace mermaid {

/// `C4Context`, `C4Container`, `C4Component`, `C4Dynamic` and `C4Deployment` source (first line included):
/// people, systems, containers, components and nodes inside nested boundaries, joined by `Rel(...)`.
bool render_c4(const std::string& source, const Options& options, std::string& svg, std::string& error);

}  // namespace mermaid

#endif  // SATORU_MERMAID_C4_H
