#pragma once
#include <cmath>
#include <cstdint>

#include "viz/frame/LiveFps.hpp"

// How the stage paces its swaps, as pure rules, so the host can test them (core/tests/swap_pace_test.cpp). The swap
// itself is Android Frame Pacing's (Swappy, core/stage/android/SwappyHost), as the docs recommend over Choreographer
// alone (developer.android.com/games/sdk/frame-pacing); where Swappy cannot pace a stage, plain eglSwapBuffers.
namespace geode::stage {

// The rate the window is asked for (ANativeWindow_setFrameRate): the cap; 0 (no preference) for Follow device or no
// cap. Follow device states no rate: the system keeps choosing the panel's mode. A rate that is not a number is none.
inline float frameRateHint(float capFps) {
    if (viz::frame::isLiveFpsFollow(capFps)) return 0.0f;
    return std::isfinite(capFps) && capFps > 0.0f ? capFps : 0.0f;
}

// The swap interval Swappy is given for a cap (SwappyGL_setSwapIntervalNS): one frame at the cap, rounded down so
// a cap the display shows evenly is a whole number of its refresh periods. 0 for no cap (Follow device): pacing off,
// every vsync of whatever rate the system runs.
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
    // The swap interval Swappy is given (0: its pacing is off, Follow device or a stage Swappy does not pace).
    int64_t swapNs = 0;
    // One frame's length: whole refresh periods of the swap interval; 0 while the period is not known.
    int64_t budgetNs = 0;
    // The rate the window is voted: the setting's own, 0 for Follow device. It never follows what the platform does.
    float voteFps = 0.0f;

    bool operator==(const FrameBudget&) const = default;
};

// `swapPaces`: Swappy holds each frame to its slot. Without it the vsync paces one frame per refresh, so no swap interval is
// handed on and the budget is one period.
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
// callback, the time it stamps the swap with). Swappy measures it from its own wake-up, which jitters by a fraction of
// a millisecond; snapped to whole refresh periods after the last frame's, the step is exactly the presentations'
// spacing. With no time from the pacer (the fallback, a time already past) the frame is taken to show two frames from
// now, as a pipelined frame does (CPU in one interval, GPU in the next).
class PresentClock {
public:
    // `desiredNs`: the pacer's time for this frame, CLOCK_MONOTONIC (0: none); `nowNs`: when the frame starts;
    // `frameNs`: the frame's length (0: not known); `periodNs`: the display's refresh period (0: not known, no snap).
    int64_t next(int64_t desiredNs, int64_t nowNs, int64_t frameNs, int64_t periodNs) {
        int64_t t = desiredNs > nowNs ? desiredNs : nowNs + 2 * (frameNs > 0 ? frameNs : 0);
        if (last_ > 0 && periodNs > 0) {
            int64_t steps = std::llround(static_cast<double>(t - last_) / static_cast<double>(periodNs));
            // Two frames are never shown on one refresh: a time at or before the last frame's is the next refresh.
            if (steps < 1) steps = 1;
            t = last_ + steps * periodNs;
        }
        last_ = t;
        return t;
    }
    // A start (the pacing starts again): the next frame is not snapped to one shown before the gap.
    void restart() { last_ = 0; }

private:
    int64_t last_ = 0;
};

}  // namespace geode::stage
