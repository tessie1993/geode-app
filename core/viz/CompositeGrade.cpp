#include "viz/CompositeGrade.hpp"

#include <algorithm>
#include <cmath>

namespace geode::viz::grade {

float integrateRotation(float angle, float rotation, float dt) { return std::fmod(angle + rotation * dt, kTau); }

float integrateCyclePhase(float phase, float cycleSpeed, float dt, bool enabled) {
    return enabled ? std::fmod(phase + cycleSpeed * dt, 1.0f) : phase;
}

float integrateBeatPulse(float envelope, float impulse, float dt) {
    return std::max(std::max(impulse, envelope - dt * kBeatDecay), 0.0f);
}

float pulseAmount(float pulse, float envelope) {
    const float e = std::clamp(envelope, 0.0f, 1.0f);
    return std::clamp(pulse, 0.0f, 1.0f) * e * e;
}

}  // namespace geode::viz::grade
