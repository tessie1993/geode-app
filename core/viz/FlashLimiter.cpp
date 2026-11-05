#include "viz/FlashLimiter.hpp"

#include <algorithm>
#include <cmath>

namespace geode::viz::safety {

namespace {
// A 2.2 power curve, both ways: what the composite's grade uses, so the limiter can unsqueeze exactly
// what the shader squeezed. Close enough to sRGB's piecewise curve for a threshold of 0.1.
constexpr float kGamma = 2.2f;
// No dark-side rule for red: WCAG counts a red flash at any level.
constexpr float kNoCeiling = 2.0f;

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
}  // namespace

float toLinear(float encoded) { return std::pow(clamp01(encoded), kGamma); }

float toEncoded(float linear) { return std::pow(clamp01(linear), 1.0f / kGamma); }

float luminance(const std::array<float, 3>& c) { return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2]; }

float saturatedRed(const std::array<float, 3>& c) {
    const float sum = c[0] + c[1] + c[2];
    if (sum <= 1.0e-6f || c[0] / sum < 0.8f) return 0.0f;
    return std::max(c[0] - c[1] - c[2], 0.0f);
}

FrameLight measureGrid(const uint8_t* rgba, int side) {
    FrameLight out;
    if (rgba == nullptr || side <= 0) return out;
    const int half = std::max(side / 2, 1);
    std::array<int, FrameLight::kRegions> cells{};
    for (int y = 0; y < side; y++) {
        for (int x = 0; x < side; x++) {
            const uint8_t* px = rgba + (static_cast<size_t>(y) * static_cast<size_t>(side) + static_cast<size_t>(x)) * 4;
            const std::array<float, 3> lin{toLinear(px[0] / 255.0f), toLinear(px[1] / 255.0f), toLinear(px[2] / 255.0f)};
            const int quarter = 1 + (x < half ? 0 : 1) + (y < half ? 0 : 2);
            for (int r : {0, quarter}) {
                for (int c = 0; c < 3; c++) out.rgb[r][c] += lin[c];
                cells[r]++;
            }
        }
    }
    for (int r = 0; r < FrameLight::kRegions; r++) {
        if (cells[r] == 0) continue;
        for (int c = 0; c < 3; c++) out.rgb[r][c] /= static_cast<float>(cells[r]);
    }
    return out;
}

void TransitionCounter::reset() {
    started_ = false;
    dir_ = 0;
    lo_ = 0.0f;
    hi_ = 0.0f;
}

// A swing that would not count (both ends bright) leaves the state as it was: the level may yet carry
// on into the dark, and that longer swing must still count.
bool TransitionCounter::step(float v, float threshold, float darkCeiling) {
    if (!started_) {
        started_ = true;
        lo_ = hi_ = v;
        return false;
    }
    if (dir_ >= 0) hi_ = std::max(hi_, v);
    if (dir_ <= 0) lo_ = std::min(lo_, v);
    if (dir_ <= 0 && v - lo_ >= threshold && lo_ < darkCeiling) {
        dir_ = 1;
        hi_ = v;
        return true;
    }
    if (dir_ >= 0 && hi_ - v >= threshold && v < darkCeiling) {
        dir_ = -1;
        lo_ = v;
        return true;
    }
    return false;
}

void FlashLimiter::Signal::record(double t) {
    times[static_cast<size_t>(head)] = t;
    head = (head + 1) % kCapacity;
    if (count < kCapacity) count++;
}

void FlashLimiter::Signal::dropOlderThan(double cutoff) {
    while (count > 0 && times[static_cast<size_t>((head - count + kCapacity) % kCapacity)] <= cutoff) count--;
}

void FlashLimiter::reset() {
    for (Signal& s : signals_) {
        s.counter.reset();
        s.head = 0;
        s.count = 0;
    }
    for (Signal& s : drives_) {
        s.counter.reset();
        s.head = 0;
        s.count = 0;
    }
    lastDriveAt_ = -1.0;
    held_ = false;
    scale_ = 1.0f;
    anchor_ = 0.0f;
    lastSource_ = 0.0f;
    sourceMean_ = 0.0f;
    seen_ = false;
    lastNow_ = -1.0;
    lastSeenAt_ = -1.0;
}

