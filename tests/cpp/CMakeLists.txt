# tests/cpp/CMakeLists.txt — ホスト単体テスト (P5)
#
# 方針: 外部依存を増やさない (gtest等のFetchContent/vcpkg追加なし)。
#   assert相当の自前CHECKを持つ単一cpp + ctest登録のみ。
# 使い方:
#   (a) 単独 (推奨・WASM設定と無関係): cmake -S tests/cpp -B build-host-tests && cmake --build build-host-tests && ctest --test-dir build-host-tests
#   (b) ルート経由: ホストconfigure時のみトップのガード付きブロックから add_subdirectory される
#       (EMSCRIPTEN/Emscripten時は登録されない = WASMビルド無影響)。
# 既存ターゲット・フラグ・ソースリストへの変更なし。

# 単独configure時のみ project 定義 (add_subdirectory 時は二重定義を避ける)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.14)
    project(html_to_image_host_tests CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    include(CTest)
endif()

add_executable(test_string_utils test_string_utils.cpp)
# string_utils.h は標準ライブラリのみのため、src/cpp を -I にするだけでホストコンパイル可
target_include_directories(test_string_utils PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../src/cpp")
target_compile_features(test_string_utils PRIVATE cxx_std_20)

add_test(NAME string_utils COMMAND test_string_utils)

add_executable(test_lru_cache test_lru_cache.cpp)
target_include_directories(test_lru_cache PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../src/cpp")
target_compile_features(test_lru_cache PRIVATE cxx_std_20)

add_test(NAME lru_cache COMMAND test_lru_cache)

# animation_sampler.h は標準ライブラリのみのため、src/cpp を -I にするだけでホストコンパイル可
add_executable(test_animation_sampler test_animation_sampler.cpp)
target_include_directories(test_animation_sampler PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../src/cpp")
target_compile_features(test_animation_sampler PRIVATE cxx_std_20)

add_test(NAME animation_sampler COMMAND test_animation_sampler)

# core/svg_paint_utils.h (color-mix evaluation, marker geometry) is standard-library only
add_executable(test_svg_paint_utils test_svg_paint_utils.cpp)
target_include_directories(test_svg_paint_utils PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../src/cpp")
target_compile_features(test_svg_paint_utils PRIVATE cxx_std_20)

add_test(NAME svg_paint_utils COMMAND test_svg_paint_utils)

# mermaid/: diagrams drawn by the engine (standard library only)
add_library(mermaid_core STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/cpp/mermaid/mermaid.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/cpp/mermaid/flowchart.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/cpp/mermaid/layered.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/cpp/mermaid/sequence.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/cpp/mermaid/state.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/cpp/mermaid/classdiagram.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/cpp/mermaid/erdiagram.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/cpp/mermaid/xychart.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/cpp/mermaid/pie.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/cpp/mermaid/gantt.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/cpp/mermaid/mindmap.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/cpp/mermaid/timeline.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/cpp/mermaid/journey.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/cpp/mermaid/quadrant.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/cpp/mermaid/gitgraph.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/cpp/mermaid/kanban.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/cpp/mermaid/packet.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/cpp/mermaid/sankey.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/cpp/mermaid/requirement.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/cpp/mermaid/block.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/cpp/mermaid/treemap.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/cpp/mermaid/radar.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/cpp/mermaid/info.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/cpp/mermaid/architecture.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/cpp/mermaid/c4.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/cpp/mermaid/venn.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/cpp/mermaid/ishikawa.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/cpp/mermaid/treeview.cpp")
target_include_directories(mermaid_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../../src/cpp")
target_compile_features(mermaid_core PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(mermaid_core PRIVATE /utf-8)
endif()

add_executable(mermaid_cli mermaid_cli.cpp)
target_link_libraries(mermaid_cli PRIVATE mermaid_core)

add_executable(test_mermaid test_mermaid.cpp)
target_link_libraries(test_mermaid PRIVATE mermaid_core)
add_test(NAME mermaid COMMAND test_mermaid)
