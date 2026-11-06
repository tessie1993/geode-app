#pragma once

#include <cstdint>

// Which sound a resting surface (the live wallpaper, the dream) hears: the app's own ring while
// something feeds it, else, when the user allows it, the output mix (any other app's audio, through
// a Visualizer the shell owns). Silence's neighbour: the same caller polls both on its own clock.
//
// The ring is the full-quality signal, so it wins whenever it is fed. "Fed" is read from the ring's
// written-frame counter alone, not from whether a player is running: a counter that moved since the
// last poll is a write. The shell starts and stops the Visualizer; this rule only says when.
//
// Hysteresis, so a ring written in bursts never flaps the capture on and off (each start creates and
// releases an audio effect):
// - ring to capture: only after the ring has had no write for kStallMs;
// - capture to ring: only once the ring's writes have run kReturnMs without a stall (first write to
//   latest), so one stray write or a ring written in bursts never pulls the source back.
// A capture start that fails (no permission yet, a device that keeps the output mix to itself) is
// reported back through startFailed and not retried for kRetryMs; capture becoming permitted again (the user just granted it) retries at
// once.
//
// Pure: the caller keeps the State (zeroed = fresh) and hands it in each poll, like Silence's one
// timestamp. Plain int64 fields so the JNI layer can carry it as one long array.
namespace geode::sound::source {

// The ring counts as fed this long after its last write (a tap writes every few ms).
inline constexpr int64_t kStallMs = 1000;
// Back on the capture, the ring must stay fed this long before it takes over again.
inline constexpr int64_t kReturnMs = 500;
// A refused capture start waits this long before the next try.
inline constexpr int64_t kRetryMs = 10000;

enum Mode : int64_t { kRing = 0, kCapture = 1 };

// Bits of a step's answer.
inline constexpr int kUseCapture = 1;    // read the capture's input (when it is running), not the ring
inline constexpr int kStartCapture = 2;  // try to start the capture now
inline constexpr int kStopCapture = 4;   // stop the capture now

struct State {
    int64_t mode = kRing;
    int64_t ringSeen = 0;      // 1 once a ring has been seen
    int64_t ringId = 0;        // which ring the counter below belongs to
    int64_t ringWritten = 0;   // its counter at the last poll
    int64_t fedAtMs = 0;       // the last poll that saw a write
    int64_t fedSinceMs = 0;    // when the current unbroken run of writes began
    int64_t retryAtMs = 0;     // no capture start before this
    int64_t permitted = 0;     // capture was permitted on the last poll
};
inline constexpr int kStateFields = 8;
static_assert(sizeof(State) == kStateFields * sizeof(int64_t), "State travels as int64 fields");

// One poll. ringPresent: the app has a ring at all; ringId and ringWritten: which ring, and its
// written-frame counter. capturePermitted: the user allows hearing other apps and the permission is
// granted (or the capture already runs). captureRunning: the Visualizer is live.
inline int step(State& s, int64_t nowMs, bool ringPresent, int64_t ringId, int64_t ringWritten,
                bool capturePermitted, bool captureRunning) {
    // Newly permitted (just granted, or the option just turned on): try at once.
    if (capturePermitted && !s.permitted) s.retryAtMs = 0;
    s.permitted = capturePermitted ? 1 : 0;

    bool fed = false;
    if (ringPresent) {
        if (!s.ringSeen || s.ringId != ringId) {
            // A ring seen for the first time gets one stall window of grace, so a surface that comes
            // up while the app plays does not open the output mix for the first poll.
            s.ringSeen = 1;
            s.ringId = ringId;
            s.ringWritten = ringWritten;
            s.fedAtMs = nowMs;
            s.fedSinceMs = nowMs;
        } else if (ringWritten != s.ringWritten) {
            if (nowMs - s.fedAtMs >= kStallMs) s.fedSinceMs = nowMs;  // a new run after a stall
            s.ringWritten = ringWritten;
            s.fedAtMs = nowMs;
        }
        fed = nowMs - s.fedAtMs < kStallMs;
    } else {
        s.ringSeen = 0;
    }

    if (!capturePermitted) {
        s.mode = kRing;
    } else if (s.mode == kRing) {
        if (!fed) s.mode = kCapture;
    } else if (fed && s.fedAtMs - s.fedSinceMs >= kReturnMs) {
        s.mode = kRing;
    }

    if (s.mode == kRing) return captureRunning ? kStopCapture : 0;
    int out = kUseCapture;
    if (!captureRunning && nowMs >= s.retryAtMs) out |= kStartCapture;
    return out;
}

// The shell's report that a start step() asked for did not take: back off before the next one.
inline void startFailed(State& s, int64_t nowMs) { s.retryAtMs = nowMs + kRetryMs; }

}  // namespace geode::sound::source
