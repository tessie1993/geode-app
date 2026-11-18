#include "viz/LookFiles.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <set>
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

size_t whitespaceAt(std::string_view text, size_t i) {
    if (i >= text.size()) return 0;
    const CodePoint cp = codePointAt(text, i);
    return whitespace(cp.value) ? cp.length : 0;
}

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

std::string framePaletteId(std::string_view lookId, const SceneParams& frame) {
    return palette_params::usesCustomPalette(frame) ? std::string(lookId) : std::string();
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

namespace {

std::string idTag(std::string_view id) {
    std::string out;
    for (size_t i = 0; i < id.size() && out.size() < 40;) {
        const CodePoint cp = codePointAt(id, i);
        const bool keep = (cp.value >= 'A' && cp.value <= 'Z') || (cp.value >= 'a' && cp.value <= 'z') ||
                          (cp.value >= '0' && cp.value <= '9') || cp.value == '_' || cp.value == '-';
        out += keep ? static_cast<char>(cp.value) : '_';
        i += cp.length;
    }
    return out;
}

// One bundled starter: its id, name and notes, and the rest of its file (look, layout, text, export).
struct Starter {
    const char* slug;
    const char* name;
    const char* notes;
    const char* body;
};

constexpr const char* kStarterIdPrefix = "geode.starter.";

// The values the starters always had. Every key a starter leaves out reads as the template model's default.
constexpr Starter kStarters[] = {
    {"reel-neon", "Reel Neon", "Vertical hook clip. Loud, loop-safe, thirty seconds around the biggest moment.",
     R"("look":{"sceneId":"fluid","attack":0.85,"decay":0.24,"params":{"speed":1.3,"audioDrive":1.6,"bloom":0.6,"contrast":1.2,)"
     R"("intensity":1.15,"bassGain":1.4,"palette":1,"paramFadeSec":0.4}},)"
     R"("layout":{"ratio":"R9_16","artwork":"BEHIND_BLURRED","progress":"BAR"},)"
     R"("export":{"fps":30,"loopSafe":true,"segment":{"kind":"LOUDEST_WINDOW","durationMs":30000}})"},
    {"lo-fi-loop", "Lo-Fi Loop", "Square, slow and soft. Made to sit under a study playlist without shouting.",
     R"("look":{"sceneId":"aurora","attack":0.4,"decay":0.06,"params":{"speed":0.5,"audioDrive":0.85,"trails":true,)"
     R"("trailLength":0.8,"saturation":0.8,"brightness":0.9,"palette":3,"sway":0.35,"vignette":0.4,"grain":0.18,"paramFadeSec":1.5}},)"
     R"("layout":{"ratio":"R1_1","artwork":"CARD","artworkScale":0.55,"progress":"NONE"},)"
     R"("text":{"slots":[{"role":"TITLE","anchor":"BOTTOM_CENTER","offsetY":-0.05,"sizeSp":26,"weight":"MEDIUM"},)"
     R"({"role":"ARTIST","anchor":"BOTTOM_CENTER","sizeSp":18,"weight":"REGULAR"}]},)"
     R"("export":{"fps":30,"loopSafe":true,"segment":{"kind":"LOUDEST_WINDOW","durationMs":45000}})"},
    {"vinyl-spin", "Vinyl Spin", "Round artwork, ring progress, whole track. The classic single-cover post.",
     R"("look":{"sceneId":"ring","attack":0.55,"decay":0.1,"params":{"speed":0.8,"audioDrive":1.1,"saturation":1.1,"bloom":0.3,)"
     R"("vignette":0.5,"palette":5,"paramFadeSec":0.8}},)"
     R"("layout":{"ratio":"R1_1","artwork":"CIRCLE","artworkScale":0.7,"progress":"RING"},)"
     R"("text":{"slots":[{"role":"ARTIST","pattern":"{artist}","anchor":"TOP_CENTER","offsetY":0.06,"sizeSp":20,"allCaps":true,)"
     R"("weight":"BOLD"}]},)"
     R"("export":{"fps":30,"loopSafe":false,"segment":{"kind":"WHOLE_TRACK"}})"},
    {"album-teaser", "Album Teaser", "Four-by-five feed post, fifteen seconds, album name front and centre.",
     R"("look":{"sceneId":"waves","attack":0.7,"decay":0.15,"params":{"speed":1.0,"audioDrive":1.25,"trails":true,)"
     R"("trailLength":0.45,"contrast":1.1,"bloom":0.4,"palette":2,"paramFadeSec":0.6}},)"
     R"("layout":{"ratio":"R4_5","artwork":"CARD","artworkScale":0.6,"progress":"BAR"},)"
     R"("text":{"slots":[{"role":"ALBUM","anchor":"MIDDLE_CENTER","sizeSp":40,"allCaps":true,"weight":"BOLD"},)"
     R"({"role":"CAPTION","pattern":"{artist} - {title}","anchor":"BOTTOM_CENTER","offsetY":-0.07,"sizeSp":20}]},)"
     R"("export":{"fps":30,"loopSafe":true,"segment":{"kind":"LOUDEST_WINDOW","durationMs":15000}})"},
    {"podcast-bars", "Podcast Bars", "Wide, calm spectrum for speech. Whole episode, readable title, no strobing.",
     R"("look":{"sceneId":"bars","attack":0.5,"decay":0.2,"params":{"speed":0.9,"audioDrive":1.0,"saturation":0.7,)"
     R"("brightness":0.95,"midGain":1.25,"vignette":0.25,"palette":0,"paramFadeSec":1.0}},)"
     R"("layout":{"ratio":"R16_9","artwork":"CARD","artworkScale":0.4,"progress":"BAR"},)"
     R"("text":{"slots":[{"role":"TITLE","anchor":"MIDDLE_LEFT","offsetX":0.08,"sizeSp":32,"weight":"BOLD"},)"
     R"({"role":"CREDIT","pattern":"{artist} - {index} of {total}","anchor":"BOTTOM_LEFT","offsetX":0.08,"sizeSp":18}]},)"
     R"("export":{"fps":30,"loopSafe":false,"segment":{"kind":"WHOLE_TRACK"}})"},
    {"master-4k", "Master 4K", "Archive render: 4K, sixty frames, no overlays. The one you keep.",
     R"("look":{"attack":0.65,"decay":0.12,"params":{"speed":1.0,"audioDrive":1.2,"bloom":0.45,"contrast":1.05,"paramFadeSec":0.9}},)"
     R"("layout":{"ratio":"R16_9","artwork":"NONE","progress":"NONE"},)"
     R"("text":{"slots":[]},)"
     R"("export":{"quality":"UHD4K","fps":60,"loopSafe":false,"segment":{"kind":"WHOLE_TRACK"},)"
     R"json("fileNamePattern":"{artist} - {title} (4K)"})json"},
};

constexpr int kStarterCount = static_cast<int>(sizeof(kStarters) / sizeof(kStarters[0]));

const std::vector<std::string>& starterTexts() {
    static const std::vector<std::string> texts = [] {
        std::vector<std::string> out;
        for (const Starter& s : kStarters) {
            out.push_back(std::string(R"({"format":"geode.template","formatVersion":1,"id":)") +
                          json::quoted(std::string(kStarterIdPrefix) + s.slug) + R"(,"name":)" + json::quoted(s.name) +
                          R"(,"author":"BeatRealms","notes":)" + json::quoted(s.notes) + R"(,"createdAtMs":0,)" + s.body + "}");
        }
        return out;
    }();
    return texts;
}

}  // namespace

std::string templateFileTail(std::string_view id, std::string_view suffix) { return "--" + idTag(id) + std::string(suffix); }

std::string templateFileName(std::string_view name, std::string_view id, std::string_view suffix) {
    std::string stem = trimmed(safeFileName(name).substr(0, 60));
    if (stem.empty()) stem = "Template";
    return stem + templateFileTail(id, suffix);
}

std::string uniqueTemplateName(std::string_view base, const std::vector<std::string>& taken) {
    std::string stem = trimmed(base);
    if (stem.empty()) stem = "Template";
    const auto isTaken = [&taken](const std::string& candidate) {
        const std::string key = caselessKey(candidate);
        return std::any_of(taken.begin(), taken.end(), [&key](const std::string& t) { return caselessKey(t) == key; });
    };
    if (!isTaken(stem)) return stem;
    for (int attempt = 2;; ++attempt) {
        std::string candidate = stem + " " + std::to_string(attempt);
        if (!isTaken(candidate)) return candidate;
    }
}

int starterCount() { return kStarterCount; }

std::string_view starter(int index) {
    if (index < 0 || index >= kStarterCount) return {};
    return starterTexts()[static_cast<size_t>(index)];
}

bool isStarter(std::string_view id) {
    for (const Starter& s : kStarters) {
        if (id == std::string(kStarterIdPrefix) + s.slug) return true;
    }
    return false;
}

std::string milkFileName(std::string_view presetName) {
    constexpr std::string_view kSuffix = ".milk";
    if (presetName.size() >= kSuffix.size() && presetName.substr(presetName.size() - kSuffix.size()) == kSuffix) {
        presetName.remove_suffix(kSuffix.size());
    }
    return safeFileName(presetName) + std::string(kSuffix);
}

namespace {

// Character.toLowerCase(Character.toUpperCase(c)), the key Kotlin's equals(ignoreCase = true) compares, for the
// scripts names are typed in; any other code point as it is.
uint32_t foldCase(uint32_t c) {
    if (c < 0x80) return (c >= 'A' && c <= 'Z') ? c + 0x20 : c;
    if (c < 0x100) {
        if (c == 0xB5) return 0x3BC;
        return (c >= 0xC0 && c <= 0xDE && c != 0xD7) ? c + 0x20 : c;
    }
    if (c <= 0x17F) {
        if (c == 0x130 || c == 0x131) return 'i';
        if (c == 0x178) return 0xFF;
        if (c == 0x17F) return 's';
        if (c <= 0x137 || (c >= 0x14A && c <= 0x177)) return c | 1u;
        if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return c + (c & 1u);
        return c;
    }
    if (c >= 0x386 && c <= 0x3F5) {
        if (c == 0x386) return 0x3AC;
        if (c >= 0x388 && c <= 0x38A) return c + 0x25;
        if (c == 0x38C) return 0x3CC;
        if (c == 0x38E || c == 0x38F) return c + 0x3F;
        if (c >= 0x391 && c <= 0x3AB && c != 0x3A2) return c + 0x20;
        if (c >= 0x3D8 && c <= 0x3EF) return c | 1u;
        switch (c) {
            case 0x3C2: return 0x3C3;
            case 0x3D0: return 0x3B2;
            case 0x3D1: return 0x3B8;
            case 0x3D5: return 0x3C6;
            case 0x3D6: return 0x3C0;
            case 0x3F0: return 0x3BA;
            case 0x3F1: return 0x3C1;
            case 0x3F4: return 0x3B8;
            case 0x3F5: return 0x3B5;
            default: return c;
        }
    }
    if (c >= 0x400 && c <= 0x52F) {
        if (c <= 0x40F) return c + 0x50;
        if (c <= 0x42F) return c + 0x20;
        if ((c >= 0x460 && c <= 0x481) || (c >= 0x48A && c <= 0x4BF) || c >= 0x4D0) return c | 1u;
        if (c == 0x4C0) return 0x4CF;
        if (c >= 0x4C1 && c <= 0x4CE) return c + (c & 1u);
        return c;
    }
    if (c >= 0x531 && c <= 0x556) return c + 0x30;
    if (c >= 0x1E00 && c <= 0x1EFF) {
        if (c == 0x1E9B) return 0x1E61;
        if (c == 0x1E9E) return 0xDF;
        return (c <= 0x1E95 || c >= 0x1EA0) ? (c | 1u) : c;
    }
    if (c == 0x1FBE) return 0x3B9;
    if (c == 0x2126) return 0x3C9;
    if (c == 0x212A) return 'k';
    if (c == 0x212B) return 0xE5;
    if (c >= 0xFF21 && c <= 0xFF3A) return c + 0x20;
    return c;
}

void appendUtf8(std::string& out, uint32_t c) {
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

// The text with every code point folded (a stray byte kept as it is).
std::string foldedKey(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const CodePoint cp = codePointAt(s, i);
        if (cp.length == 1 && cp.value >= 0x80) {
            out += s[i];
        } else {
            appendUtf8(out, foldCase(cp.value));
        }
        i += cp.length;
    }
    return out;
}

// The length of UTF-8 text as the shell's String counts it: a code point past U+FFFF is two UTF-16 units.
size_t utf16Length(std::string_view s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size();) {
        const CodePoint cp = codePointAt(s, i);
        n += cp.value >= 0x10000 ? 2 : 1;
        i += cp.length;
    }
    return n;
}

// The bytes the link prefix spans at `at`, matched as String.regionMatches(ignoreCase = true) does, or 0.
size_t linkPrefixAt(std::string_view text, size_t at) {
    size_t i = at;
    for (const char want : kPresetLinkPrefix) {
        if (i >= text.size()) return 0;
        const CodePoint cp = codePointAt(text, i);
        if (foldCase(cp.value) != foldCase(static_cast<unsigned char>(want))) return 0;
        i += cp.length;
    }
    return i - at;
}

}  // namespace

