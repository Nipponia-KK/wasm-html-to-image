#ifndef SATORU_MERMAID_WARDLEY_H
#define SATORU_MERMAID_WARDLEY_H

#include <string>

#include "mermaid/mermaid.h"

namespace mermaid {

/// `wardley-beta` source (first line included) to SVG.
bool render_wardley(const std::string& source, const Options& options, std::string& svg, std::string& error);

}  // namespace mermaid

#endif  // SATORU_MERMAID_WARDLEY_H
