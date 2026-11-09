#ifndef SATORU_MERMAID_PIE_H
#define SATORU_MERMAID_PIE_H

#include <string>

#include "mermaid/mermaid.h"

namespace mermaid {

/// `pie` source (first line included) to SVG.
bool render_pie(const std::string& source, const Options& options, std::string& svg, std::string& error);

}  // namespace mermaid

#endif  // SATORU_MERMAID_PIE_H
