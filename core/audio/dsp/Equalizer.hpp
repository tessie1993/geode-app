#pragma once
#include <array>
#include <atomic>
#include <cstddef>

#include "audio/dsp/Biquad.hpp"

namespace geode::audio {

// Ten peaking bands on ISO octave centres plus a bass-boost shelf; levels arrive in millibels from any
// thread and the audio thread recomputes coefficients when it sees them change.
class Equalizer {
public:
    static constexpr int kBands = 10;
    static constexpr int kMaxChannels = 2;
    static constexpr int kMinMillibels = -1500;
    static constexpr int kMaxMillibels = 1500;
    static constexpr int kMaxBassBoost = 1000;

    static const std::array<float, kBands>& centersHz();

    explicit Equalizer(float sampleRate) : sampleRate_(sampleRate) { recompute(); }

    void setBand(int band, int millibels);
    int band(int band) const;
    void setBassBoost(int permille);
    void setEnabled(bool enabled) { enabled_.store(enabled, std::memory_order_relaxed); }
    bool enabled() const { return enabled_.load(std::memory_order_relaxed); }
    void reset();

    // RT-safe: no allocation, no locks.
    void process(float* interleaved, size_t frames, int channels);

private:
    static constexpr float kQ = 1.41f;
    static constexpr float kBassCornerHz = 120.0f;
    static constexpr float kBassMaxDb = 9.0f;

    void recompute();

    float sampleRate_;
    std::array<std::atomic<int>, kBands> levels_{};
    std::atomic<int> bassBoost_{0};
    std::atomic<bool> enabled_{false};
    std::atomic<bool> dirty_{true};
    std::array<std::array<Biquad, kBands>, kMaxChannels> filters_{};
    std::array<Biquad, kMaxChannels> bass_{};
    bool bassActive_ = false;
    std::array<bool, kBands> active_{};
};

}  // namespace geode::audio
