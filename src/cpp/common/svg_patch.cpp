#include "svg_patch.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctre.hpp>

namespace html_to_image {

// P2b: 本ファイル(約187行)の責務: SkData デコード経路のSVG互換パッチ (振る舞い不変)。
//   Step1 feDropShadow展開 / Step2 pattern内linearGradient救済 / Step3 href→xlink:href
// renderers/svg_renderer.cpp::finalizeSvg (描画タグ注入経路) とは役割分担し重複させない:
//   - patch_svg_data: image_decoder 経由の外部SVGデコード前に適用 (raw SkData→SkData)
//   - finalizeSvg   : SkSVGCanvas 出力に対する defs/タグ注入 (std::string 組立)
// 3ステップの適用順序は互換性に影響するため変更禁止。新規throw禁止。

// 移植元: image_converter_api.cpp:408-563 の static patch_svg_data を
// html_to_image::patch_svg_data としてそのまま移設 (static を外したのみ)。
// satoru側の no-op 版は不採用。差分は svg_patch.h のコメント参照。
sk_sp<SkData> patch_svg_data(const sk_sp<SkData>& data) {
    if (!data || data->size() == 0) return data;
    std::string_view svg((const char*)data->data(), data->size());
    std::string patched_svg;
    bool changed = false;

    // Step1 (svg_patch/feDropShadow): feDropShadow→Blur/Offset/Flood/Composite/Merge 展開
    // 1. Patch feDropShadow
    {
        std::string result;
        std::string_view searchRange = svg;
        bool step_changed = false;

        while (auto m = ctre::search<"<feDropShadow([^>]*?)/?>">(searchRange)) {
            step_changed = true;
            changed = true;
            result.append(searchRange.substr(0, m.get<0>().begin() - searchRange.begin()));

            std::string_view attrs = m.get<1>().to_view();
            std::string dx = "0", dy = "0", stdDev = "0", floodColor = "black", floodOpacity = "1",
                          in = "SourceGraphic";

            std::string_view attrSearchRange = attrs;
            while (auto am = ctre::search<"([\\w\\-]+)\\s*=\\s*\"([^\"]*)\"">(attrSearchRange)) {
                std::string_view name = am.get<1>().to_view();
                std::string_view value = am.get<2>().to_view();
                if (name == "dx")
                    dx = std::string(value);
                else if (name == "dy")
                    dy = std::string(value);
                else if (name == "stdDeviation")
                    stdDev = std::string(value);
                else if (name == "flood-color")
                    floodColor = std::string(value);
                else if (name == "flood-opacity")
                    floodOpacity = std::string(value);
                else if (name == "in")
                    in = std::string(value);
                attrSearchRange = attrSearchRange.substr(am.get<0>().end() - attrSearchRange.begin());
            }

            char buf[2048];
            snprintf(buf, sizeof(buf),
                     "<feGaussianBlur in=\"%s\" stdDeviation=\"%s\" result=\"_dropShadowBlur\"/>"
                     "<feOffset in=\"_dropShadowBlur\" dx=\"%s\" dy=\"%s\" result=\"_dropShadowOffset\"/>"
                     "<feFlood flood-color=\"%s\" flood-opacity=\"%s\" result=\"_dropShadowFlood\"/>"
                     "<feComposite in=\"_dropShadowFlood\" in2=\"_dropShadowOffset\" operator=\"in\" "
                     "result=\"_dropShadow\"/>"
                     "<feMerge><feMergeNode in=\"_dropShadow\"/><feMergeNode in=\"%s\"/></feMerge>",
                     in.c_str(), stdDev.c_str(), dx.c_str(), dy.c_str(), floodColor.c_str(),
                     floodOpacity.c_str(), in.c_str());

            result.append(buf);
            searchRange = searchRange.substr(m.get<0>().end() - searchRange.begin());
        }
        if (step_changed) {
            result.append(searchRange);
            patched_svg = std::move(result);
            svg = patched_svg;
        }
    }

    // Step2 (svg_patch/pattern): pattern内linearGradient救済 (参照url(#)書換含む)
    // 2. Patch pattern containing linearGradient (Satori workaround)
    {
        std::string result;
        std::string_view searchRange = svg;
        bool patternChanged = false;

        std::vector<std::pair<std::string, std::string>> replacements;

        while (auto m =
                   ctre::search<"<pattern\\s+([^>]*?id=\"([^\"]+)\"[^>]*?)>(.*?)</pattern>">(searchRange)) {
            std::string_view patternAttrs = m.get<1>().to_view();
            std::string_view patternId = m.get<2>().to_view();
            std::string_view content = m.get<3>().to_view();

            if (patternAttrs.find("patternUnits=\"objectBoundingBox\"") != std::string_view::npos) {
                if (auto gm = ctre::search<
                        "<linearGradient\\s+([^>]*?id=\"([^\"]+)\"[^>]*?)>(.*?)</linearGradient>">(content)) {
                    patternChanged = true;
                    changed = true;
                    result.append(searchRange.substr(0, m.get<0>().begin() - searchRange.begin()));

                    std::string_view gradAttrsWithId = gm.get<1>().to_view();
                    std::string_view gradId = gm.get<2>().to_view();
                    std::string_view gradContent = gm.get<3>().to_view();

                    // Extract attributes without the ID to avoid duplicates
                    std::string gradAttrs;
                    std::string_view gaRange = gradAttrsWithId;
                    while (auto gam = ctre::search<"id=\"[^\"]+\"">(gaRange)) {
                        gradAttrs.append(gaRange.substr(0, gam.get<0>().begin() - gaRange.begin()));
                        gaRange = gaRange.substr(gam.get<0>().end() - gaRange.begin());
                    }
                    gradAttrs.append(gaRange);

                    // Put the linearGradient in place of the pattern
                    result.append("<linearGradient id=\"");
                    result.append(std::string(gradId));
                    result.append("\" ");
                    result.append(gradAttrs);
                    result.append(">");
                    result.append(std::string(gradContent));
                    result.append("</linearGradient>");

                    // Replace all references to this pattern with references to the nested gradient
                    replacements.push_back(
                        {"url(#" + std::string(patternId) + ")", "url(#" + std::string(gradId) + ")"});

                    searchRange = searchRange.substr(m.get<0>().end() - searchRange.begin());
                    continue;
                }
            }
            result.append(searchRange.substr(0, m.get<0>().end() - searchRange.begin()));
            searchRange = searchRange.substr(m.get<0>().end() - searchRange.begin());
        }

        if (patternChanged) {
            result.append(searchRange);
            patched_svg = std::move(result);

            // Apply reference replacements on the full patched string
            for (const auto& r : replacements) {
                size_t pos = 0;
                while ((pos = patched_svg.find(r.first, pos)) != std::string::npos) {
                    patched_svg.replace(pos, r.first.length(), r.second);
                    pos += r.second.length();
                }
            }
            svg = patched_svg;
        }
    }

    // Step3 (svg_patch/xlink): href→xlink:href (data: and external URLs alike) + xmlns:xlink 付与
    // 3. Patch href to xlink:href for embedded images and add xlink namespace if needed
    {
        std::string result;
        std::string_view searchRange = svg;
        bool step_changed = false;
        while (auto m =
                   ctre::search<"(<image[^>]*?)\\s+href=\"([^\"]+)\"">(searchRange)) {
            step_changed = true;
            changed = true;
            result.append(searchRange.substr(0, m.get<0>().begin() - searchRange.begin()));
            result.append(m.get<1>().to_view());
            result.append(" xlink:href=\"");
            result.append(m.get<2>().to_view());
            result.append("\"");
            searchRange = searchRange.substr(m.get<0>().end() - searchRange.begin());
        }
        if (step_changed) {
            result.append(searchRange);
            patched_svg = std::move(result);
            svg = patched_svg;

            // Ensure xmlns:xlink is present
            if (svg.find("xmlns:xlink") == std::string::npos) {
                size_t pos = svg.find("<svg");
                if (pos != std::string::npos) {
                    size_t end_pos = svg.find(">", pos);
                    if (end_pos != std::string::npos) {
                        patched_svg.insert(end_pos, " xmlns:xlink=\"http://www.w3.org/1999/xlink\"");
                        svg = patched_svg;
                    }
                }
            }
        }
    }

    if (!changed) return data;
    return SkData::MakeWithCopy(svg.data(), svg.size());
}

}  // namespace html_to_image
