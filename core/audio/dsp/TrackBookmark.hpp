#pragma once

#include <cstdint>

// Whether a track's position is remembered for the next listen. Only long tracks (a mix, an audiobook,
// a podcast) get a bookmark, and a position at either edge is not worth resuming, so it clears the old
// one. Pure; host-tested in core/tests/track_bookmark_test.cpp.
namespace geode::audio {

enum class BookmarkVerdict : int {
    Ignore = 0,  // a short or unknown-length track: leave any bookmark as it is
    Clear = 1,   // within the edge of either end: forget the bookmark
    Save = 2,    // remember this position
};

// A track of 20 minutes or more is long.
constexpr int64_t kLongTrackMs = 20 * 60 * 1000;
// A position closer than this to either end clears.
constexpr int64_t kBookmarkEdgeMs = 10 * 1000;

BookmarkVerdict bookmarkVerdict(int64_t positionMs, int64_t durationMs);

// Whether a track that has just started, at `currentPositionMs`, is still near enough its start to
// jump to its bookmark (a seek the listener already made is not undone).
bool bookmarkResumesFrom(int64_t currentPositionMs);

}  // namespace geode::audio
