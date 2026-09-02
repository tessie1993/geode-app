#pragma once
#include <atomic>
#include <cstddef>
#include <vector>

namespace geode::audio {

// A lookahead peak limiter: the signal is delayed by the lookahead while the gain falls ahead of a peak
// and recovers over the release; nothing above the ceiling leaves it.
class Limiter {
public:
    Limiter(float sampleRate, int channels);

    void setEnabled(bool enabled) { enabled_.store(enabled, std::memory_order_relaxed); }
    bool enabled() const { return enabled_.load(std::memory_order_relaxed); }
    void reset();

    // RT-safe: the delay line was sized at construction.
    void process(float* interleaved, size_t frames);

private:
    static constexpr float kLookaheadSeconds = 0.005f;
    static constexpr float kReleaseSeconds = 0.05f;
    static constexpr float kCeiling = 0.98f;

    int channels_;
    size_t lookahead_;
    std::vector<float> delay_;
    std::vector<float> peaks_;
    size_t write_ = 0;
    float gain_ = 1.0f;
    float releaseCoefficient_;
    std::atomic<bool> enabled_{true};
};

}  // namespace geode::audio
