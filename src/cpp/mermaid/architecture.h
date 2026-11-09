#ifndef SATORU_MERMAID_ARCHITECTURE_H
#define SATORU_MERMAID_ARCHITECTURE_H

#include <string>

#include "mermaid/mermaid.h"

namespace mermaid {

/// `architecture-beta` / `architecture` source (first line included): groups, services and junctions on a grid,
/// placed by the sides their edges leave from (`a:R -- L:b`).
bool render_architecture(const std::string& source, const Options& options, std::string& svg, std::string& error);

}  // namespace mermaid

#endif  // SATORU_MERMAID_ARCHITECTURE_H
