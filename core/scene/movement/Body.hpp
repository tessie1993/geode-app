#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "scene/Math.hpp"

// scene::movement's one data type: a Body, the moving part of anything in a Scene (an object of #515, a
// particle of a swarm, the world's own anchor). Plain data and no behaviour, so an object pool can hold
// thousands of them in one array and every stepper can run over a span of them.
//
// A stepper never allocates, never reads a clock and never draws a random number of its own: the time,
// dt and any seed come in as arguments, so the same inputs give the same bodies on every surface (live,
// wallpaper, dream, Cast, export) and in a host test.

namespace geode::scene::movement {

struct Body {
    Vec3 pos{0.0f, 0.0f, 0.0f};
    Vec3 vel{0.0f, 0.0f, 0.0f};    // world units per second
    Quat orient{};                 // local → world
    Vec3 spin{0.0f, 0.0f, 0.0f};   // world angular velocity, rad/s (direction = axis, length = rate)
    float scale = 1.0f;            // uniform
    float age = 0.0f;              // seconds since birth (shaders may read it)
    bool alive = true;
};

// The fixed integration step every force stepper divides a frame into. A frame of dt runs
// ceil(dt / kSubstep) equal substeps, so 30, 60 and 120 fps all run the same 1/240 s steps and land
// on the same bodies (to float rounding); a long frame (a stall) is capped at kMaxSubsteps.
constexpr float kSubstep = 1.0f / 240.0f;
constexpr int kMaxSubsteps = 64;

// A frame's dt as the steppers accept it: non-finite or negative is 0, and one frame never covers more
// than a quarter of a second (a resume after a pause must not fling everything across the world).
inline float clampDt(float dt) { return std::isfinite(dt) ? std::clamp(dt, 0.0f, 0.25f) : 0.0f; }

inline int substepsFor(float dt) {
    if (!(dt > 0.0f)) return 0;
    // The small slack keeps 1/60 from rounding up to 5 steps of 1/240 on a float dt.
    const int n = static_cast<int>(std::ceil(dt / kSubstep - 1e-3f));
    return std::clamp(n, 1, kMaxSubsteps);
}

// Moves a body by its own velocity over dt (what the force steppers do once per substep).
inline void advance(Body& b, float dt) { b.pos = add(b.pos, scale(b.vel, dt)); }

// Turns a body by its spin and ages it, once per frame, after every behaviour has run. Exact for a
// constant spin: a turn of |spin|·dt about spin's axis, multiplied on the left since spin is a world axis.
inline void turnAndAge(Body& b, float dt) {
    dt = clampDt(dt);
    const float rate = length(b.spin);
    if (rate > 1e-7f) b.orient = qnormalize(mul(axisAngle(scale(b.spin, 1.0f / rate), rate * dt), b.orient));
    b.age += dt;
}

// ---- Seeded hashing and noise ----------------------------------------------------------------------
// Integer hashing only (no std::rand, no <random> engines whose streams differ between standard
// libraries), so a seed means the same field and the same split on every phone and on the host.

inline std::uint32_t mix(std::uint32_t h) {
    // The finaliser of MurmurHash3: every input bit reaches every output bit.
    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return h;
}

inline std::uint32_t hash3(std::int32_t x, std::int32_t y, std::int32_t z, std::uint32_t seed) {
    std::uint32_t h = seed * 0x9e3779b9u;
    h = mix(h ^ static_cast<std::uint32_t>(x) * 0x8da6b343u);
    h = mix(h ^ static_cast<std::uint32_t>(y) * 0xd8163841u);
    h = mix(h ^ static_cast<std::uint32_t>(z) * 0xcb1ab31fu);
    return h;
}

// A hash as a float in [0, 1).
inline float unit(std::uint32_t h) { return static_cast<float>(h >> 8) * (1.0f / 16777216.0f); }

// A small deterministic stream (a counter through the hash): draw k of seed s is always the same number.
struct Rng {
    std::uint32_t seed = 0;
    std::uint32_t counter = 0;
    float next() { return unit(mix(seed * 0x9e3779b9u ^ mix(++counter * 0x632be5abu))); }
    // In [-1, 1).
    float signedNext() { return next() * 2.0f - 1.0f; }
};

// Value noise on the integer lattice, quintic-faded so the field and its gradient are continuous:
// a smooth field in [-1, 1] that is the same for the same seed.
inline float valueNoise(const Vec3& p, std::uint32_t seed) {
    const float fx = std::floor(p[0]);
    const float fy = std::floor(p[1]);
    const float fz = std::floor(p[2]);
    const auto ix = static_cast<std::int32_t>(fx);
    const auto iy = static_cast<std::int32_t>(fy);
    const auto iz = static_cast<std::int32_t>(fz);
    const float tx = p[0] - fx;
    const float ty = p[1] - fy;
    const float tz = p[2] - fz;
    auto fade = [](float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); };
    const float ux = fade(tx);
    const float uy = fade(ty);
    const float uz = fade(tz);
    auto corner = [&](int dx, int dy, int dz) { return unit(hash3(ix + dx, iy + dy, iz + dz, seed)) * 2.0f - 1.0f; };
    auto lerpf = [](float a, float b, float t) { return a + (b - a) * t; };
    const float x00 = lerpf(corner(0, 0, 0), corner(1, 0, 0), ux);
    const float x10 = lerpf(corner(0, 1, 0), corner(1, 1, 0), ux);
    const float x01 = lerpf(corner(0, 0, 1), corner(1, 0, 1), ux);
    const float x11 = lerpf(corner(0, 1, 1), corner(1, 1, 1), ux);
    return lerpf(lerpf(x00, x10, uy), lerpf(x01, x11, uy), uz);
}

}  // namespace geode::scene::movement
