#include "scene/movement/Shape.hpp"

#include <algorithm>
#include <cmath>

namespace geode::scene::movement {

namespace {

// elapsed / duration as a 0..1 clock, done within kTimeSlack of the end; a duration of 0 is done at once.
float clockOf(float elapsed, float duration) {
    if (!(duration > 0.0f) || !std::isfinite(duration)) return 1.0f;
    if (elapsed >= duration - kTimeSlack) return 1.0f;
    return std::clamp(elapsed / duration, 0.0f, 1.0f);
}

Ease easeOf(int v) { return (v >= 0 && v < kEaseCount) ? static_cast<Ease>(v) : Ease::Linear; }

}  // namespace

float ease(Ease curve, float t) {
    t = std::isfinite(t) ? std::clamp(t, 0.0f, 1.0f) : 0.0f;
    switch (curve) {
        case Ease::Smooth:
            return t * t * (3.0f - 2.0f * t);
        case Ease::InOutCubic: {
            if (t < 0.5f) return 4.0f * t * t * t;
            const float u = -2.0f * t + 2.0f;
            return 1.0f - 0.5f * u * u * u;
        }
        case Ease::InCubic:
            return t * t * t;
        case Ease::OutCubic: {
            const float u = 1.0f - t;
            return 1.0f - u * u * u;
        }
        case Ease::Linear:
        default:
            return t;
    }
}

void beginMorph(MorphState& s, std::uint16_t to) {
    const std::uint16_t shown = s.progress >= 0.5f ? s.to : s.from;
    if (!s.running() && shown == to) return;
    s.from = shown;
    s.to = to;
    s.elapsed = 0.0f;
    s.linear = 0.0f;
    s.progress = 0.0f;
}

void stepMorph(MorphState& s, const MorphParams& p, float dt) {
    if (!s.running()) return;
    s.elapsed += clampDt(dt);
    s.linear = clockOf(s.elapsed, p.duration);
    s.progress = ease(easeOf(p.ease), s.linear);
}

void beginCollapse(CollapseState& s, const Body& b) {
    s.active = true;
    s.done = false;
    s.elapsed = 0.0f;
    s.startScale = b.scale;
    s.applied = 0.0f;
}

void stepCollapse(Body& b, CollapseState& s, const CollapseParams& p, float dt) {
    if (!s.active) return;
    s.elapsed += clampDt(dt);
    const float lin = clockOf(s.elapsed, p.duration);
    const float progress = ease(easeOf(p.ease), lin);
    const float twist = std::isfinite(p.twist) ? p.twist : 0.0f;
    // The twist is turned in slices of the eased progress, about the body's own up (a right multiply),
    // so their sum is exactly `twist` whatever the frames were.
    const float turn = twist * (progress - s.applied);
    if (turn != 0.0f) b.orient = qnormalize(mul(b.orient, axisAngle({0.0f, 1.0f, 0.0f}, turn)));
    s.applied = progress;
    b.scale = lin >= 1.0f ? 0.0f : s.startScale * (1.0f - progress);
    if (lin >= 1.0f) {
        s.active = false;
        s.done = true;
        if (p.kill) b.alive = false;
    }
}

}  // namespace geode::scene::movement
