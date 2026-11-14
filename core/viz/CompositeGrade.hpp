#pragma once
#include <algorithm>
#include <array>

#include "viz/Scene.hpp"

namespace geode::viz::grade {

struct Gate {
    bool geo;
    bool mirrorInvert;
    bool grade;
    std::array<float, 4> toVec4() const { return {geo ? 1.0f : 0.0f, mirrorInvert ? 1.0f : 0.0f, grade ? 1.0f : 0.0f, 0.0f}; }
};

constexpr float kMinZoom = 0.05f;
constexpr float kMinGamma = 0.05f;
constexpr float kTau = 6.2831855f;

// What the composite applies to a scene's finished picture. A shader style applies all of it itself, and so
// does any scene that draws the camera rig itself (Scene::drawsCamera): a simulation presented in 3D
// (core/viz/present/FieldPresent) bends its own shape by the Kaleidoscope, Twist, Tile, Warp and Ripple and
// grades its own colour through the shared block, so the composite must not do either a second time.
inline Gate gateFor(SceneFamily family, bool drawsCamera = false) {
    if (drawsCamera) return Gate{false, false, false};
    return Gate{family != SceneFamily::Shader, family == SceneFamily::Fluid, family == SceneFamily::Fluid};
}

float integrateRotation(float angle, float rotation, float dt);
inline float brightness(float brightness, float intensity) { return brightness * intensity; }
inline float paletteSpan(float hueRange, float paletteRange) { return std::max(hueRange, 0.0f) * std::clamp(paletteRange, 0.0f, 1.0f); }
inline float paletteTintAmount(float tint) { return std::clamp(tint, 0.0f, 1.0f); }

}  // namespace geode::viz::grade
