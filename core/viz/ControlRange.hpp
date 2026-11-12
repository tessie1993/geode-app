#pragma once
#include <algorithm>
#include <cstddef>
#include <string_view>

#include "viz/Params.hpp"
#include "viz/VisualSafety.hpp"

namespace geode::viz::control {

// The range Customize's slider (and a Studio keyframe track) offers for a scene param (was the shell's
// model/ControlRange.kt): the schema's range (ParamTable.inc), cut short only where a control stops early
// on purpose. The LFO and the reaction routes still reach the schema's range; only the hand stops short.
struct Range {
    float min;
    float max;
};

// The range of the param in wire slot `slot` (0..kFieldCount-1).
inline Range of(int slot) {
    const ParamSpec& spec = SceneParams::schema()[static_cast<size_t>(slot)];
    Range r{spec.min, spec.max};
    switch (static_cast<ParamSlot>(slot)) {
        // The flash clamp takes back anything brighter than 1 + its depth, so the slider stops there.
        case ParamSlot::brightness:
        case ParamSlot::intensity:
            r.max = 1.0f + safety::kMaxFlashDepth;
            break;
        // More bloom than the flash clamp's depth is clamped away before it reaches the screen.
        case ParamSlot::bloom:
            r.max = safety::kMaxFlashDepth;
            break;
        // Fly runs both ways on the slider; 2..2.5 each way is headroom for the LFO and the routes.
        case ParamSlot::camFly:
            r.min = -2.0f;
            r.max = 2.0f;
            break;
        default:
            break;
    }
    return {std::max(r.min, spec.min), std::min(r.max, spec.max)};
}

// The same by the param's name (the schema name); false, out untouched, for a name the schema lacks.
inline bool of(std::string_view name, Range& out) {
    const int slot = SceneParams::slotOf(name);
    if (slot < 0) return false;
    out = of(slot);
    return true;
}

}  // namespace geode::viz::control
