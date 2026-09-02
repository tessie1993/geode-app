#pragma once
#include <vector>

namespace geode::analysis {

class TempoTracker {
public:
    explicit TempoTracker(float hopRateHz, float minBpm = 60.0f, float maxBpm = 200.0f, int resonatorCount = 64,
                          float preferredBpm = 120.0f, float preferenceOctaves = 1.1f, float halfLifeSeconds = 4.0f,
                          float energyAverageSeconds = 2.0f);

    float periodFrames() const { return periodFrames_; }
    float bpm() const { return bpm_; }
    float confidence() const { return confidence_; }
    void step(float onset);
    void reset();

private:
    float clarity(float bestScore);
    float bpmOf(int periodFrames) const { return 60.0f * hopRateHz_ / periodFrames; }
    std::vector<int> buildPeriods(int count) const;

    float hopRateHz_;
    float minBpm_;
    float maxBpm_;
    std::vector<int> periods_;
    std::vector<std::vector<float>> delayLines_;
    std::vector<int> delayIndex_;
    std::vector<float> feedback_;
    std::vector<float> preference_;
    std::vector<float> energy_;
    std::vector<float> scores_;
    std::vector<float> scoreScratch_;
    float energyPole_;
    float periodFrames_ = 0.0f;
    float bpm_ = 0.0f;
    float confidence_ = 0.0f;
};

}  // namespace geode::analysis
