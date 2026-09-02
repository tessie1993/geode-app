#pragma once
#include <cstddef>

#include "audio/dsp/Crossfeed.hpp"
#include "audio/dsp/Equalizer.hpp"
#include "audio/dsp/Gain.hpp"
#include "audio/dsp/Limiter.hpp"

namespace geode::audio {

// The playback chain behind geode_dsp_*: gain -> equalizer -> crossfeed -> limiter, float interleaved, in place.
class DspChain {
public:
    DspChain(int sampleRate, int channels)
        : sampleRate_(sampleRate), channels_(channels), equalizer_(static_cast<float>(sampleRate)), gain_(static_cast<float>(sampleRate)),
          loudness_(static_cast<float>(sampleRate)), crossfeed_(static_cast<float>(sampleRate)), limiter_(static_cast<float>(sampleRate), channels) {}

    int sampleRate() const { return sampleRate_; }
    int channels() const { return channels_; }
    Equalizer& equalizer() { return equalizer_; }
    Gain& gain() { return gain_; }
    Gain& loudness() { return loudness_; }
    Crossfeed& crossfeed() { return crossfeed_; }
    Limiter& limiter() { return limiter_; }

    void reset() {
        equalizer_.reset();
        gain_.reset();
        loudness_.reset();
        crossfeed_.reset();
        limiter_.reset();
    }

    // Audio thread: no allocation, no locks, no logging.
    void process(float* interleaved, size_t frames) {
        gain_.process(interleaved, frames, channels_);
        equalizer_.process(interleaved, frames, channels_);
        loudness_.process(interleaved, frames, channels_);
        crossfeed_.process(interleaved, frames, channels_);
        limiter_.process(interleaved, frames);
    }

private:
    int sampleRate_;
    int channels_;
    Equalizer equalizer_;
    Gain gain_;
    Gain loudness_;
    Crossfeed crossfeed_;
    Limiter limiter_;
};

}  // namespace geode::audio
