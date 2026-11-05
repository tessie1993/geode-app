#pragma once
#include <cstddef>
#include <cstdint>

#include "audio/dsp/Crossfeed.hpp"
#include "audio/dsp/Equalizer.hpp"
#include "audio/dsp/Gain.hpp"
#include "audio/dsp/Limiter.hpp"
#include "audio/dsp/ReplayGain.hpp"

namespace geode::audio {

// The playback chain behind geode_dsp_*: gain -> equalizer -> crossfeed -> limiter, float interleaved, in place.
// The gain is ReplayGain: the chain reads it from a ReplayGainTable for the stream it is processing, which
// beginStream names at each stream start, so a new track's gain lands on that track's first sample (#351).
class DspChain {
public:
    DspChain(int sampleRate, int channels)
        : channels_(channels), equalizer_(static_cast<float>(sampleRate)), gain_(static_cast<float>(sampleRate)),
          loudness_(static_cast<float>(sampleRate)), crossfeed_(static_cast<float>(sampleRate)), limiter_(static_cast<float>(sampleRate), channels) {}

    int channels() const { return channels_; }
    Equalizer& equalizer() { return equalizer_; }
    Gain& gain() { return gain_; }
    Gain& loudness() { return loudness_; }
    Crossfeed& crossfeed() { return crossfeed_; }
    Limiter& limiter() { return limiter_; }

    // Not owned; outlives the chain (both belong to NativeDspProcessor, which frees chains first).
    void setReplayGain(const ReplayGainTable* table) {
        replayGain_ = table;
        seenGeneration_ = 0;
        refreshGain(true);
    }

    // A new stream starts here, in stream order (after reset at the flush that starts it). Its gain is
    // taken at once, not ramped to: the previous stream's last sample has already been processed. While
    // its tags are not known the gain stays where it was, never 0 dB; it glides there when they arrive.
    void beginStream(uint64_t key) {
        streamKey_ = key;
        seenGeneration_ = 0;
        refreshGain(true);
    }

    uint64_t streamKey() const { return streamKey_; }

    void reset() {
        equalizer_.reset();
        gain_.reset();
        loudness_.reset();
        crossfeed_.reset();
        limiter_.reset();
    }

    // Audio thread: no allocation, no blocking lock, no logging (the ReplayGain read is a try-lock, taken
    // only when the table has changed, once per track or settings change). Biquad::process also flushes near-zero filter
    // state so it never decays into denormals on silence (see Biquad.hpp).
    void process(float* interleaved, size_t frames) {
        if (replayGain_ != nullptr && replayGain_->generation() != seenGeneration_) refreshGain(false);
        gain_.process(interleaved, frames, channels_);
        equalizer_.process(interleaved, frames, channels_);
        loudness_.process(interleaved, frames, channels_);
        crossfeed_.process(interleaved, frames, channels_);
        limiter_.process(interleaved, frames);
    }

private:
    void refreshGain(bool snap) {
        if (replayGain_ == nullptr) return;
        snapPending_ = snapPending_ || snap;
        float db = 0.0f;
        uint64_t generation = 0;
        switch (replayGain_->tryGain(streamKey_, db, generation)) {
            case ReplayGainTable::Lookup::Found:
                gain_.setTargetDb(db);
                if (snapPending_) gain_.reset();
                snapPending_ = false;
                seenGeneration_ = generation;
                break;
            case ReplayGainTable::Lookup::Unknown:
                snapPending_ = false;  // the stream has started at the old gain; its own one glides in
                seenGeneration_ = generation;  // keep the gain; the tags' arrival moves the generation
                break;
            case ReplayGainTable::Lookup::Busy:
                break;  // asked again on the next block, still snapping if this was a stream start
        }
    }

    const ReplayGainTable* replayGain_ = nullptr;
    uint64_t streamKey_ = 0;
    uint64_t seenGeneration_ = 0;
    bool snapPending_ = false;
    int channels_;
    Equalizer equalizer_;
    Gain gain_;
    Gain loudness_;
    Crossfeed crossfeed_;
    Limiter limiter_;
};

}  // namespace geode::audio
