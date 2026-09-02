#pragma once
#include <atomic>
#include <cmath>
#include <cstddef>

namespace geode::audio {

// A smoothed linear gain: the target arrives in decibels from any thread, the audio thread glides to it.
class Gain {
public:
    explicit Gain(float sampleRate) : coefficient_(1.0f - std::exp(-1.0f / (kRampSeconds * sampleRate))) {}

    void setTargetDb(float db) { targetDb_.store(db, std::memory_order_relaxed); }
    float targetDb() const { return targetDb_.load(std::memory_order_relaxed); }
    void reset() { current_ = std::pow(10.0f, targetDb_.load(std::memory_order_relaxed) / 20.0f); }

    void process(float* interleaved, size_t frames, int channels) {
        const float target = std::pow(10.0f, targetDb_.load(std::memory_order_relaxed) / 20.0f);
        if (target == current_ && target == 1.0f) return;
        for (size_t i = 0; i < frames; ++i) {
            current_ += (target - current_) * coefficient_;
            for (int c = 0; c < channels; ++c) interleaved[i * static_cast<size_t>(channels) + static_cast<size_t>(c)] *= current_;
        }
    }

private:
    static constexpr float kRampSeconds = 0.02f;

    float coefficient_;
    std::atomic<float> targetDb_{0.0f};
    float current_ = 1.0f;
};

}  // namespace geode::audio