void FlashLimiter::observe(const FrameLight& shown, const Grade& drawnWith, double drawnAt) {
    // Out of order or from before a reset: nothing to learn.
    if (drawnAt < lastSeenAt_) return;
    const double gap = lastSeenAt_ < 0.0 ? 0.0 : drawnAt - lastSeenAt_;
    lastSeenAt_ = drawnAt;
    const float scale = std::clamp(drawnWith.scale, 1.0e-3f, 1.0f);
    const float anchor = drawnWith.anchor;
    const float lumaThreshold = std::max(kLumaThreshold, kQuantizationFloor / scale);
    const float redThreshold = std::max(kRedThreshold, kQuantizationFloor / scale);
    float wholeSource = 0.0f;
    for (int r = 0; r < FrameLight::kRegions; r++) {
        // What the source drew, before the grade squeezed it.
        std::array<float, 3> source{};
        for (int c = 0; c < 3; c++) source[c] = clamp01(anchor + (shown.rgb[r][c] - anchor) / scale);
        const float luma = luminance(source);
        if (r == 0) wholeSource = luma;
        Signal& l = signals_[static_cast<size_t>(r * 2)];
        if (l.counter.step(luma, lumaThreshold, kLumaDarkCeiling)) l.record(drawnAt);
        Signal& red = signals_[static_cast<size_t>(r * 2 + 1)];
        if (red.counter.step(saturatedRed(source), redThreshold, kNoCeiling)) red.record(drawnAt);
    }
    lastSource_ = wholeSource;
    if (!seen_) {
        sourceMean_ = wholeSource;
        seen_ = true;
    } else {
        const float a = 1.0f - std::exp(-static_cast<float>(std::clamp(gap, 0.0, 1.0)) / kWindowSeconds);
        sourceMean_ += (wholeSource - sourceMean_) * a;
    }
}

// No dark-side rule: the drive is a param, not a light level, and it is counted before the grade, so a
// held frame keeps counting the strobe the hold hides (as the measurement does by unsqueezing).
void FlashLimiter::drive(const std::array<float, kDrives>& modulation, double now) {
    if (now < lastDriveAt_) return;
    lastDriveAt_ = now;
    for (int i = 0; i < kDrives; i++) {
        const float v = modulation[static_cast<size_t>(i)];
        Signal& s = drives_[static_cast<size_t>(i)];
        if (s.counter.step(std::isfinite(v) ? v : 0.0f, kDriveThreshold, kNoCeiling)) s.record(now);
    }
}

int FlashLimiter::transitionsInWindow(double now) {
    int most = 0;
    for (Signal& s : signals_) {
        s.dropOlderThan(now - kWindowSeconds);
        most = std::max(most, s.count);
    }
    for (Signal& s : drives_) {
        s.dropOlderThan(now - kWindowSeconds);
        most = std::max(most, s.count);
    }
    return most;
}

Grade FlashLimiter::next(double now) {
    if (now < lastNow_) reset();
    const float dt = lastNow_ < 0.0 ? 0.0f : static_cast<float>(std::min(now - lastNow_, 1.0));
    lastNow_ = now;
    const int transitions = transitionsInWindow(now);
    if (!held_ && transitions >= kEngageTransitions) held_ = true;
    if (held_ && transitions <= kReleaseTransitions) held_ = false;
    if (held_) {
        scale_ = kHeldScale;
    } else {
        scale_ = std::min(1.0f, scale_ + dt * (1.0f - kHeldScale) / kReleaseSeconds);
    }
    if (scale_ >= 1.0f) {
        // Untouched, the grey waits where the picture is, so taking hold never jumps.
        anchor_ = lastSource_;
    } else {
        const float step = kAnchorDriftPerSecond * dt;
        anchor_ += std::clamp(sourceMean_ - anchor_, -step, step);
    }
    return {scale_, anchor_};
}

}  // namespace geode::viz::safety
