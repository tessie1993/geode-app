#include "viz/liquid/LiquidMath.hpp"

#include <algorithm>
#include <cmath>

namespace geode::viz::liquid {

namespace {
float finiteOr(float v, float fallback) { return std::isfinite(v) ? v : fallback; }

// Eases `value` toward `target` with a time constant, frame-rate independent.
float ease(float value, float target, float tau, float dt) {
    const float k = 1.0f - std::exp(-std::max(dt, 0.0f) / std::max(tau, 1e-3f));
    return value + (target - value) * k;
}
}  // namespace

Born bornFrom(int v) { return v >= 0 && v <= 3 ? static_cast<Born>(v) : Born::Rerolled; }
Age ageFrom(int v) { return v >= 0 && v <= 2 ? static_cast<Age>(v) : Age::Pure; }
Surface surfaceFrom(int v) { return v >= 0 && v <= 2 ? static_cast<Surface>(v) : Surface::Ink; }
HotPolicy hotPolicyFrom(int v) { return v >= 0 && v <= 2 ? static_cast<HotPolicy>(v) : HotPolicy::Simplify; }

Mode modeFor(ThermalTier tier, HotPolicy policy) {
    switch (tier) {
        case ThermalTier::Full:
        case ThermalTier::Eased: return Mode::Full;
        case ThermalTier::Reduced: return policy == HotPolicy::Simplify ? Mode::Lite : Mode::Full;
        case ThermalTier::Minimal:
            return policy == HotPolicy::Full ? Mode::Full : policy == HotPolicy::Pause ? Mode::Paused : Mode::Warp;
    }
    return Mode::Full;
}

Plan planFor(Mode mode) {
    switch (mode) {
        case Mode::Full: return {96, 1.0f, true, true};
        case Mode::Lite: return {48, 0.6f, true, false};
        // The warp keeps a coarse solver so touch and sound still bend the picture; nothing carries over.
        case Mode::Warp: return {32, 1.0f, false, false};
        case Mode::Off:
        case Mode::Paused: return {0, 1.0f, false, false};
    }
    return {};
}

float injectWeight(float liquidity, float dt) {
    const float l = std::clamp(finiteOr(liquidity, 0.35f), 0.0f, 1.0f);
    const float per60 = std::exp(std::log(0.5f) + (std::log(0.012f) - std::log(0.5f)) * l);
    const float frames = std::clamp(finiteOr(dt, 1.0f / 60.0f), 0.0f, 0.1f) * 60.0f;
    return 1.0f - std::pow(1.0f - per60, frames);
}

float bornRadius(float liquidity) {
    const float l = std::clamp(finiteOr(liquidity, 0.35f), 0.0f, 1.0f);
    return 0.12f + 0.08f * l;
}

void Dive::reseed(uint64_t seed) {
    rng_.seed(static_cast<uint32_t>(seed ^ (seed >> 32)));
    vpX_ = vpY_ = 0.0f;
    twist_ = 0.0f;
    surge_ = 1.0f;
    pickCourse();
}

float Dive::uniform(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng_); }

void Dive::pickCourse() {
    courseLeft_ = uniform(kMinCourse, kMaxCourse);
    // Uniform over the disc, so it does not bunch at the centre.
    const float r = kWander * std::sqrt(uniform(0.0f, 1.0f));
    const float a = uniform(0.0f, 6.2831853f);
    targetX_ = r * std::cos(a);
    targetY_ = r * std::sin(a);
    // A twist about one course in three; the rest dive straight.
    targetTwist_ = uniform(0.0f, 1.0f) < 0.35f ? uniform(-kMaxTwist, kMaxTwist) : 0.0f;
    targetSurge_ = uniform(0.6f, 1.5f);
}

DiveFrame Dive::step(float dt, float octavesPerSecond, float steerX, float steerY, float steerWeight, bool reducedMotion) {
    dt = std::clamp(finiteOr(dt, 0.0f), 0.0f, 0.1f);
    courseLeft_ -= dt;
    if (courseLeft_ <= 0.0f) pickCourse();
    const float wander = reducedMotion ? 0.0f : 1.0f;
    const float w = std::clamp(finiteOr(steerWeight, 0.0f), 0.0f, 1.0f);
    const float sx = std::clamp(finiteOr(steerX, 0.0f), -kSteerReach, kSteerReach);
    const float sy = std::clamp(finiteOr(steerY, 0.0f), -kSteerReach, kSteerReach);
    const float goalX = targetX_ * wander * (1.0f - w) + sx * w;
    const float goalY = targetY_ * wander * (1.0f - w) + sy * w;
    // Fingers pull it quickly; the wander drifts over seconds.
    const float tau = w > 0.05f ? 0.25f : 1.8f;
    vpX_ = ease(vpX_, goalX, tau, dt);
    vpY_ = ease(vpY_, goalY, tau, dt);
    twist_ = ease(twist_, reducedMotion ? 0.0f : targetTwist_, 1.5f, dt);
    surge_ = ease(surge_, targetSurge_, 1.2f, dt);
    const float rate = std::max(finiteOr(octavesPerSecond, 0.0f), 0.0f) * surge_ * (reducedMotion ? 0.5f : 1.0f);
    DiveFrame f;
    f.scale = std::exp2(std::min(rate, 2.0f) * dt);
    f.twist = twist_ * dt;
    f.vpX = vpX_;
    f.vpY = vpY_;
    return f;
}

