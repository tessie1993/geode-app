#include "viz/ParamsJson.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

#include "util/Json.hpp"

namespace geode::viz {

namespace {

// Kotlin's Float.roundToInt: half up, saturated at the int range.
int roundToInt(float v) {
    const double r = std::floor(static_cast<double>(v) + 0.5);
    if (r >= static_cast<double>(std::numeric_limits<int>::max())) return std::numeric_limits<int>::max();
    if (r <= static_cast<double>(std::numeric_limits<int>::min())) return std::numeric_limits<int>::min();
    return static_cast<int>(r);
}

// Java's double to float: to nearest, and past the largest float to infinity (a C++ cast there is undefined).
float javaFloat(double d) {
    constexpr double kMax = std::numeric_limits<float>::max();
    constexpr double kOverflow = 0x1.ffffffp127;  // halfway from the largest float to the next power of two
    if (std::fabs(d) > kMax) {
        const float edge = std::fabs(d) >= kOverflow ? std::numeric_limits<float>::infinity() : std::numeric_limits<float>::max();
        return d < 0 ? -edge : edge;
    }
    return static_cast<float>(d);
}

}  // namespace

std::string writeParamsJson(const float* wire, std::string_view customPaletteId, std::string_view customPalette2Id) {
    std::string out = "{";
    const auto& rows = SceneParams::schema();
    for (int slot = 0; slot < SceneParams::kFieldCount; ++slot) {
        if (SceneParams::retired(static_cast<ParamSlot>(slot))) continue;
        const ParamSpec& row = rows[static_cast<size_t>(slot)];
        const float v = wire[slot];
        out += json::quoted(row.name);
        out += ':';
        switch (row.kind) {
            case ParamKind::Bool: out += v > 0.5f ? "true" : "false"; break;
            case ParamKind::Int: out += std::to_string(roundToInt(std::isnan(v) ? row.defaultValue : v)); break;
            case ParamKind::Float:
            case ParamKind::Circular:
                out += json::numberText(static_cast<double>(std::isfinite(v) ? v : row.defaultValue));
                break;
        }
        out += ',';
    }
    out += json::quoted(kCustomPaletteIdKey) + ':' + json::quoted(customPaletteId) + ',';
    out += json::quoted(kCustomPalette2IdKey) + ':' + json::quoted(customPalette2Id) + '}';
    return out;
}

std::string writeParamsJson(const SceneParams& params, std::string_view customPaletteId, std::string_view customPalette2Id) {
    float wire[SceneParams::kFieldCount];
    params.toWire(wire);
    return writeParamsJson(wire, customPaletteId, customPalette2Id);
}

namespace {

bool readObject(const json::Value& doc, SceneParams& out, std::string& customPaletteId, std::string& customPalette2Id) {
    if (doc.kind() != json::Value::Kind::Object) return false;
    float wire[SceneParams::kFieldCount] = {};
    const auto& rows = SceneParams::schema();
    for (int slot = 0; slot < SceneParams::kFieldCount; ++slot) {
        const ParamSpec& row = rows[static_cast<size_t>(slot)];
        const json::Value* v = doc.get(row.name);
        float& w = wire[slot];
        switch (row.kind) {
            case ParamKind::Bool: {
                const auto b = v ? json::asBoolean(*v) : std::nullopt;
                w = b.value_or(row.defaultValue > 0.5f) ? 1.0f : 0.0f;
                break;
            }
            case ParamKind::Int: {
                const auto i = v ? json::asInt(*v) : std::nullopt;
                w = static_cast<float>(i.value_or(roundToInt(row.defaultValue)));
                break;
            }
            case ParamKind::Float:
            case ParamKind::Circular: {
                const auto d = v ? json::asDouble(*v) : std::nullopt;
                w = javaFloat(d.value_or(static_cast<double>(row.defaultValue)));
                break;
            }
        }
    }
    out = SceneParams::sanitized(wire);
    const auto idOf = [&doc](const char* key) {
        const json::Value* v = doc.get(key);
        return (v ? json::asString(*v) : std::nullopt).value_or(std::string());
    };
    customPaletteId = idOf(kCustomPaletteIdKey);
    customPalette2Id = idOf(kCustomPalette2IdKey);
    return true;
}

}  // namespace

bool readParamsJson(std::string_view text, SceneParams& out, std::string& customPaletteId, std::string& customPalette2Id) {
    const std::optional<json::Value> doc = json::parse(std::string(text));
    return doc && readObject(*doc, out, customPaletteId, customPalette2Id);
}

bool readParamsJson(std::string_view document, std::string_view path, std::string_view fallback, SceneParams& out,
                    std::string& customPaletteId, std::string& customPalette2Id) {
    if (const std::optional<json::Value> doc = json::parseStandard(std::string(document))) {
        const json::Value* at = &*doc;
        for (size_t from = 0; at && !path.empty() && from <= path.size();) {
            const size_t end = std::min(path.find('\n', from), path.size());
            at = at->get(std::string(path.substr(from, end - from)));
            from = end + 1;
        }
        if (at && readObject(*at, out, customPaletteId, customPalette2Id)) return true;
    }
    return readParamsJson(fallback, out, customPaletteId, customPalette2Id);
}

}  // namespace geode::viz
