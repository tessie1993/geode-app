#include "analysis/BeatGrid.hpp"

#include <cmath>

namespace geode::analysis {

bool BeatGrid::step(float periodFrames, float confidence, bool onset) {
    locked_ = confidence >= kLockConfidence && periodFrames > 0.0f;

    if (periodFrames > 0.0f) {
        phase_ += 1.0f / periodFrames;
        while (phase_ >= 1.0f) phase_ -= 1.0f;
    }

    if (!onset) {
        beat_ = false;
    } else if (!locked_) {
        phase_ = 0.0f;
        beat_ = true;
    } else {
        const float error = phase_ > 0.5f ? phase_ - 1.0f : phase_;
        if (std::fabs(error) <= kOnGridTolerance) {
            phase_ -= error * kPhaseCorrection;
            if (phase_ < 0.0f) phase_ += 1.0f;
            beat_ = true;
        } else {
            beat_ = false;
        }
    }
    return beat_;
}

void BeatGrid::reset() {
    phase_ = 0.0f;
    beat_ = false;
    locked_ = false;
}

}  // namespace geode::analysis
