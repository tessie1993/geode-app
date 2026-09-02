#pragma once
#include <vector>

namespace geode::analysis {

class MelBank {
public:
    MelBank(int fftSize, int sampleRateHz, int melCount = 40);
    int melCount() const { return melCount_; }
    void power(const float* magnitudes, float* out) const;

private:
    static double hzToMel(double hz);
    static double melToHz(double mel);

    int melCount_;
    std::vector<int> firstBin_;
    std::vector<std::vector<double>> weights_;
};

}  // namespace geode::analysis
