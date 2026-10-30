#pragma once
#include <cstddef>
#include <string>
#include <vector>

#include "analysis/Chromagram.hpp"
#include "analysis/KeyDetector.hpp"
#include "analysis/ReactiveAnalyzer.hpp"
#include "api/geode_api.h"

namespace geode::analysis {

class AnalysisSession {
public:
    // The hop is a quarter of the window, as aubio and madmom's online trackers use: 75% overlap, so every
    // sample is analysed four times under the Hann window. Everything time-based then runs at
    // sampleRateHz / hopSamples, which comes from the stream and the window, not from a chosen frame rate.
    static constexpr int kHopDivisor = 4;

    AnalysisSession(int sampleRateHz, int fftSize);

    int hopSamples() const { return hopSamples_; }
    float hopRateHz() const { return hopRateHz_; }
    void setTuning(float sensitivity, float refractoryMs, float attackSeconds, float releaseSeconds);
    void reset();
    void push(const float* interleaved, size_t frames, int channels);
    bool pull(GeodeFeatureFrame& out);
    std::string key() const { return key_.finish(); }

private:
    void fill(const float* mid, const float* side, GeodeFeatureFrame& out) const;
    void averagedWaveform(const float* mid, float* out) const;

    int sampleRateHz_;
    int fftSize_;
    int hopSamples_;
    float hopRateHz_;
    ReactiveAnalyzer analyzer_;
    Chromagram chroma_;
    KeyDetector key_;
    std::vector<float> spectrum_;
    std::vector<float> buffer_;
    std::vector<float> sideBuffer_;
    size_t buffered_ = 0;
};

}  // namespace geode::analysis
