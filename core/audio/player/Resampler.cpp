#include "audio/player/Resampler.hpp"

#include <algorithm>
#include <cmath>

namespace geode::audio::player {

namespace {

float hermite(float x0, float x1, float x2, float x3, float t) {
    const float c1 = 0.5f * (x2 - x0);
    const float c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
    const float c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
    return ((c3 * t + c2) * t + c1) * t + x1;
}

}  // namespace

Resampler::Resampler() { reset(); }

void Resampler::setRatio(int inputRate, int outputRate) {
    step_ = (inputRate > 0 && outputRate > 0) ? static_cast<double>(inputRate) / outputRate : 1.0;
}

// One silent frame of history so the first output can look one frame back.
void Resampler::reset() {
    pending_.assign(kChannels, 0.0f);
    head_ = 0;
    phase_ = 1.0;
}

void Resampler::push(const float* stereo, size_t frames) {
    pending_.insert(pending_.end(), stereo, stereo + frames * kChannels);
}

size_t Resampler::pull(float* stereo, size_t maxFrames) {
    const size_t available = pending_.size() / kChannels - head_;
    size_t produced = 0;
    while (produced < maxFrames) {
        const auto index = static_cast<size_t>(phase_);
        if (index + 2 >= available) break;
        const float t = static_cast<float>(phase_ - static_cast<double>(index));
        const float* base = pending_.data() + (head_ + index - 1) * kChannels;
        for (int ch = 0; ch < kChannels; ++ch) {
            stereo[produced * kChannels + ch] =
                hermite(base[ch], base[kChannels + ch], base[2 * kChannels + ch], base[3 * kChannels + ch], t);
        }
        phase_ += step_;
        ++produced;
    }
    compact();
    return produced;
}

// Drops every frame the interpolator can no longer reach; keeps one frame of look-back.
void Resampler::compact() {
    const auto index = static_cast<size_t>(phase_);
    if (index <= 1) return;
    const size_t drop = index - 1;
    head_ += drop;
    phase_ -= static_cast<double>(drop);
    if (head_ * kChannels > pending_.size() / 2) {
        pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(head_ * kChannels));
        head_ = 0;
    }
}

}  // namespace geode::audio::player
