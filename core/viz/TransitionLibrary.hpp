#pragma once
#include <cstdio>
#include <string>
#include <vector>

#include "util/Json.hpp"

// The GL Transitions library asset (gl_transitions.json): parsed once here, for the renderer's
// composite (core/viz/Transition) and for the shell's one read of it (#438). The shell gets
// `encodeTransitionPicker` through geode_transition_picker: the names it lists, nothing to parse.
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

// The shell's picker table, split on two ASCII control separators: the built-in ids the picker offers,
// RS (0x1e), then the library's names in library order, the entries of each list separated by US (0x1f).
// JSON can spell any control separator as \u001c..\u001f, so the encoder writes each one inside a name as
// a space: the table always splits back cleanly.
inline constexpr char kTableList = '\x1e';
inline constexpr char kTableEntry = '\x1f';

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

// A field of the table: the separators (0x1c..0x1f) inside it become spaces.
inline void appendField(std::string& out, const std::string& field) {
    for (const char c : field) out += (c >= '\x1c' && c <= '\x1f') ? ' ' : c;
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

// What the Customize transition picker lists: `builtIns` (the ids it offers above the library, as
// transition_choice::pickerIds gives them), then every library transition's name.
inline std::string encodeTransitionPicker(const std::vector<std::string>& builtIns, const std::vector<TransitionDef>& library) {
    std::string out;
    for (size_t i = 0; i < builtIns.size(); ++i) {
        if (i > 0) out += kTableEntry;
        transition_detail::appendField(out, builtIns[i]);
    }
    out += kTableList;
    for (size_t i = 0; i < library.size(); ++i) {
        if (i > 0) out += kTableEntry;
        transition_detail::appendField(out, library[i].name);
    }
    return out;
}

}  // namespace geode::viz
