#ifndef SATORU_MERMAID_KANBAN_H
#define SATORU_MERMAID_KANBAN_H

#include <string>

#include "mermaid/mermaid.h"

namespace mermaid {

/// `kanban` source (first line included) to SVG.
bool render_kanban(const std::string& source, const Options& options, std::string& svg, std::string& error);

}  // namespace mermaid

#endif  // SATORU_MERMAID_KANBAN_H
