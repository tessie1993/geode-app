#pragma once

#include <cstdint>

#include "sound/interp/PlayoutClock.hpp"

/*
 * Speaker sync (#349, #488): how far a live renderer's cursor may read the player's ring this frame.
 *
 * The tap is first in the sink's chain, so the ring gets each sample when the sink takes it: a whole
 * sink buffer (500 ms with Media3 1.11), the HAL and on Bluetooth the A2DP link before the speaker
 * plays it. A renderer that reads up to the newest sample moves before the music is heard, at a song's
 * start, a seek and a resume most of all. So its cursor reads only up to the sample the speaker plays
 * now, as the playout clock answers it (A/V offset applied): `audio::ingest::Cursor::take(limit)`.
 *
 * Pure: `core/tests/speaker_sync_test.cpp` runs it with the real ring, cursor and clock on the host.
 */
namespace geode::sound::interp {

/** listenLimit's answer when nothing stands between the ring and the listener: read the newest. */
inline constexpr int64_t kNewest = INT64_MAX;

/**
 * The player's ring: frames kept and the most one write publishes. A reader at the speaker trails the
 * tap by the sink's buffer, the link and the A/V offset, in source frames: about 1.1 s at 1x over
 * Bluetooth, twice that at 2x. 2^19 less two runways keeps 2.5 s at 192 kHz, so it is not overrun.
 */
inline constexpr int kPlayerRingFrames = 1 << 19;
inline constexpr int kPlayerRingRunway = 1 << 14;

/**
 * The exclusive end (a ring head position) of what a live renderer may hear at monotonic [nowNs].
 * kNewest with no [clock] (a ring no sink plays: the wallpaper's output-mix capture), while the mic or
 * another app's audio writes the ring instead of the player ([playerIsSource] false), and under
 * [bypass] (Cast: the TV plays the ring as written). Else the clock's speaker sample, which is
 * PlayoutClock::kUnknown (negative: hear nothing yet) until the sink has taken a buffer.
 */
inline int64_t listenLimit(const PlayoutClock* clock, bool playerIsSource, bool bypass, int64_t nowNs) {
    if (!clock || !playerIsSource || bypass) return kNewest;
    return clock->speakerFrame(nowNs);
}

}  // namespace geode::sound::interp
