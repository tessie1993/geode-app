#pragma once
// Private to core/library: org.json's opt* reads of a parsed object, as the stores' JSONObject code made them.
#include <optional>
#include <string>

#include "util/Json.hpp"

namespace geode::library::jsonread {

// JSONObject(text): the first value, whatever follows ignored, and it must be an object.
inline std::optional<json::Value> objectOf(const std::string& text) {
    std::optional<json::Value> v = json::parseLeading(text);
    if (!v || v->kind() != json::Value::Kind::Object) return std::nullopt;
    return v;
}

// JSONArray(text), likewise.
inline std::optional<json::Value> arrayOf(const std::string& text) {
    std::optional<json::Value> v = json::parseLeading(text);
    if (!v || v->kind() != json::Value::Kind::Array) return std::nullopt;
    return v;
}

// getString / getString(i) and optString: a string as it is, anything else as String.valueOf spells it.
inline std::optional<std::string> stringOf(const json::Value& v) { return json::asString(v); }

// getString(name): nullopt where the key is absent or has no string form (org.json throws).
inline std::optional<std::string> getString(const json::Value& obj, const std::string& key) {
    const json::Value* v = obj.get(key);
    return v ? json::asString(*v) : std::nullopt;
}

inline std::string optString(const json::Value& obj, const std::string& key, const std::string& fallback = "") {
    const json::Value* v = obj.get(key);
    if (!v) return fallback;
    std::optional<std::string> s = json::asString(*v);
    return s ? *s : fallback;
}

inline long long optLong(const json::Value& obj, const std::string& key, long long fallback) {
    const json::Value* v = obj.get(key);
    if (!v) return fallback;
    std::optional<long long> n = json::asLong(*v);
    return n ? *n : fallback;
}

inline int optInt(const json::Value& obj, const std::string& key, int fallback) {
    const json::Value* v = obj.get(key);
    if (!v) return fallback;
    std::optional<int> n = json::asInt(*v);
    return n ? *n : fallback;
}

inline bool optBool(const json::Value& obj, const std::string& key, bool fallback) {
    const json::Value* v = obj.get(key);
    if (!v) return fallback;
    std::optional<bool> b = json::asBoolean(*v);
    return b ? *b : fallback;
}

// optJSONArray(name): the array, or null for anything else.
inline const json::Value* optArray(const json::Value& obj, const std::string& key) {
    const json::Value* v = obj.get(key);
    return (v && v->kind() == json::Value::Kind::Array) ? v : nullptr;
}

}  // namespace geode::library::jsonread
