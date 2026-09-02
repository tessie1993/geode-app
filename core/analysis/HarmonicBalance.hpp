#pragma once
#include <vector>

namespace geode::analysis {

class HarmonicBalance {
public:
    static constexpr float kUndecided = 0.5f;

    HarmonicBalance(int binCount, float hopRateHz, float historySeconds = 0.2f, float smoothingSeconds = 0.25f);
    float balance() const { return balance_; }
    void step(const float* magnitudes);
    void reset();

private:
    static constexpr double kSilence = 1e-7;

    int binCount_;
    float historyPole_;
    float smoothingPole_;
    std::vector<float> history_;
    float balance_ = kUndecided;
};

}  // namespace geode::analysis
