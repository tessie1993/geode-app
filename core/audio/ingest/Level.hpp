#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <mutex>

// How loud a ring is right now, for the UI: the wallpaper's edge glow breathes with it.
//
// It sits beside the ring's other poll-side answer, geode_ingest_resting (core/sound/Silence's rule
// and hold, fed by the ring). The two are one family on purpose: every surface that asks "is this
// source quiet?" or "how loud is it?" asks the ring it hears, natively, so the wallpaper, the dream,
// the edge glow and the capture screen can never disagree the way four Kotlin guesses did (#491).
//
// The level is the RMS of the ring's newest kWindowFrames mono samples (its raw mono tail, the same
// one projectM reads). A ring nobody has written for kStaleMs reads 0: a paused tap or a stopped
// capture leaves its last window in the ring, and a meter that kept showing it would freeze the
// glow wide open. Stale is judged on the poller's clock: the meter notes when the written count
// last moved between polls, so it needs no clock on the writer's side.
//
// Poll side only: the ring's writer never touches a Meter (geode_ingest owns one beside its Ring). The
// mutex is for two pollers of one ring (a GL thread and the main thread), never contended on a
// real-time thread.
namespace geode::audio::ingest::level {

// The newest frames a level reads: about 11 ms at 48 kHz, a VU meter's attack and short enough to
// move on the beat. The edge glow used 512 when it read the Kotlin ring.
inline constexpr int kWindowFrames = 512;

// No new samples for this long and the level is 0. A tap writes every few ms and a capture every
// ~20 ms, so 100 ms is well past a late buffer and well under what reads as a frozen glow.
inline constexpr int64_t kStaleMs = 100;

// RMS of n mono samples; 0 for none.
inline float rms(const float* mono, int n) {
    if (!mono || n <= 0) return 0.0f;
    double sum = 0.0;
    for (int i = 0; i < n; ++i) sum += static_cast<double>(mono[i]) * mono[i];
    return static_cast<float>(std::sqrt(sum / n));
}

class Meter {
public:
    // The level for this poll. written is the ring's frame count (geode_ingest_written); tail(out, n)
    // copies the ring's newest n mono samples into out and returns how many it copied (fewer early
    // on, before the ring holds n).
    template <class Tail>
    float poll(long long written, int64_t nowMs, Tail&& tail) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (seenWritten_ < 0) {
            // The first poll learns the count without calling it new: samples written before anyone
            // listened are old news, so a glow that comes into view on a paused ring starts dark.
            seenWritten_ = written;
        } else if (written != seenWritten_) {
            seenWritten_ = written;
            movedAtMs_ = nowMs;
        }
        if (written <= 0 || movedAtMs_ == kNever || nowMs - movedAtMs_ > kStaleMs) return 0.0f;
        const int n = tail(window_.data(), kWindowFrames);
        return rms(window_.data(), n < 0 ? 0 : (n > kWindowFrames ? kWindowFrames : n));
    }

private:
    static constexpr int64_t kNever = std::numeric_limits<int64_t>::min();

    std::mutex mutex_;
    long long seenWritten_ = -1;
    int64_t movedAtMs_ = kNever;
    std::array<float, kWindowFrames> window_{};
};

}  // namespace geode::audio::ingest::level

