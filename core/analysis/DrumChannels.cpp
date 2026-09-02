#include "analysis/DrumChannels.hpp"

namespace geode::analysis {

DrumChannels::DrumChannels(int bandCount, float hopRateHz, int sampleRateHz, float minHz, float maxHz) : bandCount_(bandCount) {
    for (int c = 0; c < kChannels; c++) {
        pickers_.emplace_back(hopRateHz, 1.5f, 3.0f, 0.05f);
        fluxes_.emplace_back(bandCount);
        slices_.emplace_back(static_cast<size_t>(bandCount), 0.0f);
        from_[c] = LogBands::bandForHz(kEdges[c * 2], bandCount, sampleRateHz, minHz, maxHz);
        to_[c] = LogBands::bandForHz(kEdges[c * 2 + 1], bandCount, sampleRateHz, minHz, maxHz);
    }
}

void DrumChannels::setSensitivity(float value) {
    for (auto& p : pickers_) p.setSensitivity(value);
}

void DrumChannels::step(const float* bands) {
    for (int c = 0; c < kChannels; c++) {
        auto& slice = slices_[c];
        for (int b = 0; b < bandCount_; b++) slice[b] = (b >= from_[c] && b <= to_[c]) ? bands[b] : 0.0f;
        const bool fired = pickers_[c].accept(fluxes_[c].next(slice.data()));
        const float impulse = fired ? pickers_[c].strength() : 0.0f;
        switch (c) {
            case 0: kick_ = impulse; break;
            case 1: snare_ = impulse; break;
            default: hat_ = impulse; break;
        }
    }
}

void DrumChannels::reset() {
    for (auto& p : pickers_) p.reset();
    for (auto& f : fluxes_) f.reset();
    kick_ = 0.0f;
    snare_ = 0.0f;
    hat_ = 0.0f;
}

}  // namespace geode::analysis
