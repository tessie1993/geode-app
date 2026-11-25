#pragma once
#include <algorithm>

namespace geode::viz {

// The step the renderer advances every dt-integrated quantity by (rotation, the dive, the camera orbit,
// LFO phases, the sound engine's smoothing) for one frame.
//
// A live surface steps by the real time between two frames' clocks (the display's expected presentation times), not
// clamped: a long frame is a long step. The first frame of a run has no interval and steps by 0 (Renderer::restartClock
// says when a run starts again). An export (geode_viz_set_frame_rate) steps by exactly 1/fps on every frame, the first
// one included.
inline float frameStepSeconds(int fixedFps, double lastTimeS, double timeS) {
    if (fixedFps > 0) return static_cast<float>(1.0 / fixedFps);
    if (lastTimeS < 0.0) return 0.0f;
    return static_cast<float>(std::max(timeS - lastTimeS, 0.0));
}

}  // namespace geode::viz
