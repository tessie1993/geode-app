#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "scene/movement/Body.hpp"

// Split: a body breaks into, or emits, N children spread round its velocity. Deterministic per seed:
// the same parent, params and seed give the same children on every surface. The children are written
// into slots the caller hands over (free slots of the object pool), so nothing is allocated.
//
// Directions fill a cone of half-angle `spread` about the parent's velocity (the whole sphere when the
// parent is at rest) on a Fibonacci spiral, so N children cover the cone evenly; the seed rolls the
// spiral and jitters each child's speed. With `conserve` on, the children's spread velocities and
// offsets have their mean taken out, so N children of equal mass (the parent's / N) carry exactly the
// parent's momentum (with inherit = 1) and centre of mass.

namespace geode::scene::movement {

struct SplitParams {
    int count = 6;
    float spread = 0.8f;       // cone half-angle, radians (π: the whole sphere)
    float speed = 2.0f;        // units/s each child flies off at, relative to the parent
    float speedJitter = 0.25f; // 0..1, a seeded fraction of `speed` up or down
    float childScale = 0.5f;   // fraction of the parent's scale
    float inherit = 1.0f;      // how much of the parent's velocity each child keeps
    float offset = 0.5f;       // spawn distance from the parent's centre, in parent scales
    bool conserve = true;
    bool keepParent = false;   // an emitter keeps going; a split parent dies
    bool operator==(const SplitParams&) const = default;
};

// The most children one split writes.
constexpr int kMaxSplitChildren = 64;

// Writes min(count, out.size(), kMaxSplitChildren) children into out and returns how many. A dead
// parent writes none. The parent dies unless keepParent. `out` must not hold the parent itself.
std::size_t split(Body& parent, const SplitParams& p, std::uint32_t seed, std::span<Body> out);

}  // namespace geode::scene::movement
