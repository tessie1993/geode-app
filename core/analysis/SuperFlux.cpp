#include "analysis/SuperFlux.hpp"

#include <algorithm>

namespace geode::analysis {

SuperFlux::SuperFlux(int bandCount, int maxFilterBands, int lagFrames)
    : bandCount_(bandCount),
      lagFrames_(lagFrames),
      radius_((maxFilterBands - 1) / 2),
      history_(static_cast<size_t>(lagFrames), std::vector<float>(static_cast<size_t>(bandCount))),
      filtered_(static_cast<size_t>(bandCount)) {}

float SuperFlux::next(const float* bands) {
    for (int k = 0; k < bandCount_; k++) {
        float peak = bands[k];
        const int from = std::max(0, k - radius_);
        const int to = std::min(bandCount_ - 1, k + radius_);
        for (int j = from; j <= to; j++) {
            if (bands[j] > peak) peak = bands[j];
        }
        filtered_[k] = peak;
    }

    float rise = 0.0f;
    if (filled_ >= lagFrames_) {
        const auto& earlier = history_[cursor_];
        for (int k = 0; k < bandCount_; k++) {
            const float delta = bands[k] - earlier[k];
            if (delta > 0.0f) rise += delta;
        }
    }

    std::copy(filtered_.begin(), filtered_.end(), history_[cursor_].begin());
    cursor_ = (cursor_ + 1) % lagFrames_;
    if (filled_ < lagFrames_) filled_++;
    return rise / bandCount_;
}

void SuperFlux::reset() {
    for (auto& frame : history_) std::fill(frame.begin(), frame.end(), 0.0f);
    cursor_ = 0;
    filled_ = 0;
}

}  // namespace geode::analysis
