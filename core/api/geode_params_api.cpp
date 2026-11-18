#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
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
#include "viz/PanelRoll.hpp"
#include "viz/Params.hpp"
#include "viz/ParamsPalette.hpp"
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

// What an edit that threw hands back: the frame as it came in, and "nothing changed".
int unchanged(const float* in, float* out) {
    if (in && out) std::copy_n(in, SceneParams::kFieldCount, out);
    return 0;
}

}  // namespace

extern "C" {

int geode_params_randomize(const char* style_id, const char* tab, const char* locked, uint64_t seed, const float* in,
                           float* out) try {
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
} catch (...) {
    return unchanged(in, out);
}

void geode_params_reset_tab(const char* tab, const float* in, float* out) try {
    const SceneParams current = SceneParams::fromWire(in);
    const auto which = tab ? randomizer::tabNamed(tab) : std::nullopt;
    (which ? randomizer::resetTab(current, *which) : current).toWire(out);
} catch (...) {
    unchanged(in, out);
}

void geode_params_sanitize(const float* in, float* out) { SceneParams::sanitized(in).toWire(out); }

void geode_params_keep_inputs(const float* current, const float* in, float* out) {
    if (current && in && out) SceneParams::keepInputs(current, in, out);
}

const char* geode_params_schema_text(void) try {
    return SceneParams::schemaText().c_str();
} catch (...) {
    return nullptr;
}

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

int geode_params_uses_custom_palette(const float* frame) {
    return frame && geode::viz::palette_params::usesCustomPalette(SceneParams::fromWire(frame)) ? 1 : 0;
}

int geode_params_palette_stops(const float* frame, int* out) {
    return frame ? geode::viz::palette_params::stopsOf(SceneParams::fromWire(frame), out) : 0;
}

void geode_params_clear_custom_palette(const float* in, float* out) {
    if (!in || !out) return;
    geode::viz::palette_params::withoutCustomPalette(SceneParams::fromWire(in)).toWire(out);
}

void geode_params_apply_stops(const float* in, const int* stops, int count, float* out) {
    if (!in || !out) return;
    geode::viz::palette_params::withStops(SceneParams::fromWire(in), stops, count).toWire(out);
}

void geode_params_apply_gradient(const float* in, float base, float span, float* out) {
    if (!in || !out) return;
    geode::viz::palette_params::withGradient(SceneParams::fromWire(in), base, span).toWire(out);
}

const char* geode_config_schema_text(void) try {
    return geode::viz::config::schemaText().c_str();
} catch (...) {
    return nullptr;
}

float geode_touch_zoom(float current, float scale_factor) { return geode::viz::touch::zoom(current, scale_factor); }

float geode_touch_rotation(float current, float degrees) { return geode::viz::touch::rotation(current, degrees); }

int geode_params_control_range(const char* name, float* out) try {
    geode::viz::control::Range range{};
    if (!name || !out || !geode::viz::control::of(name, range)) return 0;
    out[0] = range.min;
    out[1] = range.max;
    return 1;
} catch (...) {
    return 0;
}

void geode_params_defaults(float* out) {
    if (out) SceneParams{}.toWire(out);
}

float geode_params_get(const float* frame, const char* name) try {
    const int slot = name ? SceneParams::slotOf(name) : -1;
    if (!frame || slot < 0 || SceneParams::retired(static_cast<geode::viz::ParamSlot>(slot))) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    return SceneParams::fromWire(frame).getSlot(slot);
} catch (...) {
    return std::numeric_limits<float>::quiet_NaN();
}

int geode_params_set(const float* in, const char* name, float value, float* out) try {
    if (!in || !name || !out) return 0;
    SceneParams edited = SceneParams::fromWire(in);
    if (!edited.set(name, value)) return 0;
    edited.toWire(out);
    return 1;
} catch (...) {
    return 0;
}

void geode_touch_to_ndc(float* xy, int points, float width, float height) { geode::viz::touch::toNdc(xy, points, width, height); }

void geode_touch_pair(const float* start, const float* last, const float* now, float* out) {
    if (!out) return;
    const auto r = geode::viz::touch::readPair(start, last, now);
    const float values[6] = {r.panX, r.panY, r.zoom, r.twist, r.stepFactor, r.stepDegrees};
    std::memcpy(out, values, sizeof(values));
}

int geode_touch_pulse(int step, float* xy) { return geode::viz::touch::pulse(step, xy); }

const char* geode_customize_text(void) try {
    return customize::text().c_str();
} catch (...) {
    return nullptr;
}

int geode_customize_lockable(const char* key) try {
    if (!key) return 0;
    for (const auto& roll : randomizer::rolledKeys()) {
        if (std::strcmp(roll.key, key) == 0) return 1;
    }
    return 0;
} catch (...) {
    return 0;
}

const char* geode_customize_lockable_keys(void) try {
    return randomizer::lockableKeysText().c_str();
} catch (...) {
    return nullptr;
}

const char* geode_customize_shown(const char* style_id) try {
    thread_local std::string shown;
    shown = customize::shownOn(style_id ? style_id : "");
    return shown.c_str();
} catch (...) {
    return nullptr;
}

int geode_customize_randomize(const char* style_id, const char* category, const char* locked, uint64_t seed, const float* in,
                              float* out) try {
    const SceneParams current = SceneParams::fromWire(in);
    const auto which = category ? customize::categoryNamed(category) : std::nullopt;
    if (!which) {
        current.toWire(out);
        return 0;
    }
    const SceneParams rolled = customize::randomize(current, seed, *which, style_id ? style_id : "", lockedSet(locked));
    return copyOut(current, rolled, out);
} catch (...) {
    return unchanged(in, out);
}

int geode_mod_waves_randomize(const char* style_id, const char* locked, uint64_t seed, int running_only,
                              const float* params, const char* lfos, char* out, int capacity) try {
    geode::viz::ModDocument doc;
    doc.readLfos(lfos ? lfos : "");
    const std::string was = doc.lfosText();
    if (!running_only || lockedSet(locked).count(waves::kLockKey) == 0) {
        doc.lfos = waves::randomize(doc.lfos, SceneParams::fromWire(params), seed, style_id ? style_id : "",
                                    running_only ? waves::Roll::Running : waves::Roll::All);
    }
    const std::string now = doc.lfosText();
    return geode::api::textOut(now == was ? std::string() : now, out, capacity);
} catch (...) {
    return geode::api::textOut(std::string(), out, capacity);
}

const char* geode_mod_waves_roll(const char* style_id, const char* locked, int all, const float* params, const char* lfos) try {
    thread_local std::string rolled;
    const uint64_t seed = geode::viz::panel_roll::nextSeed();
    const int need = geode_mod_waves_randomize(style_id, locked, seed, all ? 0 : 1, params, lfos, nullptr, 0);
    if (need <= 0) return nullptr;
    rolled.assign(static_cast<size_t>(need) + 1, '\0');
    geode_mod_waves_randomize(style_id, locked, seed, all ? 0 : 1, params, lfos, rolled.data(), need + 1);
    rolled.resize(static_cast<size_t>(need));
    return rolled.c_str();
} catch (...) {
    return nullptr;
}

int geode_panel_roll(const char* style_id, const char* category, const char* kept, int basics_only, const float* in,
                     float* out) try {
    namespace roll = geode::viz::panel_roll;
    const std::string keys = roll::locks(kept ? kept : "", basics_only != 0);
    const uint64_t seed = roll::nextSeed();
    const int changed = category ? geode_customize_randomize(style_id, category, keys.c_str(), seed, in, out)
                                 : geode_params_randomize(style_id, nullptr, keys.c_str(), seed, in, out);
    return roll::bits(changed != 0, category == nullptr, basics_only != 0);
} catch (...) {
    return unchanged(in, out);
}

int geode_panel_roll_all(const char* style_id, const char* kept, int basics_only, uint64_t seed, const float* before,
                         const float* base, float* out) try {
    namespace roll = geode::viz::panel_roll;
    const std::string keys = roll::locks(kept ? kept : "", basics_only != 0);
    const int changed = geode_randomize_all_roll(style_id, keys.c_str(), seed, 1, before, base, out);
    return roll::bits(changed != 0, true, basics_only != 0);
} catch (...) {
    return unchanged(base, out);
}

void geode_params_reroll_reactions(const float* in, float* out) {
    if (!in || !out) return;
    SceneParams look = SceneParams::fromWire(in);
    const int slot = SceneParams::slotOf("reactSeed");
    if (slot >= 0) {
        // One step on, wrapping to 0 past the row's top: held to the range, the top would stay the top.
        const long next = std::lround(look.getSlot(slot)) + 1;
        look.setSlot(slot, next > static_cast<long>(SceneParams::schema()[static_cast<size_t>(slot)].max) ? 0.0f : static_cast<float>(next));
    }
    look.toWire(out);
}

void geode_customize_borrow(const char* category, const float* in, const float* from, float* out) try {
    const SceneParams current = SceneParams::fromWire(in);
    const auto which = category ? customize::categoryNamed(category) : std::nullopt;
    (which ? customize::borrow(current, SceneParams::fromWire(from), *which) : current).toWire(out);
} catch (...) {
    unchanged(in, out);
}

void geode_customize_reset(const char* category, const float* in, float* out) try {
    const SceneParams current = SceneParams::fromWire(in);
    const auto which = category ? customize::categoryNamed(category) : std::nullopt;
    (which ? customize::reset(current, *which) : current).toWire(out);
} catch (...) {
    unchanged(in, out);
}

int geode_customize_changed_count(const float* in) { return customize::changedCount(SceneParams::fromWire(in)); }

}
