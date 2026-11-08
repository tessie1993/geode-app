#pragma once

#include <span>

#include "scene/movement/Body.hpp"

// The closed-form movers: orbit, follow and bounce. Each places a body exactly where its law puts it
// after dt (a rotation by rate·dt, the damped-spring solution, an exponential capture), so any frame
// rate, and any split of a second into frames, lands on the same place to float rounding. None of them
// substeps, none allocates.
//
// They write the body's position (and, for orbit and follow, its velocity, so a later split or a force
// stepper starts from the motion the body really has). Bounce adds a displacement on top and leaves the
// velocity alone, so it can ride on any other motion.

namespace geode::scene::movement {

// ---- Orbit ------------------------------------------------------------------------------------------
// Circles a centre about an axis at `rate` rad/s (integrated: a rate change never jumps the body), on a
// ring of `radius`. A body off the ring is captured onto it: its distance from the axis and its height
// above the ring's plane both fall exponentially toward the ring with time constant `capture` seconds
// (0 snaps it on at once). A body on the ring stays exactly on it.
struct OrbitParams {
    Vec3 centre{0.0f, 0.0f, 0.0f};
    Vec3 axis{0.0f, 1.0f, 0.0f};  // normalised here; a zero axis is +Y
    float radius = 1.0f;
    float rate = 0.5f;            // rad/s, right-handed about the axis
    float capture = 0.5f;         // seconds
    bool operator==(const OrbitParams&) const = default;
};

void stepOrbit(Body& b, const OrbitParams& p, float dt);
void stepOrbit(std::span<Body> bodies, const OrbitParams& p, float dt);

// ---- The damped spring ------------------------------------------------------------------------------
// x'' + 2ζω x' + ω² x = 0 solved exactly over t, for x relative to the rest point: under-damped
// (ζ < 1), critical (ζ = 1, never overshoots) and over-damped (ζ > 1). Exact for any t, so it is the
// same at every frame rate while the rest point holds still.
void springStep(float& x, float& v, float omega, float zeta, float t);

// ---- Follow -----------------------------------------------------------------------------------------
// A spring toward a target point. `seconds` is the response time (ω = 2 / seconds, the same law as
// camera::Spring, so a critically damped follow is the camera's own smoothing); `damping` is ζ: 1 is
// critical (fastest with no overshoot), below 1 swings past and settles, above 1 creeps in.
struct FollowParams {
    Vec3 target{0.0f, 0.0f, 0.0f};
    float seconds = 0.5f;
    float damping = 1.0f;
    bool operator==(const FollowParams&) const = default;
};

void stepFollow(Body& b, const FollowParams& p, float dt);
void stepFollow(std::span<Body> bodies, const FollowParams& p, float dt);

// ---- Bounce on the beat -----------------------------------------------------------------------------
// A kick on the beat, then a damped return: each beat adds `strength` (units/s, scaled by the kick the
// caller passes, so a beat's strength or a route can size it) to a displacement along `axis`, which a
// damped spring (`seconds`, `damping` as Follow's) brings back to 0. The displacement is the body's own
// (BounceState), added to its position on top of whatever else moves it.
struct BounceParams {
    Vec3 axis{0.0f, 1.0f, 0.0f};  // normalised here; a zero axis is +Y
    float strength = 1.5f;
    float seconds = 0.35f;
    float damping = 0.35f;
    bool operator==(const BounceParams&) const = default;
};

struct BounceState {
    float offset = 0.0f;  // along the axis, world units
    float speed = 0.0f;   // units/s
};

// `kick` is this frame's beat: 0 none, 1 a full beat (a beat flag as 0 or 1, or its strength). The kick
// lands at the start of the frame, then the spring runs dt.
void stepBounce(Body& b, BounceState& s, const BounceParams& p, float kick, float dt);

}  // namespace geode::scene::movement
