#include <memory>
#include <string>

#include "api/geode_api.h"
#include "library/Tags.hpp"

struct geode_tags {
    geode::library::TrackTags tags;
};

namespace {

std::string textOrEmpty(const char* s) { return s ? std::string(s) : std::string(); }

}  // namespace

extern "C" {

geode_tags* geode_tags_read(int fd) {
    auto handle = std::make_unique<geode_tags>();
    if (!geode::library::readTags(fd, handle->tags)) return nullptr;
    return handle.release();
}

void geode_tags_destroy(geode_tags* h) { std::unique_ptr<geode_tags> owned(h); }

const char* geode_tags_text(const geode_tags* h, GeodeTagText field) {
    if (!h) return "";
    const geode::library::TrackTags& t = h->tags;
    switch (field) {
        case GEODE_TAG_TITLE: return t.title.c_str();
        case GEODE_TAG_ARTIST: return t.artist.c_str();
        case GEODE_TAG_ALBUM: return t.album.c_str();
        case GEODE_TAG_ALBUM_ARTIST: return t.albumArtist.c_str();
        case GEODE_TAG_GENRE: return t.genre.c_str();
        case GEODE_TAG_COMMENT: return t.comment.c_str();
        case GEODE_TAG_TEXT_COUNT: return "";
    }
    return "";
}

int geode_tags_year(const geode_tags* h) { return h ? h->tags.year : 0; }

int geode_tags_track(const geode_tags* h) { return h ? h->tags.track : 0; }

int geode_tags_duration_ms(const geode_tags* h) { return h ? h->tags.durationMs : 0; }

size_t geode_tags_art_bytes(const geode_tags* h) { return h ? h->tags.artBytes : 0; }

int geode_tags_replaygain(const geode_tags* h, float* track_gain_db, float* track_peak, float* album_gain_db,
                          float* album_peak) {
    if (!h) return 0;
    const geode::library::TrackTags& t = h->tags;
    int mask = 0;
    if (track_gain_db) *track_gain_db = t.trackGainDb;
    if (track_peak) *track_peak = t.trackPeak;
    if (album_gain_db) *album_gain_db = t.albumGainDb;
    if (album_peak) *album_peak = t.albumPeak;
    if (t.hasTrackGain) mask |= GEODE_TAG_TRACK_GAIN;
    if (t.hasTrackPeak) mask |= GEODE_TAG_TRACK_PEAK;
    if (t.hasAlbumGain) mask |= GEODE_TAG_ALBUM_GAIN;
    if (t.hasAlbumPeak) mask |= GEODE_TAG_ALBUM_PEAK;
    return mask;
}

int geode_tags_write(int fd, const char* const* texts, int year, int track) {
    geode::library::TrackTagEdit edit;
    if (texts) {
        edit.title = textOrEmpty(texts[GEODE_TAG_TITLE]);
        edit.artist = textOrEmpty(texts[GEODE_TAG_ARTIST]);
        edit.album = textOrEmpty(texts[GEODE_TAG_ALBUM]);
        edit.albumArtist = textOrEmpty(texts[GEODE_TAG_ALBUM_ARTIST]);
        edit.genre = textOrEmpty(texts[GEODE_TAG_GENRE]);
        edit.comment = textOrEmpty(texts[GEODE_TAG_COMMENT]);
    }
    edit.year = year;
    edit.track = track;
    return geode::library::writeTags(fd, edit) ? 1 : 0;
}

}  // extern "C"
