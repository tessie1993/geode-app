#include "viz/PhoneMotion.hpp"

#include <algorithm>
#include <cmath>

namespace geode::viz {

namespace {

constexpr float kTwoPi = 6.2831853f;
constexpr float kPi = 3.14159265f;

float finite(float v) { return std::isfinite(v) ? v : 0.0f; }

// A sensor's own dt: the gap between two of its events, clamped so a stalled sensor (or the first
// event, which has none) cannot fling a filter.
float sampleDt(float dt) { return std::isfinite(dt) ? std::clamp(dt, 0.0f, 0.1f) : 0.0f; }

float wrapPi(float a) { return a - kTwoPi * std::floor((a + kPi) / kTwoPi); }

// A damped spring toward target, semi-implicit Euler in substeps short enough to stay stable at any
// frame rate. damping < 1 overshoots: the camera's lean settles like something with weight.
void spring(float& x, float& v, float target, float dt, float seconds, float damping) {
    const float w = kTwoPi / std::max(seconds, 1e-3f);
    const int steps = std::max(1, static_cast<int>(std::ceil(dt / 0.008f)));
    const float h = dt / static_cast<float>(steps);
    for (int i = 0; i < steps; ++i) {
        v += (w * w * (target - x) - 2.0f * damping * w * v) * h;
        x += v * h;
    }
}

}  // namespace

void PhoneMotion::reset() { *this = PhoneMotion(); }

void PhoneMotion::writeDown(float x, float y, float dtSeconds) {
    downTargetX_ = std::clamp(finite(x), -1.0f, 1.0f);
    downTargetY_ = std::clamp(finite(y), -1.0f, 1.0f);
    sinceSample_ = 0.0f;
    sliceSeconds_ += dtSeconds;
}

void PhoneMotion::writeRotation(float downX, float downY, float downZ, float twistRate, float dtSeconds) {
    (void)downZ;
    const float dt = sampleDt(dtSeconds);
    writeDown(downX, downY, dt);
    sinceRotation_ = 0.0f;
    // Speed CHOP: the slice keeps the angle turned, so the frame's twist is what the hand really did.
    sliceTurn_ += finite(twistRate) * dt;
}

void PhoneMotion::writeAccel(float ax, float ay, float az, float dtSeconds) {
    const float dt = sampleDt(dtSeconds);
    const std::array<float, 3> a{finite(ax), finite(ay), finite(az)};
    // Android's own recipe (Motion sensors › Use the accelerometer): a low-pass isolates gravity, and
    // what is left is the hand. The time constant is per second, so it holds at any sensor rate.
    if (!accelPrimed_) {
        accelGravity_ = a;
        accelPrimed_ = true;
    }
    const float k = 1.0f - std::exp(-dt / kAccelGravitySeconds);
    float lin2 = 0.0f;
    for (size_t i = 0; i < 3; ++i) {
        accelGravity_[i] += (a[i] - accelGravity_[i]) * k;
        const float d = a[i] - accelGravity_[i];
        lin2 += d * d;
    }
    sliceShake_ = std::max(sliceShake_, std::sqrt(lin2));
    sinceSample_ = 0.0f;

    // No rotation vector on this phone (or none lately): the accelerometer's gravity is the down
    // vector, and a turn in the screen's plane is the change of its angle there. That only reads while
    // the phone stands up; lying flat, it has no in-plane angle to turn.
    if (sinceRotation_ < kRotationFreshSeconds) {
        accelAngleValid_ = false;
        return;
    }
    const float g = std::sqrt(accelGravity_[0] * accelGravity_[0] + accelGravity_[1] * accelGravity_[1] +
                              accelGravity_[2] * accelGravity_[2]);
    if (g < 1e-3f) return;
    // The accelerometer reads the push that holds the phone up, so down is its opposite.
    const float dx = -accelGravity_[0] / g;
    const float dy = -accelGravity_[1] / g;
    writeDown(dx, dy, dt);
    if (dx * dx + dy * dy > 0.25f) {
        const float angle = std::atan2(dy, dx);
        // The ground turning one way in the screen is the phone turning the other.
        if (accelAngleValid_) sliceTurn_ -= wrapPi(angle - lastAccelAngle_);
        lastAccelAngle_ = angle;
        accelAngleValid_ = true;
    } else {
        accelAngleValid_ = false;
    }
}

void PhoneMotion::step(float amount, float dt, bool reducedMotion) {
    using sound::follow;
    dt = std::isfinite(dt) ? std::clamp(dt, 0.0f, 0.1f) : 0.0f;
    amount = std::clamp(finite(amount), 0.0f, 1.0f);
    sinceSample_ += dt;
    sinceRotation_ += dt;
    f_.live = sinceSample_ < kLiveSeconds;

    // Down, in the screen's plane. With no sensor the target is rest, so a hidden surface or an
    // unregistered listener eases everything back instead of freezing it mid-fall.
    float tx = 0.0f, ty = 0.0f;
    if (f_.live) {
        tx = downTargetX_;
        ty = downTargetY_;
        const float len = std::sqrt(tx * tx + ty * ty);
        // Lying flat, the in-plane part is noise: fade it in past the dead zone, without a step.
        const float keep = std::clamp((len - kFlatDeadZone) / (2.0f * kFlatDeadZone), 0.0f, 1.0f);
        tx *= keep;
        ty *= keep;
        if (!primed_) {
            downX_ = tx;
            downY_ = ty;
            heldY_ = ty;
            primed_ = true;
        }
    } else {
        primed_ = false;
    }
    downX_ = follow(downX_, tx, kGravitySeconds, dt);
    downY_ = follow(downY_, ty, kGravitySeconds, dt);
    f_.gravityX = downX_ * amount;
    f_.gravityY = downY_ * amount;

    // Tilt as a source. Left/right is absolute: people hold a phone level. Toward/away is from the
    // pose it is held in, which drifts back over seconds, because nobody holds it at one angle.
    heldY_ = follow(heldY_, downY_, kHeldPoseSeconds, dt);
    f_.tiltX = std::clamp(downX_, -1.0f, 1.0f);
    f_.tiltY = std::clamp((downY_ - heldY_) * 2.0f, -1.0f, 1.0f);

    // Parallax: the camera leans toward the tilt late, overshoots, settles.
    const float leanScale = amount * (reducedMotion ? 0.5f : 1.0f);
    spring(leanX_, leanVX_, f_.tiltX * leanScale, dt, kParallaxSeconds, kParallaxDamping);
    spring(leanY_, leanVY_, f_.tiltY * leanScale, dt, kParallaxSeconds, kParallaxDamping);
    f_.parallaxX = std::clamp(leanX_, -1.0f, 1.0f);
    f_.parallaxY = std::clamp(leanY_, -1.0f, 1.0f);

    // Shake: the slice's peak, through Lag (instant rise, slow fall) for the level and through Trigger
    // (threshold, hysteresis, re-trigger delay) for the hit, exactly as the sound engine treats a drum.
    const float level = std::clamp((sliceShake_ - kShakeFloor) / (kShakeFull - kShakeFloor), 0.0f, 1.0f);
    f_.shake = sound::clamp01(follow(f_.shake, level, level > f_.shake ? kShakeRiseSeconds : kShakeFallSeconds, dt));
    f_.shakeHit = shakeTrigger_.step(level, kShakeOn, kShakeRefractory, dt);
    if (f_.shakeHit > 0.0f) f_.shakeCount = (f_.shakeCount + 1) % kCountWrap;

    // Twist: the slice's turn over its time (Slope), Lagged a touch for the source; past a dead zone a
    // flick flings the world the same way, and friction runs it down.
    const float span = std::max(sliceSeconds_, dt);
    const float rate = f_.live && span > 0.0f ? sliceTurn_ / span : 0.0f;
    f_.twist = std::clamp(follow(f_.twist, rate / kTwistFull, kTwistSeconds, dt), -1.0f, 1.0f);
    const float past = std::max(0.0f, std::fabs(rate) - kTwistDeadZone) * (rate < 0.0f ? -1.0f : 1.0f);
    const float maxRate = kMaxSpinRate * amount * (reducedMotion ? 0.0f : 1.0f);
    float spinRate = f_.spinRate * std::exp(-dt / kSpinFrictionSeconds) + past * kSpinGain * dt * amount;
    spinRate = std::clamp(spinRate, -maxRate, maxRate);
    f_.spinRate = spinRate;
    // Kept in -pi..pi. Once the spin has nearly run down the world rights itself, the short way
    // round, so a flick never leaves the picture tipped over for good.
    float spin = wrapPi(f_.spin + spinRate * dt);
    const float settle = std::clamp(1.0f - std::fabs(spinRate) / kSettleBelowRate, 0.0f, 1.0f);
    spin = follow(spin, 0.0f, kSettleSeconds / std::max(settle, 1e-3f), dt);
    f_.spin = spin;

    sliceShake_ = 0.0f;
    sliceTurn_ = 0.0f;
    sliceSeconds_ = 0.0f;
}

}  // namespace geode::viz
