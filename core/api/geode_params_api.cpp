#include <set>
#include <sstream>
#include <string>

#include "api/geode_api.h"
#include "viz/ParamRandomizer.hpp"
#include "viz/Params.hpp"

using geode::viz::SceneParams;
namespace randomizer = geode::viz::randomizer;

extern "C" {

int geode_params_randomize(const char* style_id, const char* tab, const char* locked, uint64_t seed, const float* in,
                           float* out) {
    const SceneParams current = SceneParams::fromWire(in);
    std::optional<randomizer::Tab> only;
    if (tab) {
        only = randomizer::tabNamed(tab);
        if (!only) {
            current.toWire(out);
            return 0;
        }
    }
    std::set<std::string> lockedKeys;
    if (locked) {
        std::istringstream lines(locked);
        for (std::string key; std::getline(lines, key);) lockedKeys.insert(key);
    }
    const SceneParams rolled = randomizer::randomize(current, seed, only, style_id ? style_id : "", lockedKeys);
    rolled.toWire(out);
    float before[SceneParams::kFieldCount];
    current.toWire(before);
    for (int i = 0; i < SceneParams::kFieldCount; ++i) {
        if (before[i] != out[i]) return 1;
    }
    return 0;
}

void geode_params_reset_tab(const char* tab, const float* in, float* out) {
    const SceneParams current = SceneParams::fromWire(in);
    const auto which = tab ? randomizer::tabNamed(tab) : std::nullopt;
    (which ? randomizer::resetTab(current, *which) : current).toWire(out);
}

const char* geode_params_roll_keys(void) {
    static const std::string kText = [] {
        std::string out;
        for (const auto& roll : randomizer::rolledKeys()) out += std::string(randomizer::name(roll.tab)) + "|" + roll.key + "\n";
        return out;
    }();
    return kText.c_str();
}

void geode_params_blend(const float* a, const float* b, float t, float* out) {
    geode::viz::blendParams(SceneParams::fromWire(a), SceneParams::fromWire(b), t).toWire(out);
}

}
