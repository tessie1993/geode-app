#pragma once
#include <algorithm>
#include <cmath>
#include <cstdlib>

// The live frame rate, taken from the display. A panel shows a rate evenly only when the rate is one of its refresh
// rates divided by a whole number (90 Hz: 90, 45, 30; 120 Hz: 120, 60, 40, 30), so those are the rates Settings ›
// Performance offers. The one the user picks is the whole app's frame rate: it picks the display mode the window asks
// for (refreshRateFor) and how many vsyncs a stage frame spans (stage/FramePacer), so every thread works to one frame
// length. Pure, with no GL and no Android, so the host can test it (core/tests/live_fps_test.cpp).
//
// Live only: the in-app stage (and its PiP window and second screen). The wallpaper and the dream pace to their own
// choice (viz/SurfaceRun), an export to its own fixed step, Cast to its fixed clock.
//
// `rates` are the refresh rates of the stage's display at its current resolution (Display.getSupportedModes): the same
// set from every mode, so a mode switch never changes what is offered. NULL or none is a display that has not said.
namespace geode::viz::frame {

inline constexpr int kLiveFpsMin = 30;
inline constexpr int kLiveFpsMax = 120;
inline constexpr int kLiveFpsDefault = 60;
// Offered on a display that says nothing (or nothing usable): Saver's 30 and the default 60, the quality tiers' rates.
// A display that does say offers only what it shows evenly: 60 on a 90 Hz-only panel would draw 45.
inline constexpr int kLiveFpsUnknown[] = {30, 60};
// "Follow device": the stage draws every vsync of whatever rate the system runs and states no rate of its own.
// Negative so no rate means it, -1 so an older build reading it draws every vsync.
inline constexpr int kLiveFpsFollow = -1;
// The most rates the tab offers, before Follow device.
inline constexpr int kLiveFpsMaxRates = 11;
// The most vsyncs one frame spans.
inline constexpr int kMaxVsyncsPerFrame = 8;
// How far a refresh rate may sit from a whole multiple of a frame rate and still show it (59.94, 119.88 Hz modes).
inline constexpr float kRefreshToleranceHz = 1.0f;

inline bool isLiveFpsFollow(float capFps) { return capFps == static_cast<float>(kLiveFpsFollow); }

// How many vsyncs of a display at `refreshHz` one frame at `fps` spans, when that is a whole number up to
// kMaxVsyncsPerFrame; 0 when the display cannot show `fps` evenly.
inline int vsyncSpan(float refreshHz, float fps) {
    if (!std::isfinite(refreshHz) || refreshHz <= 0.0f || !std::isfinite(fps) || fps <= 0.0f) return 0;
    const float n = std::round(refreshHz / fps);
    if (n < 1.0f || n > static_cast<float>(kMaxVsyncsPerFrame)) return 0;
    return std::fabs(refreshHz - n * fps) <= kRefreshToleranceHz ? static_cast<int>(n) : 0;
}

// The rates offered on a display with these refresh rates, ascending, into out (up to capacity), then Follow device;
// returns how many there are.
inline int liveFpsChoices(const float* rates, int count, int* out, int capacity) {
    int found[kLiveFpsMaxRates];
    int n = 0;
    const auto add = [&](int fps) {
        if (fps < kLiveFpsMin || fps > kLiveFpsMax || n == kLiveFpsMaxRates) return;
        if (std::find(found, found + n, fps) == found + n) found[n++] = fps;
    };
    for (int i = 0; rates && i < count; ++i) {
        const float r = rates[i];
        if (!std::isfinite(r) || r <= 0.0f) continue;
        for (int span = 1; span <= kMaxVsyncsPerFrame; ++span) {
            const int fps = static_cast<int>(std::lround(r / static_cast<float>(span)));
            if (vsyncSpan(r, static_cast<float>(fps)) == span) add(fps);
        }
    }
    if (n == 0) {
        for (const int fps : kLiveFpsUnknown) add(fps);
    }
    std::sort(found, found + n);
    for (int i = 0; out && i < n && i < capacity; ++i) out[i] = found[i];
    if (out && n < capacity) out[n] = kLiveFpsFollow;
    return n + 1;
}

inline bool liveFpsOffered(int fps, const float* rates, int count) {
    int choices[kLiveFpsMaxRates + 1];
    const int n = liveFpsChoices(rates, count, choices, kLiveFpsMaxRates + 1);
    return fps > 0 && std::find(choices, choices + n, fps) != choices + n;
}

// The stored rate as this display runs it: an offered rate as it is; anything else (a 120 picked on a foldable's
// inner screen, now on its 60 Hz outer one) the nearest offered, the lower on a tie; Follow device as it is; any other
// value <= 0 the default.
inline int liveFpsCap(int stored, const float* rates, int count) {
    if (stored == kLiveFpsFollow) return kLiveFpsFollow;
    if (stored <= 0) return kLiveFpsDefault;
    int choices[kLiveFpsMaxRates + 1];
    const int n = liveFpsChoices(rates, count, choices, kLiveFpsMaxRates + 1);
    int best = 0;
    for (int i = 0; i < n; ++i) {
        const int fps = choices[i];
        if (fps == kLiveFpsFollow) continue;
        if (best == 0 || std::abs(fps - stored) < std::abs(best - stored)) best = fps;
    }
    return best > 0 ? best : kLiveFpsDefault;
}

// The refresh rate the window asks the display for while the live rate is `fps`: the lowest of `rates` that shows it
// evenly, so 60 on a 60/90 Hz panel runs the 60 Hz mode and 45 the 90 Hz one. 0 (no preference) for Follow device,
// no rate, or a rate no mode shows evenly.
inline float refreshRateFor(int fps, const float* rates, int count) {
    if (fps <= 0) return 0.0f;
    float best = 0.0f;
    for (int i = 0; rates && i < count; ++i) {
        if (vsyncSpan(rates[i], static_cast<float>(fps)) == 0) continue;
        if (best == 0.0f || rates[i] < best) best = rates[i];
    }
    return best;
}

}  // namespace geode::viz::frame
