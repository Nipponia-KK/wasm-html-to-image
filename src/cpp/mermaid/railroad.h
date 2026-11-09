#ifndef SATORU_MERMAID_RAILROAD_H
#define SATORU_MERMAID_RAILROAD_H

#include <string>

#include "mermaid/mermaid.h"

namespace mermaid {

/// `railroad-ebnf-beta`, `railroad-abnf-beta`, `railroad-peg-beta` or `railroad-beta` source (first line included) to SVG.
bool render_railroad(const std::string& source, const Options& options, std::string& svg, std::string& error);

}  // namespace mermaid

#endif  // SATORU_MERMAID_RAILROAD_H
