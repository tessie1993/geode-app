#include "library/Tags.hpp"

#include <fileref.h>
#include <tag.h>
#include <tfilestream.h>
#include <tpropertymap.h>
#include <tvariant.h>

#include <unistd.h>

#include <cstdlib>

namespace geode::library {

namespace {

std::string utf8(const TagLib::String& s) { return s.toCString(true); }

TagLib::String fromUtf8(const std::string& s) { return TagLib::String(s, TagLib::String::UTF8); }

// ReplayGain text is "+1.23 dB" or "0.98"; strtof stops at the unit.
bool parseFloat(const TagLib::PropertyMap& map, const char* key, float& out) {
    if (!map.contains(key)) return false;
    const TagLib::StringList& values = map[key];
    if (values.isEmpty()) return false;
    const std::string text = utf8(values.front());
    char* end = nullptr;
    const float value = std::strtof(text.c_str(), &end);
    if (end == text.c_str()) return false;
    out = value;
    return true;
}

// TagLib closes the descriptor with the stream only once fdopen succeeded; a failed open leaves it to us.
bool opened(const TagLib::FileStream& stream, int fd) {
    if (stream.isOpen()) return true;
    ::close(fd);
    return false;
}

std::string firstOf(const TagLib::PropertyMap& map, const char* key) {
    if (!map.contains(key)) return {};
    const TagLib::StringList& values = map[key];
    return values.isEmpty() ? std::string() : utf8(values.front());
}

size_t pictureBytes(const TagLib::FileRef& ref) {
    size_t total = 0;
    for (const TagLib::VariantMap& picture : ref.complexProperties("PICTURE")) {
        if (picture.contains("data")) total += picture["data"].toByteVector().size();
    }
    return total;
}

}  // namespace

bool readTags(int fd, TrackTags& out) {
    TagLib::FileStream stream(fd, true);
    if (!opened(stream, fd)) return false;
    TagLib::FileRef ref(&stream, true, TagLib::AudioProperties::Average);
    if (ref.isNull()) return false;
    if (const TagLib::Tag* tag = ref.tag()) {
        out.title = utf8(tag->title());
        out.artist = utf8(tag->artist());
        out.album = utf8(tag->album());
        out.genre = utf8(tag->genre());
        out.comment = utf8(tag->comment());
        out.year = static_cast<int>(tag->year());
        out.track = static_cast<int>(tag->track());
    }
    const TagLib::PropertyMap map = ref.properties();
    out.albumArtist = firstOf(map, "ALBUMARTIST");
    out.hasTrackGain = parseFloat(map, "REPLAYGAIN_TRACK_GAIN", out.trackGainDb);
    out.hasTrackPeak = parseFloat(map, "REPLAYGAIN_TRACK_PEAK", out.trackPeak);
    out.hasAlbumGain = parseFloat(map, "REPLAYGAIN_ALBUM_GAIN", out.albumGainDb);
    out.hasAlbumPeak = parseFloat(map, "REPLAYGAIN_ALBUM_PEAK", out.albumPeak);
    out.artBytes = pictureBytes(ref);
    if (const TagLib::AudioProperties* props = ref.audioProperties()) out.durationMs = props->lengthInMilliseconds();
    return true;
}

bool writeTags(int fd, const TrackTagEdit& edit) {
    TagLib::FileStream stream(fd, false);
    if (!opened(stream, fd) || stream.readOnly()) return false;
    TagLib::FileRef ref(&stream, false, TagLib::AudioProperties::Fast);
    if (ref.isNull()) return false;
    TagLib::Tag* tag = ref.tag();
    if (!tag) return false;
    tag->setTitle(fromUtf8(edit.title));
    tag->setArtist(fromUtf8(edit.artist));
    tag->setAlbum(fromUtf8(edit.album));
    tag->setGenre(fromUtf8(edit.genre));
    tag->setComment(fromUtf8(edit.comment));
    tag->setYear(static_cast<unsigned int>(edit.year < 0 ? 0 : edit.year));
    tag->setTrack(static_cast<unsigned int>(edit.track < 0 ? 0 : edit.track));
    TagLib::PropertyMap map = ref.properties();
    if (edit.albumArtist.empty()) {
        map.erase("ALBUMARTIST");
    } else {
        map.replace("ALBUMARTIST", TagLib::StringList(fromUtf8(edit.albumArtist)));
    }
    ref.setProperties(map);
    return ref.save();
}

}  // namespace geode::library
