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

}  // namespace

const std::string& SceneParams::schemaText() {
    static const std::string kText = [] {
        std::string out = "params|" + std::to_string(kSchemaTextVersion) + "|" + std::to_string(kFieldCount) + "\n";
        const auto& rows = schema();
        for (int slot = 0; slot < kFieldCount; ++slot) {
            const ParamSpec& s = rows[static_cast<size_t>(slot)];
            out += "param|" + std::to_string(slot) + "|" + s.name + "|" + kindName(s.kind) + "|" + tableNumber(s.defaultValue) +
                   "|" + tableNumber(s.min) + "|" + tableNumber(s.max) + "|" + (s.fade == ParamFade::Fade ? "FADE" : "HOLD") + "\n";
        }
        return out;
    }();
    return kText;
}

}  // namespace geode::viz
