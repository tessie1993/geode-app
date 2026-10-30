#pragma once
#include <array>
#include <vector>

namespace geode::analysis {

// A running autocorrelation of the onset envelope, one lag per hop up to the slowest period in the search range.
// There is no preferred tempo: the octave is read from the autocorrelation itself (step()), so a 90 BPM track is
// not pulled towards 120 and a 174 BPM one is not folded down to 87.
class TempoTracker {
public:
    // The search range, the band tempo lives in (madmom's online trackers search a similar 55-215 BPM with no
    // prior inside it). A range is not a bias towards any value in it.
    static constexpr float kMinBpm = 40.0f;
    static constexpr float kMaxBpm = 240.0f;
    // A pulse train with period T correlates with itself at T, 2T, 3T... equally, and at T/2 only as far as
    // there are off-beats. So the shortest divisor of the strongest lag that keeps this fraction of its
    // correlation is the pulse itself, and the strongest lag is one of its multiples.
    static constexpr float kHarmonicTie = 0.85f;

    explicit TempoTracker(float hopRateHz, float halfLifeSeconds = 4.0f, float meanSeconds = 2.0f);

    float periodFrames() const { return periodFrames_; }
    float bpm() const { return bpm_; }
    // The autocorrelation at the period over the one at lag 0: 1 for a steady pulse, near 0 for noise.
    float confidence() const { return confidence_; }
    void step(float onset);
    void reset();

private:
    // Binomial weights over ±2 hops. A beat rarely lands on a hop, so a sharp onset splits its energy across
    // two lags, and less of it reaches the true period than a multiple of it whose fraction happens to round
    // well; that is how a whole-hop comb bank read 128 BPM as 42.7. Spreading each onset over a few hops makes
    // every lag near a period see the same share.
    static constexpr std::array<float, 5> kSmooth = {0.0625f, 0.25f, 0.375f, 0.25f, 0.0625f};

    float interpolated(float lag) const;
    float refinedPeriod(int lag) const;
    float bpmOf(float periodFrames) const { return 60.0f * hopRateHz_ / periodFrames; }

    float hopRateHz_;
    int minPeriod_;
    int maxPeriod_;
    std::array<float, kSmooth.size()> raw_{};
    std::vector<float> history_;
    size_t at_ = 0;
    std::vector<float> correlation_;
    float keep_;
    float meanPole_;
    float onsetMean_ = 0.0f;
    float periodFrames_ = 0.0f;
    float bpm_ = 0.0f;
    float confidence_ = 0.0f;
};

}  // namespace geode::analysis
