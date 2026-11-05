#include <set>
#include <sstream>
#include <string>

#include "api/geode_api.h"
#include "viz/Config.hpp"
#include "viz/ConfigMath.hpp"
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

const char* geode_params_schema_text(void) { return SceneParams::schemaText().c_str(); }

const char* geode_config_schema_text(void) { return geode::viz::config::schemaText().c_str(); }

float geode_config_lfo_rate_seconds_from_hz(float hz) { return geode::viz::config::lfoRateSecondsFromHz(hz); }

float geode_config_morph_seconds_from_legacy_beats(int beats) { return geode::viz::config::morphSecondsFromLegacyBeats(beats); }

float geode_config_palette_sample_hue(float base_hue, float hue_span, int index, int stops) {
    return geode::viz::config::paletteSampleHue(base_hue, hue_span, index, stops);
}

int geode_config_palette_preview_stops(void) { return geode::viz::config::kPalettePreviewStops; }

const char* geode_params_roll_keys(void) {
    static const std::string kText = [] {
        std::string out;
        for (const auto& roll : randomizer::rolledKeys()) out += std::string(randomizer::name(roll.tab)) + "|" + roll.key + "\n";
        return out;
    }();
    return kText.c_str();
}

}
