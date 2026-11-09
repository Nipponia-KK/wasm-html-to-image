// tests/cpp/mermaid_cli.cpp — host tool: Mermaid source on stdin, SVG on stdout (for looking at the layout).
// Options by argument: bg=#fff fg=#000 accent=#00f background

#include <iostream>
#include <iterator>
#include <string>

#include "mermaid/mermaid.h"

int main(int argc, char** argv) {
    mermaid::Options opt;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a.rfind("bg=", 0) == 0) opt.bg = a.substr(3);
        else if (a.rfind("fg=", 0) == 0) opt.fg = a.substr(3);
        else if (a.rfind("accent=", 0) == 0) opt.accent = a.substr(7);
        else if (a == "background") opt.paint_background = true;
    }
    std::string src((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>()), svg, err;
    if (!mermaid::render(src, opt, svg, err)) {
        std::cerr << "error: " << err << "\n";
        return 1;
    }
    std::cout << svg;
    return 0;
}
