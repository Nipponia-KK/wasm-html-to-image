// api/satoru_api.cpp: satoru版を正として移植・改名 (api_* → satoru_api_*)。
// 正本: `satoru/src/cpp/api/satoru_api.cpp`
#include "satoru_api.h"

#include <emscripten.h>
#include <stdio.h>

#include <chrono>
#include <cstdarg>
#include <cstring>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <vector>

#include "api/js_logger.h"
#ifndef HTML_TO_IMAGE_PDF_ONLY
#include "common/pdf_cmyk.h"
#endif
#include "common/string_utils.h"
#include "core/container_skia.h"
#include "core/master_css.h"
#include "core/resource_manager.h"
#include "core/satoru_context.h"
#include "renderers/pdf_renderer.h"
#include "renderers/png_renderer.h"
#include "renderers/svg_renderer.h"
#ifndef HTML_TO_IMAGE_PDF_ONLY
#include "renderers/webp_renderer.h"
#endif
#include "utils/logging.h"
#ifndef HTML_TO_IMAGE_PDF_ONLY
#include "utils/pdf_merger.h"
#endif

// --- Global Logger (for legacy SATORU_LOG_* macros) ---
static satoru::JsLogger g_js_logger;

// P1: 共有JsLoggerアクセサ (宣言は satoru_api.h)。converter側の log level 集約用。
// 追加シンボルのみで既存ABI不変。
satoru::JsLogger* satoru_api_get_js_logger() { return &g_js_logger; }

// --- Helpers ---
// P1: 汎用文字列処理 (json_escape / codepoints_to_utf8) は common/string_utils.h に寄せた。
// 下記は satoru 固有のヘルパーのみ残す。返却値・throw 挙動は不変。
namespace {

// Noto Sans JP に含まれない文字体系の fallback 書体 (Google Fonts css2)。
// key は font_manager の uncoveredCodepoints で被覆根拠から除外される索引 family 名。
struct ScriptFallback {
    const char* key;
    const char* css;
};
const ScriptFallback kScriptFallbacks[] = {
    {"satoru-fb-hangul", "https://fonts.googleapis.com/css2?family=Noto+Sans+KR"},
    {"satoru-fb-hebrew", "https://fonts.googleapis.com/css2?family=Noto+Sans+Hebrew"},
    {"satoru-fb-arabic", "https://fonts.googleapis.com/css2?family=Noto+Sans+Arabic"},
    {"satoru-fb-thai", "https://fonts.googleapis.com/css2?family=Noto+Sans+Thai"},
    {"satoru-fb-devanagari", "https://fonts.googleapis.com/css2?family=Noto+Sans+Devanagari"},
    {"satoru-fb-greek", "https://fonts.googleapis.com/css2?family=Noto+Sans"},
};

int script_fallback_group(char32_t cp) {
    if ((cp >= 0x1100 && cp <= 0x11FF) || (cp >= 0x3130 && cp <= 0x318F) ||
        (cp >= 0xA960 && cp <= 0xA97F) || (cp >= 0xAC00 && cp <= 0xD7FF))
        return 0;
    if ((cp >= 0x0590 && cp <= 0x05FF) || (cp >= 0xFB1D && cp <= 0xFB4F)) return 1;
    if ((cp >= 0x0600 && cp <= 0x06FF) || (cp >= 0x0750 && cp <= 0x077F) ||
        (cp >= 0x08A0 && cp <= 0x08FF) || (cp >= 0xFB50 && cp <= 0xFDFF) ||
        (cp >= 0xFE70 && cp <= 0xFEFF))
        return 2;
    if (cp >= 0x0E00 && cp <= 0x0E7F) return 3;
    if (cp >= 0x0900 && cp <= 0x097F) return 4;
    if ((cp >= 0x0370 && cp <= 0x03FF) || (cp >= 0x1F00 && cp <= 0x1FFF)) return 5;
    return -1;
}
typedef sk_sp<SkData> SkDataPtr;

// PDF colour space: Skia writes DeviceRGB, `colorSpace: 1` rewrites the finished file to CMYK.
// A file that cannot be converted is an error (null), never a silent RGB result.
sk_sp<SkData> to_requested_color_space(sk_sp<SkData> pdf, const SatoruRenderOptions& options) {
    if (!pdf || options.colorSpace != 1) return pdf;
#ifdef HTML_TO_IMAGE_PDF_ONLY
    return nullptr;
#else
    return html_to_image::pdf_to_cmyk(*pdf);
#endif
}

template <typename F>
const uint8_t* render_and_store(SatoruInstance* inst, F render_func,
                                void (SatoruContext::*setter)(sk_sp<SkData>), int& out_size) {
    auto data = render_func();
    if (!data) {
        out_size = 0;
        return nullptr;
    }
    out_size = (int)data->size();
    const uint8_t* bytes = data->bytes();
    (inst->context.*setter)(std::move(data));
    return bytes;
}
}  // namespace

// --- SatoruInstance Implementation ---