bool isBuiltInPreset(std::string_view name) { return name.find(kBuiltInSeparator) != std::string_view::npos; }

bool sameNameCaseless(std::string_view a, std::string_view b) { return foldedKey(a) == foldedKey(b); }

std::string cleanPresetName(std::string_view raw, std::string_view fallback) {
    std::string replaced;
    size_t from = 0;
    for (size_t at = raw.find(kBuiltInSeparator); at != std::string_view::npos; at = raw.find(kBuiltInSeparator, from)) {
        replaced.append(raw.substr(from, at - from));
        replaced += " - ";
        from = at + kBuiltInSeparator.size();
    }
    replaced.append(raw.substr(from));
    std::string name = trimmed(replaced);
    return name.empty() ? std::string(fallback) : name;
}

std::string cleanFolderName(std::string_view raw) { return trimmed(raw); }

NameProblem presetRenameProblem(std::string_view raw, std::string_view current, const std::vector<std::string>& taken) {
    const std::string name = cleanPresetName(raw);
    if (name.empty()) return NameProblem::Blank;
    if (name == current) return NameProblem::None;
    const bool clash = std::any_of(taken.begin(), taken.end(),
                                   [&](const std::string& t) { return t != current && sameNameCaseless(t, name); });
    return clash ? NameProblem::Taken : NameProblem::None;
}

