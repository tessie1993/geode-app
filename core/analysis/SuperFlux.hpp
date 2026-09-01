#pragma once
#include <vector>

namespace geode::analysis {

class SuperFlux {
public:
    explicit SuperFlux(int bandCount, int maxFilterBands = 3, int lagFrames = 1);
    int bandCount() const { return bandCount_; }
    float next(const float* bands);
    void reset();

private:
    int bandCount_;
    int lagFrames_;
    int radius_;
    std::vector<std::vector<float>> history_;
    std::vector<float> filtered_;
    int cursor_ = 0;
    int filled_ = 0;
};

}  // namespace geode::analysis
