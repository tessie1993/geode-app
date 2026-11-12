#include "viz/Params.hpp"

#include "viz/Colour.hpp"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace geode::viz {

const std::array<SceneParams::Palette, 21>& SceneParams::palettes() {
    // The classic rows of the palette library (viz/PaletteTable.inc), so the two cannot drift.
    static const std::array<Palette, 21> kPalettes = {{
#define GEODE_CLASSIC(id, name, base, range) {name, base, range},
#define GEODE_COLOURS(...)
#include "viz/PaletteTable.inc"
#undef GEODE_COLOURS
#undef GEODE_CLASSIC
    }};
    return kPalettes;
}

namespace {
const SceneParams::Palette& paletteAt(int index) {
    const auto& table = SceneParams::palettes();
    return table[static_cast<size_t>(std::clamp(index, 0, static_cast<int>(table.size()) - 1))];
}
}  // namespace

int SceneParams::paletteStopCount() const {
    const auto stops = paletteStops();
    return colour::count(stops.data(), static_cast<int>(stops.size()));
}

// Your colours win over a saved hue gradient and the built-in: the first stop's hue, the stops' span.
float SceneParams::paletteBase() const {
    const auto stops = paletteStops();
    if (paletteStopCount() > 0) return colour::baseHue(stops.data(), static_cast<int>(stops.size()));
    return paletteBaseOverride >= 0.0f ? paletteBaseOverride : paletteAt(palette).base;
}
float SceneParams::paletteRange() const {
    const auto stops = paletteStops();
    if (paletteStopCount() > 0) return colour::hueSpan(stops.data(), static_cast<int>(stops.size()));
    return paletteRangeOverride >= 0.0f ? paletteRangeOverride : paletteAt(palette).range;
}

// The wire's float count. The shell's SceneParams reads the slots from the schema text, so it follows on its own,
// but core/tests/params_schema.txt, the JVM tests' copy, needs rewriting (params_schema_test --write):
// a row added to viz/ParamTable.inc fails here first, on purpose.
static_assert(SceneParams::kFieldCount == 198, "a ParamTable.inc row was added or removed: update params_schema.txt too");

namespace {

// Ints and bools travel as floats: an int rounds, a bool is value > 0.5, as the wire always read them.
template <typename T>
float toFloat(T v) {
    if constexpr (std::is_same_v<T, bool>) {
        return v ? 1.0f : 0.0f;
    } else {
        return static_cast<float>(v);
    }
}

template <typename T>
T fromFloat(float v) {
    if constexpr (std::is_same_v<T, bool>) {
        return v > 0.5f;
    } else if constexpr (std::is_same_v<T, int>) {
        return static_cast<int>(std::lround(v));
    } else {
        return v;
    }
}

// One reader and one writer per wire slot, expanded from the table, so get, set and the wire never
// keep their own list of fields.
struct Access {
    float (*get)(const SceneParams&);
    void (*set)(SceneParams&, float);
};

const std::array<Access, SceneParams::kFieldCount> kAccess = {{
#define GEODE_PARAM(kind, name, def, lo, hi, fade)                                      \
    {[](const SceneParams& p) { return toFloat(p.name); },                               \
     [](SceneParams& p, float v) { p.name = fromFloat<GEODE_PARAM_TYPE_##kind>(v); }},
#include "viz/ParamTable.inc"
#undef GEODE_PARAM
}};

// lerpParams' step for one field: a Fade field glides, a Hold field keeps `to`'s value (out starts as `to`).
template <ParamFade F, typename T>
void glide(T& out, const T& a, const T& b, float k) {
    if constexpr (F == ParamFade::Fade) {
        static_assert(std::is_same_v<T, float>, "only a Float or Circular row can Fade");
        out = a + (b - a) * k;
    } else {
        (void)out;
        (void)a;
        (void)b;
        (void)k;
    }
}

}  // namespace

int SceneParams::slotOf(std::string_view name) {
    const auto& names = fieldNames();
    for (int i = 0; i < kFieldCount; ++i) {
        if (name == names[static_cast<size_t>(i)]) return i;
    }
    return -1;
}

bool SceneParams::setSlot(int slot, float value) {
    if (slot < 0 || slot >= kFieldCount || retired(static_cast<ParamSlot>(slot))) return false;
    kAccess[static_cast<size_t>(slot)].set(*this, value);
    return true;
}

float SceneParams::getSlot(int slot) const {
    if (slot < 0 || slot >= kFieldCount) return 0.0f;
    return kAccess[static_cast<size_t>(slot)].get(*this);
}

bool SceneParams::set(std::string_view name, float value) { return setSlot(slotOf(name), value); }

float SceneParams::get(std::string_view name) const { return getSlot(slotOf(name)); }

SceneParams SceneParams::fromWire(const float* values) {
    SceneParams p;
    const auto& rows = schema();
    for (int i = 0; i < kFieldCount; ++i) {
        const ParamSpec& row = rows[static_cast<size_t>(i)];
        float v = values[i];
        // A non-finite value keeps the slot's default: stored, the resolver's lerp would hold a NaN for good.
        if (!std::isfinite(v)) continue;
        // An int is held to its row's range before it rounds, so a wild float never overflows lround.
        if (row.kind == ParamKind::Int) v = std::clamp(v, row.min, row.max);
        p.setSlot(i, v);
    }
    return p;
}

SceneParams SceneParams::sanitized(const float* values) {
    SceneParams p = fromWire(values);
    const auto& rows = schema();
    for (int i = 0; i < kFieldCount; ++i) {
        const ParamSpec& row = rows[static_cast<size_t>(i)];
        if (row.kind == ParamKind::Float) p.setSlot(i, std::clamp(p.getSlot(i), row.min, row.max));
    }
    return p;
}

void SceneParams::toWire(float* out) const {
    for (int i = 0; i < kFieldCount; ++i) out[i] = getSlot(i);
}

SceneParams lerpParams(const SceneParams& from, const SceneParams& to, float k) {
    SceneParams out = to;
#define GEODE_PARAM(kind, name, def, lo, hi, fade) glide<ParamFade::fade>(out.name, from.name, to.name, k);
#include "viz/ParamTable.inc"
#undef GEODE_PARAM
    return out;
}

}  // namespace geode::viz
