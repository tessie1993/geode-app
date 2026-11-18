#include <string>
#include <string_view>

#include "api/geode_api.h"
#include "api/geode_text_out.hpp"
#include "library/Lyrics.hpp"

extern "C" {

int geode_lyrics_parse(const char* text, int len, char* out, int capacity) try {
    const std::string_view source = text && len > 0 ? std::string_view(text, static_cast<size_t>(len)) : std::string_view();
    const std::string wire = geode::library::lyricsWire(geode::library::parseLyrics(source));
    return geode::api::textOut(wire, out, capacity);
} catch (...) {
    return geode::api::textOut(std::string(), out, capacity);
}

int geode_lyrics_index_at(const long long* times_ms, int count, long long position_ms) {
    return geode::library::lyricsIndexAt(times_ms, count, position_ms);
}

}  // extern "C"
