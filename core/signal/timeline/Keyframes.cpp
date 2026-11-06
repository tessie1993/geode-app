#include "signal/timeline/Keyframes.hpp"

#include <algorithm>
#include <cmath>

namespace geode::signal::timeline {

namespace {
constexpr int kNewtonSteps = 8;
constexpr int kBisectSteps = 24;
constexpr float kSolveEpsilon = 1e-5f;
constexpr float kSlopeEpsilon = 1e-6f;

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// Kotlin's coerceIn passes NaN through; the eval paths below must not hand NaN to the solver.
float clamp01OrZero(float v) { return std::isnan(v) ? 0.0f : clamp01(v); }

float lerp(float from, float to, float t) { return from + (to - from) * t; }

Value blend(const Value& from, const Value& to, float t) {
    if (from.kind != to.kind) return from;
    switch (from.kind) {
        case ValueKind::Toggle:
        case ValueKind::Choice:
            return from;
        default:
            break;
    }
    Value out = from;
    const int n = Value::components(from.kind);
    for (int i = 0; i < n; ++i) out.v[i] = lerp(from.v[i], to.v[i], t);
    return out;
}
}  // namespace

BezierCurve::BezierCurve(float c1x, float c1y, float c2x, float c2y) {
    const float x1 = clamp01(c1x);
    const float x2 = clamp01(c2x);
    xC_ = 3.0f * x1;
    xB_ = 3.0f * (x2 - x1) - xC_;
    xA_ = 1.0f - xC_ - xB_;
    yC_ = 3.0f * c1y;
    yB_ = 3.0f * (c2y - c1y) - yC_;
    yA_ = 1.0f - yC_ - yB_;
}

float BezierCurve::eval(float x) const { return axisY(solveForX(clamp01OrZero(x))); }

float BezierCurve::solveForX(float x) const {
    float s = x;
    for (int i = 0; i < kNewtonSteps; ++i) {
        const float slope = slopeX(s);
        // A near-flat slope would send Newton off into the weeds; bisection picks it up below.
        if (slope > kSlopeEpsilon) s = clamp01(s - (axisX(s) - x) / slope);
    }
    return std::fabs(axisX(s) - x) < kSolveEpsilon ? s : bisectForX(x);
}

float BezierCurve::bisectForX(float x) const {
    float low = 0.0f;
    float high = 1.0f;
    float s = x;
    for (int i = 0; i < kBisectSteps; ++i) {
        s = (low + high) * 0.5f;
        if (axisX(s) < x) {
            low = s;
        } else {
            high = s;
        }
    }
    return s;
}

BezierCurve easeCurve(Ease shape) {
    switch (shape) {
        case Ease::In: return {0.42f, 0.0f, 1.0f, 1.0f};
        case Ease::Out: return {0.0f, 0.0f, 0.58f, 1.0f};
        case Ease::InOut: return {0.42f, 0.0f, 0.58f, 1.0f};
        case Ease::Smooth: return {0.4f, 0.0f, 0.2f, 1.0f};
    }
    return {};
}

float Interp::fraction(float progress) const {
    switch (kind) {
        case InterpKind::Hold: return 0.0f;
        case InterpKind::Linear: return clamp01OrZero(progress);
        case InterpKind::Curve: return curve.eval(progress);
    }
    return 0.0f;
}

int Value::components(ValueKind kind) {
    switch (kind) {
        case ValueKind::Scalar: return 1;
        case ValueKind::Vector2: return 2;
        case ValueKind::Colour: return 4;
        case ValueKind::Toggle:
        case ValueKind::Choice: return 1;
    }
    return 1;
}

void Track::add(const Key& key) {
    const auto at = std::lower_bound(keys_.begin(), keys_.end(), key.atMs,
                                     [](const Key& k, int64_t t) { return k.atMs < t; });
    if (at != keys_.end() && at->atMs == key.atMs) {
        *at = key;
    } else {
        keys_.insert(at, key);
    }
}

int Track::indexAtOrBefore(int64_t timeMs) const {
    const auto after = std::upper_bound(keys_.begin(), keys_.end(), timeMs,
                                        [](int64_t t, const Key& k) { return t < k.atMs; });
    return static_cast<int>(after - keys_.begin()) - 1;
}

bool Track::valueAt(int64_t timeMs, Value* out) const {
    if (keys_.empty() || !out) return false;
    const int index = indexAtOrBefore(timeMs);
    const int last = static_cast<int>(keys_.size()) - 1;
    if (index < 0) {
        *out = keys_.front().value;
    } else if (index == last) {
        *out = keys_[static_cast<size_t>(index)].value;
    } else {
        const Key& from = keys_[static_cast<size_t>(index)];
        const Key& to = keys_[static_cast<size_t>(index) + 1];
        const int64_t span = std::max<int64_t>(1, to.atMs - from.atMs);
        const float progress = clamp01(static_cast<float>(timeMs - from.atMs) / static_cast<float>(span));
        *out = blend(from.value, to.value, from.interp.fraction(progress));
    }
    return true;
}

}  // namespace geode::signal::timeline
