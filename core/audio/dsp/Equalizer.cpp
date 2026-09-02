#include "audio/dsp/Equalizer.hpp"

#include <algorithm>

namespace geode::audio {

const std::array<float, Equalizer::kBands>& Equalizer::centersHz() {
    static const std::array<float, kBands> kCenters = {31.0f, 62.0f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f};
    return kCenters;
}

void Equalizer::setBand(int band, int millibels) {
    if (band < 0 || band >= kBands) return;
    levels_[static_cast<size_t>(band)].store(std::clamp(millibels, kMinMillibels, kMaxMillibels), std::memory_order_relaxed);
    dirty_.store(true, std::memory_order_release);
}

int Equalizer::band(int band) const {
    if (band < 0 || band >= kBands) return 0;
    return levels_[static_cast<size_t>(band)].load(std::memory_order_relaxed);
}

void Equalizer::setBassBoost(int permille) {
    bassBoost_.store(std::clamp(permille, 0, kMaxBassBoost), std::memory_order_relaxed);
    dirty_.store(true, std::memory_order_release);
}

void Equalizer::reset() {
    for (auto& channel : filters_) {
        for (auto& f : channel) f.reset();
    }
    for (auto& f : bass_) f.reset();
}

void Equalizer::recompute() {
    for (int b = 0; b < kBands; ++b) {
        const float db = static_cast<float>(levels_[static_cast<size_t>(b)].load(std::memory_order_relaxed)) / 100.0f;
        const auto c = Biquad::peaking(sampleRate_, std::min(centersHz()[static_cast<size_t>(b)], sampleRate_ * 0.45f), kQ, db);
        active_[static_cast<size_t>(b)] = db != 0.0f;
        for (auto& channel : filters_) channel[static_cast<size_t>(b)].set(c);
    }
    const float boostDb = kBassMaxDb * static_cast<float>(bassBoost_.load(std::memory_order_relaxed)) / static_cast<float>(kMaxBassBoost);
    bassActive_ = boostDb > 0.0f;
    const auto shelf = Biquad::lowShelf(sampleRate_, kBassCornerHz, 1.0f, boostDb);
    for (auto& f : bass_) f.set(shelf);
}

void Equalizer::process(float* interleaved, size_t frames, int channels) {
    if (!enabled_.load(std::memory_order_relaxed)) return;
    if (dirty_.exchange(false, std::memory_order_acquire)) recompute();
    const int ch = std::min(channels, kMaxChannels);
    for (size_t i = 0; i < frames; ++i) {
        for (int c = 0; c < ch; ++c) {
            float x = interleaved[i * static_cast<size_t>(channels) + static_cast<size_t>(c)];
            auto& chain = filters_[static_cast<size_t>(c)];
            for (int b = 0; b < kBands; ++b) {
                if (active_[static_cast<size_t>(b)]) x = chain[static_cast<size_t>(b)].process(x);
            }
            if (bassActive_) x = bass_[static_cast<size_t>(c)].process(x);
            interleaved[i * static_cast<size_t>(channels) + static_cast<size_t>(c)] = x;
        }
    }
}

}  // namespace geode::audio
