#include "analysis/HarmonicBalance.hpp"

#include <algorithm>
#include <cmath>

namespace geode::analysis {

HarmonicBalance::HarmonicBalance(int binCount, float hopRateHz, float historySeconds, float smoothingSeconds)
    : binCount_(binCount),
      historyPole_(1.0f - std::exp(-1.0f / (historySeconds * hopRateHz))),
      smoothingPole_(1.0f - std::exp(-1.0f / (smoothingSeconds * hopRateHz))),
      history_(static_cast<size_t>(binCount)) {}

void HarmonicBalance::step(const float* magnitudes) {
    double harmonic = 0.0;
    double percussive = 0.0;
    for (int k = 0; k < binCount_; k++) {
        const float m = magnitudes[k];
        const float h = history_[k];
        harmonic += std::min(m, h);
        percussive += std::max(m - h, 0.0f);
        history_[k] = h + (m - h) * historyPole_;
    }
    const double total = harmonic + percussive;
    if (total <= kSilence) return;
    const float instantaneous = static_cast<float>(harmonic / total);
    balance_ += (instantaneous - balance_) * smoothingPole_;
}

void HarmonicBalance::reset() {
    std::fill(history_.begin(), history_.end(), 0.0f);
    balance_ = kUndecided;
}

}  // namespace geode::analysis
