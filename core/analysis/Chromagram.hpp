#pragma once
#include <array>

namespace geode::analysis {

class Chromagram {
public:
    static constexpr float kPeakFloor = 0.05f;
    static constexpr float kMinHz = 200.0f;
    static constexpr float kMaxHz = 5000.0f;
    static constexpr float kAttackSeconds = 0.06f;
    static constexpr float kReleaseSeconds = 0.35f;
    static constexpr float kConfidentRatio = 6.0f;
    static constexpr double kSilence = 1e-7;
    static constexpr const char* kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

    explicit Chromagram(float hopRateHz = 60.0f, float attackSeconds = kAttackSeconds,
                        float releaseSeconds = kReleaseSeconds);

    const std::array<float, 12>& bins() const { return bins_; }
    float confidence() const { return confidence_; }
    int dominantPitchClass() const { return dominantPitchClass_; }
    void reset();
    void step(const float* magnitudes, int magnitudeCount, int sampleRateHz, int fftSize);
    int top(int n, int* out) const;

    static double foldPeaks(const float* magnitudes, int magnitudeCount, int sampleRateHz, int fftSize, float minHz,
                            float maxHz, double* out);

private:
    float poleFor(float seconds) const;
    float peakToMedian() const;

    float hopRateHz_;
    std::array<float, 12> bins_{};
    float confidence_ = 0.0f;
    int dominantPitchClass_ = 0;
    std::array<double, 12> raw_{};
    float attack_;
    float release_;
};

}  // namespace geode::analysis