SatoruInstance::SatoruInstance() : resourceManager(context) {
    // Set up global logger for legacy SATORU_LOG_* macros
    satoru_log_set_logger(&g_js_logger);
    // Set logger on context for direct ILogger usage
    context.setLogger(&g_js_logger);
    context.init();
    cached_full_master_css = std::string(litehtml::master_css) + "\n" + satoru_master_css +
                             "\nbr { display: -litehtml-br !important; }\n";
}

SatoruInstance::~SatoruInstance() {}

const std::string& SatoruInstance::get_full_master_css() const { return cached_full_master_css; }

static void scan_image_sizes(litehtml::element::ptr el, SatoruContext& context) {
    if (!el) return;
    const char* tag = el->get_tagName();
    if (tag && (strcmp(tag, "img") == 0 || strcmp(tag, "image") == 0)) {
        const char* src = el->get_attr("src");
        if (!src) src = el->get_attr("href");
        if (!src) src = el->get_attr("xlink:href");

        const char* w_str = el->get_attr("width");
        const char* h_str = el->get_attr("height");
        if (src && w_str && h_str) {
            int w = atoi(w_str);
            int h_val = atoi(h_str);
            if (w > 0 && h_val > 0) {
                int curr_w, curr_h;
                if (!context.get_image_size(src, curr_w, curr_h) || (curr_w == 0 && curr_h == 0)) {
                    context.loadImage(src, nullptr, w, h_val);
                }
            }
        }
    }
    for (auto& child : el->children()) {
        scan_image_sizes(child, context);
    }
}

bool SatoruInstance::document_ready_for(const std::string& html, int width, int height,
                                       int mediaType) const {
    litehtml::media_type mt =
        (mediaType == 1) ? litehtml::media_type_print : litehtml::media_type_screen;
    // collect_resources() stores the normalised width, so normalise here too: a raw-width
    // caller (e.g. width 0 straight into the wasm API) would otherwise never hit the cache.
    const int layout_width = normalize_viewport_width(width);
    return doc && render_container && html == last_parsed_html &&
           context.getCssVersion() == last_css_version &&
           context.getFontVersion() == last_font_version && (int)mt == last_media_type &&
           layout_width == last_width && height == last_height && !context.needsRelayout;
}

