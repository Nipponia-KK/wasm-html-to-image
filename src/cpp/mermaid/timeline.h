#ifndef SATORU_MERMAID_TIMELINE_H
#define SATORU_MERMAID_TIMELINE_H

#include <string>

#include "mermaid/mermaid.h"

namespace mermaid {

/// `timeline` source (first line included) to SVG.
bool render_timeline(const std::string& source, const Options& options, std::string& svg, std::string& error);

}  // namespace mermaid

#endif  // SATORU_MERMAID_TIMELINE_H
