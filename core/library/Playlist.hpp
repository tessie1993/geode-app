#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace geode::library {

enum class PlaylistFormat { None, M3u, Pls, Xspf };

constexpr long long kUnknownDuration = -1;

// One line of a playlist file. `baseName` is the file name inside `location` (percent-decoded, query and
// fragment of a URL cut, folders gone) with its case kept: the shell lower-cases it to meet the library's names.
struct PlaylistEntry {
    std::string location;
    std::string title;
    long long durationMs = kUnknownDuration;
    std::string baseName;
};

struct Playlist {
    bool ok = false;
    std::string why;  // set when !ok
    PlaylistFormat format = PlaylistFormat::None;
    std::string name;
    std::vector<PlaylistEntry> entries;
};

// Reads an M3U, PLS or XSPF file (UTF-8 bytes; a BOM is skipped). The format comes from the first 512 bytes, then
// from `fileName`'s extension. A playlist with a DOCTYPE is refused: it can name files the user did not pick.
Playlist parsePlaylist(std::string_view fileName, std::string_view bytes);

std::string playlistBaseName(std::string_view location);

// The wire the shell splits: record 0 is "O", name or "E", reason (fields split by U+001F); then one record per
// entry (location, title, durationMs, baseName), records split by U+001E. Those two characters never survive
// inside a field.
std::string playlistWire(const Playlist& playlist);

}  // namespace geode::library
