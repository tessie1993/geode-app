#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// The times a frame is read at. Pure, with no GL and no Android, so the host can test it
// (core/tests/present_time_test.cpp). When a frame is shown, and so the time it is drawn for, is the stage's pacer's
// (core/stage/SwapPace).
namespace geode::viz::frame {

// The rate a surface runs at: the lower of two caps (the rate the display presents at and the surface's own). Either
// <= 0, or not a number, means none.
inline float effectiveCapFps(float aFps, float bFps) {
    const bool a = std::isfinite(aFps) && aFps > 0.0f;
    const bool b = std::isfinite(bFps) && bFps > 0.0f;
    if (a && b) return std::min(aFps, bFps);
    if (a) return aFps;
    return b ? bFps : 0.0f;
}

// The time the sound is read at for a frame drawn at `nowNs` that the display shows at `presentNs` (both
// CLOCK_MONOTONIC ns): the speaker plays what the viewer sees, so a frame that waits in the swap queue hears that
// much later. 0, or a time that has passed, reads at now.
inline int64_t listenAtNs(int64_t nowNs, int64_t presentNs) { return presentNs > nowNs ? presentNs : nowNs; }

}  // namespace geode::viz::frame
