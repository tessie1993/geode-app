#include "audio/dsp/TrackBookmark.hpp"

namespace geode::audio {

BookmarkVerdict bookmarkVerdict(int64_t positionMs, int64_t durationMs) {
    if (durationMs < kLongTrackMs) return BookmarkVerdict::Ignore;
    if (positionMs < kBookmarkEdgeMs || positionMs > durationMs - kBookmarkEdgeMs) return BookmarkVerdict::Clear;
    return BookmarkVerdict::Save;
}

bool bookmarkResumesFrom(int64_t currentPositionMs) { return currentPositionMs < kBookmarkEdgeMs; }

}  // namespace geode::audio
