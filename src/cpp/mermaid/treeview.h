#ifndef SATORU_MERMAID_TREEVIEW_H
#define SATORU_MERMAID_TREEVIEW_H

#include <string>

#include "mermaid/mermaid.h"

namespace mermaid {

/// `treeView-beta` / `treeView` source (first line included): a file-tree style outline by indentation.
bool render_treeview(const std::string& source, const Options& options, std::string& svg, std::string& error);

}  // namespace mermaid

#endif  // SATORU_MERMAID_TREEVIEW_H
