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
    std::string arrow;         // arrow heads; default: accent
    std::string group_fill;    // body of a subgraph / cluster; default: bg
    std::string group_header;  // header band of a subgraph; default: a faint tint of fg over bg
    std::string label_fill;    // behind edge labels; default: bg
    std::string composite_fill;    // body of a composite state; default: group_fill
    std::string composite_header;  // header band of a composite state; default: none
    std::string actor_border;      // participant boxes of a sequence diagram; default: border
    std::string note_fill;     // notes (sequence diagram); default: a faint tint of fg over bg
    std::string note_border;   // default: border
    std::string note_text;     // default: fg
    /// Series colours, comma separated (`#aaa,#bbb,...`), taken in turn by the slices of a pie chart; empty: shades of fg.
    /// With a scale the pie is drawn as the official Mermaid one is (translucent slices, an outline in scale_stroke).
    std::string scale;
    std::string scale_stroke;  // outline of the pie slices; default: bg
    /// Colours of the series of an xychart, comma separated; empty: one colour (the accent or the line colour).
    std::string plot;
    /// The fills of the five Cynefin domains (complex, complicated, chaotic, clear, confusion), comma separated; empty: tints of the accent.
    std::string domains;
    std::string label_color;     // text of the Wardley, Railroad and use case diagrams; default: fg
    std::string cliff;           // the cliff of a Cynefin diagram; default: the line colour
    std::string evolution;       // evolution arrows of a Wardley map; default: a red
    std::string component_fill;  // components of a Wardley map; default: bg
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
    /// A diagram over either limit is refused (0: no limit). The layout of a big graph grows faster than its size
    /// (600 nodes take seconds, 1500 minutes), so an input one does not trust needs a ceiling.
    size_t max_source_bytes = 200000;
    size_t max_lines = 2000;  // lines with text, comments and blank lines not counted
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
