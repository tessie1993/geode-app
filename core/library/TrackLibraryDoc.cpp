#include "library/TrackLibraryDoc.hpp"

#include <algorithm>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

#include "util/Json.hpp"

namespace geode::library {

namespace {

constexpr int kVersion = 3;

// ── Reading: org.json's getString / optString / optInt / optLong ───────────────────────────────────────────────

std::optional<LibraryTrack> fromJson(const json::Value& o) {
    if (o.kind() != json::Value::Kind::Object) return std::nullopt;  // getJSONObject
    LibraryTrack t;
    const json::Value* uri = o.get("uri");
    if (!uri) return std::nullopt;  // getString throws on a missing key
    const auto uriText = json::asString(*uri);
    if (!uriText) return std::nullopt;
    t.uri = *uriText;
    auto str = [&](const char* key, const char* fallback) {
        const json::Value* v = o.get(key);
        if (!v) return std::string(fallback);
        return json::asString(*v).value_or(fallback);
    };
    auto integer = [&](const char* key) {
        const json::Value* v = o.get(key);
        return v ? json::asInt(*v).value_or(0) : 0;
    };
    auto wide = [&](const char* key) {
        const json::Value* v = o.get(key);
        return v ? json::asLong(*v).value_or(0LL) : 0LL;
    };
    t.title = str("title", "Track");
    t.artist = str("artist", "");
    t.durationMs = wide("durationMs");
    t.album = str("album", "");
    t.genre = str("genre", "");
    t.year = integer("year");
    t.trackNo = integer("trackNo");
    t.comment = str("comment", "");
    t.fileName = str("fileName", "");
    t.sizeBytes = wide("sizeBytes");
    return t;
}

// ── Writing: org.json's JSONObject.toString, keys in the order they were put ────────────────────────────────

void field(std::string& out, const char* key, const std::string& value) {
    out += json::quoted(key);
    out += ':';
    out += json::quoted(value);
    out += ',';
}

void field(std::string& out, const char* key, long long value, bool last = false) {
    out += json::quoted(key);
    out += ':';
    out += std::to_string(value);
    if (!last) out += ',';
}

std::string toJson(const LibraryTrack& t) {
    std::string out = "{";
    field(out, "uri", t.uri);
    field(out, "title", t.title);
    field(out, "artist", t.artist);
    field(out, "durationMs", t.durationMs);
    field(out, "album", t.album);
    field(out, "genre", t.genre);
    field(out, "year", t.year);
    field(out, "trackNo", t.trackNo);
    field(out, "comment", t.comment);
    field(out, "fileName", t.fileName);
    field(out, "sizeBytes", t.sizeBytes, true);
    out += '}';
    return out;
}

// ── The rules ───────────────────────────────────────────────────────────────────────────────────────────────

// "file:<lower-case name>:<size>" when both are known, else "uri:<uri>".
std::string identityKey(const LibraryTrack& t) {
    if (!isBlank(t.fileName) && t.sizeBytes > 0) return "file:" + lowerRoot(t.fileName) + ":" + std::to_string(t.sizeBytes);
    return "uri:" + t.uri;
}

// sortedBy { it.title.lowercase() }: stable, by UTF-16 order of the lower-cased title.
void sortByTitle(std::vector<LibraryTrack>& tracks) {
    std::vector<std::u16string> keys;
    keys.reserve(tracks.size());
    for (const auto& t : tracks) keys.push_back(toUtf16(lowerRoot(t.title)));
    std::vector<size_t> order(tracks.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return keys[a] < keys[b]; });
    std::vector<LibraryTrack> sorted;
    sorted.reserve(tracks.size());
    for (const size_t i : order) sorted.push_back(std::move(tracks[i]));
    tracks = std::move(sorted);
}

std::vector<LibraryTrack> mergeAdds(const std::vector<LibraryTrack>& existing, const std::vector<LibraryTrack>& incoming) {
    std::unordered_map<std::string, const LibraryTrack*> identityByUri;  // associateBy: the last one wins
    for (const auto& t : incoming) {
        if (!isBlank(t.fileName) && t.sizeBytes > 0) identityByUri[t.uri] = &t;
    }
    std::vector<LibraryTrack> merged;
    std::unordered_set<std::string> seen;
    auto keep = [&](LibraryTrack t) {
        if (seen.insert(identityKey(t)).second) merged.push_back(std::move(t));
    };
    for (const auto& t : existing) {
        LibraryTrack held = t;
        if (isBlank(t.fileName)) {
            const auto id = identityByUri.find(t.uri);
            if (id != identityByUri.end()) {
                held.fileName = id->second->fileName;
                held.sizeBytes = id->second->sizeBytes;
            }
        }
        keep(std::move(held));
    }
    for (const auto& t : incoming) keep(t);
    sortByTitle(merged);
    return merged;
}

// A map keyed by uri (first place, last value, appended when new), then sorted by title.
std::vector<LibraryTrack> upsertInfo(const std::vector<LibraryTrack>& tracks, const std::string& uri, const TrackInfo& info) {
    std::vector<std::string> order;
    std::unordered_map<std::string, LibraryTrack> byUri;
    for (const auto& t : tracks) {
        if (byUri.find(t.uri) == byUri.end()) order.push_back(t.uri);
        byUri[t.uri] = t;
    }
    auto base = byUri.find(uri);
    if (base == byUri.end()) {
        LibraryTrack fresh;
        fresh.uri = uri;
        fresh.title = info.title;
        order.push_back(uri);
        base = byUri.emplace(uri, std::move(fresh)).first;
    }
    LibraryTrack& t = base->second;
    t.title = info.title;
    t.artist = info.artist;
    t.album = info.album;
    t.genre = info.genre;
    t.year = info.year;
    t.trackNo = info.trackNo;
    t.comment = info.comment;
    std::vector<LibraryTrack> out;
    out.reserve(order.size());
    for (const auto& key : order) out.push_back(byUri[key]);
    sortByTitle(out);
    return out;
}

const LibraryTrack* firstWithUri(const std::vector<LibraryTrack>& tracks, const std::string& uri) {
    for (const auto& t : tracks) {
        if (t.uri == uri) return &t;
    }
    return nullptr;
}

LibraryEdit finish(std::vector<LibraryTrack> tracks, bool changed, bool corrupt) {
    LibraryEdit e;
    e.changed = changed;
    e.corruptInput = corrupt;
    e.json = writeLibrary(tracks);
    e.tracks = std::move(tracks);
    return e;
}

// One re-read title and artist applied to `tracks`; false when the uri is not held.
bool titleArtist(std::vector<LibraryTrack>& tracks, const std::string& uri, const std::string& title,
                 const std::string& artist) {
    const LibraryTrack* prev = firstWithUri(tracks, uri);
    if (!prev) return false;
    TrackInfo info;
    info.title = title;
    info.artist = artist;
    info.album = prev->album;
    info.genre = prev->genre;
    info.year = prev->year;
    info.trackNo = prev->trackNo;
    info.comment = prev->comment;
    tracks = upsertInfo(tracks, uri, info);
    return true;
}

}  // namespace

