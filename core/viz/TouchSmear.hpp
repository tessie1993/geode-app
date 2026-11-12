#pragma once
#include <algorithm>
#include <array>
#include <cmath>

#include "viz/TouchField.hpp"
#include "viz/VisualSafety.hpp"

namespace geode::viz::touch {

// Settings › Gestures & controls › Touch › Smear: the fingers touch play holds (TouchField) push the
// shared flow field (fluid::FlowField), and the composite bends the finished picture of any style by
// that field (composite_frag's uFlow). Liquid mode has its own touch on the same field, so a smear runs
// only with Liquid off. The shell hands over the switch and the strength (geode_viz_set_touch_smear);
// every rule is here. Live renderers only: an export never smears.

inline constexpr float kSmearStrengthMin = 0.2f;
inline constexpr float kSmearStrengthMax = 2.0f;
inline constexpr float kSmearStrengthDefault = 1.0f;

// A finger's speed (clip units per second) into the field's kick, and the largest kick taken.
inline constexpr float kSmearPush = 8.0f;
inline constexpr float kSmearMaxKick = 40.0f;
inline constexpr float kSmearRadius = 0.09f;
// The field's velocity decay per second while it only carries a smear: settled in about 1.5 s.
inline constexpr float kSmearDissipation = 2.0f;
// After the last kick the field keeps stepping this long, its bend fading over the last second.
inline constexpr float kSmearLingerSeconds = 2.5f;
inline constexpr float kSmearFadeSeconds = 1.0f;

// The Settings slider's range; a broken value is the default.
inline float smearStrength(float s) {
    if (!std::isfinite(s)) return kSmearStrengthDefault;
    return std::clamp(s, kSmearStrengthMin, kSmearStrengthMax);
}

struct SmearSettings {
    bool on = false;
    float strength = kSmearStrengthDefault;
};

// One finger's push on the field (FlowField::queueKick): where, in clip units (y up), and its velocity.
struct SmearKick {
    float x;
    float y;
    float vx;
    float vy;
};

// Follows each finger the touch field holds down (strength 1 in TouchField::points, y-up NDC) frame to
// frame and turns its motion into a kick. GL thread only.
class SmearTrail {
public:
    // A move shorter than this (NDC) is a finger at rest: no kick.
    static constexpr float kMinMove = 1.0e-4f;
    // A jump longer than this in one frame is a finger that changed slot (another one lifted), not a drag.
    static constexpr float kMaxJump = 0.5f;

    template <typename Emit>
    void step(const std::array<float, TouchField::kMaxPoints * TouchField::kPointStride>& points, float dt, SmearSettings s,
              Emit&& emit) {
        for (int i = 0; i < TouchField::kMaxPoints; i++) {
            const int base = i * TouchField::kPointStride;
            if (points[base + 2] < 0.999f) {
                had_[i] = false;
                continue;
            }
            const float x = points[base];
            const float y = points[base + 1];
            if (had_[i] && s.on && dt > 0.0f) {
                const float dx = x - prevX_[i];
                const float dy = y - prevY_[i];
                const float move = std::hypot(dx, dy);
                if (move > kMinMove && move < kMaxJump) {
                    const float k = std::min(kSmearPush / dt, kSmearMaxKick / move);
                    emit(SmearKick{x, y, dx * k, dy * k});
                }
            }
            prevX_[i] = x;
            prevY_[i] = y;
            had_[i] = true;
        }
    }

    void reset() { had_.fill(false); }

private:
    std::array<float, TouchField::kMaxPoints> prevX_{};
    std::array<float, TouchField::kMaxPoints> prevY_{};
    std::array<bool, TouchField::kMaxPoints> had_{};
};

// How long the field still carries a smear. Steps on the frame clock, so in silence it holds.
class SmearLinger {
public:
    void kicked() { age_ = 0.0f; }
    void step(float dt) {
        if (dt > 0.0f) age_ = std::min(age_ + dt, kSmearLingerSeconds);
    }
    bool live() const { return age_ < kSmearLingerSeconds; }
    // 1 until the last kSmearFadeSeconds, then a smoothstep down to 0.
    float fade() const {
        const float t = std::clamp((kSmearLingerSeconds - age_) / kSmearFadeSeconds, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }
    void reset() { age_ = kSmearLingerSeconds; }

private:
    float age_ = kSmearLingerSeconds;
};

// The composite's bend (uFlowStrength): the slider's strength, faded, and under reduced motion the
// safety clamp's motion scale.
inline float smearWarp(SmearSettings s, float fade, bool reducedMotion) {
    if (!s.on) return 0.0f;
    const float f = std::isfinite(fade) ? std::clamp(fade, 0.0f, 1.0f) : 0.0f;
    return smearStrength(s.strength) * f * (reducedMotion ? safety::kReducedMotionScale : 1.0f);
}

}  // namespace geode::viz::touch
