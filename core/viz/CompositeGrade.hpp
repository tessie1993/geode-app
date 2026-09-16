#pragma once
#include <algorithm>
#include <array>

#include "viz/Scene.hpp"

namespace geode::viz::grade {

// The 4th (w) component is wire-compatible padding only: composite_frag.glsl
// dropped its last reader (the beat-pulse gate) in wave three, but the
// uniform stays a vec4 (see uGateA/uGateB), so toVec4() still emits four
// floats.
struct Gate {
    bool geo;
    bool mirrorInvert;
    bool grade;
    std::array<float, 4> toVec4() const { return {geo ? 1.0f : 0.0f, mirrorInvert ? 1.0f : 0.0f, grade ? 1.0f : 0.0f, 0.0f}; }
};

constexpr float kMinZoom = 0.05f;
constexpr float kMinGamma = 0.05f;
constexpr float kTau = 6.2831855f;

inline Gate gateFor(SceneFamily family) {
    return Gate{family != SceneFamily::Shader, family == SceneFamily::Fluid, family == SceneFamily::Fluid};
}

float integrateRotation(float angle, float rotation, float dt);
float integrateCyclePhase(float phase, float cycleSpeed, float dt, bool enabled);
inline float brightness(float brightness, float intensity) { return brightness * intensity; }
inline float paletteSpan(float hueRange, float paletteRange) { return std::max(hueRange, 0.0f) * std::clamp(paletteRange, 0.0f, 1.0f); }
inline float paletteTintAmount(float tint) { return std::clamp(tint, 0.0f, 1.0f); }

}  // namespace geode::viz::grade
