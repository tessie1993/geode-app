#pragma once
#include <cmath>
#include <cstdint>

#include "viz/frame/LiveFps.hpp"

// How the stage paces its swaps, as pure rules, so the host can test them (core/tests/swap_pace_test.cpp). The live
// stage's cap is fixed at 30 FPS (viz/frame/LiveFps). The swap itself is Android Frame Pacing's (Swappy,
// core/stage/android/SwappyHost), as the docs recommend over Choreographer alone
// (developer.android.com/games/sdk/frame-pacing); where Swappy cannot pace a stage, plain eglSwapBuffers.
namespace geode::stage {

// The rate the window is asked for (ANativeWindow_setFrameRate): the cap. Live callers normalize their legacy stored
// value to 30 FPS before this rule; 0 states no preference. A rate that is not a number is none.
inline float frameRateHint(float capFps) {
    if (viz::frame::isLiveFpsFollow(capFps)) return 0.0f;
    return std::isfinite(capFps) && capFps > 0.0f ? capFps : 0.0f;
}

// The one fixed live swap interval, rounded down: 33,333,333 ns (30 FPS). Swappy sets this before every new window.
inline constexpr int64_t kLiveSwapIntervalNs = 1'000'000'000 / viz::frame::kLiveFpsDefault;

// The swap interval for a cap (SwappyGL_setSwapIntervalNS): one frame at the cap, rounded down. The generic rule
// stays pure for host tests; the live stage always hands it kLiveFpsDefault. 0 means no interval.
inline int64_t swapIntervalNs(float capFps) {
    const float rate = frameRateHint(capFps);
    return rate > 0.0f ? static_cast<int64_t>(1.0e9 / static_cast<double>(rate)) : 0;
}

// Within this, a swap interval just over a whole number of refresh periods is that number: Swappy's own rounding
// (SwappyCommon::calculateSwapInterval, REFRESH_RATE_MARGIN), so the stage and Swappy agree on the frame length.
inline constexpr int64_t kRefreshMarginNs = 500;

// The refresh periods one frame spans for a swap interval of `swapNs` on a display `periodNs` apart: 1 for no
// interval or no period (every vsync).
inline int64_t periodsPerFrame(int64_t swapNs, int64_t periodNs) {
    if (swapNs <= 0 || periodNs <= 0 || swapNs < periodNs) return 1;
    const int64_t whole = swapNs / periodNs;
    return whole + (swapNs % periodNs > kRefreshMarginNs ? 1 : 0);
}

// One frame's length: whole refresh periods for the swap interval; 0 while the period is not known.
inline int64_t frameLengthNs(int64_t swapNs, int64_t periodNs) {
    return periodNs > 0 ? periodsPerFrame(swapNs, periodNs) * periodNs : 0;
}

// One frame-rate setting on one display mode, derived once whenever either changes (StageCore) and handed on whole:
// Swappy takes swapNs, the window's frame-rate vote is voteFps, the work target is budgetNs, and the UI reads budgetNs from
// the mirror StageCore publishes. None of them works a number out again.
struct FrameBudget {
    // The refresh period of the display mode running, ns; 0 until the platform has said.
    int64_t periodNs = 0;
    // The swap interval for this budget (0 while Swappy is unavailable, so the fallback waits on Choreographer).
    int64_t swapNs = 0;
    // One frame's length: whole refresh periods of the swap interval; 0 while the period is not known.
    int64_t budgetNs = 0;
    // The seamless window vote: 30 FPS for live rendering, 0 for no vote. It never follows an automatic reduction.
    float voteFps = 0.0f;

    bool operator==(const FrameBudget&) const = default;
};

// `swapPaces`: Swappy holds each frame to its fixed 30 FPS slot. Without it the existing one-shot Choreographer path
// paces one frame per refresh under the window's seamless-only 30 FPS vote; no second sleep/timer authority is added.
inline FrameBudget frameBudget(float capFps, int64_t periodNs, bool swapPaces) {
    FrameBudget b;
    b.periodNs = periodNs > 0 ? periodNs : 0;
    b.swapNs = swapPaces ? swapIntervalNs(capFps) : 0;
    b.budgetNs = frameLengthNs(b.swapNs, b.periodNs);
    b.voteFps = frameRateHint(capFps);
    return b;
}

// The presentation time each frame is drawn for: the picture's clock (its step) and where the sound is read. The
// time comes from the pacer: Swappy's desired presentation time for the frame it starts (its startFrame tracer
// callback, fired at the end of the swap before: the time it stamps this frame's swap with), or, without Swappy, the
// vsync's expected presentation time. With no time from the pacer (or one already past) the frame is taken to show two
// frames from now, as a pipelined frame does (CPU in one interval, GPU in the next).
//
// The pacer's time jitters by a fraction of a millisecond (it is measured from a Choreographer tick), and the panel's real
// period is not exactly the nominal one. So the clock counts whole refresh periods from the last frame's (never fewer
// than one: two frames are never shown on one refresh) and keeps its phase locked to the pacer's: each frame takes an
// eighth of the gap between its counted time and the pacer's. A fixed offset of the nominal period from the real one then
// settles to a small constant lag instead of growing until the count skips a period, and the jitter is divided by eight.
class PresentClock {
public:
    // How many frames the phase lock averages over.
    static constexpr int64_t kLockFrames = 8;

    // `desiredNs`: the pacer's time for this frame, CLOCK_MONOTONIC (0: none); `nowNs`: when the frame starts;
    // `frameNs`: the frame's length (0: not known); `periodNs`: the display's refresh period (0: not known, no count).
    int64_t next(int64_t desiredNs, int64_t nowNs, int64_t frameNs, int64_t periodNs) {
        const int64_t pacer = desiredNs > nowNs ? desiredNs : nowNs + 2 * (frameNs > 0 ? frameNs : 0);
        int64_t t = pacer;
        if (last_ > 0 && periodNs > 0) {
            if (anchored_ && periodNs == period_) {
                int64_t steps = std::llround(static_cast<double>(pacer - last_) / static_cast<double>(periodNs));
                if (steps < 1) steps = 1;
                const int64_t counted = last_ + steps * periodNs;
                // The lock follows a phase error within half a period; a pacer further off than that (behind the last frame,
                // or a late frame) is not a phase to follow.
                const int64_t phase = pacer - counted;
                t = counted + (std::llabs(phase) <= periodNs / 2 ? phase / kLockFrames : 0);
            } else {
                // A new display mode, or the pacer changed: the count starts again from the pacer's own time, which already
                // spaces the frame by the new frame length, and never at or before the last frame's.
                t = pacer > last_ + periodNs / 2 ? pacer : last_ + periodNs;
            }
        }
        anchored_ = periodNs > 0;
        period_ = periodNs;
        last_ = t;
        return t;
    }
    // A start (the pacing starts again): the next frame is not counted from one shown before the gap.
    void restart() {
        last_ = 0;
        anchored_ = false;
    }
    // The pacer's time base changed (Swappy to the vsync or back): the next frame takes the pacer's time as it comes, still
    // after the last frame's, and the count goes on from there.
    void reanchor() { anchored_ = false; }

private:
    int64_t last_ = 0;
    int64_t period_ = 0;
    bool anchored_ = false;
};

}  // namespace geode::stage
