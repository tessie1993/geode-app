// Host-side checks for Customize › Layers' blends (core/viz/VisualSafety's BlendMode), the list the shell shows
// from geode_layer_list (GEODE_LAYER_LIST_BLENDS). Built and run by CI ("Native host tests" in .github/workflows/native-host-tests.yml)
// from the repository root, with the system compiler; no GL.
//
// What is pinned: one label per blend, in ordinal order, none empty or repeated; a new layer starts on the
// header's default, Screen; an unknown ordinal reads as Screen; and composite_frag.glsl's layerBlend has a case
// for every listed blend but Normal (its fallback) and none past the list, so the menu offers what the
// compositor draws.
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "api/geode_api.h"
#include "viz/LayerState.hpp"
#include "viz/VisualSafety.hpp"

using namespace geode::viz;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        failures++;
    }
}

void labels() {
    std::set<std::string> seen;
    for (int i = 0; i < kBlendModes; ++i) {
        const std::string name = blendModeLabel(static_cast<BlendMode>(i));
        check(!name.empty(), "label " + std::to_string(i) + " is not empty");
        check(name.find('\n') == std::string::npos, "label " + std::to_string(i) + " is one line");
        check(seen.insert(name).second, name + " is listed once");
    }
}

void defaultsAndFallback() {
    check(layer_state::Layer{}.blend == GEODE_LAYER_BLEND_DEFAULT, "a new layer starts on the header's default");
    check(blendModeFromOrdinal(GEODE_LAYER_BLEND_DEFAULT) == BlendMode::Screen, "the default is Screen");
    for (int i = 0; i < kBlendModes; ++i) {
        check(static_cast<int>(blendModeFromOrdinal(i)) == i, "ordinal " + std::to_string(i) + " is its blend");
    }
    check(blendModeFromOrdinal(-1) == BlendMode::Screen, "a negative ordinal is Screen");
    check(blendModeFromOrdinal(kBlendModes) == BlendMode::Screen, "an ordinal past the list is Screen");
}

void compositorDrawsEveryBlend() {
    std::ifstream in("app/src/main/assets/shaders/composite_frag.glsl");
    check(in.good(), "composite_frag.glsl not found: run from the repository root");
    std::stringstream all;
    all << in.rdbuf();
    const std::string src = all.str();
    const size_t start = src.find("vec3 layerBlend(");
    const size_t end = src.find("return clamp(r", start);
    check(start != std::string::npos && end != std::string::npos, "composite_frag.glsl has layerBlend");
    if (start == std::string::npos || end == std::string::npos) return;
    const std::string body = src.substr(start, end - start);
    const auto has = [&body](int i) { return body.find("mode == " + std::to_string(i) + ")") != std::string::npos; };
    check(!has(static_cast<int>(BlendMode::Normal)), "Normal is layerBlend's fallback, not a case");
    check(body.find("} else {\n        r = t;") != std::string::npos, "the fallback draws the top layer as it is");
    for (int i = 1; i < kBlendModes; ++i) {
        check(has(i), std::string(blendModeLabel(static_cast<BlendMode>(i))) + " has a case in layerBlend");
    }
    check(!has(kBlendModes), "layerBlend has no case past the list");
}

}  // namespace

int main() {
    labels();
    defaultsAndFallback();
    compositorDrawsEveryBlend();
    if (failures == 0) std::printf("layer_blend_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
