#include "analysis/SpectralFlux.hpp"

#include <algorithm>

namespace geode::analysis {

SpectralFlux::SpectralFlux(int binCount) : previous_(static_cast<size_t>(binCount)) {}

double SpectralFlux::next(const float* magnitudes) {
    double rise = 0.0;
    if (primed_) {
        for (size_t k = 0; k < previous_.size(); k++) {
            const float delta = magnitudes[k] - previous_[k];
            if (delta > 0.0f) rise += delta;
        }
    }
    std::copy(magnitudes, magnitudes + previous_.size(), previous_.begin());
    primed_ = true;
    return rise / static_cast<double>(previous_.size());
}

void SpectralFlux::reset() {
    primed_ = false;
    std::fill(previous_.begin(), previous_.end(), 0.0f);
}

}  // namespace geode::analysis
