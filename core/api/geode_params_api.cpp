#include <cstring>
#include <optional>
#include <set>
#include <sstream>
#include <string>

#include "api/geode_api.h"
#include "api/geode_modulation_api.h"
#include "api/geode_text_out.hpp"
#include "viz/ControlRange.hpp"
#include "viz/CustomizeMap.hpp"
#include "viz/Config.hpp"
#include "viz/ModDocument.hpp"
#include "viz/ParamRandomizer.hpp"
#include "viz/Params.hpp"
#include "viz/TouchGesture.hpp"
#include "viz/TouchTransform.hpp"
#include "viz/WaveRandomizer.hpp"

using geode::viz::SceneParams;
namespace randomizer = geode::viz::randomizer;
namespace customize = geode::viz::customize;
namespace waves = geode::viz::waves;

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
    const std::string style = style_id ? style_id : "";
    const SceneParams rolled = only ? randomizer::randomize(current, seed, only, style, lockedSet(locked))
                                    : customize::randomizeAll(current, seed, style, lockedSet(locked));
    return copyOut(current, rolled, out);
}

void geode_params_reset_tab(const char* tab, const float* in, float* out) {
    const SceneParams current = SceneParams::fromWire(in);
    const auto which = tab ? randomizer::tabNamed(tab) : std::nullopt;
    (which ? randomizer::resetTab(current, *which) : current).toWire(out);
}

void geode_params_sanitize(const float* in, float* out) { SceneParams::sanitized(in).toWire(out); }

const char* geode_params_schema_text(void) { return SceneParams::schemaText().c_str(); }

float geode_params_palette_base(int palette, float base_override) {
    SceneParams p;
    p.palette = palette;
    p.paletteBaseOverride = base_override;
    return p.paletteBase();
}

float geode_params_palette_range(int palette, float range_override) {
    SceneParams p;
    p.palette = palette;
    p.paletteRangeOverride = range_override;
    return p.paletteRange();
}

const char* geode_customize_kaleidoscope(int on, int symmetry) {
    thread_local std::string menu;
    menu = SceneParams::kaleidoscopeMenu(on != 0, symmetry);
    return menu.c_str();
}

int geode_params_kaleidoscope_pick(int row) { return SceneParams::kaleidoscopePick(row); }

const char* geode_config_schema_text(void) { return geode::viz::config::schemaText().c_str(); }

float geode_touch_zoom(float current, float scale_factor) { return geode::viz::touch::zoom(current, scale_factor); }

float geode_touch_rotation(float current, float degrees) { return geode::viz::touch::rotation(current, degrees); }

int geode_params_control_range(const char* name, float* out) {
    geode::viz::control::Range range{};
    if (!name || !out || !geode::viz::control::of(name, range)) return 0;
    out[0] = range.min;
    out[1] = range.max;
    return 1;
}

void geode_touch_to_ndc(float* xy, int points, float width, float height) { geode::viz::touch::toNdc(xy, points, width, height); }

void geode_touch_pair(const float* start, const float* last, const float* now, float* out) {
    if (!out) return;
    const auto r = geode::viz::touch::readPair(start, last, now);
    const float values[6] = {r.panX, r.panY, r.zoom, r.twist, r.stepFactor, r.stepDegrees};
    std::memcpy(out, values, sizeof(values));
}

int geode_touch_pulse(int step, float* xy) { return geode::viz::touch::pulse(step, xy); }

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

int geode_mod_waves_randomize(const char* style_id, const char* locked, uint64_t seed, int running_only,
                              const float* params, const char* lfos, char* out, int capacity) {
    geode::viz::ModDocument doc;
    doc.readLfos(lfos ? lfos : "");
    const std::string was = doc.lfosText();
    if (!running_only || lockedSet(locked).count(waves::kLockKey) == 0) {
        doc.lfos = waves::randomize(doc.lfos, SceneParams::fromWire(params), seed, style_id ? style_id : "",
                                    running_only ? waves::Roll::Running : waves::Roll::All);
    }
    const std::string now = doc.lfosText();
    return geode::api::textOut(now == was ? std::string() : now, out, capacity);
}

void geode_customize_borrow(const char* category, const float* in, const float* from, float* out) {
    const SceneParams current = SceneParams::fromWire(in);
    const auto which = category ? customize::categoryNamed(category) : std::nullopt;
    (which ? customize::borrow(current, SceneParams::fromWire(from), *which) : current).toWire(out);
}

void geode_customize_reset(const char* category, const float* in, float* out) {
    const SceneParams current = SceneParams::fromWire(in);
    const auto which = category ? customize::categoryNamed(category) : std::nullopt;
    (which ? customize::reset(current, *which) : current).toWire(out);
}

int geode_customize_changed_count(const float* in) { return customize::changedCount(SceneParams::fromWire(in)); }

}
