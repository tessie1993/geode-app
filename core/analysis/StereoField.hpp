#pragma once

namespace geode::analysis::stereo {

struct Reading {
    float width;
    float correlation;
    float pan;
};

constexpr Reading kMono{0.0f, 1.0f, 0.0f};

Reading of(const float* mid, const float* side, int count);
float correlation(const float* mid, const float* side, int count);
float width(const float* mid, const float* side, int count);
float pan(const float* mid, const float* side, int count);

}  // namespace geode::analysis::stereo
