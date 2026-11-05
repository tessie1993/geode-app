#pragma once
#include "signal/timeline/TakeTimeline.hpp"

// The opaque geode_take_timeline of geode_api.h, shared by the two API files that read it: geode_timeline_api.cpp
// (the handle's own calls) and geode_viz_api.cpp (geode_viz_set_take, which copies it into an export's renderer).
struct geode_take_timeline {
    geode::signal::TakeTimeline timeline;
};