void SatoruInstance::collect_resources(const std::string& html, int width, int height,
                                       int mediaType) {
    // Non-TS callers can reach this with width 0 (the TS layer defaults it to 800).
    width = normalize_viewport_width(width);
    auto elapsed_ms = [](const auto& start, const auto& end) {
        return std::chrono::duration<double, std::milli>(end - start).count();
    };
    profile_scan_font_faces_ms = 0.0;
    profile_create_document_ms = 0.0;
    profile_render_layout_ms = 0.0;
    profile_scan_image_sizes_ms = 0.0;
    profile_font_requests_ms = 0.0;
    profile_requested_font_count = 0;
    profile_font_url_count = 0;
    profile_font_requests_with_urls_count = 0;
    profile_provider_font_request_count = 0;
    profile_mapped_font_url_request_count = 0;
    profile_font_character_count = 0;
    profile_measured_font_character_count = 0;
    profile_ext_missing_count = 0;
    profile_hana_url_count = 0;
    profile_rebuild_count = 0;
    profile_rebuild_initial_count = 0;
    profile_rebuild_html_count = 0;
    profile_rebuild_css_count = 0;
    profile_rebuild_font_count = 0;
    profile_rebuild_media_count = 0;
    profile_layout_count = 0;
    profile_layout_size_count = 0;
    profile_layout_relayout_count = 0;
    context.layoutProfile.enabled = collect_profile_enabled;
    // reset() once per instance, not once per discovery round: several rounds lay the document out
    // (and measure text) before the last, resource-free round; resetting each time would report
    // only that idle round. The JS layer builds one instance per render, so this is per render.
    if (!doc) context.layoutProfile.reset();

    try {
        litehtml::media_type mt =
            (mediaType == 1) ? litehtml::media_type_print : litehtml::media_type_screen;
        uint64_t cssVersion = context.getCssVersion();
        bool rebuild_initial = !doc;
        bool rebuild_html = html != last_parsed_html;
        bool rebuild_css = cssVersion != last_css_version;
        bool rebuild_font = context.getFontVersion() != last_font_version;
        bool rebuild_media = mt != (litehtml::media_type)last_media_type;
        if (rebuild_initial || rebuild_html || rebuild_css || rebuild_font || rebuild_media) {
            bool is_first_pass = (doc == nullptr);
            if (collect_profile_enabled) {
                profile_rebuild_count++;
                if (rebuild_initial) profile_rebuild_initial_count++;
                if (rebuild_html) profile_rebuild_html_count++;
                if (rebuild_css) profile_rebuild_css_count++;
                if (rebuild_font) profile_rebuild_font_count++;
                if (rebuild_media) profile_rebuild_media_count++;
            }

            doc.reset();  // Destroy doc first so it doesn't use the old container!
            last_parsed_html = html;
            last_extra_css_size = context.getExtraCss().size();
            last_css_version = cssVersion;
            last_font_version = context.getFontVersion();
            last_image_version = context.getImageVersion();
            last_width = -1;  // Force re-layout
            last_media_type = (int)mt;
            image_sizes_scanned = false;
            int initial_height = (height > 0) ? height : k_default_viewport_height;
            render_container = std::make_unique<container_skia>(
                width, initial_height, nullptr, context, &resourceManager, false, mt);

            if (html != last_font_face_scan_html) {
                auto scan_start = std::chrono::high_resolution_clock::time_point{};
                if (collect_profile_enabled) scan_start = std::chrono::high_resolution_clock::now();
                context.fontManager.scanFontFaces(html.c_str());
                if (collect_profile_enabled) {
                    auto scan_end = std::chrono::high_resolution_clock::now();
                    profile_scan_font_faces_ms += elapsed_ms(scan_start, scan_end);
                }
                last_font_face_scan_html = html;
            }

            auto create_start = std::chrono::high_resolution_clock::time_point{};
            if (collect_profile_enabled) create_start = std::chrono::high_resolution_clock::now();
            doc = litehtml::document::createFromString(html.c_str(), render_container.get(),
                                                       get_full_master_css().c_str(),
                                                       context.getExtraCss().c_str(),
                                                       context.getPageCss().c_str());
            if (collect_profile_enabled) {
                auto create_end = std::chrono::high_resolution_clock::now();
                profile_create_document_ms += elapsed_ms(create_start, create_end);
            }
            if (render_container) {
                render_container->set_document(doc.get());
            }

            if (is_first_pass && !image_sizes_scanned) {
                // For the first pass, scan images WITHOUT full render to start loading them early
                auto scan_images_start = std::chrono::high_resolution_clock::time_point{};
                if (collect_profile_enabled)
                    scan_images_start = std::chrono::high_resolution_clock::now();
                scan_image_sizes(doc->root(), context);
                if (collect_profile_enabled) {
                    auto scan_images_end = std::chrono::high_resolution_clock::now();
                    profile_scan_image_sizes_ms += elapsed_ms(scan_images_start, scan_images_end);
                }
                image_sizes_scanned = true;
            }
        }

        if (doc) {
            if (width != last_width || height != last_height || context.needsRelayout) {
                if (collect_profile_enabled) {
                    profile_layout_count++;
                    if (width != last_width || height != last_height) profile_layout_size_count++;
                    if (context.needsRelayout) profile_layout_relayout_count++;
                }
                auto render_start = std::chrono::high_resolution_clock::time_point{};
                if (collect_profile_enabled)
                    render_start = std::chrono::high_resolution_clock::now();
                doc->render(width);
                if (collect_profile_enabled) {
                    auto render_end = std::chrono::high_resolution_clock::now();
                    profile_render_layout_ms += elapsed_ms(render_start, render_end);
                }
                last_width = width;
                last_height = height;
                context.needsRelayout = false;
                if (render_container) {
                    render_container->set_height(doc->height());
                }
            }
            if (!image_sizes_scanned) {
                auto scan_images_start = std::chrono::high_resolution_clock::time_point{};
                if (collect_profile_enabled)
                    scan_images_start = std::chrono::high_resolution_clock::now();
                scan_image_sizes(doc->root(), context);
                if (collect_profile_enabled) {
                    auto scan_images_end = std::chrono::high_resolution_clock::now();
                    profile_scan_image_sizes_ms += elapsed_ms(scan_images_start, scan_images_end);
                }
                image_sizes_scanned = true;
            }
        }
    } catch (const std::exception& e) {
        // P0: ログにエラーコード prefix を付与 (TS diagnostics.ts の DiagnosticMessage.code に集約される想定)。
        // 振る舞いは不変 (ログ後に再 throw。ABI/bindings 変更なし)。
        if (auto* logger = context.getLogger())
            logger->logf(LogLevel::Error, "[COLLECT_RESOURCES_FAILED] Exception in collect_resources: %s",
                         e.what());
        throw;
    } catch (...) {
        if (auto* logger = context.getLogger())
            logger->log(LogLevel::Error,
                        "[COLLECT_RESOURCES_FAILED] Unknown exception in collect_resources");
        throw;
    }

    auto font_requests_start = std::chrono::high_resolution_clock::time_point{};
    if (collect_profile_enabled) font_requests_start = std::chrono::high_resolution_clock::now();
    render_container->add_used_codepoints(context.svgImageCodepoints);
    const auto& usedCodepoints = render_container->get_used_codepoints();
    auto requestedAttribs = render_container->get_requested_font_attributes();

    // Ensure sans-serif is always requested as a fallback if not already present
    bool hasSansSerif = false;
    for (const auto& req : requestedAttribs) {
        if (req.family == "sans-serif") {
            hasSansSerif = true;
            break;
        }
    }
    // The families the <text> of SVG images names (the layout never sees them).
    for (const auto& family : context.svgImageFamilies) {
        for (int weight : {400, 700}) {
            if (weight == 700 && !context.svgImageBold) continue;
            font_request req;
            req.family = family;
            req.weight = weight;
            req.slant = SkFontStyle::kUpright_Slant;
            requestedAttribs.insert(req);
        }
    }
    if (!hasSansSerif) {
        font_request req;
        req.family = "sans-serif";
        req.weight = 400;
        req.slant = SkFontStyle::kUpright_Slant;
        requestedAttribs.insert(req);
    }

    // 名前付き family (例: "Liberation Mono", "Georgia") はメトリクスプロファイル
    // 経由で汎用 family の代替書体で描かれるが、font-family に汎用名が無ければ
    // その代替書体は一切要求されず、matchMetricFallbackFonts が sans の実体に
    // 縦メトリクスだけ載せた形へ劣化する (等幅指定がプロポーショナル字形になる)。
    // プロファイルの汎用 family も同じ weight/slant で要求しておく。
    {
        std::vector<font_request> genericReqs;
        for (const auto& req : requestedAttribs) {
            const std::string generic =
                context.fontManager.metricProfileGenericFamily(req.family);
            if (!generic.empty() && generic != req.family) {
                font_request greq = req;
                greq.family = generic;
                genericReqs.push_back(greq);
            }
        }
        for (const auto& greq : genericReqs) requestedAttribs.insert(greq);
    }

    if (collect_profile_enabled) profile_requested_font_count = (int)requestedAttribs.size();
    for (const auto& req : requestedAttribs) {
        std::string charactersStr;
        std::vector<char32_t> usedFontCharacters;
        render_container->collect_used_font_characters(req, usedFontCharacters);
        const std::set<char32_t>* measuredFontCodepoints =
            usedFontCharacters.empty() ? render_container->get_measured_font_codepoints(req)
                                       : nullptr;
        if (!usedFontCharacters.empty()) {
            charactersStr = html_to_image::codepoints_to_utf8(usedFontCharacters);
        }
        if (collect_profile_enabled) {
            profile_font_character_count += (int)usedFontCharacters.size();
            if (measuredFontCodepoints) {
                profile_measured_font_character_count += (int)measuredFontCodepoints->size();
            }
        }

        std::set<char32_t> usedFontCodepoints;
        const std::set<char32_t>* fontUrlCodepoints = &usedCodepoints;
        if (!usedFontCharacters.empty()) {
            usedFontCodepoints.insert(usedFontCharacters.begin(), usedFontCharacters.end());
            fontUrlCodepoints = &usedFontCodepoints;
        } else if (measuredFontCodepoints) {
            fontUrlCodepoints = measuredFontCodepoints;
        }
        std::vector<std::string> urls =
            context.fontManager.getFontUrls(req.family, req.weight, req.slant, fontUrlCodepoints);
        if (collect_profile_enabled) {
            profile_font_url_count += (int)urls.size();
            if (!urls.empty()) profile_font_requests_with_urls_count++;
        }

        if (urls.empty()) {
            auto loaded = context.fontManager.matchFonts(req.family, req.weight, req.slant);
            bool needRequest = loaded.empty();

            if (!loaded.empty()) {
                // If the loaded font has a significantly different weight, we might still want to
                // request a specific weight from the font map.
                int loadedWeight = loaded[0]->fontStyle().weight();
                int loadedSlant = loaded[0]->fontStyle().slant();
                if (std::abs(loadedWeight - req.weight) > 100 || loadedSlant != req.slant) {
                    needRequest = true;
                }
            }

            if (needRequest) {
                const auto& fontMap = context.getFontMap();
                auto it = fontMap.find(req.family);
                if (it != fontMap.end()) {
                    const std::string& mapped = it->second;
                    if (mapped.substr(0, 7) == "http://" || mapped.substr(0, 8) == "https://") {
                        if (collect_profile_enabled) profile_mapped_font_url_request_count++;
                        resourceManager.request(mapped, req.family, ResourceType::Font, false,
                                                charactersStr);
                    } else {
                        // Treat as font family name for Google Fonts
                        std::string providerUrl =
                            "provider:google-fonts?family=" + mapped +
                            "&weight=" + std::to_string(req.weight) +
                            "&italic=" + (req.slant == SkFontStyle::kItalic_Slant ? "1" : "0");

                        if (!charactersStr.empty()) {
                            providerUrl += "&text=" + charactersStr;
                        }

                        resourceManager.request(providerUrl, req.family, ResourceType::Font, false,
                                                charactersStr);
                        if (collect_profile_enabled) profile_provider_font_request_count++;
                    }
                }
            }
        } else {
            for (const auto& url : urls) {
                resourceManager.request(url, req.family, ResourceType::Font, false, charactersStr);
            }
        }
    }

    // CJK 外字 fallback: Google Fonts の subset (unicode-range) に含まれない
    // Ext-A 以降・互換漢字補足等の usedCodepoints を HanaMin のブロック別 woff2 で補う。
    // __fallback__ 名で読み込み、per-glyph 選択 (selectFont) が拾う。
    {
        static const std::string kHanaminBase = "https://cdn.jsdelivr.net/npm/hanamin@0.0.5/";
        auto missing = context.fontManager.uncoveredCodepoints(usedCodepoints);
        if (!missing.empty()) {
            auto& unicode = context.getUnicodeService();
            std::set<char32_t> extMissing;
            bool emojiMissing = false;
            std::map<int, std::set<char32_t>> scriptMissing;
            for (char32_t cp : missing) {
                // 絵文字は別経路 (Noto Color Emoji)。HanaMin 取得対象から外す。
                if (unicode.isEmoji(cp)) {
                    emojiMissing = true;
                } else if (int g = script_fallback_group(cp); g >= 0) {
                    scriptMissing[g].insert(cp);
                } else {
                    extMissing.insert(cp);
                }
            }
            // Noto Sans JP が持たない文字体系 (ハングル・ヘブライ・アラビア・タイ・デーヴァナーガリー)
            // は文字体系別の Noto Sans を取得し、per-glyph 選択の global fallback へ登録する。
            for (const auto& [group, cps] : scriptMissing) {
                const auto& sf = kScriptFallbacks[group];
                std::vector<std::string> urls = context.fontManager.getFontUrls(
                    sf.key, 400, SkFontStyle::kUpright_Slant, &cps);
                if (urls.empty()) {
                    // subset 一覧 CSS を一度だけ取得 (取得済みなら request 側で dedup)。
                    resourceManager.request(sf.css, sf.key, ResourceType::Font, false);
                } else {
                    for (const auto& u : urls) {
                        resourceManager.request(u, "__fallback__", ResourceType::Font, false);
                    }
                }
            }
            // font-family に絵文字フォントが無くても、どの書体にも無い絵文字があれば
            // fontMap の Noto Color Emoji を自動取得する (未取得だと字が消える)。
            if (emojiMissing) {
                static const std::string kEmojiFamily = "Noto Color Emoji";
                const auto& fontMap = context.getFontMap();
                auto it = fontMap.find(kEmojiFamily);
                if (it != fontMap.end() &&
                    context.fontManager.matchFonts(kEmojiFamily, 400, SkFontStyle::kUpright_Slant)
                        .empty()) {
                    resourceManager.request(it->second, kEmojiFamily, ResourceType::Font, false);
                }
            }
            if (!extMissing.empty()) {
                std::vector<std::string> hanaUrls = context.fontManager.getFontUrls(
                    "hanamin", 400, SkFontStyle::kUpright_Slant, &extMissing);
                profile_ext_missing_count = (int)extMissing.size();
                profile_hana_url_count = (int)hanaUrls.size();
                if (hanaUrls.empty()) {
                    // HanaMin.css 未走査 (または該当なし): subset 一覧 CSS を一度だけ取得。
                    // 解決済みなら request 側で dedup される。
                    resourceManager.request(kHanaminBase + "HanaMin.css", "hanamin",
                                            ResourceType::Css, false);
                } else {
                    for (const auto& u : hanaUrls) {
                        std::string abs = u;
                        if (abs.find("://") == std::string::npos) abs = kHanaminBase + abs;
                        resourceManager.request(abs, "__fallback__", ResourceType::Font, false);
                    }
                }
            }
        }
    }
    if (collect_profile_enabled) {
        auto font_requests_end = std::chrono::high_resolution_clock::now();
        profile_font_requests_ms += elapsed_ms(font_requests_start, font_requests_end);
    }
}

