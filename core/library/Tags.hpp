#pragma once
#include <cstddef>
#include <optional>
#include <string>

namespace geode::library {

// What geode_tags_read hands back: the common text fields, ReplayGain, and the embedded art's size.
struct TrackTags {
    std::string title;
    std::string artist;
    std::string album;
    std::string albumArtist;
    std::string genre;
    std::string comment;
    int year = 0;
    int track = 0;
    bool hasTrackGain = false;
    float trackGainDb = 0.0f;
    bool hasTrackPeak = false;
    float trackPeak = 0.0f;
    bool hasAlbumGain = false;
    float albumGainDb = 0.0f;
    bool hasAlbumPeak = false;
    float albumPeak = 0.0f;
    size_t artBytes = 0;
    int durationMs = 0;
};

// A field left as std::nullopt is left unchanged by writeTags; an engaged empty string clears it.
struct TrackTagEdit {
    std::optional<std::string> title;
    std::optional<std::string> artist;
    std::optional<std::string> album;
    std::optional<std::string> albumArtist;
    std::optional<std::string> genre;
    std::optional<std::string> comment;
    int year = 0;
    int track = 0;
};

// Both take a file descriptor they own; it is closed before they return.
bool readTags(int fd, TrackTags& out);
bool writeTags(int fd, const TrackTagEdit& edit);

}  // namespace geode::library
