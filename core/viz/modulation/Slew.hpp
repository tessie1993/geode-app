#pragma once
#include <cmath>

namespace geode::viz::slew {

// The one smoothing step of a modulation cable: a one-pole slew with a time constant tau in seconds.
//
//   alpha = 1 - exp(-dt / tau);   y += alpha * (target - y)
//
// The same input sampled at 30, 60 or 120 frames a second gives the same y at equal times (a held target's gap
// shrinks by exp(-dt / tau) a frame, and the frames' factors multiply to exp(-t / tau)), where a fixed factor a frame
// runs twice as fast at twice the rate. dt is used as given; there is no clamp here.
//
// tau <= 0 is no smoothing: the cable follows its target. A dt of 0 or less holds y; a huge dt (or an infinite one)
// gives alpha 1, so y lands on the target and never overshoots. A non-finite tau counts as none.
//
// Where the clamp sits: a cable's target is bounded where it is made (its amount's range, a folded accumulator), and
// the single clamp to the field's range happens once where the cable's offset meets the base value (the params
// clamp and safety::apply). The slew is a convex mix of the last y and the target, so it never leaves the range the
// targets have: nothing is clamped again after it, and the base value is never smoothed (base and modulation stay apart).
//
// The first frame a cable resolves has no earlier y to come from: the caller snaps y to the target then (a cable with a
// floor of its own, the luminance routes, is the exception and keeps gliding from rest).
inline float alpha(float dt, float tau) {
    if (!(tau > 1e-4f) || !std::isfinite(tau)) return 1.0f;
    if (!(dt > 0.0f)) return 0.0f;
    return 1.0f - std::exp(-dt / tau);
}

inline float step(float y, float target, float dt, float tau) { return y + alpha(dt, tau) * (target - y); }

}  // namespace geode::viz::slew
