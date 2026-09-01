#pragma once
#include <array>

#include "viz/Scene.hpp"

namespace geode::viz::grade {

struct Gate {
    bool geo;
    bool mirrorInvert;
    bool grade;
    bool pulse;
    std::array<float, 4> toVec4() const { return {geo ? 1.0f : 0.0f, mirrorInvert ? 1.0f : 0.0f, grade ? 1.0f : 0.0f, pulse ? 1.0f : 0.0f}; }
};

constexpr float kMinZoom = 0.05f;
constexpr float kMinGamma = 0.05f;
constexpr float kBeatDecay = 3.0f;
constexpr float kPulseGain = 0.22f;
constexpr float kTau = 6.2831855f;

inline Gate gateFor(SceneFamily family) {
    return Gate{family != SceneFamily::Shader, family == SceneFamily::Fluid, family == SceneFamily::Fluid,
                family == SceneFamily::Milkdrop || family == SceneFamily::Fluid};
}

float integrateRotation(float angle, float rotation, float dt);
float integrateCyclePhase(float phase, float cycleSpeed, float dt, bool enabled);
float integrateBeatPulse(float envelope, float impulse, float dt);
float pulseAmount(float pulse, float envelope);
inline float pulseScale(float amount) { return 1.0f + (amount > 0.0f ? amount : 0.0f) * kPulseGain; }
inline float brightness(float brightness, float intensity) { return brightness * intensity; }

}  // namespace geode::viz::grade
