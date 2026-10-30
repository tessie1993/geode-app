#include "analysis/TempoTracker.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace geode::analysis {

TempoTracker::TempoTracker(float hopRateHz, float halfLifeSeconds, float energyAverageSeconds)
    : hopRateHz_(hopRateHz),
      minPeriod_(std::max(static_cast<int>(std::ceil(60.0f * hopRateHz / kMaxBpm)), 2)) {
    // Every whole-hop period in the range, so the winner always has both neighbours for refinedPeriod and
    // the resolution does not thin out at slow tempi the way a log-spaced bank did.
    const int maxPeriod = std::max(static_cast<int>(std::floor(60.0f * hopRateHz / kMinBpm)), minPeriod_ + 2);
    for (int p = minPeriod_; p <= maxPeriod; p++) periods_.push_back(p);
    const size_t n = periods_.size();
    delayLines_.resize(n);
    for (size_t i = 0; i < n; i++) delayLines_[i].assign(static_cast<size_t>(periods_[i]), 0.0f);
    delayIndex_.assign(n, 0);
    feedback_.resize(n);
    for (size_t i = 0; i < n; i++) feedback_[i] = std::pow(0.5f, periods_[i] / (halfLifeSeconds * hopRateHz));
    energy_.assign(n, 0.0f);
    scores_.assign(n, 0.0f);
    scoreScratch_.assign(n, 0.0f);
    energyPole_ = 1.0f - std::exp(-1.0f / (energyAverageSeconds * hopRateHz));
}

void TempoTracker::step(float onset) {
    // The resonators see the envelope minus its running mean. A DC level rings every comb the same, which
    // squeezes the scores together until kHarmonicTie can no longer tell a pulse from its multiples.
    onsetMean_ += (onset - onsetMean_) * energyPole_;
    const float x = onset - onsetMean_;
    int strongest = -1;
    float bestScore = 0.0f;
    for (size_t i = 0; i < periods_.size(); i++) {
        auto& line = delayLines_[i];
        const int at = delayIndex_[i];
        const float a = feedback_[i];
        const float y = a * line[at] + (1.0f - a) * x;
        line[at] = y;
        delayIndex_[i] = at + 1 == static_cast<int>(line.size()) ? 0 : at + 1;

        energy_[i] += (y * y - energy_[i]) * energyPole_;
        scores_[i] = energy_[i];
        if (energy_[i] > bestScore) {
            bestScore = energy_[i];
            strongest = static_cast<int>(i);
        }
    }

    if (strongest < 0 || bestScore <= 0.0f) {
        periodFrames_ = 0.0f;
        bpm_ = 0.0f;
        confidence_ = 0.0f;
        return;
    }

    periodFrames_ = refinedPeriod(bestPeriod(static_cast<size_t>(strongest)));
    bpm_ = bpmOf(periodFrames_);
    confidence_ = clarity(bestScore);
}

// The strongest resonator can be any multiple of the pulse (see kHarmonicTie). Walk down from a quarter of
// its period to a half and take the first, so the fastest, that carries the same evidence.
size_t TempoTracker::bestPeriod(size_t strongest) const {
    const float threshold = scores_[strongest] * kHarmonicTie;
    for (int divisor = 4; divisor >= 2; divisor--) {
        size_t at = 0;
        if (scoreNear(static_cast<float>(periods_[strongest]) / static_cast<float>(divisor), &at) >= threshold) return at;
    }
    return strongest;
}

// The better of the two whole periods either side of `period`, or 0 if it is outside the bank.
float TempoTracker::scoreNear(float period, size_t* at) const {
    float best = 0.0f;
    for (const float p : {std::floor(period), std::ceil(period)}) {
        const long i = static_cast<long>(p) - minPeriod_;
        if (i < 0 || i >= static_cast<long>(periods_.size())) continue;
        if (scores_[static_cast<size_t>(i)] > best) {
            best = scores_[static_cast<size_t>(i)];
            *at = static_cast<size_t>(i);
        }
    }
    return best;
}

// The resonators sit on whole-hop periods, so the raw winner can only report BPMs on that grid. A parabola
// through the winner and its two neighbours puts the period between them.
float TempoTracker::refinedPeriod(size_t best) const {
    const float period = static_cast<float>(periods_[best]);
    if (best == 0 || best + 1 >= periods_.size()) return period;
    const float before = scores_[best - 1];
    const float at = scores_[best];
    const float after = scores_[best + 1];
    const float curvature = before - 2.0f * at + after;
    if (curvature >= 0.0f) return period;
    const float offset = std::clamp(0.5f * (before - after) / curvature, -0.5f, 0.5f);
    return period + offset;
}

void TempoTracker::reset() {
    for (auto& line : delayLines_) std::fill(line.begin(), line.end(), 0.0f);
    std::fill(delayIndex_.begin(), delayIndex_.end(), 0);
    std::fill(energy_.begin(), energy_.end(), 0.0f);
    std::fill(scores_.begin(), scores_.end(), 0.0f);
    onsetMean_ = 0.0f;
    periodFrames_ = 0.0f;
    bpm_ = 0.0f;
    confidence_ = 0.0f;
}

float TempoTracker::clarity(float bestScore) {
    std::copy(scores_.begin(), scores_.end(), scoreScratch_.begin());
    std::sort(scoreScratch_.begin(), scoreScratch_.end());
    const float median = scoreScratch_[scoreScratch_.size() / 2];
    return std::clamp((bestScore - median) / bestScore, 0.0f, 1.0f);
}

}  // namespace geode::analysis
