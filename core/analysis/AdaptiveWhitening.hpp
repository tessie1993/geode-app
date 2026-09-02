#pragma once
#include <vector>

namespace geode::analysis {

class AdaptiveWhitening {
public:
    static constexpr float kDefaultFloor = 1e-8f;

    explicit AdaptiveWhitening(int bandCount, float peakDecaySeconds = 2.0f, float floor = kDefaultFloor);
    int bandCount() const { return bandCount_; }
    void whiten(const float* input, float dtSeconds, float* out);
    void reset();

private:
    int bandCount_;
    float peakDecaySeconds_;
    float floor_;
    std::vector<float> profile_;
};

}  // namespace geode::analysis
