#pragma once
#include <cstddef>
#include <vector>

namespace geode::audio::player {

// Stereo rate converter with 4-point Hermite interpolation; push input, pull as much output as is ready.
class Resampler {
public:
    static constexpr int kChannels = 2;

    Resampler();
    void setRatio(int inputRate, int outputRate);
    void reset();
    void push(const float* stereo, size_t frames);
    size_t pull(float* stereo, size_t maxFrames);
    size_t queuedFrames() const { return pending_.size() / kChannels - head_; }
    // True while at least one more output frame can be interpolated from what is queued.
    bool ready() const { return static_cast<size_t>(phase_) + 2 < queuedFrames(); }

private:
    void compact();

    double step_ = 1.0;
    double phase_ = 1.0;
    size_t head_ = 0;
    std::vector<float> pending_;
};

}  // namespace geode::audio::player
