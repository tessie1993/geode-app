#include "analysis/TempoStability.hpp"

#include <algorithm>
#include <cmath>

namespace geode::analysis {

namespace {
const float kLn2 = std::log(2.0f);
}

TempoStability::TempoStability(float hopRateHz, float meanSeconds, float deviationSeconds, float silenceSeconds)
    : meanPole_(1.0f - std::exp(-1.0f / (meanSeconds * hopRateHz))),
      devPole_(1.0f - std::exp(-1.0f / (deviationSeconds * hopRateHz))),
      silencePole_(1.0f - std::exp(-1.0f / (silenceSeconds * hopRateHz))) {}

void TempoStability::step(float bpm) {
    if (bpm <= 0.0f) {
        dev_ += (kScaleOctaves - dev_) * silencePole_;
        value_ = seeded_ ? std::clamp(1.0f - dev_ / kScaleOctaves, 0.0f, 1.0f) : 0.0f;
        return;
    }
    const float x = std::log(bpm) / kLn2;
    if (!seeded_) {
        seeded_ = true;
        mean_ = x;
        dev_ = kScaleOctaves;
    }
    mean_ += (x - mean_) * meanPole_;
    dev_ += (std::fabs(x - mean_) - dev_) * devPole_;
    value_ = std::clamp(1.0f - dev_ / kScaleOctaves, 0.0f, 1.0f);
}

void TempoStability::reset() {
    mean_ = 0.0f;
    dev_ = kScaleOctaves;
    seeded_ = false;
    value_ = 0.0f;
}

}  // namespace geode::analysis
