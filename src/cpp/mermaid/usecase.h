#ifndef SATORU_MERMAID_USECASE_H
#define SATORU_MERMAID_USECASE_H

#include <string>

#include "mermaid/mermaid.h"

namespace mermaid {

/// `usecase-beta` source (first line included) to SVG.
bool render_usecase(const std::string& source, const Options& options, std::string& svg, std::string& error);

}  // namespace mermaid

#endif  // SATORU_MERMAID_USECASE_H
