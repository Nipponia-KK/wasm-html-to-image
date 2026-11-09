#ifndef SATORU_MERMAID_NODESTYLE_H
#define SATORU_MERMAID_NODESTYLE_H

// `classDef`, `class`, `:::` and `style` lines shared by the diagrams that draw boxes (state, class, ER,
// block). A diagram feeds each source line to StyleSheet::take() and, when it draws a box, asks for the
// paint of that box's key.

#include <map>
#include <string>
#include <vector>

#include "mermaid/mermaid_internal.h"

namespace mermaid {

struct NodeStyle {
    std::string fill, stroke, color, stroke_width, dash;
};

class StyleSheet {
public:
    /// Records @p line when it is a style statement and returns true. `class a,b name` is only read when
    /// @p class_statement is set (a class diagram uses `class` for declarations and `cssClass` for this).
    bool take(const std::string& line, bool class_statement = true) {
        using litehtml::svgutil::trim;
        auto starts = [&](const char* p) { return line.compare(0, std::char_traits<char>::length(p), p) == 0; };
        if (starts("classDef ")) {
            std::string rest = trim(line.substr(9));
            size_t sp = rest.find(' ');
            if (sp == std::string::npos) return true;
            NodeStyle st = parse(rest.substr(sp + 1));
            for (const std::string& n : split(rest.substr(0, sp))) classes_[n] = st;
            return true;
        }
        bool css = starts("cssClass ");
        if ((class_statement && starts("class ")) || css) {
            std::string rest = trim(line.substr(css ? 9 : 6));
            size_t sp = rest.rfind(' ');
            if (sp == std::string::npos) return true;
            std::string names = rest.substr(0, sp);
            if (names.size() >= 2 && names.front() == '"' && names.back() == '"') names = names.substr(1, names.size() - 2);
            for (const std::string& n : split(names)) use(n, trim(rest.substr(sp + 1)));
            return true;
        }
        if (starts("style ")) {
            std::string rest = trim(line.substr(6));
            size_t sp = rest.find(' ');
            if (sp == std::string::npos) return true;
            NodeStyle st = parse(rest.substr(sp + 1));
            for (const std::string& n : split(rest.substr(0, sp))) direct_[n] = merged(direct_[n], st);
            return true;
        }
        return false;
    }

    void use(const std::string& key, const std::string& cls) { uses_.push_back({key, cls}); }

    /// The class styles first, then `style` on top.
    NodeStyle resolve(const std::string& key) const {
        NodeStyle out;
        for (const auto& u : uses_) {
            if (u.first != key) continue;
            auto c = classes_.find(u.second);
            if (c != classes_.end()) out = merged(out, c->second);
        }
        auto d = direct_.find(key);
        if (d != direct_.end()) out = merged(out, d->second);
        return out;
    }

    /// ` fill=".." stroke=".." stroke-width=".."` for a box, with the theme values where nothing is set.
    std::string paint(const std::string& key, const std::string& fill, const std::string& stroke) const {
        NodeStyle s = resolve(key);
        std::string out = " fill=\"" + (s.fill.empty() ? fill : s.fill) + "\" stroke=\"" + (s.stroke.empty() ? stroke : s.stroke) + "\" stroke-width=\"" +
                          (s.stroke_width.empty() ? "1" : strip_px(s.stroke_width)) + "\"";
        if (!s.dash.empty()) out += " stroke-dasharray=\"" + s.dash + "\"";
        return out;
    }

    std::string text_color(const std::string& key, const std::string& fallback) const {
        std::string c = resolve(key).color;
        return c.empty() ? fallback : c;
    }

private:
    static NodeStyle merged(NodeStyle base, const NodeStyle& over) {
        if (!over.fill.empty()) base.fill = over.fill;
        if (!over.stroke.empty()) base.stroke = over.stroke;
        if (!over.color.empty()) base.color = over.color;
        if (!over.stroke_width.empty()) base.stroke_width = over.stroke_width;
        if (!over.dash.empty()) base.dash = over.dash;
        return base;
    }

    static std::string strip_px(const std::string& v) { return v.size() > 2 && v.compare(v.size() - 2, 2, "px") == 0 ? v.substr(0, v.size() - 2) : v; }

    static std::vector<std::string> split(const std::string& names) {
        std::vector<std::string> out;
        size_t k = 0;
        while (k <= names.size()) {
            size_t e = names.find(',', k);
            std::string n = litehtml::svgutil::trim(names.substr(k, e == std::string::npos ? std::string::npos : e - k));
            if (!n.empty()) out.push_back(n);
            if (e == std::string::npos) break;
            k = e + 1;
        }
        return out;
    }

    static NodeStyle parse(const std::string& decl) {
        NodeStyle s;
        for (const std::string& part : split(decl)) {
            size_t c = part.find(':');
            if (c == std::string::npos) continue;
            std::string k = litehtml::svgutil::trim(part.substr(0, c)), v = litehtml::svgutil::trim(part.substr(c + 1));
            if (k == "fill") s.fill = v;
            else if (k == "stroke") s.stroke = v;
            else if (k == "color") s.color = v;
            else if (k == "stroke-width") s.stroke_width = v;
            else if (k == "stroke-dasharray") s.dash = v;
        }
        return s;
    }

    std::map<std::string, NodeStyle> classes_, direct_;
    std::vector<std::pair<std::string, std::string>> uses_;
};

}  // namespace mermaid

#endif  // SATORU_MERMAID_NODESTYLE_H
