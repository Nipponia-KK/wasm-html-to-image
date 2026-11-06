# CLI options

Generated from `wasm-html-to-image --help` (version 1.3.0). Regenerate with `node scripts/gen-skill-cli-options.mjs`.

```text
Usage: wasm-html-to-image [options] [input]

HTML to image converter on the single unified WASM module (svg/pdf direct,
png/jpeg/webp/avif/jxl/raw/thumbhash via encode or html_to_image)

Arguments:
  input                          input HTML/image file path, URL, inline HTML
                                 string (starts with <), or - for stdin

Options:
  -V, --version                  output the version number
  -o, --output <path>            output file path
  -w, --width <number>           viewport width (default: 800)
  -h, --height <number>          viewport height (omit for auto)
  -f, --format <format>          output format (svg, png, pdf, jpeg, webp, apng,
                                 gif, tiff, avif, jxl, raw, thumbhash, none);
                                 default: from the --output extension, else png
  --font <path>                  local font file or glob such as
                                 './fonts/*.woff2' (repeatable); 'Family=path'
                                 registers it under that family name, e.g.
                                 'Segoe UI=C:/Windows/Fonts/segoeui.ttf'; repeat
                                 a family for each weight, font-weight picks the
                                 nearest file
  -q, --quality <number>         encode quality 0-100 (encode stage only)
                                 (default: 85)
  --base-url <url>               base URL for relative resources (URL input:
                                 defaults to the post-redirect document URL;
                                 file input: defaults to the file's directory)
  --color-space <srgb|cmyk>      output color space of tiff and pdf (default
                                 srgb; cmyk rewrites colors for print, with no
                                 ICC profile)
  --scale <n>                    pixel density of raster output from HTML
                                 (default 1; 3.125 draws an 800px layout as
                                 2500px)
  --dpi <n>                      resolution written to tiff output, in pixels
                                 per inch (default 96 x scale)
  --animation <duration-ms>      sample the document's @keyframes opacity /
                                 transform / color animation into an animated
                                 WebP / APNG / GIF; one loop duration in ms
                                 (webp, apng and gif output only; the CSS
                                 animation-duration wins and this value is an
                                 upper bound)
  --animation-fps <n>            animation frame rate (default 15, capped at ~75
                                 frames total; requires --animation)
  --animation-loop <n>           how many times the animated WebP / APNG / GIF
                                 repeats (0 = infinite, the default; 1 = play
                                 once; requires --animation)
  --markdown                     treat the input as Markdown (implied by a .md /
                                 .markdown file; inline text and stdin need it;
                                 needs the optional dependency marked)
  --template <name|file>         look of a PDF from Markdown: a bundled template
                                 (see --list-templates) or a .json / .css file
  --list-templates               print the bundled templates and exit
  --page-size <size>             PDF page size for Markdown: A3, A4, A5, B4, B5,
                                 Letter, or e.g. '210mm 297mm'
  --accent <colour>              accent colour of a Markdown document (links,
                                 rules, cover), e.g. '#c2410c'
  --body-font <family>           body font family of a Markdown document (the
                                 renderer loads it; serif and sans-serif work)
  --stamp <text>                 text printed in the header of every page of a
                                 Markdown PDF (DRAFT, Confidential, ...)
  --cover                        a cover page with the title, subtitle, author
                                 and date (from the front matter)
  --no-cover                     no cover page, whatever the template says
  --no-toc                       no table of contents in a PDF made from
                                 Markdown (there is one by default)
  --toc-depth <n>                deepest heading level in the table of contents
                                 (default 3)
  --no-outline                   leave the bookmarks (outline) out of a PDF
                                 (they are built from the headings by default)
  --no-math                      do not typeset $...$ and $$...$$ in Markdown
                                 (done with KaTeX when it is installed)
  --markdown-theme <light|dark>  colour scheme of the built-in Markdown
                                 stylesheet (default light)
  --markdown-css <path>          extra CSS file appended after the built-in
                                 Markdown stylesheet
  --mermaid                      treat the input as a Mermaid diagram (implied
                                 by a .mmd / .mermaid file; inline text and
                                 stdin need it)
  --mermaid-theme <name>         colour theme of a Mermaid diagram: light, dark,
                                 neutral, forest, nord, dracula,
                                 solarized-light, solarized-dark, rose-pine,
                                 sepia
  --mermaid-mode <mode>          how much a broken Mermaid source is forgiven:
                                 strict (default), compat, recover
  --mermaid-directives           honour %%{init}%% and frontmatter config
                                 (theme, themeVariables) in a Mermaid source
  --js                           run the page's scripts in QuickJS before
                                 rendering (trusted pages only)
  --js-wait <ms|selector>        with --js: milliseconds to wait after load
                                 (default 1000), or a CSS selector to wait for
  --help                         display help for command
```
