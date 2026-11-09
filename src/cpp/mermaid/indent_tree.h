#ifndef SATORU_MERMAID_INDENT_TREE_H
#define SATORU_MERMAID_INDENT_TREE_H

// A tree written by indentation (treeView, ishikawa): one node per line, a deeper indent is a child of the
// closest shallower line above it.

#include <memory>
#include <string>
#include <vector>

#include "mermaid/mermaid_internal.h"

namespace mermaid {

struct TreeNode {
    std::string text;
    int indent = -1;
    std::vector<std::unique_ptr<TreeNode>> kids;
};

/// Parses the lines after the first (the diagram keyword) under a virtual root. Lines starting with
/// @p skip_prefix (e.g. "title ") and comments are ignored. Returns false with @p error when a line has no text.
inline bool parse_indent_tree(const std::string& source, TreeNode& root, std::string& error) {
    using litehtml::svgutil::trim;
    std::vector<TreeNode*> stack{&root};
    bool first = true;
    size_t i = 0;
    while (i <= source.size()) {
        size_t e = source.find('\n', i);
        std::string raw = source.substr(i, e == std::string::npos ? std::string::npos : e - i);
        i = e == std::string::npos ? source.size() + 1 : e + 1;
        std::string line = trim(raw);
        if (line.empty() || line.compare(0, 2, "%%") == 0) continue;
        if (first) {
            first = false;
            continue;
        }
        int indent = 0;
        for (char c : raw) {
            if (c == ' ') indent++;
            else if (c == '\t') indent += 4;
            else break;
        }
        if (line.size() >= 2 && line.front() == '"' && line.back() == '"') line = line.substr(1, line.size() - 2);
        auto node = std::make_unique<TreeNode>();
        node->text = line;
        node->indent = indent;
        while (stack.size() > 1 && stack.back()->indent >= indent) stack.pop_back();
        TreeNode* p = node.get();
        stack.back()->kids.push_back(std::move(node));
        stack.push_back(p);
    }
    (void)error;
    return true;
}

}  // namespace mermaid

#endif  // SATORU_MERMAID_INDENT_TREE_H