std::string SatoruInstance::get_collect_profile_json() const {
    std::ostringstream ss;
    ss << "{\"cppScanFontFaces\":" << profile_scan_font_faces_ms
       << ",\"cppCreateDocument\":" << profile_create_document_ms
       << ",\"cppRenderLayout\":" << profile_render_layout_ms
       << ",\"cppScanImageSizes\":" << profile_scan_image_sizes_ms
       << ",\"cppFontRequests\":" << profile_font_requests_ms
       << ",\"cppRequestedFontCount\":" << profile_requested_font_count
       << ",\"cppFontUrlCount\":" << profile_font_url_count
       << ",\"cppFontRequestsWithUrlsCount\":" << profile_font_requests_with_urls_count
       << ",\"cppProviderFontRequestCount\":" << profile_provider_font_request_count
       << ",\"cppMappedFontUrlRequestCount\":" << profile_mapped_font_url_request_count
       << ",\"cppFontCharacterCount\":" << profile_font_character_count
       << ",\"cppMeasuredFontCharacterCount\":" << profile_measured_font_character_count
       << ",\"cppExtMissingCount\":" << profile_ext_missing_count
       << ",\"cppHanaUrlCount\":" << profile_hana_url_count
       << ",\"cppDocumentRebuildCount\":" << profile_rebuild_count
       << ",\"cppDocumentRebuildInitialCount\":" << profile_rebuild_initial_count
       << ",\"cppDocumentRebuildHtmlCount\":" << profile_rebuild_html_count
       << ",\"cppDocumentRebuildCssCount\":" << profile_rebuild_css_count
       << ",\"cppDocumentRebuildFontCount\":" << profile_rebuild_font_count
       << ",\"cppDocumentRebuildMediaCount\":" << profile_rebuild_media_count
       << ",\"cppLayoutCount\":" << profile_layout_count
       << ",\"cppLayoutSizeCount\":" << profile_layout_size_count
       << ",\"cppLayoutRelayoutCount\":" << profile_layout_relayout_count
       << ",\"cppCssVersion\":" << context.getCssVersion()
       << ",\"cppUserCssVersion\":" << context.getUserCssVersion()
       << ",\"cppExternalCssVersion\":" << context.getExternalCssVersion()
       << ",\"cppFontResourceCssVersion\":" << context.getFontResourceCssVersion()
       << ",\"cppFontAliasCssVersion\":" << context.getFontAliasCssVersion()
       << ",\"cppGeneratedFontFaceCssVersion\":" << context.getGeneratedFontFaceCssVersion()
       << ",\"cppFontResourceNamedLoadCount\":" << context.getFontResourceNamedLoadCount()
       << ",\"cppFontResourceFallbackLoadCount\":" << context.getFontResourceFallbackLoadCount()
       << ",\"cppFontResourceRegisteredCount\":" << context.getFontResourceRegisteredCount()
       << ",\"cppFontResourceDuplicateLoadCount\":" << context.getFontResourceDuplicateLoadCount()
       << ",\"cppGeneratedFontFaceAttemptCount\":" << context.getGeneratedFontFaceAttemptCount()
       << ",\"cppGeneratedFontFaceAddedCount\":" << context.getGeneratedFontFaceAddedCount()
       << ",\"cppGeneratedFontFaceDuplicateCount\":" << context.getGeneratedFontFaceDuplicateCount()
       << ",\"cppCreateFont\":" << context.layoutProfile.container_create_font_ms
       << ",\"cppTextWidth\":" << context.layoutProfile.container_text_width_ms
       << ",\"cppSplitText\":" << context.layoutProfile.container_split_text_ms
       << ",\"cppBidiLevel\":" << context.layoutProfile.container_bidi_ms
       << ",\"cppTextMeasure\":" << context.layoutProfile.text_measure_ms
       << ",\"cppTextAnalyze\":" << context.layoutProfile.text_analyze_ms
       << ",\"cppTextShape\":" << context.layoutProfile.text_shape_ms
       << ",\"cppTextShapePrepared\":" << context.layoutProfile.text_shape_prepared_ms
       << ",\"cppCreateFontCount\":" << context.layoutProfile.container_create_font_count
       << ",\"cppTextWidthCount\":" << context.layoutProfile.container_text_width_count
       << ",\"cppTextWidthUniqueCount\":" << context.layoutProfile.container_text_width_unique_count
       << ",\"cppTextWidthDuplicateCount\":"
       << context.layoutProfile.container_text_width_duplicate_count
       << ",\"cppSplitTextCount\":" << context.layoutProfile.container_split_text_count
       << ",\"cppBidiLevelCount\":" << context.layoutProfile.container_bidi_count
       << ",\"cppTextMeasureCount\":" << context.layoutProfile.text_measure_count
       << ",\"cppTextMeasureCacheableCount\":" << context.layoutProfile.text_measure_cacheable_count
       << ",\"cppTextMeasureCacheHitCount\":" << context.layoutProfile.text_measure_cache_hit_count
       << ",\"cppTextMeasureMissCount\":" << context.layoutProfile.text_measure_miss_count
       << ",\"cppTextMeasureMissColdCount\":" << context.layoutProfile.text_measure_miss_cold_count
       << ",\"cppTextMeasureMissWidthCount\":" << context.layoutProfile.text_measure_miss_width_count
       << ",\"cppTextMeasureMissVersionCount\":"
       << context.layoutProfile.text_measure_miss_version_count
       << ",\"cppTextMeasureMissEvictedCount\":"
       << context.layoutProfile.text_measure_miss_evicted_count
       << ",\"cppTextAnalyzeCount\":" << context.layoutProfile.text_analyze_count
       << ",\"cppTextShapeCount\":" << context.layoutProfile.text_shape_count
       << ",\"cppTextShapePreparedCount\":" << context.layoutProfile.text_shape_prepared_count
       << ",\"cppTextShapeCacheableCount\":" << context.layoutProfile.text_shape_cacheable_count
       << ",\"cppTextShapeCacheHitCount\":" << context.layoutProfile.text_shape_cache_hit_count
       << "}";
    return ss.str();
}

