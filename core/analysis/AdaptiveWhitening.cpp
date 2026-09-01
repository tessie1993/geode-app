#include "analysis/AdaptiveWhitening.hpp"

#include <algorithm>
#include <cmath>

namespace geode::analysis {

AdaptiveWhitening::AdaptiveWhitening(int bandCount, float peakDecaySeconds, float floor)
    : bandCount_(bandCount), peakDecaySeconds_(peakDecaySeconds), floor_(floor), profile_(static_cast<size_t>(bandCount)) {}

void AdaptiveWhitening::whiten(const float* input, float dtSeconds, float* out) {
    const float decay = dtSeconds <= 0.0f ? 1.0f : std::exp(-dtSeconds / peakDecaySeconds_);
    for (int b = 0; b < bandCount_; b++) {
        const float x = input[b];
        const float peak = std::max(std::max(x, floor_), profile_[b] * decay);
        profile_[b] = peak;
        out[b] = std::clamp(x / peak, 0.0f, 1.0f);
    }
}

void AdaptiveWhitening::reset() {
    std::fill(profile_.begin(), profile_.end(), 0.0f);
}

}  // namespace geode::analysis
