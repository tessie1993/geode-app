#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// The times a frame is read and shown at. Pure, with no GL and no Android, so the host can test it
// (core/tests/present_time_test.cpp). Which vsyncs a surface draws on is not decided here: every one the system delivers
// (core/stage/VsyncRule).
namespace geode::viz::frame {

// The rate a surface runs at: the lower of two caps (the pacer's paced rate and the surface's own). Either
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
// much later. 0, or a time that has passed, reads at now. The presentation time is the platform's own.
inline int64_t listenAtNs(int64_t nowNs, int64_t presentNs) { return presentNs > nowNs ? presentNs : nowNs; }

// The stamp (EGL_ANDROID_presentation_time) of a frame meant for the presentation at `presentNs`, where presentations
// are `intervalNs` apart: the midpoint between it and the one before. The extension leaves the use of the time to
// the window system; Android's compositor (read in its source for Android 13 to 16) holds a buffer until a refresh
// it expects to present later than the stamp, so a stamp on `presentNs` itself can fall either side. No interval:
// `presentNs` itself. No presentation time: 0, no stamp.
inline int64_t latchStampNs(int64_t presentNs, int64_t intervalNs) {
    if (presentNs <= 0) return 0;
    return intervalNs > 0 ? presentNs - intervalNs / 2 : presentNs;
}

}  // namespace geode::viz::frame
