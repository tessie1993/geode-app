#include "viz/Reactions.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "viz/ParamMatrix.hpp"

namespace geode::viz {

namespace {

// A fired trigger always crosses Step/Jump/Toggle's 0.5 edge, however soft the hit; its strength
// shows in how far above 0.5 it lands.
constexpr float kTriggerFloor = 0.5f;

// A NaN or infinite frame time (a clock glitch) is a frame that did not
// happen; anything else is clamped to [0, 0.1] s so one long stall cannot
// fling an envelope or an accumulator.
float sanitizeDt(float dt) { return std::isfinite(dt) ? std::clamp(dt, 0.0f, 0.1f) : 0.0f; }

float onePoleK(float dt, float tau) { return 1.0f - std::exp(-std::max(dt, 0.0f) / std::max(tau, 1e-4f)); }

float field(const GeodeSoundFrame& f, size_t offset) {
    const float v = *reinterpret_cast<const float*>(reinterpret_cast<const unsigned char*>(&f) + offset);
    return std::isfinite(v) ? v : 0.0f;
}

}  // namespace

void ReactSignals::reset() {
    values_.fill(0.0f);
    lastClock_.fill(0.0f);
    clocksPrimed_ = false;
    onsetCount_ = 0.0f;
    cutCount_ = 0.0f;
}

void ReactSignals::step(const GeodeSoundFrame& f, float dt) {
    dt = sanitizeDt(dt);
    using matrix::SourceKind;
    for (const matrix::SourceRow& spec : matrix::sources()) {
        const size_t k = static_cast<size_t>(spec.id);
        const float x = field(f, spec.offset);
        float& v = values_[k];
        switch (spec.kind) {
            case SourceKind::Relative: v = std::clamp(0.5f * x, 0.0f, 1.0f); break;
            case SourceKind::Unit: v = std::clamp(x, 0.0f, 1.0f); break;
            case SourceKind::Signed: v = std::clamp(x, -1.0f, 1.0f); break;
            case SourceKind::Clock: {
                // A wrap (an hour, or 4096 counts) reads as one still frame, not a backwards leap.
                const float delta = clocksPrimed_ ? x - lastClock_[k] : 0.0f;
                lastClock_[k] = x;
                v = dt > 0.0f && delta > 0.0f ? std::clamp(delta / dt * spec.scale, 0.0f, 1.0f) : (dt > 0.0f ? 0.0f : v);
                break;
            }
            case SourceKind::Trigger:
                v *= std::exp(-dt / spec.scale);
                if (x > 0.0f) v = std::max(v, kTriggerFloor + (1.0f - kTriggerFloor) * std::clamp(x, 0.0f, 1.0f));
                break;
        }
    }
    clocksPrimed_ = true;
    if (f.onset > 0.0f) onsetCount_ = std::fmod(onsetCount_ + 1.0f, kCountWrap);
    if (f.hardCut > 0.0f) cutCount_ = std::fmod(cutCount_ + 1.0f, kCountWrap);
}

void ReactionEngine::reset() {
    signals_.reset();
    acc_.fill(0.0f);
    out_.fill(0.0f);
    high_.fill(false);
    for (int i = 0; i < kRoutes; ++i) jumpSeed_[static_cast<size_t>(i)] = 0x9E3779B9u * static_cast<unsigned int>(i + 1);
}

// The next held value for a Jump route, -1..1, seeded per route so a replayed
// feature sequence (an export) draws the same values. Each draw lands at least
// half a unit from the last, so a jump is always visible.
float ReactionEngine::nextJump(int route) {
    unsigned int& x = jumpSeed_[static_cast<size_t>(route)];
    if (x == 0u) x = 0x9E3779B9u * static_cast<unsigned int>(route + 1);
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    const float u = static_cast<float>(x >> 8) / 16777216.0f;  // 0..1
    const float last = acc_[static_cast<size_t>(route)];
    float v = 2.0f * u - 1.0f;
    if (std::fabs(v - last) < 0.5f) v = v > 0.0f ? v - 1.0f : v + 1.0f;
    return std::clamp(v, -1.0f, 1.0f);
}

// Triangle wave of period 4 through -1..1: continuous, bounded, slope 1 at 0.
float ReactionEngine::fold(float accumulator) {
    const float t = std::fmod(accumulator + 1.0f, 4.0f);
    const float u = t < 0.0f ? t + 4.0f : t;
    return u < 2.0f ? u - 1.0f : 3.0f - u;
}

void ReactionEngine::tick(const GeodeSoundFrame& f, float dt) {
    dt = sanitizeDt(dt);
    signals_.step(f, dt);
    for (int i = 0; i < kRoutes; ++i) {
        const ReactRoute& r = routes[static_cast<size_t>(i)];
        const size_t k = static_cast<size_t>(i);
        if (!r.enabled || r.target == LfoTarget::None) {
            out_[k] = 0.0f;
            continue;
        }
        const float src = signals_.value(r.source);
        float target = 0.0f;
        switch (r.mode) {
            case ReactMode::Drive:
                // Read by matrix::drive, which scales the target instead of adding to it.
                break;
            case ReactMode::Follow:
                target = shape(src, r.curve) * r.amount;
                break;
            case ReactMode::Step: {
                const bool high = src >= 0.5f;
                if (high && !high_[k]) acc_[k] = std::fmod(acc_[k] + r.amount, 4.0f);
                high_[k] = src >= 0.3f && (high || high_[k]);  // hysteresis: re-arm below 0.3
                target = fold(acc_[k]);
                break;
            }
            case ReactMode::Spin:
                acc_[k] = std::fmod(acc_[k] + shape(src, r.curve) * r.amount * dt, 4.0f);
                target = fold(acc_[k]);
                break;
            case ReactMode::Jump: {
                const bool high = src >= 0.5f;
                if (high && !high_[k]) acc_[k] = nextJump(i);
                high_[k] = src >= 0.3f && (high || high_[k]);
                target = acc_[k] * r.amount;
                break;
            }
            case ReactMode::Toggle: {
                const bool high = src >= 0.5f;
                if (high && !high_[k]) acc_[k] = acc_[k] > 0.5f ? 0.0f : 1.0f;
                high_[k] = src >= 0.3f && (high || high_[k]);
                target = acc_[k] * r.amount;
                break;
            }
        }
        float attack = std::max(r.attackSeconds, 0.0f);
        float release = std::max(r.releaseSeconds, 0.0f);
        if (isLuminanceTarget(r.target)) {
            attack = std::max(attack, kLuminanceMinAttack);
            release = std::max(release, kLuminanceMinRelease);
        }
        const float tau = std::fabs(target) > std::fabs(out_[k]) ? attack : release;
        out_[k] += (target - out_[k]) * (tau <= 1e-4f ? 1.0f : onePoleK(dt, tau));
    }
}

SceneParams ReactionEngine::apply(const SceneParams& p) const {
    const float scale = std::clamp(p.reactAmount, 0.0f, 2.0f);
    SceneParams o = p;
    if (scale <= 0.0f) return o;
    for (int i = 0; i < kRoutes; ++i) {
        const ReactRoute& r = routes[static_cast<size_t>(i)];
        ModChain chain{};
        if (!r.enabled || r.mode == ReactMode::Drive || r.target == LfoTarget::None || chainOf(r.target, chain)) continue;
        o = applyLfoTarget(o, r.target, out_[static_cast<size_t>(i)] * scale);
    }
    return o;
}

}  // namespace geode::viz
