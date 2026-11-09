#ifndef SATORU_MERMAID_GANTT_H
#define SATORU_MERMAID_GANTT_H

#include <string>

#include "mermaid/mermaid.h"

namespace mermaid {

/// `gantt` source (first line included) to SVG.
bool render_gantt(const std::string& source, const Options& options, std::string& svg, std::string& error);

}  // namespace mermaid

#endif  // SATORU_MERMAID_GANTT_H
