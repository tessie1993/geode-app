#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "viz/params/Params.hpp"

namespace geode::viz::shader {

// Frame-invariant half of lib_scene_uniforms.glsl::view(). The shader keeps the
// pixel-dependent folds, polar remap, noise fields, twist and ripple; these
// terms are resolved once per ShaderScene frame and uploaded unchanged.
struct SceneViewTerms {
    // aspect, pixelate cells per unit, kaleidoscope segment, morph fade
    std::array<float, 4> view{1.0f, 0.0f, 0.0f, 0.0f};
    // Column-major mat2 constructor payload: cos, -sin, sin, cos.
    std::array<float, 4> rotation{1.0f, 0.0f, 0.0f, 1.0f};
    // Bounded drift x/y, twist distance exponent, domain-warp amplitude.
    std::array<float, 4> motion{0.0f, 0.0f, 1.0f, 0.0f};

    bool operator==(const SceneViewTerms&) const = default;
};

// Frame-invariant half of lib_scene_grade.glsl::grade().
struct SceneGradeTerms {
    // Posterize levels, inverse gamma, temperature shift, brightness * intensity.
    std::array<float, 4> shade{24.0f, 1.0f, 0.0f, 1.0f};

    bool operator==(const SceneGradeTerms&) const = default;
};

// GLSL ES mix(): x (1 - a) + y a, in the same operation order as the shader.
inline float mix(float x, float y, float a) { return x * (1.0f - a) + y * a; }

inline float fract(float x) { return x - std::floor(x); }

inline float driftOffset(float drift, float time) {
    const float phase = fract(drift * time * 0.025f + 0.25f);
    return 1.0f - 2.0f * std::abs(2.0f * phase - 1.0f);
}

inline SceneViewTerms describeView(const SceneParams& p, int width, int height, float rotation, float time, float twistBend) {
    SceneViewTerms terms;
    terms.view[0] = static_cast<float>(width) / static_cast<float>(std::max(height, 1));
    if (p.pixelate > 0.001f) terms.view[1] = mix(1.0f, 12.0f, p.pixelate) * 24.0f;

    const SceneParams::KaleidoUniforms kaleido = SceneParams::kaleidoUniforms(p.kaleidoFolds);
    if (kaleido.on > 0.5f && kaleido.folds >= 2.0f) terms.view[2] = 6.2831853f / kaleido.folds;
    if (p.morph > 0.001f) terms.view[3] = p.morph * (0.6f + 0.15f * std::sin(time * 0.31f));

    const float angle = rotation + p.sway * 0.35f * std::sin(time * 0.7f);
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    terms.rotation = {c, -s, s, c};

    terms.motion[0] = driftOffset(p.driftX, time);
    terms.motion[1] = driftOffset(p.driftY, time);
    terms.motion[2] = 1.0f + twistBend;
    terms.motion[3] = p.warp * 0.5f;
    return terms;
}

inline SceneGradeTerms describeGrade(const SceneParams& p) {
    SceneGradeTerms terms;
    terms.shade = {mix(24.0f, 3.0f, p.posterize), 1.0f / std::max(p.gamma, 0.05f), p.temperature * 0.12f,
                   p.brightness * p.intensity};
    return terms;
}

}  // namespace geode::viz::shader
