#include "viz/LookFiles.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <utility>

#include "util/Json.hpp"
#include "viz/Catalogue.hpp"
#include "viz/Colour.hpp"
#include "viz/ParamsJson.hpp"
#include "viz/ParamsPalette.hpp"
#include "viz/Sha256.hpp"

namespace geode::viz::files {

namespace {

// One code point of UTF-8 text at `i`: its value and its length in bytes. A stray byte is one code point.
struct CodePoint {
    uint32_t value;
    size_t length;
};

CodePoint codePointAt(std::string_view s, size_t i) {
    const auto b = static_cast<unsigned char>(s[i]);
    size_t n = b < 0x80 ? 1 : (b >> 5) == 0x6 ? 2 : (b >> 4) == 0xE ? 3 : (b >> 3) == 0x1E ? 4 : 1;
    if (i + n > s.size()) n = 1;
    if (n == 1) return {b, 1};
    uint32_t v = b & (0xFFu >> (n + 1));
    for (size_t k = 1; k < n; ++k) {
        const auto c = static_cast<unsigned char>(s[i + k]);
        if ((c & 0xC0) != 0x80) return {b, 1};
        v = (v << 6) | (c & 0x3Fu);
    }
    return {v, n};
}

// Kotlin's Char.isWhitespace: Character.isWhitespace or isSpaceChar.
bool whitespace(uint32_t c) {
    if ((c >= 0x09 && c <= 0x0D) || (c >= 0x1C && c <= 0x20)) return true;
    if (c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A)) return true;
    return c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}

bool safeChar(uint32_t c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == ' ';
}

// Java's double to float: to nearest, and past the largest float to infinity (a C++ cast there is undefined).
float javaFloat(double d) {
    constexpr double kMax = std::numeric_limits<float>::max();
    constexpr double kOverflow = 0x1.ffffffp127;
    if (std::isnan(d)) return std::numeric_limits<float>::quiet_NaN();
    if (std::fabs(d) > kMax) {
        const float edge = std::fabs(d) >= kOverflow ? std::numeric_limits<float>::infinity() : std::numeric_limits<float>::max();
        return d < 0 ? -edge : edge;
    }
    return static_cast<float>(d);
}

float unitOr(float v, float fallback) { return std::isnan(v) ? fallback : std::clamp(v, 0.0f, 1.0f); }

std::string caselessKey(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

std::vector<int> realColours(const std::vector<int>& stops) {
    std::vector<int> out;
    for (int rgb : stops) {
        if (colour::valid(rgb) && static_cast<int>(out.size()) < colour::kMaxStops) out.push_back(rgb);
    }
    return out;
}

// org.json's toString(2) for one "key": value line, `level` deep.
void line(std::string& out, int level, const std::string& key, const std::string& value, bool last) {
    out.append(static_cast<size_t>(level) * 2, ' ');
    out += json::quoted(key) + ": " + value;
    out += last ? "\n" : ",\n";
}

}  // namespace

std::string trimmed(std::string_view text) {
    size_t from = 0;
    while (from < text.size()) {
        const CodePoint cp = codePointAt(text, from);
        if (!whitespace(cp.value)) break;
        from += cp.length;
    }
    size_t to = from;
    for (size_t i = from; i < text.size();) {
        const CodePoint cp = codePointAt(text, i);
        i += cp.length;
        if (!whitespace(cp.value)) to = i;
    }
    return std::string(text.substr(from, to - from));
}

bool blank(std::string_view text) { return trimmed(text).empty(); }

std::string safeChars(std::string_view name) {
    std::string out;
    for (size_t i = 0; i < name.size();) {
        const CodePoint cp = codePointAt(name, i);
        out += safeChar(cp.value) ? static_cast<char>(cp.value) : '_';
        i += cp.length;
    }
    return out;
}

std::string safeFileName(std::string_view name) {
    const std::string stem = safeChars(name);
    if (stem == name && !blank(name)) return stem;
    Sha256 sha;
    sha.update(name.data(), name.size());
    return stem + "-" + Sha256::hex(sha.digest(), 4);
}

std::vector<int> order(const std::vector<std::string>& names, bool caseless) {
    std::vector<int> out(names.size());
    std::iota(out.begin(), out.end(), 0);
    std::vector<std::string> keys;
    keys.reserve(names.size());
    for (const auto& n : names) keys.push_back(caseless ? caselessKey(n) : n);
    std::stable_sort(out.begin(), out.end(), [&keys](int a, int b) { return keys[static_cast<size_t>(a)] < keys[static_cast<size_t>(b)]; });
    return out;
}

std::string paletteIdFor(std::string_view name) {
    const std::string id = trimmed(safeFileName(name));
    return id.empty() ? "palette" : id;
}

SavedPalette sanitized(SavedPalette p) {
    p.id = paletteIdFor(blank(p.id) ? p.name : p.id);
    p.name = trimmed(p.name);
    if (p.name.empty()) p.name = "Palette";
    p.baseHue = unitOr(p.baseHue, 0.0f);
    p.hueSpan = unitOr(p.hueSpan, 0.0f);
    p.stops = realColours(p.stops);
    return p;
}

std::optional<SavedPalette> readPalette(std::string_view text) {
    const std::optional<json::Value> doc = json::parse(std::string(text));
    if (!doc || doc->kind() != json::Value::Kind::Object) return std::nullopt;
    const json::Value* name = doc->get("name");
    const std::optional<std::string> nameText = name ? json::asString(*name) : std::nullopt;
    if (!nameText) return std::nullopt;
    SavedPalette p;
    p.name = *nameText;
    const json::Value* id = doc->get("id");
    p.id = (id ? json::asString(*id) : std::nullopt).value_or("");
    if (blank(p.id)) p.id = paletteIdFor(p.name);
    const auto hue = [&doc](const char* key, double fallback) {
        const json::Value* v = doc->get(key);
        return unitOr(javaFloat((v ? json::asDouble(*v) : std::nullopt).value_or(fallback)), static_cast<float>(fallback));
    };
    p.baseHue = hue("baseHue", 0.0);
    p.hueSpan = hue("hueSpan", 1.0);
    if (const json::Value* stops = doc->get("stops"); stops && stops->kind() == json::Value::Kind::Array) {
        std::vector<int> read;
        for (const json::Value& v : stops->array()) read.push_back(json::asInt(v).value_or(colour::kNone));
        p.stops = realColours(read);
    }
    return p;
}

std::string writePalette(const SavedPalette& p) {
    std::string out = "{\n";
    line(out, 1, "id", json::quoted(p.id), false);
    line(out, 1, "name", json::quoted(p.name), false);
    const auto number = [](float v) { return json::numberText(static_cast<double>(std::isfinite(v) ? v : 0.0f)); };
    line(out, 1, "baseHue", number(p.baseHue), false);
    line(out, 1, "hueSpan", number(p.hueSpan), false);
    std::string stops = "[]";
    if (!p.stops.empty()) {
        stops = "[\n";
        for (size_t i = 0; i < p.stops.size(); ++i) {
            stops += "    " + std::to_string(p.stops[i]) + (i + 1 < p.stops.size() ? ",\n" : "\n");
        }
        stops += "  ]";
    }
    line(out, 1, "stops", stops, true);
    return out + "}";
}

SceneParams applyPalette(const SceneParams& params, const SavedPalette& p) {
    if (!p.stops.empty()) return palette_params::withStops(params, p.stops.data(), static_cast<int>(p.stops.size()));
    return palette_params::withGradient(palette_params::withoutCustomPalette(params), p.baseHue, p.hueSpan);
}

std::string keptPaletteId(std::string_view lookId, std::string_view deletedId) {
    return !deletedId.empty() && lookId == deletedId ? std::string() : std::string(lookId);
}

std::optional<std::string> migratedPalette(std::string_view text) {
    std::optional<SavedPalette> p = readPalette(text);
    if (!p) return std::nullopt;
    p->id = paletteIdFor(p->name);
    return writePalette(*p);
}

float finiteOr(double raw, float fallback) {
    const float narrowed = javaFloat(raw);
    return std::isfinite(narrowed) ? narrowed : fallback;
}

float finiteIn(double raw, float lo, float hi) { return std::clamp(finiteOr(raw, lo), lo, hi); }

std::optional<Preset> readPreset(std::string_view text) {
    const std::string source(text);
    const std::optional<json::Value> doc = json::parse(source);
    if (!doc || doc->kind() != json::Value::Kind::Object) return std::nullopt;
    const auto textAt = [&doc](const char* key) {
        const json::Value* v = doc->get(key);
        return v ? json::asString(*v) : std::nullopt;
    };
    const auto numberAt = [&doc](const char* key) {
        const json::Value* v = doc->get(key);
        return v ? json::asDouble(*v) : std::nullopt;
    };
    const auto name = textAt("name");
    const auto sceneId = textAt("sceneId");
    const auto attack = numberAt("attack");
    const auto decay = numberAt("decay");
    if (!name || !sceneId || !attack || !decay) return std::nullopt;
    Preset p;
    p.name = *name;
    p.sceneId = catalogue::resolveId(*sceneId);
    p.attack = finiteIn(*attack, kAttackMin, kAttackMax);
    p.decay = finiteIn(*decay, kDecayMin, kDecayMax);
    if (doc->get("customShader")) p.customShader = textAt("customShader");
    if (doc->get("milkPreset")) p.milkPreset = textAt("milkPreset");
    // The params are read from the stored text itself where it is standard JSON: org.json would print a whole
    // double as a long first. Anything else org.json took is read as org.json printed it.
    std::string ignored;
    if (!readParamsJson(source, "", json::orgJsonText(*doc).value_or(""), p.params, p.customPaletteId, ignored)) return std::nullopt;
    return p;
}

std::string writePreset(const Preset& p) {
    const auto number = [](float v) { return json::numberText(static_cast<double>(std::isfinite(v) ? v : 0.0f)); };
    std::vector<std::pair<std::string, std::string>> entries = {
        {"name", json::quoted(p.name)},
        {"sceneId", json::quoted(p.sceneId)},
        {"attack", number(p.attack)},
        {"decay", number(p.decay)},
    };
    // The params object's keys beside the envelope's, each as org.json printed it after reading it back.
    const std::optional<json::Value> params = json::parse(writeParamsJson(p.params, p.customPaletteId, ""));
    if (params) {
        for (const std::string& key : params->keys()) {
            if (const auto value = json::orgJsonText(*params->get(key))) entries.emplace_back(key, *value);
        }
    }
    if (p.customShader) entries.emplace_back("customShader", json::quoted(*p.customShader));
    if (p.milkPreset) entries.emplace_back("milkPreset", json::quoted(*p.milkPreset));
    std::string out = "{\n";
    for (size_t i = 0; i < entries.size(); ++i) line(out, 1, entries[i].first, entries[i].second, i + 1 == entries.size());
    return out + "}";
}

std::string milkFileName(std::string_view presetName) {
    constexpr std::string_view kSuffix = ".milk";
    if (presetName.size() >= kSuffix.size() && presetName.substr(presetName.size() - kSuffix.size()) == kSuffix) {
        presetName.remove_suffix(kSuffix.size());
    }
    return safeFileName(presetName) + std::string(kSuffix);
}

}  // namespace geode::viz::files
