#pragma once
#include <cstddef>

namespace geode::analysis::pulse {

struct Frame {
    float beat;
    float strength;
    float transient;
    float phase;
    float confidence;
    float energy;
};

void decide(const float* flux, size_t fluxCount, const float* rms, size_t rmsCount, float hopRateHz, float sensitivity,
            float refractoryMs, Frame* out);

}  // namespace geode::analysis::pulse
