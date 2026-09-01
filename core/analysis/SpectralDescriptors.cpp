#include "analysis/SpectralDescriptors.hpp"

#include <algorithm>
#include <cmath>

namespace geode::analysis::spectral {

namespace {
constexpr double kPowerFloor = 1e-10;
}

double centroidHz(const float* magnitudes, int count, double binHz) {
    double weighted = 0.0;
    double total = 0.0;
    for (int k = 0; k < count; k++) {
        const double m = magnitudes[k];
        weighted += k * binHz * m;
        total += m;
    }
    return total > kSilenceTotal ? weighted / total : 0.0;
}

double bandwidthHz(const float* magnitudes, int count, double binHz, double centroidHz) {
    double deviation = 0.0;
    double total = 0.0;
    for (int k = 0; k < count; k++) {
        const double m = magnitudes[k];
        const double offset = k * binHz - centroidHz;
        deviation += m * offset * offset;
        total += m;
    }
    return total > kSilenceTotal ? std::sqrt(deviation / total) : 0.0;
}

double rolloffHz(const float* magnitudes, int count, double binHz, double fraction) {
    double total = 0.0;
    for (int k = 0; k < count; k++) total += magnitudes[k];
    if (total <= kSilenceTotal) return 0.0;

    const double threshold = fraction * total;
    double running = 0.0;
    for (int k = 0; k < count; k++) {
        running += magnitudes[k];
        if (running >= threshold) return k * binHz;
    }
    return (count - 1) * binHz;
}

double flatness(const float* magnitudes, int count) {
    if (count <= 0) return 0.0;
    double logSum = 0.0;
    double sum = 0.0;
    for (int k = 0; k < count; k++) {
        const double power = std::max(static_cast<double>(magnitudes[k]) * magnitudes[k], kPowerFloor);
        logSum += std::log(power);
        sum += power;
    }
    return std::exp(logSum / count) / (sum / count);
}

}  // namespace geode::analysis::spectral