NameProblem folderNameProblem(std::string_view raw, const std::optional<std::string>& current,
                              const std::vector<std::string>& folders) {
    const std::string name = cleanFolderName(raw);
    if (name.empty()) return NameProblem::Blank;
    if (current && sameNameCaseless(name, *current)) return NameProblem::None;
    const bool clash =
        std::any_of(folders.begin(), folders.end(), [&](const std::string& f) { return sameNameCaseless(f, name); });
    return clash ? NameProblem::Taken : NameProblem::None;
}

std::string uniquePresetName(std::string_view base, const std::vector<std::string>& taken) {
    std::set<std::string> keys;
    for (const std::string& t : taken) keys.insert(foldedKey(t));
    const std::string stem(base);
    if (keys.count(foldedKey(stem)) == 0) return stem;
    int n = 2;
    while (keys.count(foldedKey(stem + " " + std::to_string(n))) != 0) ++n;
    return stem + " " + std::to_string(n);
}

std::string presetCopyName(std::string_view name, const std::vector<std::string>& taken) {
    return uniquePresetName(std::string(name) + " copy", taken);
}

std::string importedPresetName(std::string_view raw, std::string_view fallback, const std::vector<std::string>& taken) {
    const std::string base = cleanPresetName(raw, fallback);
    const auto held = [&taken](const std::string& name) { return std::find(taken.begin(), taken.end(), name) != taken.end(); };
    std::string name = base;
    for (int n = 2; held(name); ++n) name = base + " " + std::to_string(n);
    return name;
}

