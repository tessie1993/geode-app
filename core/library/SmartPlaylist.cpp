#include "library/SmartPlaylist.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>

#include "library/History.hpp"
#include "library/JsonRead.hpp"
#include "library/JvmText.hpp"
#include "util/Json.hpp"

namespace geode::library {

namespace {

struct Token {
    const char* name;
    int value;
};

constexpr Token kFields[] = {{"TITLE", static_cast<int>(RuleField::Title)},
                             {"ARTIST", static_cast<int>(RuleField::Artist)},
                             {"ALBUM", static_cast<int>(RuleField::Album)},
                             {"FOLDER", static_cast<int>(RuleField::Folder)},
                             {"DURATION_MINUTES", static_cast<int>(RuleField::DurationMinutes)},
                             {"ADDED_DAYS_AGO", static_cast<int>(RuleField::AddedDaysAgo)},
                             {"PLAY_COUNT", static_cast<int>(RuleField::PlayCount)},
                             {"FAVOURITE", static_cast<int>(RuleField::Favourite)}};

constexpr Token kOps[] = {{"CONTAINS", static_cast<int>(RuleOp::Contains)},
                          {"IS", static_cast<int>(RuleOp::Is)},
                          {"IS_NOT", static_cast<int>(RuleOp::IsNot)},
                          {"AT_LEAST", static_cast<int>(RuleOp::AtLeast)},
                          {"AT_MOST", static_cast<int>(RuleOp::AtMost)}};

template <size_t N>
const char* nameOf(const Token (&tokens)[N], int value) {
    for (const Token& t : tokens) {
        if (t.value == value) return t.name;
    }
    return "";
}

// entries.firstOrNull { it.name == token }: an exact match, or none (the rule is dropped).
template <size_t N>
std::optional<int> lookup(const Token (&tokens)[N], const std::string& token) {
    for (const Token& t : tokens) {
        if (token == t.name) return t.value;
    }
    return std::nullopt;
}

bool isFlag(RuleField f) { return f == RuleField::Favourite; }

// Float.toInt(): truncated, saturated, NaN 0.
int floatToInt(float f) {
    if (std::isnan(f)) return 0;
    if (f >= 2147483648.0f) return std::numeric_limits<int>::max();
    if (f <= -2147483648.0f) return std::numeric_limits<int>::min();
    return static_cast<int>(f);
}

// String.toFloatOrNull(): Java's float grammar (after Kotlin's own trim; Float.parseFloat trims again). Read
// as a double and narrowed, which rounds twice: it differs from Float.parseFloat only for a decimal that lies
// within a double rounding of a float midpoint.
std::optional<float> toFloatOrNull(const std::string& text) {
    const auto d = json::javaParseDouble(text);
    if (!d) return std::nullopt;
    if (std::isnan(*d)) return std::numeric_limits<float>::quiet_NaN();
    if (std::isinf(*d)) return static_cast<float>(*d);
    // Past the midpoint between the largest float and the next power of two a float reads as infinity.
    if (std::fabs(*d) >= 3.4028235677973366e38) return *d < 0 ? -std::numeric_limits<float>::infinity() : std::numeric_limits<float>::infinity();
    return static_cast<float>(*d);
}

bool textMatches(const SmartRule& rule, const std::string& field) {
    const std::string wanted = jvm::trim(rule.value);
    switch (rule.op) {
        case RuleOp::Contains: return jvm::containsIgnoreCase(field, wanted);
        case RuleOp::Is: return jvm::equalsIgnoreCase(field, wanted);
        case RuleOp::IsNot: return !jvm::equalsIgnoreCase(field, wanted);
        case RuleOp::AtLeast:
        case RuleOp::AtMost: return false;
    }
    return false;
}

bool numberMatches(const SmartRule& rule, float actual) {
    const auto wanted = toFloatOrNull(jvm::trim(rule.value));
    if (!wanted) return false;
    switch (rule.op) {
        case RuleOp::Is: return floatToInt(actual) == floatToInt(*wanted);
        case RuleOp::IsNot: return floatToInt(actual) != floatToInt(*wanted);
        case RuleOp::AtLeast: return actual >= *wanted;
        case RuleOp::AtMost: return actual <= *wanted;
        case RuleOp::Contains: return false;
    }
    return false;
}

}  // namespace

std::optional<SmartPlaylist> smartParse(const std::string& text) {
    const auto root = jsonread::objectOf(text);
    if (!root) return std::nullopt;
    const auto name = jsonread::getString(*root, "name");
    if (!name) return std::nullopt;
    SmartPlaylist p;
    p.name = *name;
    p.matchAll = jsonread::optBool(*root, "matchAll", true);
    p.limit = jsonread::optInt(*root, "limit", 0);
    // A token from a newer or foreign build drops only its own rule, not the whole file.
    if (const json::Value* rules = jsonread::optArray(*root, "rules")) {
        for (const json::Value& r : rules->array()) {
            if (r.kind() != json::Value::Kind::Object) continue;
            const auto field = lookup(kFields, jsonread::optString(r, "field"));
            if (!field) continue;
            const auto op = lookup(kOps, jsonread::optString(r, "op"));
            if (!op) continue;
            p.rules.push_back({static_cast<RuleField>(*field), static_cast<RuleOp>(*op), jsonread::optString(r, "value", "")});
        }
    }
    return p;
}

std::string smartToJson(const SmartPlaylist& p) {
    std::string out = "{\"name\":" + json::quoted(p.name) + ",\"matchAll\":" + (p.matchAll ? "true" : "false") +
                      ",\"limit\":" + std::to_string(p.limit) + ",\"rules\":[";
    for (size_t i = 0; i < p.rules.size(); ++i) {
        if (i) out += ',';
        out += "{\"field\":" + json::quoted(nameOf(kFields, static_cast<int>(p.rules[i].field))) +
               ",\"op\":" + json::quoted(nameOf(kOps, static_cast<int>(p.rules[i].op))) +
               ",\"value\":" + json::quoted(p.rules[i].value) + "}";
    }
    return out + "]}";
}

std::optional<std::string> smartSaveText(const SmartPlaylist& p) {
    if (jvm::isBlank(p.name)) return std::nullopt;
    return smartToJson(p);
}

std::vector<SmartPlaylist> smartList(const std::vector<std::string>& fileTexts) {
    std::vector<SmartPlaylist> out;
    for (const std::string& text : fileTexts) {
        if (auto p = smartParse(text)) out.push_back(std::move(*p));
    }
    jvm::stableSortByLowercase(out, [](const SmartPlaylist& p) -> const std::string& { return p.name; });
    return out;
}

std::vector<int> smartResolve(const SmartPlaylist& playlist, const std::vector<SmartTrack>& tracks,
                              const std::function<int(const std::string&)>& playCountOf,
                              const std::vector<std::string>& favourites, long long nowMs) {
    std::vector<const SmartRule*> rules;
    for (const SmartRule& r : playlist.rules) {
        if (!jvm::isBlank(r.value) || isFlag(r.field)) rules.push_back(&r);
    }
    const std::set<std::string> favs(favourites.begin(), favourites.end());
    std::vector<int> picked;
    for (size_t i = 0; i < tracks.size(); ++i) {
        const SmartTrack& t = tracks[i];
        bool keep = true;
        if (!rules.empty()) {
            bool all = true;
            bool any = false;
            for (const SmartRule* r : rules) {
                bool hit = false;
                switch (r->field) {
                    case RuleField::Title: hit = textMatches(*r, t.title); break;
                    case RuleField::Artist: hit = textMatches(*r, t.artist); break;
                    case RuleField::Album: hit = textMatches(*r, t.album); break;
                    case RuleField::Folder: hit = textMatches(*r, t.folder); break;
                    // Float arithmetic as Kotlin's: the Long converted to Float, divided by a Float.
                    case RuleField::DurationMinutes: hit = numberMatches(*r, static_cast<float>(t.durationMs) / 60000.0f); break;
                    case RuleField::AddedDaysAgo:
                        hit = numberMatches(*r, static_cast<float>(nowMs / 1000LL - t.addedSec) / 86400.0f);
                        break;
                    case RuleField::PlayCount: hit = numberMatches(*r, static_cast<float>(playCountOf(t.uri))); break;
                    case RuleField::Favourite: hit = (favs.count(t.uri) > 0) == (r->op != RuleOp::IsNot); break;
                }
                all = all && hit;
                any = any || hit;
            }
            keep = playlist.matchAll ? all : any;
        }
        if (keep) picked.push_back(static_cast<int>(i));
    }
    if (playlist.limit > 0 && picked.size() > static_cast<size_t>(playlist.limit)) picked.resize(static_cast<size_t>(playlist.limit));
    return picked;
}

std::optional<std::vector<int>> smartResolveText(const std::string& smartJson, const std::vector<SmartTrack>& tracks,
                                                 const std::string& historyJson, const std::vector<std::string>& favourites,
                                                 long long nowMs) {
    const auto playlist = smartParse(smartJson);
    if (!playlist) return std::nullopt;
    std::map<std::string, int> counts;
    for (const HistoryEntry& e : historyParse(historyJson).entries) counts[e.uri] = e.playCount;
    return smartResolve(*playlist, tracks,
                        [&](const std::string& uri) {
                            const auto it = counts.find(uri);
                            return it == counts.end() ? 0 : it->second;
                        },
                        favourites, nowMs);
}

}  // namespace geode::library