void SatoruInstance::add_resource(const std::string& url, ResourceType type,
                                  const std::vector<uint8_t>& data) {
    resourceManager.add(url.c_str(), data.data(), (int)data.size(), type);
}

void SatoruInstance::scan_css(const std::string& css) {
    if (context.addCss(css.c_str(), CssChangeKind::UserScan)) {
        context.fontManager.scanFontFaces(css.c_str());
    }
}

void SatoruInstance::load_font(const std::string& name, const std::vector<uint8_t>& data) {
    context.load_font(name.c_str(), data.data(), (int)data.size());
}

void SatoruInstance::load_image(const std::string& name, const std::string& data_url, int width,
                                int height) {
    context.load_image(name.c_str(), data_url.c_str(), width, height);
}

void SatoruInstance::load_image_pixels(const std::string& name, int width, int height,
                                       const std::vector<uint8_t>& pixels,
                                       const std::string& data_url) {
    context.loadImageFromPixels(name.c_str(), width, height, pixels.data(), data_url.c_str());
}

const uint8_t* SatoruInstance::get_pending_resources_binary(int& out_size) {
    auto requests = resourceManager.getPendingRequests();
    pending_resources_buffer.clear();
    if (requests.empty()) {
        out_size = 0;
        return nullptr;
    }

    pending_resources_buffer.reserve(requests.size() * 128);

    auto write_u32 = [&](uint32_t val) {
        pending_resources_buffer.push_back((uint8_t)(val & 0xFF));
        pending_resources_buffer.push_back((uint8_t)((val >> 8) & 0xFF));
        pending_resources_buffer.push_back((uint8_t)((val >> 16) & 0xFF));
        pending_resources_buffer.push_back((uint8_t)((val >> 24) & 0xFF));
    };

    auto write_str = [&](const std::string& s) {
        write_u32((uint32_t)s.size());
        for (char c : s) pending_resources_buffer.push_back((uint8_t)c);
    };

    write_u32((uint32_t)requests.size());
    for (const auto& req : requests) {
        pending_resources_buffer.push_back((uint8_t)req.type);
        pending_resources_buffer.push_back(req.redraw_on_ready ? 1 : 0);
        write_str(req.url);
        write_str(req.name);
        write_str(req.characters);
    }

    out_size = (int)pending_resources_buffer.size();
    return pending_resources_buffer.data();
}

