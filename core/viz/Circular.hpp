#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace geode::viz {

// Values on a circle, 0..1 with 1 == 0: a hue, a pitch class, a key on the circle of fifths, a beat,
// bar or phrase phase. A smoother that treats one as a line goes the long way round at the seam: a
// chord hue easing 0.95 -> 0.05 would sweep back through every colour on the wheel, and a beat phase
// that wraps every beat would run backward once a beat. Every smoother of such a value goes through
// these helpers, so the rule lives in one place (fosfora study, PR #300).
//
// Two kinds, because they move differently:
// - Hue: no direction is "forward"; take the shorter arc (0.95 -> 0.05 goes up through 1.0 = 0.0).
// - Phase: a clock that only ramps forward and wraps; follow it forward round the seam, never back.
enum class Circle : std::uint8_t { None, Hue, Phase };

namespace circular {

// Into [0, 1). A NaN or infinity reads 0, and the float rounding of a tiny negative to 1.0 reads 0.
inline float wrap01(float x) {
    if (!std::isfinite(x)) return 0.0f;
    const float w = x - std::floor(x);
    return w >= 1.0f ? 0.0f : w;
}

// The shorter signed way from `from` to `to`, in [-0.5, 0.5).
inline float arcDelta(float from, float to) { return wrap01(to - from + 0.5f) - 0.5f; }

// The forward way from `from` to `to`, in [0, 0.5): what a phase follower may move. A target behind the
// short way (the beat tracker nudging its lock back, or a follower that lags) gives 0: the follower holds
// until the ramp comes round. It never runs backward, and never races a whole lap forward either.
inline float phaseDelta(float from, float to) { return std::max(arcDelta(from, to), 0.0f); }

// The shortest-arc blend of two hues, `k` = 0 at `from`, 1 at `to`.
inline float mixHue(float from, float to, float k) { return wrap01(from + arcDelta(from, to) * k); }

// One smoothing step of a value on the circle: move `k` (0..1, a one-pole's gain) of the way from `cur`
// toward `target` along the circle's own path, wrapped back into [0, 1). A Circle::None value is a line.
inline float follow(Circle c, float cur, float target, float k) {
    switch (c) {
        case Circle::Hue: return mixHue(cur, target, k);
        case Circle::Phase: return wrap01(cur + phaseDelta(cur, target) * k);
        case Circle::None: break;
    }
    return cur + (target - cur) * k;
}

}  // namespace circular
}  // namespace geode::viz
