#ifndef SATORU_MERMAID_H
#define SATORU_MERMAID_H

// Mermaid diagrams drawn by the engine itself: source text in, a self-contained SVG out (plain colours,
// no CSS variables, no <marker>), so the same SVG renders on every output path (PNG, PDF, SVG).
// Standard library only, so the layout can be unit tested on the host.
//
// Supported: flowchart / graph. (state, sequence, class, ER and xychart follow the same shape: a parser,
// a layout and an SVG writer behind render().)

#include <functional>
#include <string>
#include <vector>

namespace mermaid {

/// Width in px of one line of @p text at @p size px. The engine passes its real font metrics; the
/// default is a per-character estimate (CJK full width).
using MeasureFn = std::function<double(const std::string& text, double size, int weight, bool mono)>;

struct Options {
    MeasureFn measure;  // empty: estimate_text_width
    /// Two colours decide the whole diagram; the rest are derived from them unless given.
    std::string bg = "#ffffff";
    std::string fg = "#1f2328";
    std::string accent;   // arrow heads and highlights; default: fg
    std::string line;     // edges
    std::string muted;    // secondary text (edge labels)
    std::string surface;  // node fill
    std::string border;   // node outline
    /// Paint the background rectangle. Off by default so a diagram sits on the page it is placed on.
    bool paint_background = false;
    std::string font_family = "sans-serif";
    std::string mono_family = "monospace";
    /// Honour `%%{init: {...}}%%` and frontmatter `config:` (theme, themeVariables). Off by default: the page
    /// that places the diagram decides the colours. The frontmatter block itself is always skipped.
    bool allow_directives = false;
    /// How much a broken source is forgiven.
    ///  Strict:  none; the error says what was meant when the keyword is a near miss.
    ///  Compat:  a BOM or CRLF, a keyword in the wrong case, and unfinished `end` / `}` blocks.
    ///  Recover: also a misspelt keyword and lines the diagram cannot read (skipped, and reported).
    enum class Mode { Strict, Compat, Recover };
    Mode mode = Mode::Strict;
};

/// Sets the colours of a named theme (light, dark, neutral, forest, nord, dracula, solarized-light, solarized-dark,
/// rose-pine, sepia; `default` and `base` are light). False for an unknown name, leaving @p options alone.
bool apply_theme(const std::string& name, Options& options);

/// The names apply_theme() accepts.
std::vector<std::string> theme_names();

/// Estimated width of one line of text (no font access): narrow, regular, wide and full-width classes.
double estimate_text_width(const std::string& text, double size, int weight, bool mono);

/// Render @p source (the text between the code fences). Returns false and fills @p error for text that is
/// not a supported diagram or does not parse; @p svg is then untouched.
/// With a Compat or Recover mode, what was repaired is added to @p warnings (when given) and written into the
/// SVG as a leading comment.
bool render(const std::string& source, const Options& options, std::string& svg, std::string& error, std::vector<std::string>* warnings = nullptr);

}  // namespace mermaid

#endif  // SATORU_MERMAID_H
