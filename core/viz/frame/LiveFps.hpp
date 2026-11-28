#pragma once
#include <algorithm>
#include <cmath>
#include <cstdlib>

// The live visualizer's frame rate is fixed at 30 FPS from creation. Display refresh rates are observations used
// only for whole-refresh-period pacing; they never select another render rate or a preferred display mode. Pure,
// with no GL and no Android, so the host can test it (core/tests/live_fps_test.cpp).
//
// Live only: the in-app stage (and its PiP window and second screen). An export paces to its own fixed step,
// Cast to its fixed clock. The choice/cap/refresh functions keep their old signatures for the Kotlin/JNI ABI, but
// every legacy stored value (including Follow device) normalizes to 30 and the manual refresh preference is retired.
namespace geode::viz::frame {

inline constexpr int kLiveFpsMin = 30;
inline constexpr int kLiveFpsMax = 30;
inline constexpr int kLiveFpsDefault = 30;
inline constexpr int kLiveFpsUnknown[] = {kLiveFpsDefault};
static_assert(kLiveFpsMin == kLiveFpsDefault && kLiveFpsMax == kLiveFpsDefault);
// Retired stored value. Kept so old preferences and the ABI still deserialize; it is never offered or returned.
inline constexpr int kLiveFpsFollow = -1;
inline constexpr int kLiveFpsMaxRates = 1;
// The most vsyncs one frame spans.
inline constexpr int kMaxVsyncsPerFrame = 8;
// How far a refresh rate may sit from a whole multiple of a frame rate and still show it (59.94, 119.88 Hz modes).
inline constexpr float kRefreshToleranceHz = 1.0f;

inline bool isLiveFpsFollow(float capFps) { return capFps == static_cast<float>(kLiveFpsFollow); }

// Pure timing math: how many vsyncs of a display at `refreshHz` one frame at `fps` spans, when that is a whole
// number up to kMaxVsyncsPerFrame; 0 when the ratio is uneven. This never creates a live FPS option.
inline int vsyncSpan(float refreshHz, float fps) {
    if (!std::isfinite(refreshHz) || refreshHz <= 0.0f || !std::isfinite(fps) || fps <= 0.0f) return 0;
    const float n = std::round(refreshHz / fps);
    if (n < 1.0f || n > static_cast<float>(kMaxVsyncsPerFrame)) return 0;
    return std::fabs(refreshHz - n * fps) <= kRefreshToleranceHz ? static_cast<int>(n) : 0;
}

// Compatibility ABI: the only live rate offered is 30 FPS, regardless of the display's refresh-rate list.
// Writes up to capacity and returns the whole count (one), so counting needs no output array.
inline int liveFpsChoices(const float* rates, int count, int* out, int capacity) {
    static_cast<void>(rates);
    static_cast<void>(count);
    if (out && capacity > 0) out[0] = kLiveFpsDefault;
    return 1;
}

inline bool liveFpsOffered(int fps, const float* rates, int count) {
    static_cast<void>(rates);
    static_cast<void>(count);
    return fps == kLiveFpsDefault;
}

// Compatibility ABI: a legacy stored cap always becomes the fixed live rate. No nearest-rate snapping and no
// Follow device mode remain.
inline int liveFpsCap(int stored, const float* rates, int count) {
    static_cast<void>(stored);
    static_cast<void>(rates);
    static_cast<void>(count);
    return kLiveFpsDefault;
}

// Retired compatibility ABI: manual display-mode selection is no longer performed. Always return 0 (no
// preference). The standing window vote is made by StageThread at the fixed 30 FPS rate; these legacy arguments
// are retained only so older Kotlin/JNI/native callers keep their ABI.
inline float refreshRateFor(int fps, const float* rates, int count) {
    static_cast<void>(fps);
    static_cast<void>(rates);
    static_cast<void>(count);
    return 0.0f;
}

}  // namespace geode::viz::frame
