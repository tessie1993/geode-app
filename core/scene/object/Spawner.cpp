#include "scene/object/Spawner.hpp"

#include <algorithm>
#include <cmath>

#include "scene/movement/Body.hpp"

namespace geode::scene::object {

namespace {

using movement::mix;
using movement::Rng;

// A uniform direction: z uniform in [-1, 1] and an angle round it (Archimedes' hat-box theorem).
Vec3 randomDirection(Rng& rng) {
    const float z = rng.signedNext();
    const float a = rng.next() * kTwoPi;
    const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
    return {r * std::cos(a), r * std::sin(a), z};
}

float jittered(float value, float fraction, Rng& rng) { return value * (1.0f + fraction * rng.signedNext()); }

}  // namespace

std::uint32_t firingSeed(std::uint64_t playSeed, std::size_t rule, std::uint32_t firing) {
    std::uint32_t h = mix(static_cast<std::uint32_t>(playSeed) ^ mix(static_cast<std::uint32_t>(playSeed >> 32) + 0x9e3779b9u));
    h = mix(h ^ (static_cast<std::uint32_t>(rule) + 1u) * 0x85ebca6bu);
    h = mix(h ^ (firing + 1u) * 0xc2b2ae35u);
    return h;
}

int firingCount(const SpawnRule& rule, std::uint32_t seed) {
    Rng rng{seed, 0};
    const int extra = rule.countJitter > 0 ? std::min(static_cast<int>(rng.next() * static_cast<float>(rule.countJitter + 1)), rule.countJitter) : 0;
    return std::max(0, rule.count) + extra;
}

ObjectSpec emitOne(const SpawnRule& rule, std::uint32_t seed, int index, int count, const camera::Pose& cam) {
    // Each child has its own stream, so a cap that stops a firing early never changes the children
    // before it.
    Rng rng{mix(seed ^ (static_cast<std::uint32_t>(index) + 1u) * 0x27d4eb2fu), 0};
    ObjectSpec s = rule.base;

    Vec3 offset{0.0f, 0.0f, 0.0f};
    Vec3 dir{0.0f, 0.0f, 1.0f};
    switch (rule.shape) {
        case EmitShape::Point:
            dir = randomDirection(rng);
            break;
        case EmitShape::Sphere:
            dir = randomDirection(rng);
            offset = scale(dir, rule.radius);
            break;
        case EmitShape::Ring: {
            const float n = static_cast<float>(std::max(count, 1));
            const float a = kTwoPi * (static_cast<float>(index) + 0.25f * rng.signedNext()) / n;
            dir = {std::cos(a), std::sin(a), 0.0f};
            offset = scale(dir, rule.radius);
            break;
        }
        case EmitShape::Box: {
            offset = {rng.signedNext() * rule.radius, rng.signedNext() * rule.radius, rng.signedNext() * rule.radius};
            dir = normalize(offset, randomDirection(rng));
            break;
        }
    }
    const float speed = std::max(0.0f, jittered(rule.speed, rule.speedJitter, rng));
    const Vec3 spinAxis = randomDirection(rng);
    const float spinRate = rule.spinMax * rng.next();

    Vec3 pos = add(rule.centre, offset);
    Vec3 vel = scale(dir, speed);
    Vec3 spin = scale(spinAxis, spinRate);
    switch (rule.frame) {
        case EmitFrame::World:
            s.anchor = s.anchor == Anchor::Camera ? Anchor::World : s.anchor;
            break;
        case EmitFrame::CameraFront:
            pos = add(cam.pos, rotate(cam.orient, pos));
            vel = rotate(cam.orient, vel);
            spin = rotate(cam.orient, spin);
            s.anchor = s.anchor == Anchor::Camera ? Anchor::World : s.anchor;
            break;
        case EmitFrame::Camera:
            s.anchor = Anchor::Camera;
            break;
    }
    s.pos = add(s.pos, pos);
    s.vel = add(s.vel, vel);
    s.spin = add(s.spin, spin);
    s.scale = std::max(0.0f, jittered(s.scale, rule.scaleJitter, rng));
    s.lifetime = std::max(0.0f, jittered(rule.lifetime, rule.lifetimeJitter, rng));
    s.palette = s.palette + rule.paletteSpread * rng.next();
    s.palette -= std::floor(s.palette);
    s.birth = std::max(0.0f, rule.stagger) * static_cast<float>(index);
    s.death = Death::Time;
    if (rule.shapeMask != 0) {
        int options[kShapeTypeCount];
        int n = 0;
        for (int t = 0; t < kShapeTypeCount; ++t) {
            if (rule.shapeMask & (1u << t)) options[n++] = t;
        }
        if (n > 0) s.shape = static_cast<ShapeType>(options[std::min(static_cast<int>(rng.next() * static_cast<float>(n)), n - 1)]);
    }
    return s;
}

}  // namespace geode::scene::object
