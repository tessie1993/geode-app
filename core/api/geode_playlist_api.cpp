// C ABI over the playlist file reader (library/Playlist). See geode_api.h.
#include <string_view>

#include "api/geode_api.h"
#include "api/geode_text_out.hpp"
#include "library/Playlist.hpp"

int geode_playlist_parse(const char* file_name, const unsigned char* data, int len, char* out, int capacity) {
    const std::string_view bytes(reinterpret_cast<const char*>(data), data && len > 0 ? static_cast<size_t>(len) : 0);
    const geode::library::Playlist playlist = geode::library::parsePlaylist(file_name ? file_name : "", bytes);
    return geode::api::textOut(geode::library::playlistWire(playlist), out, capacity);
}
