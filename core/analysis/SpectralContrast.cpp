#include "analysis/SpectralContrast.hpp"

#include <algorithm>
#include <cmath>

#include "analysis/Mfcc.hpp"

namespace geode::analysis {

SpectralContrast::SpectralContrast(int fftSize, int sampleRateHz, int bands, float fminHz, float alpha)
    : bands_(bands), alpha_(alpha), firstBin_(static_cast<size_t>(bands)), binCount_(static_cast<size_t>(bands)) {
    const double binHz = static_cast<double>(sampleRateHz) / fftSize;
    const int binsTotal = fftSize / 2 + 1;
    int widest = 0;
    for (int b = 0; b < bands; b++) {
        const float lo = fminHz * static_cast<float>(1 << b);
        const float hi = fminHz * static_cast<float>(1 << (b + 1));
        int start = -1;
        int count = 0;
        for (int k = 0; k < binsTotal; k++) {
            const double f = k * binHz;
            if (f >= lo && f < hi) {
                if (start < 0) start = k;
                count++;
            }
        }
        firstBin_[b] = std::max(start, 0);
        binCount_[b] = count;
        if (count > widest) widest = count;
    }
    scratch_.resize(static_cast<size_t>(std::max(widest, 1)));
}

void SpectralContrast::compute(const float* magnitudes, float* out) {
    for (int b = 0; b < bands_; b++) {
        const int n = binCount_[b];
        if (n <= 0) {
            out[b] = 0.0f;
            continue;
        }
        const int base = firstBin_[b];
        for (int i = 0; i < n; i++) {
            const double mag = magnitudes[base + i];
            scratch_[i] = mag * mag;
        }
        std::sort(scratch_.begin(), scratch_.begin() + n);
        const int k = std::max(1, static_cast<int>(alpha_ * n));
        double valley = 0.0;
        double peak = 0.0;
        for (int i = 0; i < k; i++) {
            valley += scratch_[i];
            peak += scratch_[n - 1 - i];
        }
        valley = std::max(valley / k, Mfcc::kLogPowerFloor);
        peak = std::max(peak / k, Mfcc::kLogPowerFloor);
        out[b] = static_cast<float>(10.0 * (std::log10(peak) - std::log10(valley)));
    }
}

}  // namespace geode::analysis
