#pragma once
#include <vector>

namespace geode::analysis {

class OnsetPeakPicker {
public:
    static constexpr float kSpreadFloorFraction = 0.05f;
    static constexpr float kMinSpread = 1e-6f;
    static constexpr float kNumericFloor = 1e-6f;
    static constexpr float kPeakMemorySeconds = 8.0f;

    explicit OnsetPeakPicker(float hopRateHz, float windowSeconds = 1.5f, float sensitivity = 3.0f,
                             float refractorySeconds = 0.06f, float localMaxSeconds = 0.03f);

    float sensitivity() const { return sensitivity_; }
    void setSensitivity(float value) { sensitivity_ = value; }
    float refractorySeconds() const { return refractorySeconds_; }
    void setRefractorySeconds(float value) { refractorySeconds_ = value; }
    float threshold() const { return threshold_; }
    float strength() const { return strength_; }

    bool accept(float onset);
    void reset();

private:
    float precedingMax() const;
    float median();
    float deviation(float median) const;

    float hopRateHz_;
    float sensitivity_;
    float refractorySeconds_;
    int windowSize_;
    std::vector<float> window_;
    std::vector<float> scratch_;
    int writeIndex_ = 0;
    int filled_ = 0;
    int localMaxSize_;
    std::vector<float> recent_;
    int recentIndex_ = 0;
    int framesSinceOnset_;
    float threshold_ = 0.0f;
    float strength_ = 0.0f;
    float peakEnvelope_ = 0.0f;
    float peakDecayPerFrame_;
};

}  // namespace geode::analysis
