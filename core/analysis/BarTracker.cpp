#include "analysis/BarTracker.hpp"

#include <algorithm>

namespace geode::analysis {

void BarTracker::step(float phase, bool beat, bool locked, float accent) {
    if (phase < prevPhase_ - kWrapThreshold) {
        beatIndex_ = (beatIndex_ + 1) % kBeatsPerBar;
        for (auto& s : scores_) s *= kScoreLeak;
    }
    prevPhase_ = phase;

    int beatSlot = beatIndex_;
    if (beat && accent > 0.0f) {
        if (phase > 0.5f) beatSlot = (beatIndex_ + 1) % kBeatsPerBar;
        scores_[beatSlot] += accent;
        if (beatsSeen_ < kBeatsPerBar) beatsSeen_++;
        elect();
    }

    beatInBar_ = (beatIndex_ - downbeatPos_ + kBeatsPerBar) % kBeatsPerBar;
    barPhase_ = (beatInBar_ + std::clamp(phase, 0.0f, 1.0f)) / kBeatsPerBar;
    downbeat_ = beat && beatSlot == downbeatPos_ && beatsSeen_ >= kBeatsPerBar;
    confidence_ = locked ? clarity() : 0.0f;
}

void BarTracker::reset() {
    prevPhase_ = 0.0f;
    beatIndex_ = 0;
    downbeatPos_ = 0;
    beatsSeen_ = 0;
    scores_.fill(0.0f);
    barPhase_ = 0.0f;
    beatInBar_ = 0;
    downbeat_ = false;
    confidence_ = 0.0f;
}

void BarTracker::elect() {
    int best = downbeatPos_;
    for (int i = 0; i < kBeatsPerBar; i++) {
        if (scores_[i] > scores_[best]) best = i;
    }
    if (best != downbeatPos_ && scores_[best] > scores_[downbeatPos_] * kSwitchMargin) downbeatPos_ = best;
}

float BarTracker::clarity() const {
    float best = 0.0f;
    float second = 0.0f;
    for (const float s : scores_) {
        if (s > best) {
            second = best;
            best = s;
        } else if (s > second) {
            second = s;
        }
    }
    return best <= 1e-6f ? 0.0f : std::clamp((best - second) / best, 0.0f, 1.0f);
}

}  // namespace geode::analysis
