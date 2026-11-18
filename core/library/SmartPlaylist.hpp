#pragma once
#include <functional>
#include <optional>
#include <string>
#include <vector>

// Smart playlists: rules over the library's tracks, how their file reads and is written, and which tracks a
// rule set picks. Text is UTF-8; case and trimming follow java.lang.String (see JvmText.hpp), numbers follow
// Kotlin's toFloatOrNull and Float arithmetic.
namespace geode::library {

enum class RuleField { Title, Artist, Album, Folder, DurationMinutes, AddedDaysAgo, PlayCount, Favourite };
enum class RuleOp { Contains, Is, IsNot, AtLeast, AtMost };

struct SmartRule {
    RuleField field = RuleField::Title;
    RuleOp op = RuleOp::Contains;
    std::string value;
};

struct SmartPlaylist {
    std::string name;
    std::vector<SmartRule> rules;
    bool matchAll = true;
    int limit = 0;  // 0 or less: every match
};

// What a rule reads from a library track.
struct SmartTrack {
    std::string uri;
    std::string title;
    std::string artist;
    std::string album;
    std::string folder;
    long long durationMs = 0;
    long long addedSec = 0;
};

// The file's text as a playlist; nullopt for no playlist (not an object, no string "name"). A rule with a
// field or op this build does not know is dropped and the rest kept; "matchAll" defaults true, "limit" 0.
std::optional<SmartPlaylist> smartParse(const std::string& json);
// {"name","matchAll","limit","rules":[{"field","op","value"}]}, fields in that order, tokens as the enum names
// the stores always wrote (TITLE, DURATION_MINUTES, ..., CONTAINS, IS_NOT, ...).
std::string smartToJson(const SmartPlaylist& playlist);
// SmartPlaylistStore.save: the text to write, or nullopt for a blank name (nothing is saved).
std::optional<std::string> smartSaveText(const SmartPlaylist& playlist);
// Every readable playlist among the folder's file texts, sorted by lower-case name (ties in the order given).
std::vector<SmartPlaylist> smartList(const std::vector<std::string>& fileTexts);

// The row numbers (into `tracks`, in order) the playlist picks. Rules with a blank value are ignored unless
// their field is Favourite; none left picks every track; matchAll needs every rule, else any; then the first
// `limit` when limit > 0. `playCountOf` is the history's count for a uri; `favourites` the favourite uris;
// `nowMs` the wall clock (Added-days-ago reads it).
std::vector<int> smartResolve(const SmartPlaylist& playlist, const std::vector<SmartTrack>& tracks,
                              const std::function<int(const std::string&)>& playCountOf,
                              const std::vector<std::string>& favourites, long long nowMs);

// smartResolve for a playlist file's text and the history document's text (counts read from it, see History.hpp).
// nullopt when the playlist text does not read.
std::optional<std::vector<int>> smartResolveText(const std::string& smartJson, const std::vector<SmartTrack>& tracks,
                                                 const std::string& historyJson,
                                                 const std::vector<std::string>& favourites, long long nowMs);

}  // namespace geode::library
