#pragma once
// library.json, the app's own track list: the version-3 document the Kotlin TrackLibrary read and wrote, and
// the edits made to it. Every edit takes the document's text and returns the new one; the shell reads and
// writes the bytes (a file that cannot be read is not handed here). Nothing is written by this code.
#include <string>
#include <string_view>
#include <vector>

#include "library/TrackRules.hpp"

namespace geode::library {

// What a document held. Blank text is an empty library; text that is not a library (not JSON, no "tracks"
// array, a track without a "uri") is `corrupt` with no tracks: the shell moves the file aside to
// "library.json.corrupt" and carries on with an empty library. A bare array is read too (the oldest form).
struct LibraryRead {
    bool corrupt = false;
    std::vector<LibraryTrack> tracks;
};
LibraryRead readLibrary(std::string_view text);

// The document for `tracks`: {"version":3,"tracks":[...]}, fields in the order the app has always written.
std::string writeLibrary(const std::vector<LibraryTrack>& tracks);

// One edit's outcome. `tracks` and `json` are the library after the edit (`json` is what to write when
// `changed`); `corruptInput` tells the shell the old file was unreadable as a library (see LibraryRead) and the
// edit went on from an empty one.
struct LibraryEdit {
    bool changed = false;
    bool corruptInput = false;
    std::vector<LibraryTrack> tracks;
    std::string json;
};

// Adds tracks (`addedText` is a library document or bare array): a track whose identity (file name + size when
// both are known, else its uri) is already held is dropped; a held track with no file name takes the identity of
// an added track with its uri; the result is sorted by lower-case title (stable). Always `changed`. An
// `addedText` that is not a library adds nothing.
LibraryEdit addTracks(std::string_view libraryText, std::string_view addedText);

// The edited fields of one track (everything but its duration and file facts).
struct TrackInfo {
    std::string title;
    std::string artist;
    std::string album;
    std::string genre;
    int year = 0;
    int trackNo = 0;
    std::string comment;
};
// Sets a track's info, adding the track (title only) when the uri is not held; sorted as addTracks. Always `changed`.
LibraryEdit saveTrackInfo(std::string_view libraryText, const std::string& uri, const TrackInfo& info);

// Re-reads a title and artist, keeping the track's other info. An unknown uri changes nothing.
LibraryEdit saveTitleArtist(std::string_view libraryText, const std::string& uri, const std::string& title,
                            const std::string& artist);

// The tracks whose title looks like an unread number (see needsTitleRefresh), as uris, in library order.
std::vector<std::string> titleRefreshUris(std::string_view libraryText);

struct FreshTitle {
    std::string uri;
    std::string title;
    std::string artist;
};
// Applies re-read titles one after another: each whose title or artist differs from the held track's is saved
// as saveTitleArtist; `changed` when any was.
LibraryEdit applyFreshTitles(std::string_view libraryText, const std::vector<FreshTitle>& fresh);

}  // namespace geode::library
