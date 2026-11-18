#include "library/SessionDoc.hpp"

#include <algorithm>

#include "library/JsonRead.hpp"
#include "library/JvmText.hpp"
#include "util/Json.hpp"

namespace geode::library {

std::optional<SavedSession> sessionParse(const std::string& text) {
    const auto root = jsonread::objectOf(text);
    if (!root) return std::nullopt;
    // A file from a newer app may use a schema this one cannot read safely: refused like a corrupt file.
    if (jsonread::optInt(*root, "version", 1) > kSessionVersion) return std::nullopt;
    const json::Value* array = jsonread::optArray(*root, "tracks");
    if (!array) return std::nullopt;
    SavedSession s;
    for (const json::Value& item : array->array()) {
        if (item.kind() != json::Value::Kind::Object) continue;
        const std::string uri = jsonread::optString(item, "uri");
        if (jvm::isBlank(uri)) continue;
        s.tracks.push_back({uri, jsonread::optString(item, "title"), jsonread::optString(item, "artist")});
    }
    if (s.tracks.empty()) return std::nullopt;
    const int last = static_cast<int>(s.tracks.size()) - 1;
    s.index = std::clamp(jsonread::optInt(*root, "index", 0), 0, last);
    s.positionMs = std::max(jsonread::optLong(*root, "positionMs", 0), 0LL);
    return s;
}

SessionWrite sessionWrite(const SavedSession& session) {
    SessionWrite out;
    if (session.tracks.empty()) {
        out.clear = true;
        return out;
    }
    std::string items;
    for (size_t i = 0; i < session.tracks.size(); ++i) {
        const SavedTrack& t = session.tracks[i];
        if (i) items += ',';
        items += "{\"uri\":" + json::quoted(t.uri) + ",\"title\":" + json::quoted(t.title) + ",\"artist\":" + json::quoted(t.artist) + "}";
    }
    const int last = static_cast<int>(session.tracks.size()) - 1;
    out.json = "{\"version\":" + std::to_string(kSessionVersion) + ",\"tracks\":[" + items +
               "],\"index\":" + std::to_string(std::clamp(session.index, 0, last)) +
               ",\"positionMs\":" + std::to_string(std::max(session.positionMs, 0LL)) + "}";
    return out;
}

std::string sessionPositionWrite(int index, long long positionMs) {
    return "{\"version\":" + std::to_string(kSessionVersion) + ",\"index\":" + std::to_string(std::max(index, 0)) +
           ",\"positionMs\":" + std::to_string(std::max(positionMs, 0LL)) + "}";
}

void sessionApplyPosition(SavedSession& session, const std::string& text) {
    const auto root = jsonread::objectOf(text);
    if (!root || jsonread::optInt(*root, "version", 1) > kSessionVersion) return;
    if (session.tracks.empty()) return;
    session.index = std::clamp(jsonread::optInt(*root, "index", session.index), 0, static_cast<int>(session.tracks.size()) - 1);
    session.positionMs = std::max(jsonread::optLong(*root, "positionMs", session.positionMs), 0LL);
}

std::string sessionWire(const SavedSession& session) {
    constexpr char kField = '\x1f';
    constexpr char kRecord = '\x1e';
    const auto text = [](std::string& out, const std::string& s) {
        out += kField;
        for (const char c : s) {
            if (c != kField && c != kRecord) out += c;
        }
    };
    std::string out = std::to_string(session.index) + kField + std::to_string(session.positionMs);
    for (const SavedTrack& t : session.tracks) {
        text(out, t.uri);
        text(out, t.title);
        text(out, t.artist);
    }
    return out + kRecord;
}

}  // namespace geode::library
