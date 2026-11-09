#ifndef SATORU_MERMAID_CYNEFIN_H
#define SATORU_MERMAID_CYNEFIN_H

#include <string>

#include "mermaid/mermaid.h"

namespace mermaid {

/// `cynefin-beta` source (first line included) to SVG.
bool render_cynefin(const std::string& source, const Options& options, std::string& svg, std::string& error);

}  // namespace mermaid

#endif  // SATORU_MERMAID_CYNEFIN_H
