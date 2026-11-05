#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "scene/movement/Body.hpp"

// The force movers: drift, flock and react to the camera. Each adds an acceleration (and flock and
// collide add a position constraint); stepForces sums the ones a body carries and integrates them in
// fixed substeps of kSubstep, so 30, 60 and 120 fps run the same 1/240 s steps. One integrator for all
// of them, so a body that drifts and scatters is moved once, not once per behaviour.
//
// Per substep, in order: flock's accelerations for every body (from the positions at the substep's
// start, so the order bodies are listed in never matters), then each body's drift and react force,
// semi-implicit Euler (v += a·h, v *= e^{-drag·h}, the speed cap, x += v·h), then flock's minimum
// distance and the collide sphere as position constraints.

namespace geode::scene::movement {

// ---- Drift ------------------------------------------------------------------------------------------
// A smooth seeded noise field of velocities (three channels of value noise over position and time): a
// body's velocity relaxes toward the field's with time constant `response`. `frequency` is lattice
// cells per world unit (how wide the swirls are), `speed` how fast the field itself evolves, `seed`
// which field it is.
struct DriftParams {
    float amplitude = 0.5f;  // world units/s at the field's strongest
    float frequency = 0.5f;
    float speed = 0.2f;
    float response = 0.6f;   // seconds
    std::uint32_t seed = 1;
    bool operator==(const DriftParams&) const = default;
};

// The field's velocity at a point and time: what a drifting body tends to.
Vec3 driftField(const DriftParams& p, const Vec3& pos, float time);

// ---- Flock ------------------------------------------------------------------------------------------
// Reynolds' boids: separation from neighbours within `radius`, alignment with their mean velocity,
// cohesion toward their centre, each weighted; the sum is capped at `maxAccel`. `cruise` pulls each
// body's speed toward a cruising speed (0: no pull). `minDistance` is a hard floor: after each substep
// any pair closer than it is pushed apart symmetrically (Jacobi, so momentum is kept and the order of
// the bodies does not matter).
struct FlockParams {
    float radius = 1.5f;
    float separation = 1.5f;
    float alignment = 1.0f;
    float cohesion = 0.5f;
    float cruise = 1.0f;     // units/s
    float minDistance = 0.4f;
    float maxAccel = 8.0f;   // units/s²
    bool operator==(const FlockParams&) const = default;
};

// ---- React to the camera ----------------------------------------------------------------------------
enum class ReactMode : int {
    Scatter = 0,  // inside `radius` of the eye, pushed straight away at `strength` units/s²
    Swarm = 1,    // pulled onto a shell of `radius` round the eye (in from outside, out from inside)
    Collide = 2,  // kept out of a sphere of `radius`: pushed to its surface, the inward speed reflected
};

struct ReactParams {
    int mode = static_cast<int>(ReactMode::Scatter);
    float radius = 3.0f;
    float strength = 6.0f;
    float restitution = 0.3f;  // Collide: how much of the inward speed comes back out
    bool operator==(const ReactParams&) const = default;
};

// ---- The integrator's own numbers -------------------------------------------------------------------
struct MotionParams {
    float drag = 0.8f;      // 1/s: velocity falls by e^{-drag·t} with no force
    float maxSpeed = 0.0f;  // units/s; 0 is no cap
    bool operator==(const MotionParams&) const = default;
};

// Which force behaviours a set of bodies carries: a null pointer is a behaviour switched off.
struct ForceSet {
    const DriftParams* drift = nullptr;
    const FlockParams* flock = nullptr;
    const ReactParams* react = nullptr;
    MotionParams motion{};
};

// What the world tells the forces this frame.
struct ForceInput {
    float time = 0.0f;              // the scene clock at the frame's start, seconds (drift's field moves on it)
    Vec3 eye{0.0f, 0.0f, 0.0f};     // the camera's position
    Vec3 eyeVel{0.0f, 0.0f, 0.0f};  // and velocity (Collide reflects speed relative to the eye)
};

// Flock's working memory, sized once (by the object pool that owns the bodies), so stepping never
// allocates. Up to `gridAbove` live bodies the flock compares every pair (O(n²), cheapest for small
// flocks); above it, a spatial hash grid with cells one neighbour radius wide, built by a counting sort
// each substep. Bodies past `capacity` are moved by the other forces but take no part in the flock.
struct FlockScratch {
    static constexpr std::size_t kBuckets = 4096;  // a power of two
    explicit FlockScratch(std::size_t capacity = 64);
    std::size_t capacity() const { return accel.size(); }

    std::size_t gridAbove = 64;
    std::vector<Vec3> accel;
    std::vector<Vec3> correction;
    std::vector<std::uint32_t> bucketOf;  // per body: its grid bucket, or kNone when dead
    std::vector<std::uint32_t> order;     // bodies sorted by bucket
    std::vector<std::uint32_t> start;     // kBuckets + 1 prefix sums into order
    std::vector<std::uint32_t> fill;      // kBuckets cursors
    float cellSize = 1.0f;
    bool gridBuilt = false;
};

// Moves the bodies by the forces in `set` over dt. `scratch` is only read when set.flock is on.
void stepForces(std::span<Body> bodies, const ForceSet& set, const ForceInput& in, float dt, FlockScratch& scratch);

// The single-behaviour forms, for a body or a span that carries just one.
void stepDrift(std::span<Body> bodies, const DriftParams& p, const MotionParams& m, float time, float dt);
void stepFlock(std::span<Body> bodies, const FlockParams& p, const MotionParams& m, float dt, FlockScratch& scratch);
void stepReact(std::span<Body> bodies, const ReactParams& p, const MotionParams& m, const ForceInput& in, float dt);

}  // namespace geode::scene::movement
