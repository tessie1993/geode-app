#pragma once
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The rules of the user's ordinary playlists: how a playlist file reads and is written, which edits change
// it and how, which files an older name rule left under a stale name, the order they are listed in, and
// the free name an imported playlist takes. The shell lists the folder and reads and writes the files'
// bytes; it hands in text and acts on what comes back.
namespace geode::library {

struct MusicPlaylist {
    std::string name;
    std::vector<std::string> tracks;  // track uris, in order
};

// The file stem a playlist name is stored under (files::safeFileName in core/viz/LookFiles); handed in so this
// module stays free of the look files.
using StemFn = std::function<std::string(const std::string&)>;

// {"name":..., "tracks":[...]}; nullopt where the text is no playlist (not an object, no string "name", no array
// "tracks", or a track with no string form), which the store skipped.
std::optional<MusicPlaylist> playlistParse(const std::string& json);
// The text a playlist is written as, fields in the order the store wrote them.
std::string playlistToJson(const MusicPlaylist& playlist);
// Every readable playlist among the folder's file texts, sorted by lower-case name (ties in the order given).
std::vector<MusicPlaylist> playlistList(const std::vector<std::string>& fileTexts);

enum class PlaylistEditKind {
    Create,       // name + tracks: a new playlist of the distinct tracks, the name trimmed; a blank name does nothing
    Import,       // name (a base) + tracks: the playlist under the first free name from the base, tracks as given
    AddTrack,     // name + track: appended unless already in it
    RemoveTrack,  // name + track: every copy taken out
    Move,         // name + from + to: the track at `from` goes to `to`, clamped into the list
    Rename,       // name -> newName (trimmed here); refused when blank or taken
};

struct PlaylistEdit {
    PlaylistEditKind kind = PlaylistEditKind::Create;
    std::string name;
    std::string newName;
    std::string track;
    std::vector<std::string> tracks;
    int from = 0;
    int to = 0;
};

// What the store holds when an edit is made: every playlist file's text, and the text of the file the edit's
// name maps to (nullopt when there is none).
struct PlaylistFolder {
    std::vector<std::string> fileTexts;
    std::optional<std::string> namedFileText;
};

struct PlaylistOutcome {
    MusicPlaylist playlist;    // what the store returned (the edited playlist, or the unchanged one)
    bool write = false;        // true: save `json` under the file for playlist.name
    std::string json;          // playlistToJson(playlist), set with `write`
    bool ok = true;            // Rename: false when it was refused
    std::string deleteName;    // Rename: once `json` is written, delete the file for this name ("" for none)
};

// One edit of the store (MusicPlaylistStore.addTrack/removeTrack/move/rename, MusicLibraryRepository's create
// and import). An add or remove on a playlist that does not exist starts it empty and writes it; a move on one
// returns it empty, unwritten. When the named file exists but does not read, the edit returns an empty playlist
// of that name, unwritten, so the file is never overwritten.
PlaylistOutcome playlistApply(const PlaylistFolder& folder, const PlaylistEdit& edit, const StemFn& stem);

// The first of base, "base (2)", "base (3)", ... not among the names taken.
std::string playlistUniqueName(const std::vector<std::string>& taken, const std::string& base);

// The name an imported file's playlist starts from: the playlist's own name, else the file's name, else
// `untitled` (ifBlank, ifBlank), each kept as it is when not blank.
std::string playlistImportBase(const std::string& parsedName, const std::string& fileName, const std::string& untitled);

struct PlaylistFileText {
    std::string fileName;  // as in the folder, with its extension
    std::string text;
};

struct PlaylistRename {
    std::string from;  // file names in the folder
    std::string to;
};

// The one-time rename of files saved under an older name rule: each ".json" file whose playlist reads, and
// whose name's stem is not the file's own, moves to "<stem>.json" unless that file exists (or an earlier
// rename of this plan made it). Files are taken in the order given.
std::vector<PlaylistRename> playlistMigrationPlan(const std::vector<PlaylistFileText>& files, const StemFn& stem);

}  // namespace geode::library
