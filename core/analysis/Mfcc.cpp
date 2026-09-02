#include "analysis/Mfcc.hpp"

#include <algorithm>
#include <cmath>

namespace geode::analysis {

Mfcc::Mfcc(int melCount, int count)
    : melCount_(melCount),
      count_(count),
      basis_(static_cast<size_t>(count)),
      logMel_(static_cast<size_t>(melCount)),
      previous_(static_cast<size_t>(count)),
      coefficients_(static_cast<size_t>(count)),
      delta_(static_cast<size_t>(count)) {
    for (int c = 0; c < count; c++) {
        const double scale = c == 0 ? std::sqrt(1.0 / melCount) : std::sqrt(2.0 / melCount);
        basis_[c].resize(static_cast<size_t>(melCount));
        for (int n = 0; n < melCount; n++) basis_[c][n] = scale * std::cos(M_PI * c * (2 * n + 1) / (2.0 * melCount));
    }
}

void Mfcc::compute(const float* melPower) {
    for (int m = 0; m < melCount_; m++) {
        logMel_[m] = 10.0 * std::log10(std::max(static_cast<double>(melPower[m]), kLogPowerFloor));
    }
    for (int c = 0; c < count_; c++) {
        const auto& row = basis_[c];
        double acc = 0.0;
        for (int m = 0; m < melCount_; m++) acc += row[m] * logMel_[m];
        coefficients_[c] = static_cast<float>(acc);
    }
    if (hasPrevious_) {
        double sq = 0.0;
        for (int c = 0; c < count_; c++) {
            const float d = coefficients_[c] - previous_[c];
            delta_[c] = d;
            if (c > 0) sq += static_cast<double>(d) * d;
        }
        timbreFlux_ = static_cast<float>(std::sqrt(sq));
    } else {
        std::fill(delta_.begin(), delta_.end(), 0.0f);
        timbreFlux_ = 0.0f;
        hasPrevious_ = true;
    }
    std::copy(coefficients_.begin(), coefficients_.end(), previous_.begin());
}

void Mfcc::reset() {
    hasPrevious_ = false;
    std::fill(previous_.begin(), previous_.end(), 0.0f);
    std::fill(coefficients_.begin(), coefficients_.end(), 0.0f);
    std::fill(delta_.begin(), delta_.end(), 0.0f);
    timbreFlux_ = 0.0f;
}

}  // namespace geode::analysis
