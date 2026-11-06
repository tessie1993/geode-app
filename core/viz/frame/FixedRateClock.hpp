#pragma once
#include <cstdint>

namespace geode::viz::frame {

// A fixed-rate frame clock for a stream that must keep its own time, whatever the GL thread does:
// Cast to TV today (#572), which draws offscreen into an encoder and paces itself off the monotonic
// clock rather than vsync. Pure maths, no GL and no clock read: the caller hands in "now" (in ns, on
// one monotonic clock) and keeps the state, so the whole rule is testable on the host
// (core/tests/cast_clock_test.cpp).
//
// The stream's time is frame slots, slot i starting at start + pts(i), pts(i) = i * 1e9 / fps. Each
// frame's PTS is its slot's start, never the wall time it was drawn at, so PTS is exact, evenly spaced
// and strictly increasing, and the audio queued beside it can be cut at the same slot boundary.
//
// The one rule that matters is what happens after a stall (a scene build, a GC pause on the thread, a
// slow encoder): at most one frame per slot. A late take skips to the slot that is due now and drops
// the ones it missed, instead of drawing them back to back to catch up. A burst would hand the encoder
// several frames in a few milliseconds that the TV then plays as a fast-forward; a dropped slot is just
// a held picture. Audio does not drop: its cut is the end of the frame taken, so the gap is filled by
// the audio pump (with music or silence) and sound and picture stay on one timeline.
struct FixedRateClock {
    static constexpr int64_t kNanosPerSec = 1'000'000'000;
    static constexpr int64_t kMicrosPerSec = 1'000'000;

    int64_t startNs = 0;
    int64_t next = 0;   // the first slot not yet taken
    int fps = 30;

    static FixedRateClock started(int fps, int64_t nowNs) {
        FixedRateClock c;
        c.startNs = nowNs;
        c.next = 0;
        c.fps = fps > 0 ? fps : 30;
        return c;
    }

    // Slot i's start, as an offset from start (and so its PTS).
    int64_t ptsNs(int64_t slot) const { return slot * kNanosPerSec / fps; }

    // The last slot whose start is at or before now (0 before the clock started). Exact against
    // ptsNs: the division can land one short, so the next slot is checked too.
    int64_t dueSlot(int64_t nowNs) const {
        const int64_t elapsed = nowNs - startNs;
        if (elapsed <= 0) return 0;
        int64_t slot = elapsed * fps / kNanosPerSec;
        if (ptsNs(slot + 1) <= elapsed) ++slot;
        return slot;
    }

    // How long until the next slot starts; 0 when a frame is due now.
    int64_t waitNs(int64_t nowNs) const {
        const int64_t wait = startNs + ptsNs(next) - nowNs;
        return wait > 0 ? wait : 0;
    }

    // Claims the frame to draw now and returns its PTS: the next slot, or, when that slot has already
    // ended, the slot due now (the missed ones are dropped). Called early (before waitNs reached 0) it
    // still takes the next slot, so a frame is never pushed back.
    int64_t take(int64_t nowNs) {
        const int64_t due = dueSlot(nowNs);
        const int64_t slot = due > next ? due : next;
        next = slot + 1;
        return ptsNs(slot);
    }

    // The audio cut after the last take: the end of the frame taken, in µs of stream time. The audio
    // pump queues up to here, so sound runs exactly as far as the picture.
    int64_t audioUntilUs() const { return next * kMicrosPerSec / fps; }
};

// The Cast stream's format: what the TV's receiver (Google's Default Media Receiver, live HLS of
// H.264 + AAC) plays smoothly over home Wi-Fi from a phone encoder, at the 4 Mbit/s CastEncoder asks.
struct CastFormat {
    static constexpr int kWidth = 1280;
    static constexpr int kHeight = 720;
    static constexpr int kFps = 30;
};

}  // namespace geode::viz::frame
