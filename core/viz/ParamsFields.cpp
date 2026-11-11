// The scene params schema's rows (viz/ParamTable.inc) as data: the wire names, one ParamSpec per slot,
// and the text table the shell reads through geode_params_schema_text. The ranges some rows quote
// from their owners (the catalogue's touch and Detail ranges, the cymatics plate, the fluid tiers) are
// why this file includes those headers; the struct itself (Params.hpp) needs none of them.
#include <string>

#include "viz/Catalogue.hpp"
#include "viz/Params.hpp"
#include "viz/TableNumber.hpp"
#include "viz/fluid/FluidQuality.hpp"
#include "viz/scenes/CymaticsMath.hpp"

namespace geode::viz {

const std::array<const char*, SceneParams::kFieldCount>& SceneParams::fieldNames() {
    static const std::array<const char*, kFieldCount> kNames = {{
#define GEODE_PARAM(kind, name, def, lo, hi, fade) #name,
#include "viz/ParamTable.inc"
#undef GEODE_PARAM
    }};
    return kNames;
}

const std::array<ParamSpec, SceneParams::kFieldCount>& SceneParams::schema() {
    static const std::array<ParamSpec, kFieldCount> kSchema = {{
#define GEODE_PARAM(kind, name, def, lo, hi, fade)                                                      \
    {#name, ParamKind::kind, static_cast<float>(def), static_cast<float>(lo), static_cast<float>(hi), \
     ParamFade::fade},
#include "viz/ParamTable.inc"
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
#include "viz/ParamTable.inc"
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

}  // namespace

const std::array<ParamMeta, SceneParams::kFieldCount>& SceneParams::metaTable() {
    static const std::array<ParamMeta, kFieldCount> kMeta = [] {
        std::array<ParamMeta, kFieldCount> out{};
        out.fill(ParamMeta{false, "", catalogue::Scope::Universal, 0u, 0.0f, 0.0f});
#define GEODE_PARAM_META(name, label, scope, flags, lo, hi)                                                        \
    out[static_cast<size_t>(ParamSlot::name)] =                                                                    \
        ParamMeta{true, label, catalogue::Scope::scope, static_cast<unsigned>(flags), static_cast<float>(lo), \
                  static_cast<float>(hi)};
#include "viz/ParamTable.inc"
#undef GEODE_PARAM_META
        return out;
    }();
    return kMeta;
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
        return out;
    }();
    return kText;
}

}  // namespace geode::viz
