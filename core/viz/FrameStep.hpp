#pragma once
#include "viz/frame/Pacer.hpp"

namespace geode::viz {

// The step the renderer advances every dt-integrated quantity by (rotation, the dive, the camera orbit,
// LFO phases, the sound engine's smoothing) for one frame.
//
// A live surface measures it: the time since the last frame, clamped to 1 ms..100 ms, with 1/60 s for the
// first frame. An export (geode_viz_set_frame_rate) steps by exactly 1/fps on every frame, the first one
// included. Before #377 an export also measured, from times truncated to whole milliseconds, so a 60 fps
// render stepped 16, 17, 17 ms and its first frame always 1/60 s.
inline float frameStepSeconds(int fixedFps, double lastTimeS, double timeS) {
    if (fixedFps > 0) return static_cast<float>(1.0 / fixedFps);
    // The one dt rule every clock shares (core/viz/frame/Pacer, #436).
    const double elapsed = lastTimeS < 0.0 ? frame::kFirstDtSeconds : timeS - lastTimeS;
    return static_cast<float>(frame::clampDt(elapsed));
}

}  // namespace geode::viz
