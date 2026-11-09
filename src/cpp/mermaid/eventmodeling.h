#ifndef SATORU_MERMAID_EVENTMODELING_H
#define SATORU_MERMAID_EVENTMODELING_H

#include <string>

#include "mermaid/mermaid.h"

namespace mermaid {

/// `eventmodeling` source (first line included) to SVG.
bool render_eventmodeling(const std::string& source, const Options& options, std::string& svg, std::string& error);

}  // namespace mermaid

#endif  // SATORU_MERMAID_EVENTMODELING_H
