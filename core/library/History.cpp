#include "library/History.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <unordered_map>

#include "library/JsonRead.hpp"
#include "library/JvmText.hpp"
#include "util/Json.hpp"

namespace geode::library {

namespace {

// HistoryStore.readEntries: into a LinkedHashMap keyed by uri, so a repeated uri keeps its first place.
void readEntries(const json::Value& arr, std::vector<HistoryEntry>& out) {
    std::unordered_map<std::string, size_t> at;
    for (const json::Value& o : arr.array()) {
        if (o.kind() != json::Value::Kind::Object) continue;
        const std::string uri = jsonread::optString(o, "uri");
        if (jvm::isBlank(uri)) continue;
        HistoryEntry e;
        e.uri = uri;
        e.lastPlayedMs = jsonread::optLong(o, "last", 0);
        e.playCount = jsonread::optInt(o, "count", 0);
        e.title = jsonread::optString(o, "title");
        e.artist = jsonread::optString(o, "artist");
        e.listenedMs = jsonread::optLong(o, "ms", 0);
        const auto it = at.find(uri);
        if (it == at.end()) {
            at[uri] = out.size();
            out.push_back(std::move(e));
        } else {
            out[it->second] = std::move(e);
        }
    }
}

std::string entriesText(const std::vector<const HistoryEntry*>& entries) {
    std::string out = "{\"tracks\":[";
    bool first = true;
    for (const HistoryEntry* e : entries) {
        if (!first) out += ',';
        first = false;
        out += "{\"uri\":" + json::quoted(e->uri) + ",\"last\":" + std::to_string(e->lastPlayedMs) +
               ",\"count\":" + std::to_string(e->playCount) + ",\"title\":" + json::quoted(e->title) +
               ",\"artist\":" + json::quoted(e->artist) + ",\"ms\":" + std::to_string(e->listenedMs) + "}";
    }
    return out + "]}";
}

std::string textOf(const std::vector<HistoryEntry>& entries) {
    std::vector<const HistoryEntry*> all;
    for (const auto& e : entries) all.push_back(&e);
    return entriesText(all);
}

// The entries written to disk: the kHistoryRetainRecent newest, then the kHistoryRetainPlayed most played that
// those did not already hold (a repeat keeps its first place).
std::string fileOf(const std::vector<HistoryEntry>& all) {
    std::vector<const HistoryEntry*> byRecency;
    for (const auto& e : all) byRecency.push_back(&e);
    // sortedByDescending: stable, ties keep the held order.
    std::stable_sort(byRecency.begin(), byRecency.end(),
                     [](const HistoryEntry* a, const HistoryEntry* b) { return a->lastPlayedMs > b->lastPlayedMs; });
    std::vector<const HistoryEntry*> kept;
    std::map<std::string, bool> in;
    const auto keep = [&](const HistoryEntry* e) {
        if (in.emplace(e->uri, true).second) kept.push_back(e);
    };
    for (size_t i = 0; i < byRecency.size() && i < static_cast<size_t>(kHistoryRetainRecent); ++i) keep(byRecency[i]);

    std::vector<const HistoryEntry*> byPlays;
    for (const auto& e : all) {
        if (e.playCount > 0) byPlays.push_back(&e);
    }
    std::stable_sort(byPlays.begin(), byPlays.end(), [](const HistoryEntry* a, const HistoryEntry* b) {
        if (a->playCount != b->playCount) return a->playCount > b->playCount;
        return a->listenedMs > b->listenedMs;
    });
    for (size_t i = 0; i < byPlays.size() && i < static_cast<size_t>(kHistoryRetainPlayed); ++i) keep(byPlays[i]);
    return entriesText(kept);
}

HistoryEntry* find(std::vector<HistoryEntry>& entries, const std::string& uri) {
    for (auto& e : entries) {
        if (e.uri == uri) return &e;
    }
    return nullptr;
}

}  // namespace

HistoryDoc historyParse(const std::string& text) {
    HistoryDoc doc;
    if (jvm::isBlank(text)) return doc;
    // load(): a text starting with '[' (after trimStart) is a bare array, else an object's "tracks" (none: empty).
    if (jvm::trimStart(text).rfind('[', 0) == 0) {
        const auto arr = jsonread::arrayOf(text);
        if (!arr) {
            doc.corrupt = true;
            return doc;
        }
        readEntries(*arr, doc.entries);
        return doc;
    }
    const auto root = jsonread::objectOf(text);
    if (!root) {
        doc.corrupt = true;
        return doc;
    }
    if (const json::Value* tracks = jsonread::optArray(*root, "tracks")) readEntries(*tracks, doc.entries);
    return doc;
}

HistoryUpdate historyRecordPlay(const std::string& state, const std::string& uri, const std::string& title,
                                const std::string& artist, long long nowMs) {
    std::vector<HistoryEntry> entries = historyParse(state).entries;
    HistoryEntry* e = find(entries, uri);
    if (!e) {
        HistoryEntry fresh;
        fresh.uri = uri;
        fresh.title = title;
        entries.push_back(std::move(fresh));
        e = &entries.back();
    }
    long long newest = 0;  // maxOfOrNull { lastPlayedMs } ?: 0
    bool any = false;
    for (const auto& x : entries) {
        if (!any || x.lastPlayedMs > newest) newest = x.lastPlayedMs;
        any = true;
    }
    // maxOf(now, newest + 1); the Long wraps at its top as the JVM's does.
    const long long next = static_cast<long long>(static_cast<unsigned long long>(newest) + 1ULL);
    e->lastPlayedMs = std::max(nowMs, next);
    e->playCount += 1;
    e->title = title;
    if (!jvm::isBlank(artist)) e->artist = artist;
    return {textOf(entries), fileOf(entries)};
}

HistoryUpdate historyAddListen(const std::string& state, const std::string& uri, long long deltaMs) {
    std::vector<HistoryEntry> entries = historyParse(state).entries;
    if (deltaMs > 0) {
        if (HistoryEntry* e = find(entries, uri)) {
            e->listenedMs = static_cast<long long>(static_cast<unsigned long long>(e->listenedMs) + static_cast<unsigned long long>(deltaMs));
        }
    }
    return {textOf(entries), fileOf(entries)};
}

std::string historyFileText(const std::string& state) { return fileOf(historyParse(state).entries); }

std::vector<HistoryEntry> historyRecent(const std::string& state, int limit) {
    std::vector<HistoryEntry> entries = historyParse(state).entries;
    std::stable_sort(entries.begin(), entries.end(),
                     [](const HistoryEntry& a, const HistoryEntry& b) { return a.lastPlayedMs > b.lastPlayedMs; });
    if (limit <= 0) return {};
    if (entries.size() > static_cast<size_t>(limit)) entries.resize(static_cast<size_t>(limit));
    return entries;
}

std::optional<HistoryEntry> historyEntry(const std::string& state, const std::string& uri) {
    for (auto& e : historyParse(state).entries) {
        if (e.uri == uri) return e;
    }
    return std::nullopt;
}

std::string historyWire(const std::vector<HistoryEntry>& entries) {
    constexpr char kField = '\x1f';
    constexpr char kRecord = '\x1e';
    const auto field = [](std::string& out, const std::string& text) {
        for (const char c : text) {
            if (c != kField && c != kRecord) out += c;
        }
    };
    std::string out;
    for (const HistoryEntry& e : entries) {
        field(out, e.uri);
        out += kField + std::to_string(e.lastPlayedMs) + kField + std::to_string(e.playCount) + kField;
        field(out, e.title);
        out += kField;
        field(out, e.artist);
        out += kField + std::to_string(e.listenedMs) + kRecord;
    }
    return out;
}

}  // namespace geode::library
