#pragma once
#include <memory>

#include "output/Recorder.hpp"

// The recorder behind its C handle. Shared, because two owners outlive each other in either order: the
// shell's handle (the encoder thread) and the stage renderer that tees into it (geode_viz_record_start).
struct geode_recorder {
    std::shared_ptr<geode::output::record::Recorder> recorder;
};
