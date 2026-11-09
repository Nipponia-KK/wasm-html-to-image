#ifndef SATORU_MERMAID_VENN_H
#define SATORU_MERMAID_VENN_H

#include <string>

#include "mermaid/mermaid.h"

namespace mermaid {

/// `venn-beta` / `venn` source (first line included): `set` circles, `union` labels on their overlaps and
/// indented `text` lines inside a set or union.
bool render_venn(const std::string& source, const Options& options, std::string& svg, std::string& error);

}  // namespace mermaid

#endif  // SATORU_MERMAID_VENN_H