std::optional<std::string> presetReplaceTarget(std::string_view raw, const std::vector<std::string>& names) {
    const std::string name = cleanPresetName(raw);
    if (name.empty()) return std::nullopt;
    for (const std::string& n : names) {
        if (!isBuiltInPreset(n) && n == name) return n;
    }
    return std::nullopt;
}

bool builtInNameClash(std::string_view raw, const std::vector<std::string>& names) {
    const std::string name = cleanPresetName(raw);
    if (name.empty()) return false;
    return std::any_of(names.begin(), names.end(),
                       [&](const std::string& n) { return isBuiltInPreset(n) && sameNameCaseless(n, name); });
}

bool presetMatchesScene(const std::string& presetSceneId, const std::string& activeSceneId) {
    return presetSceneId == activeSceneId || presetSceneId == catalogue::presetStyle(activeSceneId);
}

std::string presetLink(std::string_view payload) { return std::string(kPresetLinkPrefix) + std::string(payload); }

bool presetLinkFits(std::string_view link) { return utf16Length(link) <= static_cast<size_t>(kMaxLinkLength); }

bool isPresetLink(std::string_view text) { return linkPrefixAt(trimmed(text), 0) > 0; }

std::optional<std::string> presetLinkPayload(std::string_view text) {
    const std::string link = trimmed(text);
    const size_t prefix = linkPrefixAt(link, 0);
    if (prefix == 0 || !presetLinkFits(link)) return std::nullopt;
    std::string payload = link.substr(prefix);
    payload = payload.substr(0, payload.find_first_of("#?"));
    if (utf16Length(payload) > static_cast<size_t>(kMaxLinkPayload)) return std::nullopt;
    return payload;
}

std::optional<std::string> findPresetLink(std::string_view text) {
    for (size_t at = 0; at < text.size(); at += codePointAt(text, at).length) {
        if (linkPrefixAt(text, at) == 0) continue;
        size_t end = at;
        while (end < text.size()) {
            const CodePoint cp = codePointAt(text, end);
            if (whitespace(cp.value)) break;
            end += cp.length;
        }
        return std::string(text.substr(at, end - at));
    }
    return std::nullopt;
}

bool presetLinkBodyAccepted(std::string_view body) { return !blank(body); }

}  // namespace geode::viz::files
