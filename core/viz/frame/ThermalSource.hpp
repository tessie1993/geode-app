#pragma once

// The platform's thermal readings, read through the NDK (android/thermal.h) and handed raw to the process's one
// thermal state (viz/frame/Thermal.hpp), which decides everything: the cadence, the NaN rule, the dwells and
// the ladder. Android only, never compiled on the host; nothing here decides anything.
namespace geode::viz::thermal_source {

// Once per process, from any thread (every live surface's surface_created asks; the first does it): the status
// now, then on every change from the platform's listener, each pushed with no headroom.
void attach();

// The platform's headroom forecast, pushed with no status: asked only by the frame that won the process's slot
// on a second a headroom is due (ThermalState::headroomDue), every ten seconds. NaN when the platform has none.
void sampleHeadroom(double nowSeconds);

}  // namespace geode::viz::thermal_source
