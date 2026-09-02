#include "analysis/AdaptiveRange.hpp"

#include <algorithm>
#include <cmath>

namespace geode::analysis {

AdaptiveRange::AdaptiveRange(int bandCount, float floorRiseSeconds, float floorFallSeconds, float ceilingRiseSeconds,
                             float ceilingFallSeconds, float minSpanDb, float warmupSeconds)
    : bandCount_(bandCount),
      floorRiseSeconds_(floorRiseSeconds),
      floorFallSeconds_(floorFallSeconds),
      ceilingRiseSeconds_(ceilingRiseSeconds),
      ceilingFallSeconds_(ceilingFallSeconds),
      minSpanDb_(minSpanDb),
      warmupSeconds_(warmupSeconds),
      floorDb_(static_cast<size_t>(bandCount)),
      ceilingDb_(static_cast<size_t>(bandCount)) {}

float AdaptiveRange::warmup() const {
    return std::clamp(adaptedSeconds_ / warmupSeconds_, 0.0f, 1.0f);
}

void AdaptiveRange::normalize(const float* inputDb, float dtSeconds, float* out) {
    if (!primed_) {
        for (int b = 0; b < bandCount_; b++) {
            const float x = inputDb[b];
            floorDb_[b] = x - minSpanDb_ * 0.5f;
            ceilingDb_[b] = x + minSpanDb_ * 0.5f;
        }
        primed_ = true;
    }

    bool adapted = false;
    for (int b = 0; b < bandCount_; b++) {
        const float x = inputDb[b];
        if (x <= kSilenceDb) {
            out[b] = 0.0f;
            continue;
        }
        adapted = true;
        floorDb_[b] = follow(floorDb_[b], x, x > floorDb_[b] ? floorRiseSeconds_ : floorFallSeconds_, dtSeconds);
        ceilingDb_[b] = follow(ceilingDb_[b], x, x > ceilingDb_[b] ? ceilingRiseSeconds_ : ceilingFallSeconds_, dtSeconds);
        const float span = std::max(ceilingDb_[b] - floorDb_[b], minSpanDb_);
        out[b] = std::clamp((x - floorDb_[b]) / span, 0.0f, 1.0f);
    }
    if (adapted && dtSeconds > 0.0f) adaptedSeconds_ += dtSeconds;
}

void AdaptiveRange::reset() {
    std::fill(floorDb_.begin(), floorDb_.end(), 0.0f);
    std::fill(ceilingDb_.begin(), ceilingDb_.end(), 0.0f);
    primed_ = false;
    adaptedSeconds_ = 0.0f;
}

float AdaptiveRange::follow(float current, float target, float tauSeconds, float dtSeconds) {
    if (dtSeconds <= 0.0f) return current;
    if (tauSeconds <= 0.0f) return target;
    const float k = std::clamp(1.0f - std::exp(-dtSeconds / tauSeconds), 0.0f, 1.0f);
    return current + (target - current) * k;
}

}  // namespace geode::analysis
