// Randomize everything (core/viz/RandomizeAll) behind the C ABI: the two-circle gesture's style, look and roll.
#include <algorithm>
#include <cstring>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "api/geode_api.h"
#include "viz/Params.hpp"
#include "viz/RandomizeAll.hpp"

using geode::viz::SceneParams;
namespace randomize_all = geode::viz::randomize_all;

namespace {

// One entry per line, empty lines kept, so a list's indices are the shell's.
std::vector<std::string> lines(const char* text) {
    std::vector<std::string> out;
    if (text) {
        std::istringstream in(text);
        for (std::string line; std::getline(in, line);) out.push_back(line);
    }
    return out;
}

std::set<std::string> lineSet(const char* text) {
    const auto all = lines(text);
    return {all.begin(), all.end()};
}

}  // namespace

extern "C" {

int geode_randomize_all_pick(uint64_t seed, const char* current_style, const char* locked_styles, const char* look_styles,
                             int milk_count, char* style_out, int style_capacity, int* look_out, int* milk_out) {
    const randomize_all::Pick picked =
        randomize_all::pick(seed, current_style ? current_style : "", lineSet(locked_styles), lines(look_styles), milk_count);
    if (style_out && style_capacity > 0) {
        const size_t n = std::min(picked.style.size(), static_cast<size_t>(style_capacity - 1));
        std::memcpy(style_out, picked.style.data(), n);
        style_out[n] = '\0';
    }
    if (look_out) *look_out = picked.look;
    if (milk_out) *milk_out = picked.milk;
    return picked.style.empty() ? 0 : 1;
}

int geode_randomize_all_roll(const char* style_id, const char* locked, uint64_t seed, int roll_categories, const float* before,
                             const float* base, float* out) {
    const SceneParams start = SceneParams::fromWire(base);
    const SceneParams rolled =
        randomize_all::roll(SceneParams::fromWire(before), start, seed, style_id ? style_id : "", lineSet(locked), roll_categories != 0);
    rolled.toWire(out);
    float was[SceneParams::kFieldCount];
    start.toWire(was);
    for (int i = 0; i < SceneParams::kFieldCount; ++i) {
        if (was[i] != out[i]) return 1;
    }
    return 0;
}

}
