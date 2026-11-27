#pragma once
#include <cmath>
#include <cstdint>

#include "viz/frame/LiveFps.hpp"

// The stage's whole vsync rule (docs/agents/native-stage.md): draw one frame on every vsync the system delivers, and
// skip a vsync only when the last frame is still in flight. Pure, with no EGL and no Android, so the host can test it
// (core/tests/vsync_rule_test.cpp).
//
// Which vsyncs a frame is drawn on is the frame clock's (stage/FramePacer); this is what holds whatever the clock says.
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

}  // namespace geode::stage
