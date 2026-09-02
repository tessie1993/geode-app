#include "analysis/TempoTracker.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace geode::analysis {

namespace {
const float kLn2 = std::log(2.0f);
}

TempoTracker::TempoTracker(float hopRateHz, float minBpm, float maxBpm, int resonatorCount, float preferredBpm,
                           float preferenceOctaves, float halfLifeSeconds, float energyAverageSeconds)
    : hopRateHz_(hopRateHz), minBpm_(minBpm), maxBpm_(maxBpm), periods_(buildPeriods(resonatorCount)) {
    const size_t n = periods_.size();
    delayLines_.resize(n);
    for (size_t i = 0; i < n; i++) delayLines_[i].assign(static_cast<size_t>(periods_[i]), 0.0f);
    delayIndex_.assign(n, 0);
    feedback_.resize(n);
    preference_.resize(n);
    for (size_t i = 0; i < n; i++) {
        feedback_[i] = std::pow(0.5f, periods_[i] / (halfLifeSeconds * hopRateHz));
        const float octaves = std::log(bpmOf(periods_[i]) / preferredBpm) / kLn2;
        preference_[i] = static_cast<float>(std::exp(-0.5 * (octaves / preferenceOctaves) * (octaves / preferenceOctaves)));
    }
    energy_.assign(n, 0.0f);
    scores_.assign(n, 0.0f);
    scoreScratch_.assign(n, 0.0f);
    energyPole_ = 1.0f - std::exp(-1.0f / (energyAverageSeconds * hopRateHz));
}

void TempoTracker::step(float onset) {
    int best = -1;
    float bestScore = 0.0f;
    for (size_t i = 0; i < periods_.size(); i++) {
        auto& line = delayLines_[i];
        const int at = delayIndex_[i];
        const float a = feedback_[i];
        const float y = a * line[at] + (1.0f - a) * onset;
        line[at] = y;
        delayIndex_[i] = at + 1 == static_cast<int>(line.size()) ? 0 : at + 1;

        energy_[i] += (y * y - energy_[i]) * energyPole_;
        const float score = energy_[i] * preference_[i];
        scores_[i] = score;
        if (score > bestScore) {
            bestScore = score;
            best = static_cast<int>(i);
        }
    }

    if (best < 0 || bestScore <= 0.0f) {
        periodFrames_ = 0.0f;
        bpm_ = 0.0f;
        confidence_ = 0.0f;
        return;
    }

    periodFrames_ = static_cast<float>(periods_[best]);
    bpm_ = bpmOf(periods_[best]);
    confidence_ = clarity(bestScore);
}

void TempoTracker::reset() {
    for (auto& line : delayLines_) std::fill(line.begin(), line.end(), 0.0f);
    std::fill(delayIndex_.begin(), delayIndex_.end(), 0);
    std::fill(energy_.begin(), energy_.end(), 0.0f);
    std::fill(scores_.begin(), scores_.end(), 0.0f);
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

std::vector<int> TempoTracker::buildPeriods(int count) const {
    const double logLow = std::log(static_cast<double>(minBpm_));
    const double logHigh = std::log(static_cast<double>(maxBpm_));
    std::set<int> seen;
    for (int i = 0; i < count; i++) {
        const double candidateBpm = std::exp(logLow + (logHigh - logLow) * i / (count - 1));
        const int period = static_cast<int>(std::lround(60.0 * hopRateHz_ / candidateBpm));
        if (period >= 2) seen.insert(period);
    }
    return std::vector<int>(seen.begin(), seen.end());
}

}  // namespace geode::analysis
