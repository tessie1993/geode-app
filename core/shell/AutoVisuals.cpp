#include "shell/AutoVisuals.hpp"

#include <map>

#include "util/Json.hpp"
#include "viz/AutoPlan.hpp"

namespace geode::shell {

namespace {

// JSONObject.optString(name, ""): a member of any type spelled as org.json spells it, "" when missing.
std::string optString(const json::Value& o, const std::string& key) {
    const json::Value* v = o.get(key);
    if (!v) return "";
    return json::asString(*v).value_or("");
}

}  // namespace

std::vector<PlaylistEntry> autoVisualsEntriesFromJson(const std::string& text, const StyleResolver& resolve) {
    std::vector<PlaylistEntry> out;
    // JSONArray(String) reads the first value and fails unless it is an array; any failure is "no entries".
    const auto doc = json::parseLeading(text);
    if (!doc || doc->kind() != json::Value::Kind::Array) return out;
    for (const json::Value& o : doc->array()) {
        if (o.kind() != json::Value::Kind::Object) continue;  // optJSONObject
        const std::string sceneId = optString(o, "sceneId");
        if (sceneId.empty()) continue;
        PlaylistEntry e;
        e.sceneId = resolve(sceneId);
        const std::string preset = optString(o, "presetName");
        if (!preset.empty()) e.presetName = preset;
        e.label = optString(o, "label");
        if (e.label.empty()) e.label = sceneId;
        out.push_back(std::move(e));
    }
    return out;
}

std::string autoVisualsEntriesToJson(const std::vector<PlaylistEntry>& entries) {
    std::vector<json::Value> arr;
    for (const PlaylistEntry& e : entries) {
        std::map<std::string, json::Value> o;
        std::vector<std::string> keys;
        auto put = [&](const char* key, const std::string& value) {
            o.emplace(key, json::Value::string(value));
            keys.emplace_back(key);
        };
        put("sceneId", e.sceneId);
        put("label", e.label);
        if (e.presetName) put("presetName", *e.presetName);
        arr.push_back(json::Value::object(std::move(o), std::move(keys)));
    }
    return json::orgJsonText(json::Value::array(std::move(arr))).value_or("[]");
}

AutoVisualsState autoVisualsApply(const AutoVisualsState& state, const AutoVisualsStored& stored, const StyleResolver& resolve) {
    AutoVisualsState out = state;
    if (stored.entriesJson) out.playlist = autoVisualsEntriesFromJson(*stored.entriesJson, resolve);
    out.playlistEnabled = stored.playlistEnabled.value_or(state.playlistEnabled) && !out.playlist.empty();
    out.randomEnabled = false;
    out.randomIntervalSec = viz::auto_plan::clampInterval(stored.randomIntervalSec.value_or(state.randomIntervalSec));
    out.randomIncludeStyles = stored.randomIncludeStyles.value_or(state.randomIncludeStyles);
    out.randomIncludePresets = stored.randomIncludePresets.value_or(state.randomIncludePresets);
    out.playlistIntervalSec = viz::auto_plan::clampInterval(stored.playlistIntervalSec.value_or(state.playlistIntervalSec));
    return out;
}

}  // namespace geode::shell
