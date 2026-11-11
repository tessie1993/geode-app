#include "viz/fluid/RippleMath.hpp"

#include <algorithm>
#include <cmath>

#include "sound/DriveGain.hpp"
#include "viz/LiveSignal.hpp"

namespace geode::viz::fluid::ripple {

namespace {
constexpr float kTau = 6.2831853f;
constexpr float kGoldenAngle = 2.3999631f;
constexpr double kGoldenFract = 0.6180339887;
constexpr float kMinAmplitude = 0.004f;
constexpr float kLipWidth = 0.55f;    // the window ahead of the crest, in wavelengths
constexpr float kTrainWidth = 1.15f;  // and behind it
constexpr float kAhead = 1.7f;        // past this the window is under 1e-4
constexpr float kBehind = -4.0f;      // and behind this
constexpr float kCoreRadius = 0.3f;   // rounds the centre, in wavelengths
constexpr float kRise = 0.8f;         // a drop is born this far behind its centre, so it wells up
constexpr float kSpreadLength = 1.5f;
constexpr float kMergeSeconds = 0.25f;
constexpr float kMergeLength = 0.75f;
constexpr float kReplaceRatio = 0.12f;
constexpr float kWakeStride = 0.2f;
constexpr float kWakeFullSpeed = 2.0f;

float softRadius(float dx, float dy, float core) { return std::sqrt(dx * dx + dy * dy + core * core) - core; }
float window(float s) {
    const float w = s > 0.0f ? kLipWidth : kTrainWidth;
    return std::exp(-(s * s) / (w * w));
}
}  // namespace

float lifetimeFor(float damping) {
    const float u = (std::clamp(damping, 0.9f, 0.999f) - 0.9f) / 0.099f;
    return std::min(kMinLifetime + (kMaxLifetime - kMinLifetime) * u, kMaxLifetime);
}

float speedFor(float waveSpeed) { return 0.6f * std::clamp(waveSpeed, 0.2f, 2.0f); }

float wavelengthFor(float radius) { return std::clamp(2.2f * radius, 0.07f, 0.26f); }

float envelope(float age, float lifetime) {
    if (!(lifetime > 0.0f) || !(age < lifetime)) return 0.0f;
    const float left = 1.0f - std::max(age, 0.0f) / lifetime;
    return left * left;
}

float spread(float front, float wavelength) { return 1.0f / std::sqrt(1.0f + std::max(front, 0.0f) / (kSpreadLength * wavelength)); }

float peak(const Drop& d) { return std::fabs(d.amplitude) * envelope(d.age, d.lifetime) * spread(d.front, d.wavelength); }

float profile(float s) { return std::cos(kTau * s) * window(s); }

float profileSlope(float s) {
    const float w = s > 0.0f ? kLipWidth : kTrainWidth;
    return (-kTau * std::sin(kTau * s) - 2.0f * s / (w * w) * std::cos(kTau * s)) * window(s);
}

float height(const Wavelet& w, float x, float y) {
    const float s = (softRadius(x - w.x, y - w.y, kCoreRadius / w.invWavelength) - w.front) * w.invWavelength;
    if (s > kAhead || s < kBehind) return 0.0f;
    return w.amplitude * profile(s);
}

float heightRate(const Wavelet& w, float x, float y, float speed) {
    const float s = (softRadius(x - w.x, y - w.y, kCoreRadius / w.invWavelength) - w.front) * w.invWavelength;
    if (s > kAhead || s < kBehind) return 0.0f;
    return -w.amplitude * profileSlope(s) * speed * w.invWavelength;
}

float inkDissipation(float dissipation, float dt) { return std::clamp(1.0f - std::clamp(dissipation, 0.0f, 8.0f) * dt, 0.0f, 1.0f); }

void Surface::configure(float speed, float lifetime) {
    speed_ = std::max(speed, 0.0f);
    lifetime_ = std::clamp(lifetime, kMinLifetime, kMaxLifetime);
}

bool Surface::add(float x, float y, float radius, float amplitude) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(radius) || !std::isfinite(amplitude)) return false;
    const float a = std::clamp(amplitude, -kMaxAmplitude, kMaxAmplitude);
    if (std::fabs(a) < kMinAmplitude) return false;
    const float wavelength = wavelengthFor(radius);
    int weakest = -1;
    float weakestPeak = 0.0f;
    for (int i = 0; i < count_; ++i) {
        const Drop& d = drops_[static_cast<size_t>(i)];
        const float reach = kMergeLength * std::max(wavelength, d.wavelength);
        const float dx = d.x - x;
        const float dy = d.y - y;
        if (d.age < kMergeSeconds && dx * dx + dy * dy < reach * reach) return false;
        const float p = peak(d);
        if (weakest < 0 || p < weakestPeak) {
            weakest = i;
            weakestPeak = p;
        }
    }
    int slot = count_;
    if (count_ >= kMaxDrops) {
        if (weakestPeak > kReplaceRatio * std::fabs(a)) return false;
        slot = weakest;
    } else {
        count_++;
    }
    drops_[static_cast<size_t>(slot)] = {x, y, wavelength, a, 0.0f, lifetime_, -kRise * wavelength};
    return true;
}

