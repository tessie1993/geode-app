#include "analysis/StereoField.hpp"

#include <algorithm>
#include <cmath>

namespace geode::analysis::stereo {

namespace {
constexpr float kSilence = 1e-9f;
}

Reading of(const float* mid, const float* side, int count) {
    return Reading{width(mid, side, count), correlation(mid, side, count), pan(mid, side, count)};
}

float correlation(const float* mid, const float* side, int count) {
    float mm = 0.0f;
    float ss = 0.0f;
    float ms = 0.0f;
    for (int i = 0; i < count; i++) {
        const float m = mid[i];
        const float s = side[i];
        mm += m * m;
        ss += s * s;
        ms += m * s;
    }
    const float ll = mm + 2.0f * ms + ss;
    const float rr = mm - 2.0f * ms + ss;
    const float denom = std::sqrt(std::max(ll, 0.0f) * std::max(rr, 0.0f));
    if (denom <= kSilence) return 1.0f;
    return std::clamp((mm - ss) / denom, -1.0f, 1.0f);
}

float width(const float* mid, const float* side, int count) {
    if (count <= 0) return 0.0f;
    float mm = 0.0f;
    float ss = 0.0f;
    for (int i = 0; i < count; i++) {
        mm += mid[i] * mid[i];
        ss += side[i] * side[i];
    }
    const float m = std::sqrt(mm / count);
    const float s = std::sqrt(ss / count);
    const float total = m + s;
    if (total <= kSilence) return 0.0f;
    return std::clamp(s / total, 0.0f, 1.0f);
}

float pan(const float* mid, const float* side, int count) {
    if (count <= 0) return 0.0f;
    float mm = 0.0f;
    float ss = 0.0f;
    float ms = 0.0f;
    for (int i = 0; i < count; i++) {
        const float m = mid[i];
        const float s = side[i];
        mm += m * m;
        ss += s * s;
        ms += m * s;
    }
    const float left = std::sqrt(std::max(mm + 2.0f * ms + ss, 0.0f) / count);
    const float right = std::sqrt(std::max(mm - 2.0f * ms + ss, 0.0f) / count);
    const float total = left + right;
    if (total <= kSilence) return 0.0f;
    return std::clamp((right - left) / total, -1.0f, 1.0f);
}

}  // namespace geode::analysis::stereo
