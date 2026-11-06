#pragma once
#include <cstddef>

#include "audio/dsp/Crossfeed.hpp"
#include "audio/dsp/Equalizer.hpp"
#include "audio/dsp/Gain.hpp"
#include "audio/dsp/Limiter.hpp"

namespace geode::audio {

// One format's chain behind geode_dsp_*: equalizer -> loudness makeup -> crossfeed -> limiter, float interleaved,
// in place. The playback gain (ReplayGain, fades, crossfade, sleep) runs before it in DspProcessor's GainStage,
// one for the processor rather than one per chain, because a stream's gain and a fade in progress must carry
// on across a format change.
class DspChain {
public:
    DspChain(int sampleRate, int channels)
        : sampleRate_(sampleRate), channels_(channels), equalizer_(static_cast<float>(sampleRate)), loudness_(static_cast<float>(sampleRate)),
          crossfeed_(static_cast<float>(sampleRate)), limiter_(static_cast<float>(sampleRate), channels) {}

    int sampleRate() const { return sampleRate_; }
    int channels() const { return channels_; }
    Equalizer& equalizer() { return equalizer_; }
    Gain& loudness() { return loudness_; }
    Crossfeed& crossfeed() { return crossfeed_; }
    Limiter& limiter() { return limiter_; }

    void reset() {
        equalizer_.reset();
        loudness_.reset();
        crossfeed_.reset();
        limiter_.reset();
    }

    // Audio thread: no allocation, no locks, no logging. Biquad::process also flushes near-zero filter
    // state so it never decays into denormals on silence (see Biquad.hpp).
    void process(float* interleaved, size_t frames) {
        equalizer_.process(interleaved, frames, channels_);
        loudness_.process(interleaved, frames, channels_);
        crossfeed_.process(interleaved, frames, channels_);
        limiter_.process(interleaved, frames);
    }

private:
    int sampleRate_;
    int channels_;
    Equalizer equalizer_;
    Gain loudness_;
    Crossfeed crossfeed_;
    Limiter limiter_;
};

}  // namespace geode::audio
