#include "viz/Params.hpp"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace geode::viz {

const std::array<SceneParams::Palette, 21>& SceneParams::palettes() {
    static const std::array<Palette, 21> kPalettes = {{
        {"Spectrum", 0.0f, 1.0f},   {"Neon", 0.5f, 0.45f},    {"Fire", 0.0f, 0.14f},    {"Ocean", 0.5f, 0.2f},
        {"Mono", 0.6f, 0.02f},      {"Candy", 0.85f, 0.5f},   {"Forest", 0.33f, 0.18f}, {"Aurora", 0.45f, 0.7f},
        {"Sunset", 0.05f, 0.3f},    {"Ice", 0.55f, 0.15f},    {"Vapor", 0.78f, 0.35f},  {"Toxic", 0.25f, 0.25f},
        {"Royal", 0.7f, 0.25f},     {"Blush", 0.93f, 0.12f},  {"Copper", 0.07f, 0.1f},  {"Mint", 0.4f, 0.12f},
        {"Galaxy", 0.65f, 0.5f},    {"Cherry", 0.97f, 0.08f}, {"Cyan", 0.5f, 0.08f},    {"Magenta", 0.833f, 0.08f},
        {"Yellow", 0.167f, 0.08f},
    }};
    return kPalettes;
}

namespace {
const SceneParams::Palette& paletteAt(int index) {
    const auto& table = SceneParams::palettes();
    return table[static_cast<size_t>(std::clamp(index, 0, static_cast<int>(table.size()) - 1))];
}
}  // namespace

float SceneParams::paletteBase() const { return paletteBaseOverride >= 0.0f ? paletteBaseOverride : paletteAt(palette).base; }
float SceneParams::paletteRange() const { return paletteRangeOverride >= 0.0f ? paletteRangeOverride : paletteAt(palette).range; }
float SceneParams::palette2Base() const { return palette2BaseOverride >= 0.0f ? palette2BaseOverride : paletteAt(palette2).base; }
float SceneParams::palette2Range() const { return palette2RangeOverride >= 0.0f ? palette2RangeOverride : paletteAt(palette2).range; }

// The wire's float count. Kotlin's SceneParamsCodec packs by the schema text, so it follows on its own,
// but the Kotlin SceneParams data class needs the new field and core/tests/params_schema.txt needs
// rewriting (params_schema_test --write): a row added to viz/ParamTable.inc fails here first, on purpose.
static_assert(SceneParams::kFieldCount == 173, "a ParamTable.inc row was added or removed: update SceneParams.kt and params_schema.txt too");

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
    if (slot < 0 || slot >= kFieldCount) return false;
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
