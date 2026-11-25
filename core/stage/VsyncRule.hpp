#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "viz/frame/LiveFps.hpp"

// The stage's whole vsync rule (docs/agents/native-stage.md): draw one frame on every vsync the system delivers, and
// skip a vsync only when the last frame is still in flight. Pure, with no EGL and no Android, so the host can test it
// (core/tests/vsync_rule_test.cpp).
//
// There is no pacing here. The user's frame rate is a request to the platform (ANativeWindow_setFrameRate), never a
// divisor of the vsyncs we draw on: the display decides how often it calls, the stage answers every call it can.
namespace geode::stage {

// True when this vsync draws nothing. A frame in flight is the one fixed back-pressure rule: stageBusy, the stage
// thread has not finished the frame it was asked for (CPU side, known to the vsync thread); gpuBusy, the last swapped
// frame's fence has not signalled (GPU side, known to the stage thread, which inserted it). A skipped vsync is not
// queued: the next one asks again.
inline bool skipVsync(bool stageBusy, bool gpuBusy) { return stageBusy || gpuBusy; }

// The rate the window is asked for: the cap, 0 (no preference) for Follow device or no cap. Follow device states no
// rate: the system keeps choosing the panel's mode. A rate that is not a number is no rate.
inline float frameRateHint(float capFps) {
    if (viz::frame::isLiveFpsFollow(capFps)) return 0.0f;
    return std::isfinite(capFps) && capFps > 0.0f ? capFps : 0.0f;
}

// The rate the display is presenting at, from the ns between two presentations; 0 when not known.
inline float presentRateFps(int64_t intervalNs) {
    return intervalNs > 0 ? static_cast<float>(1.0e9 / static_cast<double>(intervalNs)) : 0.0f;
}

// The user's own frame-rate cap, kept when the platform does not honour the hint (ANativeWindow_setFrameRate may be
// ignored where the switch is not seamless): the display then keeps presenting at its own rate, over what the user
// asked for. This is the user's setting enforced, not an adaptive part: it reads only the presentation rate and the cap,
// never frame time or load, and it lowers nothing else. While the rate stays over the cap by kOverFactor for
// kSustainFrames frames, it draws every whole-number divide of the vsyncs that lands at or under the cap
// (round(rate / cap), at least 2); the moment the platform follows the hint, or a frame's rate or the cap is not known,
// it draws every vsync again. Follow device (a negative cap), no cap and a cap the display is not over are never touched.
class CapGuard {
public:
    static constexpr float kOverFactor = 1.5f;
    static constexpr int kSustainFrames = 30;

    // One frame a vsync asked for, after the in-flight rule let it through: true to draw it.
    bool draws(float presentRateFps, float capFps) {
        const bool known = std::isfinite(capFps) && capFps > 0.0f && std::isfinite(presentRateFps) && presentRateFps > 0.0f;
        if (!known || presentRateFps <= kOverFactor * capFps) {
            over_ = 0;
            sinceDrawn_ = 0;
            return true;
        }
        if (over_ < kSustainFrames) {
            ++over_;
            if (over_ < kSustainFrames) return true;
        }
        const int divide = std::max(2, static_cast<int>(std::lround(presentRateFps / capFps)));
        if (++sinceDrawn_ < divide) return false;
        sinceDrawn_ = 0;
        return true;
    }

private:
    int over_ = 0;
    int sinceDrawn_ = 0;
};

}  // namespace geode::stage
