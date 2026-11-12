#pragma once
#include <algorithm>
#include <array>
#include <cmath>

#include "viz/TouchField.hpp"

namespace geode::viz::touch {

// Settings › Visuals › Touch › Smear: the fingers that touch the visual (touch play, TouchField) also drag
// the picture, as strokes. They went into the ripple overlay, or into the pool on Water; both are gone with
// Pool (#1036), so the renderer keeps the settings and steps no trail until the smear has a new target.
// The shell only hands over the switch and the strength (geode_viz_set_touch_smear); every rule is here.
// The stroke path itself (queue, drain, ripple, water) was there all along; what went with the old live
// view was its only feeder, the one-finger drag. Now any finger the touch field holds feeds it.

inline constexpr float kSmearStrengthMin = 0.2f;
inline constexpr float kSmearStrengthMax = 2.0f;
inline constexpr float kSmearStrengthDefault = 1.0f;

// The Settings slider's range; a broken value is the default.
inline float smearStrength(float s) {
    if (!std::isfinite(s)) return kSmearStrengthDefault;
    return std::clamp(s, kSmearStrengthMin, kSmearStrengthMax);
}

struct SmearSettings {
    bool on = false;
    float strength = kSmearStrengthDefault;
};

// One stroke in the overlay's frame: 0..1 surface space, y down, the motion since the last frame.
struct SmearStroke {
    float nx;
    float ny;
    float ndx;
    float ndy;
    float dt;
    float strength;
};

// Follows each finger the touch field holds down (strength 1 in TouchField::points, y-up NDC) frame to frame and
// turns its motion into a stroke. GL thread only.
class SmearTrail {
public:
    // A move shorter than this (NDC) is a finger at rest: no stroke.
    static constexpr float kMinMove = 1.0e-4f;
    // A jump longer than this in one frame is a finger that changed slot (another one lifted), not a drag.
    static constexpr float kMaxJump = 0.5f;

    template <typename Emit>
    void step(const std::array<float, TouchField::kMaxPoints * TouchField::kPointStride>& points, float dt, SmearSettings s,
              Emit&& emit) {
        const float strength = smearStrength(s.strength);
        for (int i = 0; i < TouchField::kMaxPoints; i++) {
            const int base = i * TouchField::kPointStride;
            const bool down = points[base + 2] >= 0.999f;
            if (!down) {
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
                    emit(SmearStroke{(x + 1.0f) * 0.5f, (1.0f - y) * 0.5f, dx * 0.5f, -dy * 0.5f, dt, strength});
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

}  // namespace geode::viz::touch
