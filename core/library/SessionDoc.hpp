#pragma once
#include <optional>
#include <string>
#include <vector>

// The saved play queue (session.json): which text reads as a session, and what is written for one.
//   {"version":1,"tracks":[{"uri","title","artist"}],"index":n,"positionMs":ms}
namespace geode::library {

struct SavedTrack {
    std::string uri;
    std::string title;
    std::string artist;
};

struct SavedSession {
    std::vector<SavedTrack> tracks;
    int index = 0;
    long long positionMs = 0;
};

inline constexpr int kSessionVersion = 1;

// The session a file's text holds, or nullopt: the shell moves such a file aside. That is: not an object, a
// "version" above kSessionVersion (written by a newer app; absent reads as 1), no "tracks" array, or no track
// with a non-blank uri. Items that are not objects or have a blank uri are skipped; index is clamped into the
// tracks, positionMs to 0 or more.
std::optional<SavedSession> sessionParse(const std::string& text);

struct SessionWrite {
    bool clear = false;  // no tracks: delete the file instead
    std::string json;
};

// The text to write for a session (index and position clamped), or `clear` when it holds no tracks.
SessionWrite sessionWrite(const SavedSession& session);

}  // namespace geode::library
