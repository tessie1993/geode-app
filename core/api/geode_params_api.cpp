#include <cstring>
#include <optional>
#include <set>
#include <sstream>
#include <string>

#include "api/geode_api.h"
#include "viz/CustomizeMap.hpp"
#include "viz/Config.hpp"
#include "viz/ParamRandomizer.hpp"
#include "viz/Params.hpp"
#include "viz/TouchTransform.hpp"

using geode::viz::SceneParams;
namespace randomizer = geode::viz::randomizer;
namespace customize = geode::viz::customize;

namespace {

std::set<std::string> lockedSet(const char* locked) {
    std::set<std::string> keys;
    if (locked) {
        std::istringstream lines(locked);
        for (std::string key; std::getline(lines, key);) keys.insert(key);
    }
    return customize::expandLocks(keys);
}

int copyOut(const SceneParams& before, const SceneParams& after, float* out) {
    after.toWire(out);
    float was[SceneParams::kFieldCount];
    before.toWire(was);
    for (int i = 0; i < SceneParams::kFieldCount; ++i) {
        if (was[i] != out[i]) return 1;
    }
    return 0;
}

}  // namespace

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
    const SceneParams rolled = randomizer::randomize(current, seed, only, style_id ? style_id : "", lockedSet(locked));
    return copyOut(current, rolled, out);
}

void geode_params_reset_tab(const char* tab, const float* in, float* out) {
    const SceneParams current = SceneParams::fromWire(in);
    const auto which = tab ? randomizer::tabNamed(tab) : std::nullopt;
    (which ? randomizer::resetTab(current, *which) : current).toWire(out);
}

const char* geode_params_schema_text(void) { return SceneParams::schemaText().c_str(); }

const char* geode_config_schema_text(void) { return geode::viz::config::schemaText().c_str(); }

float geode_touch_zoom(float current, float scale_factor) { return geode::viz::touch::zoom(current, scale_factor); }

float geode_touch_rotation(float current, float degrees) { return geode::viz::touch::rotation(current, degrees); }

const char* geode_params_roll_keys(void) {
    static const std::string kText = [] {
        std::string out;
        for (const auto& roll : randomizer::rolledKeys()) out += std::string(randomizer::name(roll.tab)) + "|" + roll.key + "\n";
        return out;
    }();
    return kText.c_str();
}

const char* geode_customize_text(void) { return customize::text().c_str(); }

int geode_customize_lockable(const char* key) {
    if (!key) return 0;
    for (const auto& roll : randomizer::rolledKeys()) {
        if (std::strcmp(roll.key, key) == 0) return 1;
    }
    return 0;
}

const char* geode_customize_lockable_keys(void) { return randomizer::lockableKeysText().c_str(); }

const char* geode_customize_shown(const char* style_id) {
    thread_local std::string shown;
    shown = customize::shownOn(style_id ? style_id : "");
    return shown.c_str();
}

int geode_customize_randomize(const char* style_id, const char* category, const char* locked, uint64_t seed, const float* in,
                              float* out) {
    const SceneParams current = SceneParams::fromWire(in);
    const auto which = category ? customize::categoryNamed(category) : std::nullopt;
    if (!which) {
        current.toWire(out);
        return 0;
    }
    const SceneParams rolled = customize::randomize(current, seed, *which, style_id ? style_id : "", lockedSet(locked));
    return copyOut(current, rolled, out);
}

void geode_customize_reset(const char* category, const float* in, float* out) {
    const SceneParams current = SceneParams::fromWire(in);
    const auto which = category ? customize::categoryNamed(category) : std::nullopt;
    (which ? customize::reset(current, *which) : current).toWire(out);
}

}
