#pragma once
#include <algorithm>

// What Things (viz/things/ThingPool.hpp) and Fields (viz/fields/FieldSet.hpp) share: the fade a short-lived effect
// has over its life, and how close a pull may come to its centre before it stops growing. Pure and header-only.
namespace geode::viz::fx {

inline constexpr float kSoftening = 0.05f;  // frame heights; keeps a pull finite up close

// How much of its strength an effect has at `age`, fading in over `attack` and out over `release` before `end`.
inline float envelope(double age, double end, float attack, float release) {
    if (!(age >= 0.0) || age >= end) return 0.0f;
    float in = 1.0f;
    float out = 1.0f;
    if (attack > 0.0f) in = static_cast<float>(std::min(1.0, age / static_cast<double>(attack)));
    if (release > 0.0f) out = static_cast<float>(std::min(1.0, (end - age) / static_cast<double>(release)));
    const float k = std::min(in, out);
    return k * k * (3.0f - 2.0f * k);
}

}  // namespace geode::viz::fx
