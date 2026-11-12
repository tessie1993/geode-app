#include <string>

#include "api/geode_api.h"
#include "api/geode_text_out.hpp"
#include "library/Lyrics.hpp"

extern "C" {

int geode_lyrics_parse(const char* text, char* out, int capacity) {
    const std::string wire = geode::library::lyricsWire(geode::library::parseLyrics(text ? text : ""));
    return geode::api::textOut(wire, out, capacity);
}

int geode_lyrics_index_at(const long long* times_ms, int count, long long position_ms) {
    return geode::library::lyricsIndexAt(times_ms, count, position_ms);
}

}  // extern "C"