// --- API Functions (Wrappers) ---

SatoruInstance* satoru_api_create_instance() { return new SatoruInstance(); }

void satoru_api_destroy_instance(SatoruInstance* inst) { delete inst; }

std::string satoru_api_html_to_svg(SatoruInstance* inst, const char* html, int width, int height,
                            const SatoruRenderOptions& options) {
    // Deliberately no document reuse here (unlike the bitmap path): the collect-phase
    // container draws a different SVG (embedded @font-face etc.), so reusing it
    // would change the output.
    width = normalize_viewport_width(width);
    return renderHtmlToSvg(html, width, height, inst->context, inst->get_full_master_css().c_str(),
                           inst->context.getExtraCss().c_str(), options);
}

bool satoru_api_html_to_bitmap(SatoruInstance* inst, const char* html, int width, int height,
                               const SatoruRenderOptions& options, SkBitmap& out) {
    if (!inst) return false;
    width = normalize_viewport_width(width);
    // Reuse the document collect_resources already built when it is current.
    if (inst->document_ready_for(html, width, height, options.mediaType)) {
        return renderDocumentToBitmap(inst, width, height, options, out);
    }
    return renderHtmlToBitmap(html, width, height, inst->context,
                              inst->get_full_master_css().c_str(),
                              inst->context.getExtraCss().c_str(), options, out);
}

