#include "analysis/MelBank.hpp"

#include <algorithm>
#include <cmath>

namespace geode::analysis {

MelBank::MelBank(int fftSize, int sampleRateHz, int melCount)
    : melCount_(melCount), firstBin_(static_cast<size_t>(melCount)), weights_(static_cast<size_t>(melCount)) {
    const double nyquist = sampleRateHz / 2.0;
    const double melTop = hzToMel(nyquist);
    std::vector<double> edges(static_cast<size_t>(melCount + 2));
    for (int i = 0; i < melCount + 2; i++) edges[i] = melToHz(melTop * i / (melCount + 1));
    const double binHz = static_cast<double>(sampleRateHz) / fftSize;
    const int bins = fftSize / 2 + 1;
    std::vector<double> scratch(static_cast<size_t>(bins));

    for (int m = 0; m < melCount; m++) {
        const double lo = edges[m];
        const double mid = edges[m + 1];
        const double hi = edges[m + 2];
        int start = -1;
        int end = -1;
        std::fill(scratch.begin(), scratch.end(), 0.0);
        for (int k = 0; k < bins; k++) {
            const double f = k * binHz;
            const double w = std::max(0.0, std::min((f - lo) / (mid - lo), (hi - f) / (hi - mid)));
            if (w > 0.0) {
                if (start < 0) start = k;
                end = k;
                scratch[k] = w;
            }
        }
        // A mel that covers no bin keeps an empty weight row and contributes zero power.
        firstBin_[m] = std::max(start, 0);
        if (start >= 0) weights_[m].assign(scratch.begin() + start, scratch.begin() + end + 1);
    }
}

void MelBank::power(const float* magnitudes, float* out) const {
    for (int m = 0; m < melCount_; m++) {
        const auto& w = weights_[m];
        const int base = firstBin_[m];
        double acc = 0.0;
        for (size_t i = 0; i < w.size(); i++) {
            const double mag = magnitudes[base + i];
            acc += w[i] * mag * mag;
        }
        out[m] = static_cast<float>(acc);
    }
}

double MelBank::hzToMel(double hz) { return 2595.0 * std::log10(1.0 + hz / 700.0); }

double MelBank::melToHz(double mel) { return 700.0 * (std::pow(10.0, mel / 2595.0) - 1.0); }

}  // namespace geode::analysis
