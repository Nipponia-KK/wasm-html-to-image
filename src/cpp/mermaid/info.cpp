#include "mermaid/info.h"

#include "mermaid/mermaid_internal.h"

namespace mermaid {

bool render_info(const std::string& source, const Options& options, std::string& svg, std::string& error) {
    using litehtml::svgutil::trim;
    bool first = true;
    size_t i = 0;
    while (i <= source.size()) {
        size_t e = source.find('\n', i);
        std::string line = trim(source.substr(i, e == std::string::npos ? std::string::npos : e - i));
        i = e == std::string::npos ? source.size() + 1 : e + 1;
        if (line.empty() || line.compare(0, 2, "%%") == 0) continue;
        if (first) {
            first = false;
            continue;
        }
        if (line != "showInfo") {
            error = "unrecognised line: " + line;
            return false;
        }
    }
    Theme th = make_theme(options);
    std::string body;
    centered_text(body, th, {"v11"}, 60, 30, 15, 600, th.text);
    svg = svg_document(th, 120, 60, body);
    return true;
}

}  // namespace mermaid