// TrackLibrary.readLocked + parseOrNull: blank is empty; a bare array, or an object's "tracks" array, of objects
// each with a "uri"; anything else is corrupt. org.json reads the first value and ignores what follows it.
LibraryRead readLibrary(std::string_view text) {
    LibraryRead r;
    if (isBlank(text)) return r;
    const std::string trimmed = trimKotlin(text);
    const auto root = json::parseLeading(trimmed);
    const json::Value* array = nullptr;
    if (root && trimmed.front() == '[') {
        if (root->kind() == json::Value::Kind::Array) array = &*root;
    } else if (root && root->kind() == json::Value::Kind::Object) {
        const json::Value* tracks = root->get("tracks");
        if (tracks && tracks->kind() == json::Value::Kind::Array) array = tracks;
    }
    if (!array) {
        r.corrupt = true;
        return r;
    }
    for (const auto& element : array->array()) {
        auto t = fromJson(element);
        if (!t) {
            r.corrupt = true;
            r.tracks.clear();
            return r;
        }
        r.tracks.push_back(std::move(*t));
    }
    return r;
}

std::string writeLibrary(const std::vector<LibraryTrack>& tracks) {
    std::string out = "{\"version\":" + std::to_string(kVersion) + ",\"tracks\":[";
    for (size_t i = 0; i < tracks.size(); ++i) {
        if (i) out += ',';
        out += toJson(tracks[i]);
    }
    out += "]}";
    return out;
}

LibraryEdit addTracks(std::string_view libraryText, std::string_view addedText) {
    const LibraryRead cur = readLibrary(libraryText);
    const LibraryRead added = readLibrary(addedText);
    return finish(mergeAdds(cur.tracks, added.tracks), true, cur.corrupt);
}

LibraryEdit saveTrackInfo(std::string_view libraryText, const std::string& uri, const TrackInfo& info) {
    LibraryRead cur = readLibrary(libraryText);
    return finish(upsertInfo(cur.tracks, uri, info), true, cur.corrupt);
}

LibraryEdit saveTitleArtist(std::string_view libraryText, const std::string& uri, const std::string& title,
                            const std::string& artist) {
    LibraryRead cur = readLibrary(libraryText);
    const bool changed = titleArtist(cur.tracks, uri, title, artist);
    return finish(std::move(cur.tracks), changed, cur.corrupt);
}

std::vector<std::string> titleRefreshUris(std::string_view libraryText) {
    std::vector<std::string> out;
    for (const auto& t : readLibrary(libraryText).tracks) {
        if (needsTitleRefresh(t.title, t.artist)) out.push_back(t.uri);
    }
    return out;
}

LibraryEdit applyFreshTitles(std::string_view libraryText, const std::vector<FreshTitle>& fresh) {
    LibraryRead cur = readLibrary(libraryText);
    bool changed = false;
    for (const auto& f : fresh) {
        const LibraryTrack* held = firstWithUri(cur.tracks, f.uri);
        if (!held || (held->title == f.title && held->artist == f.artist)) continue;
        changed = titleArtist(cur.tracks, f.uri, f.title, f.artist) || changed;
    }
    return finish(std::move(cur.tracks), changed, cur.corrupt);
}

}  // namespace geode::library
