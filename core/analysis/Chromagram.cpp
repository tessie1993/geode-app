#include "analysis/Chromagram.hpp"

#include <algorithm>
#include <cmath>

namespace geode::analysis {

Chromagram::Chromagram(float hopRateHz, float attackSeconds, float releaseSeconds)
    : hopRateHz_(hopRateHz), attack_(poleFor(attackSeconds)), release_(poleFor(releaseSeconds)) {}

float Chromagram::poleFor(float seconds) const {
    return seconds <= 0.0f ? 1.0f : 1.0f - std::exp(-1.0f / std::max(seconds * hopRateHz_, 1e-3f));
}

void Chromagram::reset() {
    bins_.fill(0.0f);
    raw_.fill(0.0);
    confidence_ = 0.0f;
    dominantPitchClass_ = 0;
}

void Chromagram::step(const float* magnitudes, int magnitudeCount, int sampleRateHz, int fftSize) {
    const double total = foldPeaks(magnitudes, magnitudeCount, sampleRateHz, fftSize, kMinHz, kMaxHz, raw_.data());

    if (total <= kSilence) {
        for (int i = 0; i < 12; i++) bins_[i] += (0.0f - bins_[i]) * release_;
        confidence_ = 0.0f;
        return;
    }

    double peak = 0.0;
    for (int i = 0; i < 12; i++) peak = std::max(peak, raw_[i]);
    for (int i = 0; i < 12; i++) {
        const float target = static_cast<float>(raw_[i] / peak);
        const float k = target > bins_[i] ? attack_ : release_;
        bins_[i] += (target - bins_[i]) * k;
    }

    int best = 0;
    for (int i = 1; i < 12; i++) {
        if (bins_[i] > bins_[best]) best = i;
    }
    dominantPitchClass_ = best;
    confidence_ = peakToMedian();
}

float Chromagram::peakToMedian() const {
    std::array<float, 12> scratch = bins_;
    std::sort(scratch.begin(), scratch.end());
    const float median = (scratch[5] + scratch[6]) * 0.5f;
    const float peak = scratch[11];
    if (peak <= 1e-6f) return 0.0f;
    if (median <= 1e-6f) return 1.0f;
    const float ratio = peak / median;
    return std::clamp((ratio - 1.0f) / (kConfidentRatio - 1.0f), 0.0f, 1.0f);
}

int Chromagram::top(int n, int* out) const {
    const int count = std::clamp(n, 0, 12);
    std::array<float, 12> scratch = bins_;
    for (int slot = 0; slot < count; slot++) {
        int best = 0;
        for (int i = 1; i < 12; i++) {
            if (scratch[i] > scratch[best]) best = i;
        }
        out[slot] = best;
        scratch[best] = -1.0f;
    }
    return count;
}

double Chromagram::foldPeaks(const float* magnitudes, int magnitudeCount, int sampleRateHz, int fftSize, float minHz,
                             float maxHz, double* out) {
    std::fill(out, out + 12, 0.0);
    const int minBin = std::max(static_cast<int>(std::ceil(minHz * fftSize / sampleRateHz)), 1);
    const int maxBin = std::min(static_cast<int>(maxHz * fftSize / sampleRateHz), magnitudeCount - 2);
    float frameMax = 0.0f;
    for (int k = minBin; k <= maxBin; k++) {
        if (magnitudes[k] > frameMax) frameMax = magnitudes[k];
    }
    if (frameMax <= 0.0f) return 0.0;
    const float floor = frameMax * kPeakFloor;
    double total = 0.0;
    for (int k = minBin; k <= maxBin; k++) {
        const float mid = magnitudes[k];
        if (mid < floor || mid <= magnitudes[k - 1] || mid < magnitudes[k + 1]) continue;
        const double left = magnitudes[k - 1];
        const double right = magnitudes[k + 1];
        const double denominator = left - 2.0 * mid + right;
        const double offset = denominator < -1e-12 ? std::clamp(0.5 * (left - right) / denominator, -0.5, 0.5) : 0.0;
        const double f = (k + offset) * static_cast<double>(sampleRateHz) / fftSize;
        const double midi = 69.0 + 12.0 * std::log2(f / 440.0);
        const int pc = ((static_cast<int>(std::lround(midi)) % 12) + 12) % 12;
        out[pc] += mid;
        total += mid;
    }
    return total;
}

}  // namespace geode::analysis
