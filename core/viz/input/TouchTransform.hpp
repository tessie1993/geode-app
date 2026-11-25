#pragma once
#include <algorithm>
#include <cmath>

#include "viz/Catalogue.hpp"
#include "viz/Params.hpp"

namespace geode::viz::touch {

// The two-finger transform on the visual (#654, was the shell's TouchTransform.kt): a pinch scales Zoom and a
// twist turns Rotation, each clamped to its field's range in the params schema (ParamTable.inc), so a gesture
// can never push a value the panel's slider could not. The shell hands over the raw gesture (the pinch's scale
// factor, the twist in degrees) through geode_touch_zoom / geode_touch_rotation and stores what comes back.

// Zoom times the pinch's scale factor; a factor that is not finite or not positive leaves it alone.
inline float zoom(float current, float scaleFactor) {
    if (!std::isfinite(scaleFactor) || scaleFactor <= 0.0f) return current;
    const ParamSpec& spec = SceneParams::spec(ParamSlot::zoom);
    return std::clamp(current * scaleFactor, spec.min, spec.max);
}

// Rotation plus the twist at catalogue::kTouchRotationPerDegree; a twist that is not finite leaves it alone.
inline float rotation(float current, float degrees) {
    if (!std::isfinite(degrees)) return current;
    const ParamSpec& spec = SceneParams::spec(ParamSlot::rotation);
    return std::clamp(current + degrees * catalogue::kTouchRotationPerDegree, spec.min, spec.max);
}

}  // namespace geode::viz::touch
