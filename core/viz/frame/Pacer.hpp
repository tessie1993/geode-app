#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// The times a frame is read and shown at. Pure, with no GL and no Android, so the host can test it
// (core/tests/pacer_test.cpp). Which vsyncs a surface draws on is not decided here: every one the system delivers
// (core/stage/VsyncRule).
namespace geode::viz::frame {

// One dt rule for every clock (#436). Before, Kotlin clamped to 0.001..1/15 s and the renderer to
// 0.001..0.1 s, so the same stall stepped the pacer and the engine by different amounts. 0.1 s keeps
// the sound engine and the musical layer honest down to 10 fps. The field sims still clamp their own
// integrator step to 1/15 s, where the integrators were tuned.
inline constexpr double kMinDtSeconds = 0.001;
inline constexpr double kMaxDtSeconds = 0.1;
// The first frame of a run has no interval to measure. A nominal step beats zero, which freezes every
// envelope, and beats the clamp, which jumps the scene on resume.
inline constexpr double kFirstDtSeconds = 1.0 / 60.0;

inline double clampDt(double elapsedSeconds) { return std::clamp(elapsedSeconds, kMinDtSeconds, kMaxDtSeconds); }

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
// much later. 0, or a time that has passed, reads at now. The wait is held to the dt clamp, past which the
// estimate is no better than now.
inline int64_t listenAtNs(int64_t nowNs, int64_t presentNs) {
    if (presentNs <= nowNs) return nowNs;
    const int64_t maxAhead = static_cast<int64_t>(kMaxDtSeconds * 1e9);
    return nowNs + std::min(presentNs - nowNs, maxAhead);
}

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
