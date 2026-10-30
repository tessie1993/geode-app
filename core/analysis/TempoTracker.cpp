#include "analysis/TempoTracker.hpp"

#include <algorithm>
#include <cmath>

namespace geode::analysis {

TempoTracker::TempoTracker(float hopRateHz, float halfLifeSeconds, float meanSeconds)
    : hopRateHz_(hopRateHz),
      minPeriod_(std::max(static_cast<int>(std::ceil(60.0f * hopRateHz / kMaxBpm)), 2)),
      maxPeriod_(std::max(static_cast<int>(std::floor(60.0f * hopRateHz / kMinBpm)), minPeriod_ + 2)),
      // One lag past the slowest period, so refinedPeriod always has both neighbours.
      history_(static_cast<size_t>(maxPeriod_ + 2), 0.0f),
      correlation_(static_cast<size_t>(maxPeriod_ + 2), 0.0f),
      keep_(std::pow(0.5f, 1.0f / (halfLifeSeconds * hopRateHz))),
      meanPole_(1.0f - std::exp(-1.0f / (meanSeconds * hopRateHz))) {}

void TempoTracker::step(float onset) {
    // The envelope minus its running mean: a DC level correlates with itself at every lag the same, which
    // squeezes the lags together until kHarmonicTie can no longer tell a pulse from its multiples.
    onsetMean_ += (onset - onsetMean_) * meanPole_;
    std::rotate(raw_.begin(), raw_.begin() + 1, raw_.end());
    raw_.back() = onset - onsetMean_;
    float x = 0.0f;
    for (size_t i = 0; i < kSmooth.size(); i++) x += kSmooth[i] * raw_[i];

    const size_t n = history_.size();
    at_ = at_ + 1 == n ? 0 : at_ + 1;
    history_[at_] = x;
    for (size_t lag = 0; lag < n; lag++) {
        const float past = history_[at_ >= lag ? at_ - lag : at_ + n - lag];
        correlation_[lag] = keep_ * correlation_[lag] + (1.0f - keep_) * x * past;
    }

    int strongest = -1;
    float best = 0.0f;
    for (int lag = minPeriod_; lag <= maxPeriod_; lag++) {
        if (correlation_[static_cast<size_t>(lag)] > best) {
            best = correlation_[static_cast<size_t>(lag)];
            strongest = lag;
        }
    }
    const float energy = correlation_[0];
    if (strongest < 0 || energy <= 0.0f) {
        periodFrames_ = 0.0f;
        bpm_ = 0.0f;
        confidence_ = 0.0f;
        return;
    }

    // The strongest lag can be any multiple of the pulse (see kHarmonicTie). Of its divisors still in range,
    // the shortest that carries the same evidence is the pulse.
    const float strongestPeriod = refinedPeriod(strongest);
    float period = strongestPeriod;
    for (int divisor = 2; strongestPeriod / static_cast<float>(divisor) >= static_cast<float>(minPeriod_) - 0.5f;
         divisor++) {
        const float candidate = strongestPeriod / static_cast<float>(divisor);
        if (interpolated(candidate) >= best * kHarmonicTie) period = candidate;
    }
    if (period != strongestPeriod) period = refinedPeriod(static_cast<int>(std::lround(period)));

    periodFrames_ = period;
    bpm_ = bpmOf(period);
    confidence_ = std::clamp(best / energy, 0.0f, 1.0f);
}

float TempoTracker::interpolated(float lag) const {
    const auto below = static_cast<size_t>(std::floor(lag));
    if (lag < 0.0f || below + 1 >= correlation_.size()) return 0.0f;
    const float t = lag - static_cast<float>(below);
    return correlation_[below] * (1.0f - t) + correlation_[below + 1] * t;
}

// The lags are whole hops, so the raw winner can only report BPMs on that grid. A parabola through the winner
// and its two neighbours puts the period between them.
float TempoTracker::refinedPeriod(int lag) const {
    const auto at = static_cast<size_t>(lag);
    if (lag <= 0 || at + 1 >= correlation_.size()) return static_cast<float>(lag);
    const float before = correlation_[at - 1];
    const float peak = correlation_[at];
    const float after = correlation_[at + 1];
    const float curvature = before - 2.0f * peak + after;
    if (curvature >= 0.0f) return static_cast<float>(lag);
    return static_cast<float>(lag) + std::clamp(0.5f * (before - after) / curvature, -0.5f, 0.5f);
}

void TempoTracker::reset() {
    raw_.fill(0.0f);
    std::fill(history_.begin(), history_.end(), 0.0f);
    std::fill(correlation_.begin(), correlation_.end(), 0.0f);
    at_ = 0;
    onsetMean_ = 0.0f;
    periodFrames_ = 0.0f;
    bpm_ = 0.0f;
    confidence_ = 0.0f;
}

}  // namespace geode::analysis
