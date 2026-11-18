// The C ABI of the app's track library (core/library: TrackRules, TrackLibraryDoc): whole batches in, one answer out.
// Marshals only: the arrays are gathered, the answer is written as U+001E-ended records of U+001F-split fields.
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "api/geode_api.h"
#include "library/TrackLibraryDoc.hpp"
#include "library/TrackRules.hpp"

namespace {

using namespace geode::library;

constexpr char kField = '\x1F';
constexpr char kRecord = '\x1E';

std::string textOf(const char* s) { return s ? s : ""; }

std::optional<std::string> optionalOf(const char* s) {
    if (!s) return std::nullopt;
    return std::string(s);
}

const char* hold(std::string& slot, std::string value) {
    slot = std::move(value);
    return slot.c_str();
}

// A decimal text as a number; anything else is 0 (the shell writes these itself).
long long numberOf(const char* s) {
    long long v = 0;
    bool negative = false;
    for (const char* p = s; p && *p; ++p) {
        if (p == s && *p == '-') {
            negative = true;
        } else if (*p >= '0' && *p <= '9') {
            v = v * 10 + (*p - '0');
        } else {
            return 0;
        }
    }
    return negative ? -v : v;
}

void field(std::string& out, std::string_view text) {
    out.append(text);
    out.push_back(kField);
}

void endRecord(std::string& out) {
    if (!out.empty() && out.back() == kField) out.back() = kRecord;
    else out.push_back(kRecord);
}

void trackRecord(std::string& out, const LibraryTrack& t) {
    field(out, t.uri);
    field(out, t.title);
    field(out, t.artist);
    field(out, std::to_string(t.durationMs));
    field(out, t.album);
    field(out, t.genre);
    field(out, std::to_string(t.year));
    field(out, std::to_string(t.trackNo));
    field(out, t.comment);
    field(out, t.fileName);
    field(out, std::to_string(t.sizeBytes));
    endRecord(out);
}

LibraryTrack trackOf(const char* const* cell) {
    LibraryTrack t;
    t.uri = textOf(cell[0]);
    t.title = textOf(cell[1]);
    t.artist = textOf(cell[2]);
    t.durationMs = numberOf(cell[3]);
    t.album = textOf(cell[4]);
    t.genre = textOf(cell[5]);
    t.year = static_cast<int>(numberOf(cell[6]));
    t.trackNo = static_cast<int>(numberOf(cell[7]));
    t.comment = textOf(cell[8]);
    t.fileName = textOf(cell[9]);
    t.sizeBytes = numberOf(cell[10]);
    return t;
}

std::string editWire(const LibraryEdit& e) {
    std::string out;
    field(out, e.changed ? "1" : "0");
    field(out, e.corruptInput ? "1" : "0");
    field(out, e.json);
    endRecord(out);
    for (const auto& t : e.tracks) trackRecord(out, t);
    return out;
}

}  // namespace

