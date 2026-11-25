// The scene params schema's rows (viz/params/ParamTable.inc) as data: the wire names, one ParamSpec per slot,
// and the text table the shell reads through geode_params_schema_text. The ranges some rows quote
// from their owners (the catalogue's touch and Detail ranges, the cymatics plate, the fluid tiers) are
// why this file includes those headers; the struct itself (Params.hpp) needs none of them.
#include <string>

#include "viz/Catalogue.hpp"
#include "viz/params/Params.hpp"
#include "viz/TableNumber.hpp"
#include "viz/fluid/FluidQuality.hpp"
#include "viz/scenes/CymaticsMath.hpp"

namespace geode::viz {

const std::array<const char*, SceneParams::kFieldCount>& SceneParams::fieldNames() {
    static const std::array<const char*, kFieldCount> kNames = {{
#define GEODE_PARAM(kind, name, def, lo, hi, fade) #name,
#include "viz/params/ParamTable.inc"
#undef GEODE_PARAM
    }};
    return kNames;
}

const std::array<ParamSpec, SceneParams::kFieldCount>& SceneParams::schema() {
    static const std::array<ParamSpec, kFieldCount> kSchema = {{
#define GEODE_PARAM(kind, name, def, lo, hi, fade)                                                      \
    {#name, ParamKind::kind, static_cast<float>(def), static_cast<float>(lo), static_cast<float>(hi), \
     ParamFade::fade},
#include "viz/params/ParamTable.inc"
#undef GEODE_PARAM
    }};
    return kSchema;
}

namespace {

const char* kindName(ParamKind kind) {
    switch (kind) {
        case ParamKind::Float: return "FLOAT";
        case ParamKind::Int: return "INT";
        case ParamKind::Bool: return "BOOL";
        case ParamKind::Circular: return "CIRCULAR";
    }
    return "FLOAT";
}

// How many meta rows name each slot. A live row has exactly one and a retired row none, checked here
// at compile time so a row added without its meta row, or a retired one that kept it, does not build.
constexpr std::array<int, SceneParams::kFieldCount> metaRowCounts() {
    std::array<int, SceneParams::kFieldCount> n{};
#define GEODE_PARAM_META(name, label, scope, flags, lo, hi) ++n[static_cast<size_t>(ParamSlot::name)];
#include "viz/params/ParamTable.inc"
#undef GEODE_PARAM_META
    return n;
}

constexpr bool everyLiveRowHasOneMeta() {
    const auto n = metaRowCounts();
    for (int slot = 0; slot < SceneParams::kFieldCount; ++slot) {
        if (n[static_cast<size_t>(slot)] != (SceneParams::retired(static_cast<ParamSlot>(slot)) ? 0 : 1)) return false;
    }
    return true;
}
static_assert(everyLiveRowHasOneMeta(), "ParamTable.inc: every live row needs exactly one GEODE_PARAM_META row, a retired row none");

// A mod row (a modulation target, viz/modulation/ModTable.hpp) only under a live Float or Circular row that fades and has a
// Customize label (not "@": a field read before the modulation runs), at most one per field, and kMod in the field's meta
// flags exactly when it has one: an offset on a pick, a count, a switch or a held value would jump, not glide.
constexpr bool modRowsOnContinuousFields() {
    constexpr int n = SceneParams::kFieldCount;
    std::array<bool, n> continuous{};
    std::array<bool, n> labelled{};
    std::array<bool, n> modFlag{};
    std::array<int, n> mods{};
#define GEODE_PARAM(kind, name, def, lo, hi, fade)                \
    continuous[static_cast<size_t>(ParamSlot::name)] =            \
        (ParamKind::kind == ParamKind::Float || ParamKind::kind == ParamKind::Circular) && ParamFade::fade == ParamFade::Fade;
#define GEODE_PARAM_META(name, label, scope, flags, lo, hi)                                    \
    labelled[static_cast<size_t>(ParamSlot::name)] = (label)[0] != '\0' && (label)[0] != '@'; \
    modFlag[static_cast<size_t>(ParamSlot::name)] = ((flags) & kMod) != 0;
#define GEODE_PARAM_MOD(name, scale, flags) ++mods[static_cast<size_t>(ParamSlot::name)];
#include "viz/params/ParamTable.inc"
#undef GEODE_PARAM_MOD
#undef GEODE_PARAM_META
#undef GEODE_PARAM
    for (int slot = 0; slot < n; ++slot) {
        const auto i = static_cast<size_t>(slot);
        if (mods[i] > 1) return false;
        if (mods[i] == 1 && (!continuous[i] || !labelled[i] || SceneParams::retired(static_cast<ParamSlot>(slot)))) return false;
        if (modFlag[i] != (mods[i] == 1)) return false;
    }
    return true;
}
static_assert(modRowsOnContinuousFields(), "ParamTable.inc: a GEODE_PARAM_MOD row only on a live, labelled Float or Circular row that fades, kMod with it");

// A tier added without a label (or the other way round) does not build; CameraRig.cpp holds camPath the same way.
static_assert(choiceCount(ParamSlot::fluidQuality) == fluid::quality::kTierCount, "fluidQuality: one label per tier");

}  // namespace

const std::vector<std::string_view>& SceneParams::choices(ParamSlot slot) {
    static const std::array<std::vector<std::string_view>, kFieldCount> kChoices = [] {
        std::array<std::vector<std::string_view>, kFieldCount> out{};
#define GEODE_PARAM_CHOICES(name, ...) out[static_cast<size_t>(ParamSlot::name)] = {__VA_ARGS__};
#include "viz/params/ParamTable.inc"
#undef GEODE_PARAM_CHOICES
        return out;
    }();
    static const std::vector<std::string_view> kNone;
    const auto i = static_cast<size_t>(slot);
    return i < kChoices.size() ? kChoices[i] : kNone;
}

const std::array<ParamMeta, SceneParams::kFieldCount>& SceneParams::metaTable() {
    static const std::array<ParamMeta, kFieldCount> kMeta = [] {
        std::array<ParamMeta, kFieldCount> out{};
        out.fill(ParamMeta{false, "", catalogue::Scope::Universal, 0u, 0.0f, 0.0f});
#define GEODE_PARAM_META(name, label, scope, flags, lo, hi)                                                        \
    out[static_cast<size_t>(ParamSlot::name)] =                                                                    \
        ParamMeta{true, label, catalogue::Scope::scope, static_cast<unsigned>(flags), static_cast<float>(lo), \
                  static_cast<float>(hi)};
#include "viz/params/ParamTable.inc"
#undef GEODE_PARAM_META
        return out;
    }();
    return kMeta;
}

void SceneParams::keepInputs(const float* current, const float* incoming, float* out) {
    const auto& meta = metaTable();
    for (size_t i = 0; i < meta.size(); ++i) out[i] = (meta[i].flags & kInput) != 0 ? current[i] : incoming[i];
}

const std::string& SceneParams::schemaText() {
    static const std::string kText = [] {
        std::string out = "params|" + std::to_string(kSchemaTextVersion) + "|" + std::to_string(kFieldCount) + "\n";
        const auto& rows = schema();
        for (int slot = 0; slot < kFieldCount; ++slot) {
            const ParamSpec& s = rows[static_cast<size_t>(slot)];
            out += "param|" + std::to_string(slot) + "|" + s.name + "|" + kindName(s.kind) + "|" + tableNumber(s.defaultValue) +
                   "|" + tableNumber(s.min) + "|" + tableNumber(s.max) + "|" + (s.fade == ParamFade::Fade ? "FADE" : "HOLD") +
                   (retired(static_cast<ParamSlot>(slot)) ? "|RETIRED\n" : "|LIVE\n");
        }
        // The listed built-in palettes' names: Customize's labels for the `palette` index, so Kotlin keeps no copy.
        // The retired rows after them are not offered, so not named.
        for (int i = 0; i < kListedPalettes; ++i) out += "palette|" + std::to_string(i) + "|" + palettes()[static_cast<size_t>(i)].name + "\n";
        // Every picker's labels, the same way.
        for (int slot = 0; slot < kFieldCount; ++slot) {
            const auto& labels = choices(static_cast<ParamSlot>(slot));
            for (size_t i = 0; i < labels.size(); ++i) {
                out += std::string("choice|") + rows[static_cast<size_t>(slot)].name + "|" + std::to_string(i) + "|" + std::string(labels[i]) + "\n";
            }
        }
        return out;
    }();
    return kText;
}

}  // namespace geode::viz
