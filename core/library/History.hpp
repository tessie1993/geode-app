#pragma once
#include <optional>
#include <string>
#include <vector>

// The play history: which plays count, how recency and play counts are kept, what survives on disk and the
// order "recently played" lists. The state is a document in history.json's own schema; the shell keeps its text,
// hands it to each call and keeps what comes back. The clock is a parameter, and the shell does the locking,
// the quarantine of an unreadable file and the write queue.
//
//   {"tracks":[{"uri","last","count","title","artist","ms"}, ...]}   (a bare array of those is read too)
namespace geode::library {

struct HistoryEntry {
    std::string uri;
    long long lastPlayedMs = 0;
    int playCount = 0;
    std::string title;
    std::string artist;
    long long listenedMs = 0;
};

struct HistoryDoc {
    std::vector<HistoryEntry> entries;  // in the order the store held them
    // The text was not blank and did not read: the shell moves the file aside and starts empty.
    bool corrupt = false;
};

// The state a file's text holds. Blank text is an empty state. An entry with a blank uri (or not an object) is
// skipped; a repeated uri keeps its first place and its last values.
HistoryDoc historyParse(const std::string& text);

// A state, and the text of the file to write for it: the union of the most recent kRetainRecent entries and the
// kRetainPlayed most played (play count above 0; ties by listened time), recent first, then the others.
struct HistoryUpdate {
    std::string state;  // every entry, same schema
    std::string file;   // the retained entries
};

inline constexpr int kHistoryRetainRecent = 200;
inline constexpr int kHistoryRetainPlayed = 200;

// A track starts playing at nowMs: its entry is made if new, its last-played time becomes the later of nowMs and
// one past the newest stamp held (so two plays in one millisecond keep their order), its count goes up, its title
// is set, and its artist unless blank.
HistoryUpdate historyRecordPlay(const std::string& state, const std::string& uri, const std::string& title,
                                const std::string& artist, long long nowMs);

// Adds deltaMs (ignored at 0 or less, and for a uri not held) to a track's listened time. `changed` is false
// when nothing was added to a held entry (the shell sets its dirty flag for any delta above 0, as before).
HistoryUpdate historyAddListen(const std::string& state, const std::string& uri, long long deltaMs);

// The file text for a state (retention applied).
std::string historyFileText(const std::string& state);

// Entries newest first (ties in held order), at most `limit` of them; a limit of 0 or less gives none.
std::vector<HistoryEntry> historyRecent(const std::string& state, int limit);

// The entry for a uri.
std::optional<HistoryEntry> historyEntry(const std::string& state, const std::string& uri);

}  // namespace geode::library
