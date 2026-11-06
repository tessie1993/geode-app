#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>

/*
 * Speaker sync (#349, #488): how far a live renderer's cursor may read this frame.
 *
 * The player's tap sits first in the sink's chain, so its ingest ring gets each sample when the sink takes
 * it, a whole AudioTrack buffer (Media3 sizes it at about 250 ms for PCM), the HAL and on Bluetooth the
 * A2DP link before the speaker plays it. A renderer that hears up to the newest written sample flashes
 * every kick 0.3 s early on the speaker and 0.5 s early on a headset. So a live renderer's cursor reads
 * only up to the sample the speaker plays now, as the playout clock (`PlayoutClock`, `geode_playout_*`)
 * answers it with the user's A/V offset already applied: never past it, never skipping a sample to catch
 * up, never reading one twice.
 *
 * The clock maps the sink's media-time position through the buffers' anchors, and the ring holds source
 * samples (the tap is ahead of Sonic and SilenceSkipping), so at 1.5x the bound moves 1.5 source seconds
 * per second and the cursor reads 1.5x the samples, which is what is heard; a silence SilenceSkipping cut
 * is a jump of the bound, and the cursor reads through the silence over the next frames instead of
 * around it (the samples are in the ring; the engine only hears them quickly).
 *
 * Pure: ring positions in, ring positions out, so `core/tests/speaker_sync_test.cpp` proves it with the
 * real ring, cursor and clock on the host.
 */
namespace geode::sound::interp {

/**
 * The exclusive end of what a live renderer may hear now. [speakerFrame] is the playout clock's answer
 * (where the speaker is, offset applied): nullopt when no sink stands between the ring and the
 * listener (the mic, another app's audio, whose input latency is unknown and so 0: read the newest), and
 * negative while the clock knows nothing of the stream yet (hold). Never past [written].
 */
inline int64_t listenLimit(int64_t written, std::optional<int64_t> speakerFrame) {
    if (!speakerFrame) return written;
    if (*speakerFrame < 0) return -1;
    // The clock counts like the ring's write position (one second in at 48 kHz it answers 48 000: samples
    // 0 .. 47 999 are heard), so its answer is already an exclusive end.
    return std::min(written, *speakerFrame);
}

/** What a cursor hands its engine this frame: ring positions [from, from + count). */
struct ReadWindow {
    int64_t from = 0;
    int64_t count = 0;
};

/**
 * The next frames for a cursor whose next unread position is [read]: from there (or from [oldest] when the
 * ring has overwritten it: the ring's overrun rule, the only skip), up to [limit] and [written], at most
 * [maxFrames] (the rest come next frame). A limit at or behind [read] (the offset grew, the clock holds,
 * a pause) reads nothing: the engine waits; it never rewinds.
 */
inline ReadWindow window(int64_t read, int64_t written, int64_t oldest, int64_t limit, int64_t maxFrames) {
    const int64_t from = std::max(read, oldest);
    const int64_t end = std::min({limit, written, from + std::max<int64_t>(0, maxFrames)});
    return {from, std::max<int64_t>(0, end - from)};
}

}  // namespace geode::sound::interp