extern "C" {

const char* geode_track_library_read(const char* text) try {
    thread_local std::string out;
    const LibraryRead read = readLibrary(textOf(text));
    LibraryEdit edit;
    edit.corruptInput = read.corrupt;
    edit.tracks = read.tracks;
    return hold(out, editWire(edit));
} catch (...) {
    return nullptr;
}

const char* geode_track_library_add(const char* text, const char* const* tracks, int count) try {
    thread_local std::string out;
    std::vector<LibraryTrack> added;
    for (int i = 0; tracks && i < count; ++i) {
        added.push_back(trackOf(tracks + static_cast<size_t>(i) * GEODE_TRACK_FIELDS));
    }
    return hold(out, editWire(addTracks(textOf(text), writeLibrary(added))));
} catch (...) {
    return nullptr;
}

const char* geode_track_library_save_info(const char* text, const char* uri, const char* title, const char* artist,
                                          const char* album, const char* genre, int year, int track_no,
                                          const char* comment) try {
    thread_local std::string out;
    TrackInfo info;
    info.title = textOf(title);
    info.artist = textOf(artist);
    info.album = textOf(album);
    info.genre = textOf(genre);
    info.year = year;
    info.trackNo = track_no;
    info.comment = textOf(comment);
    return hold(out, editWire(saveTrackInfo(textOf(text), textOf(uri), info)));
} catch (...) {
    return nullptr;
}

const char* geode_track_library_apply_fresh(const char* text, const char* const* fresh, int count) try {
    thread_local std::string out;
    std::vector<FreshTitle> titles;
    for (int i = 0; fresh && i < count; ++i) {
        const char* const* cell = fresh + static_cast<size_t>(i) * 3;
        titles.push_back({textOf(cell[0]), textOf(cell[1]), textOf(cell[2])});
    }
    return hold(out, editWire(applyFreshTitles(textOf(text), titles)));
} catch (...) {
    return nullptr;
}

const char* geode_track_tag_fallbacks(const char* const* files, const long long* sizes, int count,
                                      const char* untitled) try {
    thread_local std::string out;
    std::string wire;
    for (int i = 0; files && i < count; ++i) {
        const char* const* cell = files + static_cast<size_t>(i) * 9;
        TagRead tags;
        tags.title = optionalOf(cell[0]);
        tags.artist = optionalOf(cell[1]);
        tags.album = optionalOf(cell[2]);
        tags.genre = optionalOf(cell[3]);
        tags.year = optionalOf(cell[4]);
        tags.trackNumber = optionalOf(cell[5]);
        FileFacts facts;
        facts.uri = textOf(cell[6]);
        facts.displayName = textOf(cell[7]);
        facts.sizeBytes = sizes ? sizes[i] : 0;
        facts.lastPathSegment = optionalOf(cell[8]);
        facts.untitled = textOf(untitled);
        trackRecord(wire, tagFallbacks(tags, facts));
    }
    return hold(out, std::move(wire));
} catch (...) {
    return nullptr;
}

int geode_track_walk_actions(const char* const* names, const char* const* mimes, const int* is_dir, int count,
                             int parent_depth, int* out) try {
    if (count < 0 || (count > 0 && (!names || !out))) return 0;
    for (int i = 0; i < count; ++i) {
        std::optional<std::string_view> name;
        if (names[i]) name = std::string_view(names[i]);
        std::optional<std::string_view> mime;
        if (mimes && mimes[i]) mime = std::string_view(mimes[i]);
        out[i] = static_cast<int>(walkAction(name, is_dir && is_dir[i] != 0, mime, parent_depth));
    }
    return count;
} catch (...) {
    return 0;
}

int geode_track_title_refresh(const char* const* title_artist, int count, int* out) try {
    if (count < 0 || (count > 0 && (!title_artist || !out))) return 0;
    for (int i = 0; i < count; ++i) {
        const char* const* cell = title_artist + static_cast<size_t>(i) * 2;
        out[i] = needsTitleRefresh(textOf(cell[0]), textOf(cell[1])) ? 1 : 0;
    }
    return count;
} catch (...) {
    return 0;
}

const char* geode_track_device_rows(const char* const* rows, const long long* durations_ms, int count, int min_seconds,
                                    const char* const* fallbacks) try {
    thread_local std::string out;
    std::string wire;
    int hidden = 0;
    for (int i = 0; rows && i < count; ++i) {
        if (!minTrackKeeps(durations_ms ? durations_ms[i] : 0, min_seconds)) {
            ++hidden;
            continue;
        }
        const char* const* cell = rows + static_cast<size_t>(i) * 4;
        auto view = [](const char* s) -> std::optional<std::string_view> {
            if (!s) return std::nullopt;
            return std::string_view(s);
        };
        field(wire, std::to_string(i));
        field(wire, tagOr(view(cell[0]), textOf(fallbacks ? fallbacks[0] : nullptr)));
        field(wire, tagOr(view(cell[1]), textOf(fallbacks ? fallbacks[1] : nullptr)));
        field(wire, tagOr(view(cell[2]), textOf(fallbacks ? fallbacks[2] : nullptr)));
        field(wire, folderOf(textOf(cell[3])));
        endRecord(wire);
    }
    wire += std::to_string(hidden);
    wire.push_back(kRecord);
    return hold(out, std::move(wire));
} catch (...) {
    return nullptr;
}

const char* geode_track_resolve_entries(const char* const* base_names, int entry_count, const char* const* library,
                                        int track_count) try {
    thread_local std::string out;
    std::vector<std::string> names;
    for (int i = 0; base_names && i < entry_count; ++i) names.push_back(textOf(base_names[i]));
    std::vector<std::string> fileNames;
    std::vector<std::string> uris;
    for (int i = 0; library && i < track_count; ++i) {
        fileNames.push_back(textOf(library[static_cast<size_t>(i) * 2]));
        uris.push_back(textOf(library[static_cast<size_t>(i) * 2 + 1]));
    }
    const PlaylistResolution r = resolvePlaylistEntries(names, fileNames, uris);
    std::string wire;
    for (const auto& uri : r.uris) field(wire, uri);
    endRecord(wire);
    for (const int i : r.missing) field(wire, std::to_string(i));
    endRecord(wire);
    for (const int i : r.ambiguous) field(wire, std::to_string(i));
    endRecord(wire);
    return hold(out, std::move(wire));
} catch (...) {
    return nullptr;
}

}  // extern "C"
