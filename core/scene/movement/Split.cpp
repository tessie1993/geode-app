#include "scene/movement/Split.hpp"

#include <algorithm>
#include <cmath>

namespace geode::scene::movement {

namespace {

constexpr float kGoldenAngle = 2.39996323f;  // π (3 - √5)

float finiteOr(float v, float fallback) { return std::isfinite(v) ? v : fallback; }

}  // namespace

std::size_t split(Body& parent, const SplitParams& p, std::uint32_t seed, std::span<Body> out) {
    if (!parent.alive) return 0;
    const int wanted = std::clamp(p.count, 0, kMaxSplitChildren);
    const std::size_t n = std::min(static_cast<std::size_t>(wanted), out.size());
    if (n == 0) return 0;

    const float speedNow = length(parent.vel);
    const bool still = !(speedNow > 1e-5f);
    const Vec3 axis = still ? Vec3{0.0f, 1.0f, 0.0f} : scale(parent.vel, 1.0f / speedNow);
    const float spread = still ? kPi : std::clamp(finiteOr(p.spread, 0.0f), 0.0f, kPi);
    // Two axes across the cone's axis.
    const Vec3 other = std::fabs(axis[0]) < 0.9f ? Vec3{1.0f, 0.0f, 0.0f} : Vec3{0.0f, 1.0f, 0.0f};
    const Vec3 u = normalize(cross(axis, other), {1.0f, 0.0f, 0.0f});
    const Vec3 w = cross(axis, u);

    Rng rng{seed, 0};
    const float roll = rng.next() * kTwoPi;
    const float cosSpread = std::cos(spread);
    const float speed = finiteOr(p.speed, 0.0f);
    const float jitter = std::clamp(finiteOr(p.speedJitter, 0.0f), 0.0f, 1.0f);
    const float offset = finiteOr(p.offset, 0.0f) * parent.scale;

    Vec3 meanVel{0.0f, 0.0f, 0.0f};
    Vec3 meanOff{0.0f, 0.0f, 0.0f};
    for (std::size_t k = 0; k < n; ++k) {
        // Equal-area rings of the cap: cos θ runs evenly from 1 to cos(spread).
        const float frac = (static_cast<float>(k) + 0.5f) / static_cast<float>(n);
        const float cosT = 1.0f - frac * (1.0f - cosSpread);
        const float sinT = std::sqrt(std::max(0.0f, 1.0f - cosT * cosT));
        const float phi = roll + kGoldenAngle * static_cast<float>(k);
        const Vec3 dir = add(scale(axis, cosT), scale(add(scale(u, std::cos(phi)), scale(w, std::sin(phi))), sinT));
        const float sp = speed * (1.0f + jitter * rng.signedNext());
        Body& c = out[k];
        c = Body{};
        c.vel = scale(dir, sp);          // the spread part for now; the parent's is added below
        c.pos = scale(dir, offset);      // relative to the parent for now
        c.orient = parent.orient;
        c.spin = parent.spin;
        c.scale = parent.scale * std::max(finiteOr(p.childScale, 0.0f), 0.0f);
        c.age = 0.0f;
        c.alive = true;
        meanVel = add(meanVel, c.vel);
        meanOff = add(meanOff, c.pos);
    }
    const float inv = 1.0f / static_cast<float>(n);
    meanVel = p.conserve ? scale(meanVel, inv) : Vec3{0.0f, 0.0f, 0.0f};
    meanOff = p.conserve ? scale(meanOff, inv) : Vec3{0.0f, 0.0f, 0.0f};
    const Vec3 inherited = scale(parent.vel, finiteOr(p.inherit, 0.0f));
    for (std::size_t k = 0; k < n; ++k) {
        Body& c = out[k];
        c.vel = add(inherited, sub(c.vel, meanVel));
        c.pos = add(parent.pos, sub(c.pos, meanOff));
    }
    if (!p.keepParent) parent.alive = false;
    return n;
}

}  // namespace geode::scene::movement
