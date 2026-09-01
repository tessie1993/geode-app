#pragma once
#include <vector>

namespace geode::analysis {

class AdaptiveRange {
public:
    static constexpr float kSilenceDb = -120.0f;

    explicit AdaptiveRange(int bandCount, float floorRiseSeconds = 6.0f, float floorFallSeconds = 0.5f,
                           float ceilingRiseSeconds = 0.15f, float ceilingFallSeconds = 2.5f, float minSpanDb = 15.0f,
                           float warmupSeconds = 1.5f);

    int bandCount() const { return bandCount_; }
    float warmup() const;
    void normalize(const float* inputDb, float dtSeconds, float* out);
    void reset();

private:
    static float follow(float current, float target, float tauSeconds, float dtSeconds);

    int bandCount_;
    float floorRiseSeconds_;
    float floorFallSeconds_;
    float ceilingRiseSeconds_;
    float ceilingFallSeconds_;
    float minSpanDb_;
    float warmupSeconds_;
    std::vector<float> floorDb_;
    std::vector<float> ceilingDb_;
    bool primed_ = false;
    float adaptedSeconds_ = 0.0f;
};

}  // namespace geode::analysis
