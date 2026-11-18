#pragma once
#include <cmath>
#include <cstdint>

namespace geode::viz {

// Where the radial Twist turns the picture from, and how its angle grows with distance. One centred twist, the
// same on every style, winds a line through the middle into the same S, so each style (its variant seed, the
// FNV-1a of its id) gets its own centre off the middle, drifting slowly, and its own falloff. `bend` is added to
// 1: the angle grows with distance^(1 + bend), so 0 is the plain linear twist and a shader reading nothing for
// these (centre 0, bend 0) draws exactly the old one. At Twist 0 nothing turns whatever these hold.
struct TwistShape {
    float x = 0.0f;     // the centre, in the shaders' half-heights from the middle
    float y = 0.0f;
    float bend = 0.0f;  // the falloff's exponent beyond 1, -0.35 .. 0.6
};

inline TwistShape twistShape(std::uint32_t seed, float seconds) {
    const auto unit = [seed](std::uint32_t k) {
        std::uint32_t h = seed ^ (k * 0x9E3779B9u);
        h ^= h >> 16;
        h *= 0x7feb352du;
        h ^= h >> 15;
        h *= 0x846ca68bu;
        h ^= h >> 16;
        return static_cast<float>(h >> 8) / 16777216.0f;
    };
    constexpr float kTau = 6.2831853f;
    const float heading = unit(1u) * kTau;
    const float away = 0.15f + 0.35f * unit(2u);
    // Two slow wobbles (periods of 1.5 to 4 minutes), each at most 0.12 across.
    const float wx = 0.025f + 0.025f * unit(3u);
    const float wy = 0.025f + 0.025f * unit(4u);
    const float t = std::isfinite(seconds) ? seconds : 0.0f;
    TwistShape s;
    s.x = away * std::cos(heading) + 0.12f * std::sin(t * wx + unit(5u) * kTau);
    s.y = away * std::sin(heading) + 0.12f * std::cos(t * wy + unit(6u) * kTau);
    s.bend = -0.35f + 0.95f * unit(7u);
    return s;
}

}  // namespace geode::viz
