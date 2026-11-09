#ifndef SATORU_MERMAID_JOURNEY_H
#define SATORU_MERMAID_JOURNEY_H

#include <string>

#include "mermaid/mermaid.h"

namespace mermaid {

/// `journey` source (first line included) to SVG.
bool render_journey(const std::string& source, const Options& options, std::string& svg, std::string& error);

}  // namespace mermaid

#endif  // SATORU_MERMAID_JOURNEY_H
