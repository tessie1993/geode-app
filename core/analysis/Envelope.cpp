#include "analysis/Envelope.hpp"

#include <algorithm>
#include <cmath>

namespace geode::analysis {

float Envelope::step(float target, float dtSeconds) {
    if (dtSeconds <= 0.0f) return value_;
    const float tau = target > value_ ? attackSeconds_ : releaseSeconds_;
    if (tau <= 0.0f) {
        value_ = target;
    } else {
        const float k = std::clamp(1.0f - std::exp(-dtSeconds / tau), 0.0f, 1.0f);
        value_ = value_ + (target - value_) * k;
    }
    return value_;
}

}  // namespace geode::analysis
