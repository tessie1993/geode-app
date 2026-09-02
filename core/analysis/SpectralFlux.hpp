#pragma once
#include <vector>

namespace geode::analysis {

class SpectralFlux {
public:
    explicit SpectralFlux(int binCount);
    double next(const float* magnitudes);
    void reset();

private:
    std::vector<float> previous_;
    bool primed_ = false;
};

}  // namespace geode::analysis