const uint8_t* satoru_api_html_to_png(SatoruInstance* inst, const char* html, int width, int height,
                               const SatoruRenderOptions& options, int& out_size) {
    width = normalize_viewport_width(width);
    return render_and_store(
        inst,
        [&]() {
            return renderHtmlToPng(html, width, height, inst->context,
                                   inst->get_full_master_css().c_str(),
                                   inst->context.getExtraCss().c_str(), options);
        },
        &SatoruContext::set_last_png, out_size);
}

const uint8_t* satoru_api_html_to_webp(SatoruInstance* inst, const char* html, int width, int height,
                                const SatoruRenderOptions& options, int& out_size) {
    width = normalize_viewport_width(width);
    return render_and_store(
        inst,
        [&]() {
#ifdef HTML_TO_IMAGE_PDF_ONLY
            return sk_sp<SkData>();
#else
            return renderHtmlToWebp(html, width, height, inst->context,
                                    inst->get_full_master_css().c_str(),
                                    inst->context.getExtraCss().c_str(), options);
#endif
        },
        &SatoruContext::set_last_webp, out_size);
}

const uint8_t* satoru_api_html_to_pdf(SatoruInstance* inst, const char* html, int width, int height,
                               const SatoruRenderOptions& options, int& out_size) {
    width = normalize_viewport_width(width);
    return render_and_store(
        inst,
        [&]() {
            // P1: single HTML -> Layout-once + Slice multi-page path (independent of the array
            // renderHtmlsToPdf).
            return to_requested_color_space(
                renderHtmlToPdf(html, width, height, inst->context,
                                inst->get_full_master_css().c_str(),
                                inst->context.getExtraCss().c_str(), options),
                options);
        },
        &SatoruContext::set_last_pdf, out_size);
}

