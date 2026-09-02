#pragma once
#include <vector>

namespace geode::analysis {

class LogBands {
public:
    static constexpr float kDefaultMinHz = 30.0f;
    static constexpr float kDefaultMaxHz = 16000.0f;
    static constexpr float kPinkTiltDbPerOctave = 3.0103f;
    static constexpr float kTiltReferenceHz = 1000.0f;

    LogBands(int bandCount, int fftSize, int sampleRateHz, float minHz = kDefaultMinHz, float maxHz = kDefaultMaxHz,
             float tiltDbPerOctave = kPinkTiltDbPerOctave);

    int bandCount() const { return bandCount_; }
    int sampleRateHz() const { return sampleRateHz_; }
    void setSampleRateHz(int value);
    float lowerHz(int band) const { return lowerEdgeHz_[band]; }
    float upperHz(int band) const { return upperEdgeHz_[band]; }
    void energyDb(const float* magnitudes, float* out) const;
    void energy(const float* magnitudes, float* out) const;

    static int bandForHz(float hz, int bandCount, int sampleRateHz, float minHz = kDefaultMinHz, float maxHz = kDefaultMaxHz);

private:
    void rebuild();

    int bandCount_;
    int fftSize_;
    int sampleRateHz_;
    float minHz_;
    float maxHz_;
    float tiltDbPerOctave_;
    int binCount_;
    std::vector<int> firstBin_;
    std::vector<int> lastBin_;
    std::vector<float> lowerEdgeHz_;
    std::vector<float> upperEdgeHz_;
    std::vector<float> tiltWeight_;
    float magnitudeScale_;
};

}  // namespace geode::analysis
