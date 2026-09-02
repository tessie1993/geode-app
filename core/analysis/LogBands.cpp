#include "analysis/LogBands.hpp"

#include <algorithm>
#include <cmath>

#include "analysis/AdaptiveRange.hpp"

namespace geode::analysis {

LogBands::LogBands(int bandCount, int fftSize, int sampleRateHz, float minHz, float maxHz, float tiltDbPerOctave)
    : bandCount_(bandCount),
      fftSize_(fftSize),
      sampleRateHz_(sampleRateHz),
      minHz_(minHz),
      maxHz_(maxHz),
      tiltDbPerOctave_(tiltDbPerOctave),
      binCount_(fftSize / 2 + 1),
      firstBin_(static_cast<size_t>(bandCount)),
      lastBin_(static_cast<size_t>(bandCount)),
      lowerEdgeHz_(static_cast<size_t>(bandCount)),
      upperEdgeHz_(static_cast<size_t>(bandCount)),
      tiltWeight_(static_cast<size_t>(binCount_)),
      magnitudeScale_(2.0f / fftSize) {
    rebuild();
}

void LogBands::setSampleRateHz(int value) {
    if (value != sampleRateHz_) {
        sampleRateHz_ = value;
        rebuild();
    }
}

void LogBands::energyDb(const float* magnitudes, float* out) const {
    energy(magnitudes, out);
    for (int b = 0; b < bandCount_; b++) {
        const float mean = out[b];
        out[b] = mean <= 0.0f ? AdaptiveRange::kSilenceDb : std::max(10.0f * std::log10(mean), AdaptiveRange::kSilenceDb);
    }
}

void LogBands::energy(const float* magnitudes, float* out) const {
    for (int b = 0; b < bandCount_; b++) {
        double power = 0.0;
        const int from = firstBin_[b];
        const int to = lastBin_[b];
        for (int k = from; k <= to; k++) {
            const float m = magnitudes[k] * magnitudeScale_;
            power += static_cast<double>(m) * m * tiltWeight_[k];
        }
        out[b] = static_cast<float>(power / (to - from + 1));
    }
}

void LogBands::rebuild() {
    const float nyquist = sampleRateHz_ / 2.0f;
    const float top = std::min(maxHz_, nyquist);
    const float bottom = std::min(minHz_, top * 0.5f);
    const float binHz = static_cast<float>(sampleRateHz_) / fftSize_;

    const double logBottom = std::log(static_cast<double>(bottom));
    const double logTop = std::log(static_cast<double>(top));
    int cursor = 1;
    for (int b = 0; b < bandCount_; b++) {
        const float lo = static_cast<float>(std::exp(logBottom + (logTop - logBottom) * b / bandCount_));
        const float hi = static_cast<float>(std::exp(logBottom + (logTop - logBottom) * (b + 1) / bandCount_));
        lowerEdgeHz_[b] = lo;
        upperEdgeHz_[b] = hi;

        const int wantFirst = std::max(cursor, static_cast<int>(lo / binHz));
        const int first = std::min(wantFirst, binCount_ - 1);
        const int last = std::min(std::max(first, static_cast<int>(hi / binHz)), binCount_ - 1);
        firstBin_[b] = first;
        lastBin_[b] = last;
        cursor = std::min(last + 1, binCount_ - 1);
    }

    const float exponent = tiltDbPerOctave_ / kPinkTiltDbPerOctave;
    for (int k = 0; k < binCount_; k++) {
        const float hz = k * binHz;
        tiltWeight_[k] = (exponent == 0.0f || hz <= 0.0f)
            ? 1.0f
            : static_cast<float>(std::pow(static_cast<double>(hz / kTiltReferenceHz), static_cast<double>(exponent)));
    }
}

int LogBands::bandForHz(float hz, int bandCount, int sampleRateHz, float minHz, float maxHz) {
    const float top = std::min(maxHz, sampleRateHz / 2.0f);
    const float bottom = std::min(minHz, top * 0.5f);
    if (hz <= bottom) return 0;
    if (hz >= top) return bandCount - 1;
    const float fraction = std::log(hz / bottom) / std::log(top / bottom);
    return std::clamp(static_cast<int>(fraction * bandCount), 0, bandCount - 1);
}

}  // namespace geode::analysis
