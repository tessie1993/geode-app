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
    AnalysisSession(int sampleRateHz, int fftSize, float hopRateHz);

    void setSampleRateHz(int value);
    void setTuning(float sensitivity, float refractoryMs, float attackSeconds, float releaseSeconds);
    void reset();
    void analyze(const float* mid, const float* side, size_t frames, float dtSeconds, GeodeFeatureFrame& out);
    void push(const float* interleaved, size_t frames, int channels);
    bool pull(GeodeFeatureFrame& out);
    std::string key() const { return key_.finish(); }

private:
    void fill(const float* mid, const float* side, GeodeFeatureFrame& out) const;
    void averagedWaveform(const float* mid, float* out) const;
    void decimatedWaveform(const float* mid, float* out) const;

    int sampleRateHz_;
    int fftSize_;
    float hopRateHz_;
    int hopSamples_;
    ReactiveAnalyzer analyzer_;
    Chromagram chroma_;
    KeyDetector key_;
    std::vector<float> spectrum_;
    std::vector<float> buffer_;
    std::vector<float> sideBuffer_;
    size_t buffered_ = 0;
};

}  // namespace geode::analysis
