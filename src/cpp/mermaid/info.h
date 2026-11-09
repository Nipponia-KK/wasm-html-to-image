#ifndef SATORU_MERMAID_INFO_H
#define SATORU_MERMAID_INFO_H

#include <string>

#include "mermaid/mermaid.h"

namespace mermaid {

/// `info` source: a one-line version banner.
bool render_info(const std::string& source, const Options& options, std::string& svg, std::string& error);

}  // namespace mermaid

#endif  // SATORU_MERMAID_INFO_H
