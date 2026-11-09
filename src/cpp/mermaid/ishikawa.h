#ifndef SATORU_MERMAID_ISHIKAWA_H
#define SATORU_MERMAID_ISHIKAWA_H

#include <string>

#include "mermaid/mermaid.h"

namespace mermaid {

/// `ishikawa-beta` / `ishikawa` source (first line included): a fishbone diagram written by indentation:
/// the effect, then categories, then causes.
bool render_ishikawa(const std::string& source, const Options& options, std::string& svg, std::string& error);

}  // namespace mermaid

#endif  // SATORU_MERMAID_ISHIKAWA_H
