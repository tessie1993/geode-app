#pragma once
#include <vector>

namespace geode::analysis {

// A bank of comb resonators over the onset envelope (Scheirer 1998), one per whole-hop period in the search
// range. There is no preferred tempo: the octave is read from the resonators themselves (bestPeriod()), so a
// 90 BPM track is not pulled towards 120 and a 174 BPM one is not folded down to 87.
class TempoTracker {
public:
    // The search range, the band tempo lives in (madmom's online trackers search a similar 55-215 BPM with no
    // prior inside it). A range is not a bias towards any value in it.
    static constexpr float kMinBpm = 40.0f;
    static constexpr float kMaxBpm = 240.0f;
    // A pulse train with period T rings the T, 2T, 3T... resonators equally (each sees a pulse on every pass of
    // one slot) and the T/2 one at about a quarter of the energy. So the fastest resonator that scores within
    // this fraction of the best is the pulse itself, and anything slower is one of its multiples.
    static constexpr float kHarmonicTie = 0.85f;

    explicit TempoTracker(float hopRateHz, float halfLifeSeconds = 4.0f, float energyAverageSeconds = 2.0f);

    float periodFrames() const { return periodFrames_; }
    float bpm() const { return bpm_; }
    float confidence() const { return confidence_; }
    void step(float onset);
    void reset();

private:
    float clarity(float bestScore);
    size_t bestPeriod(size_t strongest) const;
    float refinedPeriod(size_t best) const;
    float scoreNear(float period, size_t* at) const;
    float bpmOf(float periodFrames) const { return 60.0f * hopRateHz_ / periodFrames; }

    float hopRateHz_;
    int minPeriod_;
    std::vector<int> periods_;
    std::vector<std::vector<float>> delayLines_;
    std::vector<int> delayIndex_;
    std::vector<float> feedback_;
    std::vector<float> energy_;
    std::vector<float> scores_;
    std::vector<float> scoreScratch_;
    float energyPole_;
    float onsetMean_ = 0.0f;
    float periodFrames_ = 0.0f;
    float bpm_ = 0.0f;
    float confidence_ = 0.0f;
};

}  // namespace geode::analysis
