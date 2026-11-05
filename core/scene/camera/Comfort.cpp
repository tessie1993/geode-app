#include "scene/camera/Comfort.hpp"

#include <algorithm>
#include <cmath>

namespace geode::scene::camera {

namespace {

constexpr Vec3 kWorldUp{0.0f, 1.0f, 0.0f};
// Hysteresis on "upside down", so a camera hovering on its side never counts a flip each frame.
constexpr float kInvertIn = -0.02f;
constexpr float kInvertOut = 0.02f;

float clampDt(float dt) { return std::isfinite(dt) ? std::clamp(dt, 0.0f, 0.1f) : 0.0f; }

// Turns a view about its own forward.
Quat rollBy(const Quat& q, float angle) { return qnormalize(mul(q, axisAngle({0.0f, 0.0f, 1.0f}, angle))); }

// Tips a view whose forward is more than maxElevation above or below the horizon back to it, about the
// horizontal axis across its forward (a world turn), keeping its roll about that forward.
Quat clampElevation(const Quat& q, float maxElevation) {
    const Vec3 f = fwdOf(q);
    const float e = std::asin(std::clamp(f[1], -1.0f, 1.0f));
    if (std::fabs(e) <= maxElevation) return q;
    // cross(worldUp, f) is horizontal and across f; turning f about it by a positive angle lowers f's
    // height on either side of the horizon, so the angle takes e's sign. Straight up or down, the view's
    // own right is that axis.
    const Vec3 across = normalize(cross(kWorldUp, f), normalize(rightOf(q), {1.0f, 0.0f, 0.0f}));
    const float angle = (e > 0.0f ? 1.0f : -1.0f) * (std::fabs(e) - maxElevation);
    return qnormalize(mul(axisAngle(across, angle), q));
}

}  // namespace

ComfortLimits limitsFor(Comfort level) {
    switch (level) {
        case Comfort::Calm:
            return {kPi / 4.0f, 0, 1.5f, 2.0f};
        case Comfort::Wild:
            return {kSafetyMaxTurn, -1, 3.0f, kSafetyCutGap};
        case Comfort::Normal:
        default:
            return {kPi / 2.0f, 2, 3.0f, 2.0f};
    }
}

void ComfortStage::reset() { *this = ComfortStage{}; }

void ComfortStage::prime(const Pose& p) {
    primed_ = true;
    last_ = p;
    posGlide_ = {0.0f, 0.0f, 0.0f};
    excessKnown_ = false;
    inverted_ = dot(upOf(p.orient), kWorldUp) < kInvertIn;
}

int ComfortStage::recentFlips() const {
    int n = 0;
    for (int i = 0; i < flipCount_; ++i) {
        if (clock_ - flipTimes_[static_cast<std::size_t>(i)] < kFlipWindowSeconds) ++n;
    }
    return n;
}

void ComfortStage::recordFlip() {
    flipTimes_[static_cast<std::size_t>(flipHead_)] = clock_;
    flipHead_ = (flipHead_ + 1) % static_cast<int>(flipTimes_.size());
    flipCount_ = std::min(flipCount_ + 1, static_cast<int>(flipTimes_.size()));
}

ComfortStage::Output ComfortStage::step(const Input& in, Comfort level, float dt) {
    dt = clampDt(dt);
    Output out;
    if (!primed_ || in.primeOnly) {
        prime(in.target);
        out.pose = in.target;
        out.untouched = true;
        return out;
    }
    clock_ += dt;
    sinceCut_ += dt;

    // The safety clamp bounds every level; Rails keeps today's law under it and nothing else.
    const ComfortLimits comfort = limitsFor(level);
    const float maxTurn = in.safetyOnly ? kSafetyMaxTurn : std::min(comfort.maxTurn, kSafetyMaxTurn);
    const float cutGap = std::max(comfort.cutGap, kSafetyCutGap);

    Pose t = in.target;
    bool touched = false;

    // ---- Cut spacing --------------------------------------------------------------------------------
    if (in.cut) {
        if (sinceCut_ >= cutGap) {
            out.cutTaken = true;
            sinceCut_ = 0.0f;
            ++cutsTaken_;
            posGlide_ = {0.0f, 0.0f, 0.0f};
        } else {
            // Too soon: play the jump as motion. The position starts where the camera is and eases over;
            // the turn goes under the speed cap below.
            posGlide_ = sub(last_.pos, t.pos);
        }
    }

    // ---- Horizon recovery and the flip budget ---------------------------------------------------------
    // Where no flip is allowed the view is kept off the vertical (so its roll is always defined) and its
    // roll short of a quarter turn: then its up vector stays above the horizon.
    const bool noFlip = !in.safetyOnly && comfort.flipsPerMinute >= 0 && !inverted_ && recentFlips() >= comfort.flipsPerMinute;
    if (!in.safetyOnly) {
        if (noFlip) {
            const Quat lowered = clampElevation(t.orient, kNoFlipElevation);
            if (angleBetween(lowered, t.orient) > 1e-6f) {
                t.orient = lowered;
                touched = true;
            }
        }
        float roll = 0.0f;
        if (rollFromLevel(t.orient, roll)) {
            const float excess = wrapAngle(roll - in.referenceRoll);
            if (!excessKnown_ || in.rollHeld || out.cutTaken) {
                outputExcess_ = excess;
            } else {
                // Follow what the drivers do to the roll, and let the rest bleed off: ~95 % in recoverySeconds.
                outputExcess_ = wrapAngle(outputExcess_ + wrapAngle(excess - driverExcess_));
                outputExcess_ *= std::exp(-3.0f * dt / std::max(comfort.recoverySeconds, 0.05f));
            }
            driverExcess_ = excess;
            excessKnown_ = true;

            float desired = wrapAngle(in.referenceRoll + outputExcess_);
            if (noFlip) {
                desired = std::clamp(desired, -kNoFlipRoll, kNoFlipRoll);
                outputExcess_ = wrapAngle(desired - in.referenceRoll);
            }
            const float delta = wrapAngle(desired - roll);
            if (std::fabs(delta) > 1e-6f) {
                t.orient = rollBy(t.orient, delta);
                touched = true;
            }
        }
    }

    // ---- Turn speed -----------------------------------------------------------------------------------
    if (!out.cutTaken) {
        const float maxStep = maxTurn * dt;
        if (angleBetween(last_.orient, t.orient) > maxStep) {
            t.orient = stepToward(last_.orient, t.orient, maxStep);
            touched = true;
            // A capped step between two upright views can pass over the top or dip past level: where no
            // flip is allowed, the step is held to the same bounds as the target.
            if (noFlip) {
                t.orient = clampElevation(t.orient, kNoFlipElevation);
                float roll = 0.0f;
                if (rollFromLevel(t.orient, roll)) t.orient = rollBy(t.orient, std::clamp(roll, -kNoFlipRoll, kNoFlipRoll) - roll);
            }
        }
    }

    // ---- A refused cut's position glide ---------------------------------------------------------------
    if (posGlide_[0] != 0.0f || posGlide_[1] != 0.0f || posGlide_[2] != 0.0f) {
        t.pos = add(t.pos, posGlide_);
        posGlide_ = scale(posGlide_, std::exp(-3.0f * dt / kCutGlideSeconds));
        if (dot(posGlide_, posGlide_) < 1e-10f) posGlide_ = {0.0f, 0.0f, 0.0f};
        touched = true;
    }

    // ---- Count flips on what is drawn -----------------------------------------------------------------
    const float upness = dot(upOf(t.orient), kWorldUp);
    if (!inverted_ && upness < kInvertIn) {
        inverted_ = true;
        recordFlip();
        out.flipped = true;
    } else if (inverted_ && upness > kInvertOut) {
        inverted_ = false;
    }

    last_ = t;
    out.pose = t;
    out.untouched = !touched;
    return out;
}

}  // namespace geode::scene::camera
