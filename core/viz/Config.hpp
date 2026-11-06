#pragma once
#include <array>
#include <cmath>
#include <string>

namespace geode::viz::config {

// The config params schema (#423): the values the data stores hold beside the scene params (LFO and
// envelope slots, macro knobs, the palette maker's gradient, the look's envelope, the Studio clip edit,
// the preset morph), one row each in viz/ConfigTable.inc. Everything here is constexpr and header-only,
// so viz/Lfo, viz/Adsr and viz/Macros read their defaults and ranges from the rows without a new
// translation unit to link; only the shell's text table (geode_config_schema_text) lives in
// viz/ConfigFields.cpp. The conversions the stores run on old saves are in viz/ConfigMath.hpp.

enum class Kind { Float, Int, Bool };
enum class Unit { None, Seconds, Fraction, Turns, Degrees, Points, Times };

// Every row's slot, as <group>_<name>: Slot::lfo_rateSeconds is the LFO period's row.
enum class Slot : int {
#define GEODE_CONFIG(group, name, kind, def, lo, hi, unit) group##_##name,
#include "viz/ConfigTable.inc"
#undef GEODE_CONFIG
    kCount
};
inline constexpr int kCount = static_cast<int>(Slot::kCount);

struct Spec {
    const char* group;
    const char* name;
    Kind kind;
    float defaultValue;
    float min;
    float max;
    Unit unit;
};

inline constexpr std::array<Spec, kCount> kTable = {{
#define GEODE_CONFIG(group, name, kind, def, lo, hi, unit)                                                      \
    {#group, #name, Kind::kind, static_cast<float>(def), static_cast<float>(lo), static_cast<float>(hi), \
     Unit::unit},
#include "viz/ConfigTable.inc"
#undef GEODE_CONFIG
}};

constexpr const Spec& spec(Slot s) { return kTable[static_cast<size_t>(s)]; }

namespace detail {
constexpr bool sameText(const char* a, const char* b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}
}  // namespace detail

// A row's position inside its group: for lfo and adsr the float index on the geode_viz_set_lfo /
// geode_viz_set_adsr wire, for macro the knob index.
constexpr int indexInGroup(Slot s) {
    int index = 0;
    for (int i = 0; i < static_cast<int>(s); ++i) {
        if (detail::sameText(kTable[static_cast<size_t>(i)].group, spec(s).group)) ++index;
    }
    return index;
}

// v held to the row's range, the way Kotlin's coerceIn and std::clamp hold it: a NaN passes through.
constexpr float clampTo(Slot s, float v) {
    const Spec& r = spec(s);
    return v < r.min ? r.min : (r.max < v ? r.max : v);
}

// clampTo, with a non-finite value read as the row's default: what a store does with a number it
// cannot trust (a hand-edited prefs file, a shared link).
inline float sanitized(Slot s, float v) { return std::isfinite(v) ? clampTo(s, v) : spec(s).defaultValue; }

// The rows as the shell reads them (geode_config_schema_text). '|'-separated, one per line:
//   configs|<kTextVersion>|<kCount>
//   config|<group>|<index>|<name>|<FLOAT|INT|BOOL>|<default>|<min>|<max>|<UNIT>
// one config row per table row, in table order; <index> is indexInGroup; numbers in the shortest form
// that reads back to the same float; UNIT is NONE, SECONDS, FRACTION, TURNS, DEGREES, POINTS or TIMES.
inline constexpr int kTextVersion = 1;
const std::string& schemaText();

}  // namespace geode::viz::config
