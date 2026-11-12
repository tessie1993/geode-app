// Host-side checks for which track positions are remembered (core/audio/dsp/TrackBookmark) and their C ABI
// (geode_bookmark_verdict, geode_bookmark_resumes_from). Built and run by CI ("Native host tests"); no device.
//
// What is pinned: only tracks of 20 minutes or more get a bookmark, a position within 10 s of either end
// clears it, and a resume only jumps when the track is still within 10 s of its start.
#include <cstdio>

#include "api/geode_api.h"
#include "audio/dsp/TrackBookmark.hpp"

using geode::audio::BookmarkVerdict;
using geode::audio::bookmarkResumesFrom;
using geode::audio::bookmarkVerdict;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

constexpr int64_t kLong = 20 * 60 * 1000;

void onlyLongTracksAreRemembered() {
    check(bookmarkVerdict(5 * 60 * 1000, kLong - 1) == BookmarkVerdict::Ignore, "a track just under 20 minutes is ignored");
    check(bookmarkVerdict(60 * 1000, 3 * 60 * 1000) == BookmarkVerdict::Ignore, "a song is ignored");
    check(bookmarkVerdict(60 * 1000, -1) == BookmarkVerdict::Ignore, "an unknown length is ignored");
    check(bookmarkVerdict(5 * 60 * 1000, kLong) == BookmarkVerdict::Save, "exactly 20 minutes is long");
}

void edgesClear() {
    const int64_t dur = 47 * 60 * 1000 + 13 * 1000;
    check(bookmarkVerdict(0, dur) == BookmarkVerdict::Clear, "the very start clears");
    check(bookmarkVerdict(9999, dur) == BookmarkVerdict::Clear, "just inside the start edge clears");
    check(bookmarkVerdict(10000, dur) == BookmarkVerdict::Save, "exactly the edge from the start saves");
    check(bookmarkVerdict(dur - 10000, dur) == BookmarkVerdict::Save, "exactly the edge from the end saves");
    check(bookmarkVerdict(dur - 9999, dur) == BookmarkVerdict::Clear, "just inside the end edge clears");
    check(bookmarkVerdict(dur, dur) == BookmarkVerdict::Clear, "the very end clears");
}

void resumeOnlyNearTheStart() {
    check(bookmarkResumesFrom(0) && bookmarkResumesFrom(9999), "a fresh start resumes");
    check(!bookmarkResumesFrom(10000), "a listener who already moved on is left alone");
}

void cAbi() {
    check(geode_bookmark_verdict(5 * 60 * 1000, kLong) == static_cast<int>(BookmarkVerdict::Save), "the C ABI's verdict is the engine's");
    check(geode_bookmark_verdict(0, kLong) == static_cast<int>(BookmarkVerdict::Clear), "the C ABI clears at the edge");
    check(geode_bookmark_resumes_from(5) == 1 && geode_bookmark_resumes_from(20000) == 0, "the C ABI's resume is the engine's");
}

}  // namespace

int main() {
    onlyLongTracksAreRemembered();
    edgesClear();
    resumeOnlyNearTheStart();
    cAbi();
    if (failures == 0) std::printf("track_bookmark_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
