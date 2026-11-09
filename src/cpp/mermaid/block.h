#ifndef SATORU_MERMAID_BLOCK_H
#define SATORU_MERMAID_BLOCK_H

#include <string>

#include "mermaid/mermaid.h"

namespace mermaid {

/// `block-beta` / `block` source (first line included): blocks on a column grid, with links.
bool render_block(const std::string& source, const Options& options, std::string& svg, std::string& error);

}  // namespace mermaid

#endif  // SATORU_MERMAID_BLOCK_H
