#pragma once

#include <algorithm>
#include <cmath>

#include "scene/Math.hpp"

// The scene camera's own maths: the look offsets of viz/CameraRig, the roll from level and the rig's
// critically damped spring. The vectors and quaternions it builds on are scene/Math.hpp, shared with
// scene::movement; the using-declarations below keep every camera::name that existed before the move.

namespace geode::scene::camera {

using geode::scene::Vec3;
using geode::scene::kPi;
using geode::scene::kTwoPi;
using geode::scene::add;
using geode::scene::sub;
using geode::scene::scale;
using geode::scene::dot;
using geode::scene::cross;
using geode::scene::length;
using geode::scene::normalize;
using geode::scene::lerp;
using geode::scene::finite;
using geode::scene::wrapAngle;
using geode::scene::Quat;
using geode::scene::mul;
using geode::scene::conj;
using geode::scene::qdot;
using geode::scene::qnormalize;
using geode::scene::axisAngle;
using geode::scene::rotate;
using geode::scene::rightOf;
using geode::scene::upOf;
using geode::scene::fwdOf;
using geode::scene::fromBasis;
using geode::scene::angleBetween;
using geode::scene::slerp;
using geode::scene::stepToward;

// The look offsets viz/CameraRig applies to its path frame, in its order: yaw about the camera's up,
// then pitch about its right (positive looks up), then roll about its forward:
//   q = path * axisAngle(Y, yaw) * axisAngle(X, -pitch) * axisAngle(Z, roll).
struct Look {
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
};

inline Quat fromLook(const Look& l) {
    return mul(mul(axisAngle({0.0f, 1.0f, 0.0f}, l.yaw), axisAngle({1.0f, 0.0f, 0.0f}, -l.pitch)),
               axisAngle({0.0f, 0.0f, 1.0f}, l.roll));
}

// fromLook's inverse for a relative turn q: yaw in (-pi, pi], pitch in [-pi/2, pi/2], roll in (-pi, pi].
// For M = Ry(a) Rx(b) Rz(c): M12 = -sin b, M02 / M22 = tan a, M10 / M11 = tan c; pitch = -b.
inline Look toLook(const Quat& q) {
    const Vec3 r = rightOf(q);
    const Vec3 u = upOf(q);
    const Vec3 f = fwdOf(q);
    Look l;
    l.pitch = std::asin(std::clamp(f[1], -1.0f, 1.0f));
    l.yaw = std::atan2(f[0], f[2]);
    l.roll = std::atan2(r[1], u[1]);
    return l;
}

// The camera's roll away from level: the signed turn about its forward that takes the level up (world
// up with the forward part removed) to its own up. Undefined looking straight up or down: nothing there.
inline bool rollFromLevel(const Quat& q, float& out) {
    const Vec3 f = fwdOf(q);
    const Vec3 worldUp{0.0f, 1.0f, 0.0f};
    const Vec3 level = sub(worldUp, scale(f, dot(f, worldUp)));
    const float l = length(level);
    if (l < 0.03f) return false;
    const Vec3 lu = scale(level, 1.0f / l);
    const Vec3 u = upOf(q);
    out = std::atan2(dot(cross(lu, u), f), dot(lu, u));
    return true;
}

// The critically damped spring viz/CameraRig uses: exact for a constant target, stable for any dt.
struct Spring {
    float x = 0.0f;
    float v = 0.0f;
    void reset(float to) {
        x = to;
        v = 0.0f;
    }
    void step(float target, float dt, float seconds) {
        const float omega = 2.0f / std::max(seconds, 1e-3f);
        const float e = std::exp(-omega * dt);
        const float d = x - target;
        const float t = (v + omega * d) * dt;
        x = target + (d + t) * e;
        v = (v - omega * t) * e;
    }
};

}  // namespace geode::scene::camera
