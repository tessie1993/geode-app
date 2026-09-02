#pragma once
#include <array>
#include <atomic>
#include <cstddef>

#include "audio/dsp/Biquad.hpp"

namespace geode::audio {

// Headphone crossfeed: each ear also hears the other channel low-passed and attenuated, as a room would give it.
class Crossfeed {
public:
    explicit Crossfeed(float sampleRate);

    void setEnabled(bool enabled) { enabled_.store(enabled, std::memory_order_relaxed); }
    bool enabled() const { return enabled_.load(std::memory_order_relaxed); }
    void reset();

    // Stereo only; anything else passes through untouched.
    void process(float* interleaved, size_t frames, int channels);

private:
    static constexpr float kCutoffHz = 700.0f;
    static constexpr float kFeedDb = -4.5f;

    std::atomic<bool> enabled_{false};
    std::array<Biquad, 2> lowPass_{};
    float feed_;
    float direct_;
};

}  // namespace geode::audio
