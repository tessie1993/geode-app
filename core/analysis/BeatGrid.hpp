#pragma once

namespace geode::analysis {

class BeatGrid {
public:
    static constexpr float kLockConfidence = 0.4f;
    static constexpr float kOnGridTolerance = 0.12f;
    static constexpr float kPhaseCorrection = 0.25f;

    float phase() const { return phase_; }
    bool beat() const { return beat_; }
    bool locked() const { return locked_; }
    bool step(float periodFrames, float confidence, bool onset);
    void reset();

private:
    float phase_ = 0.0f;
    bool beat_ = false;
    bool locked_ = false;
};

}  // namespace geode::analysis
