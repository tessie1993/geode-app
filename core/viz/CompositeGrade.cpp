#include "viz/CompositeGrade.hpp"

#include <cmath>

namespace geode::viz::grade {

float integrateRotation(float angle, float rotation, float dt) { return std::fmod(angle + rotation * dt, kTau); }

float integrateCyclePhase(float phase, float cycleSpeed, float dt, bool enabled) {
    return enabled ? std::fmod(phase + cycleSpeed * dt, 1.0f) : phase;
}

}  // namespace geode::viz::grade