const uint8_t* satoru_api_htmls_to_pdf(SatoruInstance* inst, const std::vector<std::string>& htmls,
                                int width, int height, const SatoruRenderOptions& options,
                                int& out_size) {
    width = normalize_viewport_width(width);
    return render_and_store(
        inst,
        [&]() {
            return to_requested_color_space(
                renderHtmlsToPdf(htmls, width, height, inst->context,
                                 inst->get_full_master_css().c_str(),
                                 inst->context.getExtraCss().c_str(), options),
                options);
        },
        &SatoruContext::set_last_pdf, out_size);
}

const uint8_t* satoru_api_render(SatoruInstance* inst, const std::vector<std::string>& htmls, int width,
                          int height, RenderFormat format, const SatoruRenderOptions& options,
                          int& out_size) {
    if (!inst) {
        out_size = 0;
        return nullptr;
    }
    if (htmls.empty()) {
        out_size = 0;
        return nullptr;
    }
    width = normalize_viewport_width(width);

    switch (format) {
        case RenderFormat::SVG: {
            std::string svg = satoru_api_html_to_svg(inst, htmls[0].c_str(), width, height, options);
            auto data = SkData::MakeWithCopy(svg.c_str(), svg.length());
            inst->context.set_last_svg(std::move(data));
            out_size = (int)inst->context.get_last_svg()->size();
            return inst->context.get_last_svg()->bytes();
        }
        case RenderFormat::PNG:
            return satoru_api_html_to_png(inst, htmls[0].c_str(), width, height, options, out_size);
        case RenderFormat::WebP:
            return satoru_api_html_to_webp(inst, htmls[0].c_str(), width, height, options, out_size);
        case RenderFormat::PDF:
            // P1: a single HTML paginates (Layout-once + Slice); the array form keeps its
            // documented one-item-per-page behaviour.
            if (htmls.size() == 1) {
                return satoru_api_html_to_pdf(inst, htmls[0].c_str(), width, height, options,
                                              out_size);
            }
            return satoru_api_htmls_to_pdf(inst, htmls, width, height, options, out_size);
        default:
            break;
    }
    out_size = 0;
    return nullptr;
}

int satoru_api_get_last_png_size(SatoruInstance* inst) { return (int)inst->context.get_last_png_size(); }
int satoru_api_get_last_webp_size(SatoruInstance* inst) { return (int)inst->context.get_last_webp_size(); }
int satoru_api_get_last_pdf_size(SatoruInstance* inst) { return (int)inst->context.get_last_pdf_size(); }
int satoru_api_get_last_svg_size(SatoruInstance* inst) { return (int)inst->context.get_last_svg_size(); }

void satoru_api_collect_resources(SatoruInstance* inst, const std::string& html, int width, int height,
                           int mediaType) {
    inst->context.refreshSvgImages();
    inst->collect_resources(html, normalize_viewport_width(width), height, mediaType);
}

void satoru_api_collect_resources_pages(SatoruInstance* inst, const std::vector<std::string>& htmls,
                                        int width, int height, int mediaType) {
    int layout_width = normalize_viewport_width(width);
    inst->context.refreshSvgImages();
    for (const auto& html : htmls) {
        inst->collect_resources(html, layout_width, height, mediaType);
    }
}

std::string satoru_api_get_collect_profile(SatoruInstance* inst) {
    return inst ? inst->get_collect_profile_json() : "{}";
}

void satoru_api_set_collect_profile_enabled(SatoruInstance* inst, bool enabled) {
    if (inst) inst->set_collect_profile_enabled(enabled);
}

void satoru_api_add_resource(SatoruInstance* inst, const std::string& url, int type,
                      const std::vector<uint8_t>& data) {
    inst->add_resource(url, (ResourceType)type, data);
}

void satoru_api_scan_css(SatoruInstance* inst, const std::string& css) { inst->scan_css(css); }

void satoru_api_load_font(SatoruInstance* inst, const std::string& name,
                   const std::vector<uint8_t>& data) {
    inst->load_font(name, data);
}

void satoru_api_load_fallback_font(SatoruInstance* inst, const std::vector<uint8_t>& data) {
    inst->context.loadFont("__fallback__", data.data(), (int)data.size());
    auto tfs =
        inst->context.fontManager.matchFonts("__fallback__", 400, SkFontStyle::kUpright_Slant);
    if (!tfs.empty()) {
        inst->context.fontManager.addFallbackTypeface(tfs[0]);
        inst->context.markFontChanged();
    }
}

void satoru_api_load_image(SatoruInstance* inst, const std::string& name, const std::string& data_url,
                    int width, int height) {
    inst->load_image(name, data_url, width, height);
}

void satoru_api_set_font_map(SatoruInstance* inst, const std::map<std::string, std::string>& fontMap) {
    inst->context.setFontMap(fontMap);
}

void satoru_api_set_log_level(int level) { g_js_logger.setLogLevel((LogLevel)level); }

const uint8_t* satoru_api_get_pending_resources_binary(SatoruInstance* inst, int& out_size) {
    return inst->get_pending_resources_binary(out_size);
}
