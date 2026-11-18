#pragma once
#include <cstdint>
#include <vector>

#include "api/geode_api.h"

// Defined in core/api/geode_viz_api.cpp, called from core/api/geode_stage_api.cpp; not part of the C ABI. Both files
// include this header, so a signature that drifts on one side is a compile error, not a link failure.
// geode_viz_frame at a vsync's frame time (0: none), and the still geode_viz_still_request asked for, its buffer
// handed over instead of copied (false: none ready).
namespace geode::api {
int vizFrameAt(geode_viz* v, const geode_pcm_ring* ring, float pacedFps, float capFps, int64_t vsyncNs);
bool takeVizStill(geode_viz* v, int& width, int& height, std::vector<uint8_t>& rgba);
// The cap "Adapt to frame time" (viz/frame/FrameGovernor) has the stage pace to instead of the user's; 0 for none.
float vizGovernedCapFps(const geode_viz* v);
}  // namespace geode::api
