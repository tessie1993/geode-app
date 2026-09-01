#include "analysis/OnsetPeakPicker.hpp"

#include <algorithm>
#include <climits>
#include <cmath>

namespace geode::analysis {

OnsetPeakPicker::OnsetPeakPicker(float hopRateHz, float windowSeconds, float sensitivity, float refractorySeconds,
                                 float localMaxSeconds)
    : hopRateHz_(hopRateHz),
      sensitivity_(sensitivity),
      refractorySeconds_(refractorySeconds),
      windowSize_(std::max(static_cast<int>(std::lround(hopRateHz * windowSeconds)), 3)),
      window_(static_cast<size_t>(windowSize_)),
      scratch_(static_cast<size_t>(windowSize_)),
      localMaxSize_(std::max(static_cast<int>(std::lround(hopRateHz * localMaxSeconds)), 1)),
      recent_(static_cast<size_t>(localMaxSize_)),
      framesSinceOnset_(INT_MAX / 2),
      peakDecayPerFrame_(std::exp(-1.0f / (hopRateHz * kPeakMemorySeconds))) {}

bool OnsetPeakPicker::accept(float onset) {
    const float preceding = precedingMax();

    window_[writeIndex_] = onset;
    writeIndex_ = (writeIndex_ + 1) % windowSize_;
    if (filled_ < windowSize_) filled_++;

    const float med = median();
    const float spread = std::max(std::max(deviation(med), med * kSpreadFloorFraction), kMinSpread);
    threshold_ = med + sensitivity_ * spread;

    const int refractoryFrames = std::max(static_cast<int>(std::lround(hopRateHz_ * refractorySeconds_)), 1);
    const bool isOnset = onset > threshold_ && onset > kNumericFloor && onset > preceding && framesSinceOnset_ > refractoryFrames;

    const float decayedPeak = peakEnvelope_ * peakDecayPerFrame_;
    strength_ = isOnset ? std::clamp(onset / std::max(decayedPeak, kNumericFloor), 0.0f, 1.0f) : 0.0f;
    peakEnvelope_ = std::max(onset, decayedPeak);

    framesSinceOnset_ = isOnset ? 0 : std::min(framesSinceOnset_ + 1, 1000000);

    recent_[recentIndex_] = onset;
    recentIndex_ = (recentIndex_ + 1) % localMaxSize_;
    return isOnset;
}

void OnsetPeakPicker::reset() {
    std::fill(window_.begin(), window_.end(), 0.0f);
    std::fill(recent_.begin(), recent_.end(), 0.0f);
    writeIndex_ = 0;
    filled_ = 0;
    recentIndex_ = 0;
    framesSinceOnset_ = INT_MAX / 2;
    threshold_ = 0.0f;
    strength_ = 0.0f;
    peakEnvelope_ = 0.0f;
}

float OnsetPeakPicker::precedingMax() const {
    float peak = 0.0f;
    for (int i = 0; i < localMaxSize_; i++) {
        if (recent_[i] > peak) peak = recent_[i];
    }
    return peak;
}

float OnsetPeakPicker::median() {
    std::copy(window_.begin(), window_.begin() + filled_, scratch_.begin());
    std::sort(scratch_.begin(), scratch_.begin() + filled_);
    const int mid = filled_ / 2;
    return filled_ % 2 == 1 ? scratch_[mid] : (scratch_[mid - 1] + scratch_[mid]) * 0.5f;
}

float OnsetPeakPicker::deviation(float median) const {
    float acc = 0.0f;
    for (int i = 0; i < filled_; i++) acc += std::fabs(window_[i] - median);
    return acc / filled_;
}

}  // namespace geode::analysis
