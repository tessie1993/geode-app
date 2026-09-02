#pragma once
#include <vector>

namespace geode::analysis {

class SpectralContrast {
public:
    SpectralContrast(int fftSize, int sampleRateHz, int bands = 6, float fminHz = 200.0f, float alpha = 0.02f);
    int bands() const { return bands_; }
    void compute(const float* magnitudes, float* out);

private:
    int bands_;
    float alpha_;
    std::vector<int> firstBin_;
    std::vector<int> binCount_;
    std::vector<double> scratch_;
};

}  // namespace geode::analysis