void Surface::step(float dt) {
    if (!(dt > 0.0f)) return;
    int kept = 0;
    for (int i = 0; i < count_; ++i) {
        Drop d = drops_[static_cast<size_t>(i)];
        d.age += dt;
        d.front += speed_ * dt;
        if (d.age < d.lifetime) drops_[static_cast<size_t>(kept++)] = d;
    }
    count_ = kept;
}

float Surface::energy() const {
    float e = 0.0f;
    for (int i = 0; i < count_; ++i) {
        const float p = peak(drops_[static_cast<size_t>(i)]);
        e += p * p;
    }
    return e;
}

int Surface::wavelets(float aspect, float gain, Wavelet* out) const {
    int n = 0;
    for (int i = 0; i < count_; ++i) {
        const Drop& d = drops_[static_cast<size_t>(i)];
        const float p = peak(d) * gain;
        if (!(p > 0.0f)) continue;
        const float a = d.amplitude < 0.0f ? -p : p;
        const float inv = 1.0f / d.wavelength;
        out[n++] = {d.x, d.y, d.front, a, inv};
        const float reach = d.front + kAhead * d.wavelength;
        const float toEdge[kEdges] = {aspect - d.x, d.x + aspect, 1.0f - d.y, d.y + 1.0f};
        const float mirrorX[kEdges] = {2.0f * aspect - d.x, -2.0f * aspect - d.x, d.x, d.x};
        const float mirrorY[kEdges] = {d.y, d.y, 2.0f - d.y, -2.0f - d.y};
        for (int e = 0; e < kEdges; ++e) {
            if (toEdge[e] < 0.0f || reach <= toEdge[e]) continue;
            out[n++] = {mirrorX[e], mirrorY[e], d.front, a * kReflection, inv};
        }
    }
    return n;
}

void Wake::reset() { down_ = 0; }

void Wake::step(const float* xy, int fingers, float dt, void* ctx, Emit emit) {
    const int n = std::clamp(fingers, 0, kMaxFingers);
    for (int i = 0; i < n; ++i) {
        const size_t k = static_cast<size_t>(i);
        const float x = xy[i * 2];
        const float y = xy[i * 2 + 1];
        if (i >= down_) {
            emit(ctx, x, y, 0.45f);
            run_[k] = 0.0f;
        } else {
            const float moved = std::hypot(x - x_[k], y - y_[k]);
            run_[k] += moved;
            if (run_[k] >= kWakeStride) {
                run_[k] = 0.0f;
                const float pace = dt > 1e-4f ? std::clamp(moved / dt / kWakeFullSpeed, 0.0f, 1.0f) : 0.0f;
                emit(ctx, x, y, 0.2f + 0.4f * pace);
            }
        }
        x_[k] = x;
        y_[k] = y;
    }
    down_ = n;
}

OnsetDrops::Out OnsetDrops::step(const GeodeSoundFrame& frame, float aspect) {
    Out out;
    if (!(frame.onset > 0.0f)) return out;
    const float heard = sound::motionRate(frame);
    const float amp = (0.25f + 0.45f * live::bass(frame) / 1.5f) * live::hit(frame) * heard;
    if (!(amp > 0.0f)) return out;
    const int n = index_;
    index_ = (index_ + 1) % (1 << 20);
    const float angle = static_cast<float>(n) * kGoldenAngle;
    const float radius = 0.85f * std::sqrt(static_cast<float>(std::fmod(n * kGoldenFract, 1.0)));
    out.drop = true;
    out.x = std::cos(angle) * radius * aspect;
    out.y = std::sin(angle) * radius;
    out.amplitude = amp;
    return out;
}

}  // namespace geode::viz::fluid::ripple
