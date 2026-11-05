#pragma once
#include <cstdio>
#include <string>
#include <vector>

#include "util/Json.hpp"

// The GL Transitions library asset (gl_transitions.json): parsed once here, for the renderer's
// composite (core/viz/Transition) and for the shell's one read of it (#438). The shell gets
// `encodeTransitionTable` through geode_transitions_table, so Kotlin no longer re-parses the JSON.
// Pure, with no GL and no assets, so the host can test it (core/tests/transition_library_test.cpp).
namespace geode::viz {

struct TransitionParam {
    std::string name;
    std::string type;
    std::vector<float> values;
};

struct TransitionDef {
    std::string name;
    std::string author;
    std::string license;
    std::vector<TransitionParam> params;
    std::string glsl;
};

// The shell's table. GLSL holds newlines and tabs but never ASCII control separators, so:
//   defs  separated by RS (0x1e)
//   def   name US author US license US params US glsl         (US = 0x1f)
//   params separated by GS (0x1d), each name FS type FS values (FS = 0x1c), values comma-separated
inline constexpr char kTableDef = '\x1e';
inline constexpr char kTableField = '\x1f';
inline constexpr char kTableParam = '\x1d';
inline constexpr char kTableParamField = '\x1c';

// parseTransitionLibrary: an empty list for text that is not a JSON array. Entries that are not
// objects are skipped. A param whose default is missing or not a number reads as one 0, and
// booleans read as 0 or 1.
namespace transition_detail {

inline std::vector<float> floatsOf(const json::Value* value) {
    if (!value) return {0.0f};
    switch (value->kind()) {
        case json::Value::Kind::Bool: return {value->boolValue() ? 1.0f : 0.0f};
        case json::Value::Kind::Number: return {static_cast<float>(value->number())};
        case json::Value::Kind::Array: {
            std::vector<float> out;
            for (const auto& v : value->array()) out.push_back(v.kind() == json::Value::Kind::Number ? static_cast<float>(v.number()) : 0.0f);
            return out;
        }
        default: return {0.0f};
    }
}

}  // namespace transition_detail

inline std::vector<TransitionDef> parseTransitionLibrary(const std::string& text) {
    std::vector<TransitionDef> library;
    const auto parsed = json::parse(text);
    if (!parsed || parsed->kind() != json::Value::Kind::Array) return library;
    for (const auto& o : parsed->array()) {
        if (o.kind() != json::Value::Kind::Object) continue;
        TransitionDef def;
        def.name = o.optString("name");
        def.author = o.optString("author");
        def.license = o.optString("license");
        def.glsl = o.optString("glsl");
        const json::Value* types = o.get("paramsTypes");
        const json::Value* defaults = o.get("defaultParams");
        if (types && types->kind() == json::Value::Kind::Object) {
            for (const auto& [key, type] : types->object()) {
                TransitionParam p;
                p.name = key;
                p.type = type.kind() == json::Value::Kind::String ? type.string() : "float";
                p.values = transition_detail::floatsOf(defaults ? defaults->get(key) : nullptr);
                def.params.push_back(std::move(p));
            }
        }
        library.push_back(std::move(def));
    }
    return library;
}

inline std::string encodeTransitionTable(const std::vector<TransitionDef>& defs) {
    std::string out;
    char number[32];
    for (size_t d = 0; d < defs.size(); ++d) {
        const TransitionDef& def = defs[d];
        if (d > 0) out += kTableDef;
        out += def.name;
        out += kTableField;
        out += def.author;
        out += kTableField;
        out += def.license;
        out += kTableField;
        for (size_t p = 0; p < def.params.size(); ++p) {
            const TransitionParam& param = def.params[p];
            if (p > 0) out += kTableParam;
            out += param.name;
            out += kTableParamField;
            out += param.type;
            out += kTableParamField;
            for (size_t v = 0; v < param.values.size(); ++v) {
                if (v > 0) out += ',';
                std::snprintf(number, sizeof number, "%.9g", static_cast<double>(param.values[v]));
                out += number;
            }
        }
        out += kTableField;
        out += def.glsl;
    }
    return out;
}

}  // namespace geode::viz
