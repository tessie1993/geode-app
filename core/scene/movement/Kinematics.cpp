#include "scene/movement/Kinematics.hpp"

#include <algorithm>
#include <cmath>

namespace geode::scene::movement {

namespace {

// A unit vector perpendicular to k, the same one every time for the same k.
Vec3 perpendicular(const Vec3& k) {
    const Vec3 other = std::fabs(k[0]) < 0.9f ? Vec3{1.0f, 0.0f, 0.0f} : Vec3{0.0f, 1.0f, 0.0f};
    return normalize(cross(k, other), {1.0f, 0.0f, 0.0f});
}

float omegaFor(float seconds) { return 2.0f / std::max(std::isfinite(seconds) ? seconds : 1.0f, 1e-3f); }

float safeZeta(float zeta) { return std::isfinite(zeta) ? std::clamp(zeta, 0.0f, 16.0f) : 1.0f; }

}  // namespace

void stepOrbit(Body& b, const OrbitParams& p, float dt) {
    dt = clampDt(dt);
    if (!(dt > 0.0f) || !b.alive) return;
    const Vec3 k = normalize(p.axis, {0.0f, 1.0f, 0.0f});
    const Vec3 d = sub(b.pos, p.centre);
    const float axial = dot(d, k);
    const Vec3 radial = sub(d, scale(k, axial));
    const float rl = length(radial);
    Vec3 dir = rl > 1e-6f ? scale(radial, 1.0f / rl) : perpendicular(k);
    // The phase moves by rate·dt: a turn about the axis, exact and additive over frames.
    const float rate = std::isfinite(p.rate) ? p.rate : 0.0f;
    dir = normalize(rotate(axisAngle(k, rate * dt), dir), dir);
    // Capture: exp(-dt/τ) composes exactly over frames (e^{-a} · e^{-b} = e^{-(a+b)}).
    const float pull = p.capture > 0.0f ? std::exp(-dt / p.capture) : 0.0f;
    const float radius = std::max(std::isfinite(p.radius) ? p.radius : 0.0f, 0.0f);
    const float r = radius + (rl - radius) * pull;
    const float h = axial * pull;
    const Vec3 next = add(p.centre, add(scale(dir, r), scale(k, h)));
    b.vel = scale(sub(next, b.pos), 1.0f / dt);
    b.pos = next;
}

void stepOrbit(std::span<Body> bodies, const OrbitParams& p, float dt) {
    for (Body& b : bodies) stepOrbit(b, p, dt);
}

void springStep(float& x, float& v, float omega, float zeta, float t) {
    if (!(t > 0.0f) || !(omega > 0.0f) || !std::isfinite(x) || !std::isfinite(v)) return;
    zeta = safeZeta(zeta);
    if (std::fabs(zeta - 1.0f) < 1e-3f) {
        // Critical: x = (x0 + c t) e^{-ωt}, c = v0 + ω x0.
        const float e = std::exp(-omega * t);
        const float c = v + omega * x;
        const float nx = (x + c * t) * e;
        const float nv = (v - omega * c * t) * e;
        x = nx;
        v = nv;
    } else if (zeta < 1.0f) {
        const float wd = omega * std::sqrt(1.0f - zeta * zeta);
        const float e = std::exp(-zeta * omega * t);
        const float cs = std::cos(wd * t);
        const float sn = std::sin(wd * t);
        const float nx = e * (x * cs + (v + zeta * omega * x) / wd * sn);
        const float nv = e * (v * cs - (zeta * omega * v + omega * omega * x) / wd * sn);
        x = nx;
        v = nv;
    } else {
        const float s = std::sqrt(zeta * zeta - 1.0f);
        const float r1 = -omega * (zeta - s);
        const float r2 = -omega * (zeta + s);
        const float c1 = (v - r2 * x) / (r1 - r2);
        const float c2 = x - c1;
        const float e1 = std::exp(r1 * t);
        const float e2 = std::exp(r2 * t);
        x = c1 * e1 + c2 * e2;
        v = c1 * r1 * e1 + c2 * r2 * e2;
    }
}

void stepFollow(Body& b, const FollowParams& p, float dt) {
    dt = clampDt(dt);
    if (!(dt > 0.0f) || !b.alive || !finite(p.target)) return;
    const float omega = omegaFor(p.seconds);
    for (int a = 0; a < 3; ++a) {
        float x = b.pos[a] - p.target[a];
        float v = b.vel[a];
        springStep(x, v, omega, p.damping, dt);
        b.pos[a] = p.target[a] + x;
        b.vel[a] = v;
    }
}

void stepFollow(std::span<Body> bodies, const FollowParams& p, float dt) {
    for (Body& b : bodies) stepFollow(b, p, dt);
}

void stepBounce(Body& b, BounceState& s, const BounceParams& p, float kick, float dt) {
    dt = clampDt(dt);
    if (!b.alive) return;
    if (std::isfinite(kick) && kick > 0.0f && std::isfinite(p.strength)) s.speed += p.strength * kick;
    if (!(dt > 0.0f)) return;
    const Vec3 k = normalize(p.axis, {0.0f, 1.0f, 0.0f});
    const float before = s.offset;
    springStep(s.offset, s.speed, omegaFor(p.seconds), p.damping, dt);
    b.pos = add(b.pos, scale(k, s.offset - before));
}

}  // namespace geode::scene::movement