void Gestures::reset() {
    had_ = false;
    swirl_ = 0.0f;
    steer_ = 0.0f;
}

GestureOut Gestures::step(int count, const float* xy, float dt) {
    GestureOut out;
    dt = std::clamp(finiteOr(dt, 0.0f), 0.0f, 0.1f);
    if (count == 1 && xy != nullptr && std::isfinite(xy[0]) && std::isfinite(xy[1])) {
        const float x = xy[0];
        const float y = xy[1];
        float vx = 0.0f;
        float vy = 0.0f;
        if (had_ && dt > 0.0f) {
            vx = std::clamp((x - lastX_) / dt, -kMaxSpeed, kMaxSpeed);
            vy = std::clamp((y - lastY_) / dt, -kMaxSpeed, kMaxSpeed);
        }
        const float speed = std::hypot(vx, vy);
        if (speed < kStillSpeed) {
            swirl_ = std::min(1.0f, swirl_ + dt / kSwirlRise);
        } else {
            swirl_ = std::max(0.0f, swirl_ - dt / kSwirlFall);
            // The whirlpool turns the way the finger last curved around the centre of the screen.
            const float turn = x * vy - y * vx;
            if (std::fabs(turn) > 1e-4f) spinSign_ = turn > 0.0f ? 1.0f : -1.0f;
        }
        out.push = speed >= kStillSpeed;
        out.x = x;
        out.y = y;
        out.vx = vx;
        out.vy = vy;
        had_ = true;
        lastX_ = x;
        lastY_ = y;
    } else {
        had_ = false;
        swirl_ = std::max(0.0f, swirl_ - dt / kSwirlFall);
        out.x = lastX_;
        out.y = lastY_;
    }
    out.swirl = swirl_ * spinSign_;
    if (count >= 2 && xy != nullptr) {
        const float cx = 0.5f * (xy[0] + xy[2]);
        const float cy = 0.5f * (xy[1] + xy[3]);
        if (std::isfinite(cx) && std::isfinite(cy)) {
            steerX_ = cx;
            steerY_ = cy;
            steer_ = 1.0f;
        }
    } else {
        // Let go: the dive drifts back to its own course over a second or so.
        steer_ = ease(steer_, 0.0f, 0.6f, dt);
    }
    out.steerWeight = steer_;
    out.steerX = steerX_;
    out.steerY = steerY_;
    return out;
}

void BornClock::reseed(uint64_t seed) {
    rng_.seed(static_cast<uint32_t>(seed ^ (seed >> 32)));
    dip_ = -1.0f;
    gain_ = 1.0f;
    rearm();
}

void BornClock::rearm() { fallback_ = std::uniform_real_distribution<float>(kMinFallback, kMaxFallback)(rng_); }

bool BornClock::step(float dt, bool trigger, bool locked) {
    dt = std::clamp(finiteOr(dt, 0.0f), 0.0f, 0.1f);
    bool swap = false;
    if (dip_ < 0.0f) {
        fallback_ -= dt;
        if (trigger || (!locked && fallback_ <= 0.0f)) dip_ = 0.0f;
    }
    if (dip_ >= 0.0f) {
        const float half = 0.5f * kFade;
        const float before = dip_;
        dip_ += dt;
        if (before < half && dip_ >= half) {
            swap = true;
            rollSeed_ = (static_cast<uint64_t>(rng_()) << 32) | rng_();
        }
        gain_ = std::clamp(std::fabs(dip_ - half) / half, 0.0f, 1.0f);
        if (dip_ >= kFade) {
            dip_ = -1.0f;
            gain_ = 1.0f;
            rearm();
        }
    }
    return swap;
}

float dropFlare(float flare, bool drop, float dt) {
    if (drop) return 1.0f;
    return std::max(0.0f, finiteOr(flare, 0.0f) * std::exp(-std::clamp(finiteOr(dt, 0.0f), 0.0f, 0.1f) / 0.3f));
}

}  // namespace geode::viz::liquid
