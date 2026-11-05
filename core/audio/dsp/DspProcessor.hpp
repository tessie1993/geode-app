#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

#include "audio/dsp/DspChain.hpp"
#include "audio/dsp/Equalizer.hpp"

namespace geode::audio {

// The playback DSP behind geode_dsp_*: one DspChain per input format, switched on the Media3
// AudioProcessor contract (issue #356).
//
// Media3 calls configure() for the next item while the old item is still draining: the configuration
// describes "the format of audio that will be queued after the next call to flush()", and input in the
// old format may still be queued until then (a tail SilenceSkipping or Sonic held back). So configure()
// only stages the next chain and flush() makes it live. Swapping at configure fed a mono tail to a stereo
// chain, which walked twice the floats the buffer held: a native heap overrun.
//
// process() also refuses what it cannot fit: a block whose channel count is not the live chain's passes
// through untouched, and frames are clamped to the buffer's capacity, so no caller mistake can walk past it.
//
// Threads: configure, flush and process run on the playback thread; the setters may run on any thread.
// Every chain ever built stays owned here until destruction, so a setter racing a flush never reaches a
// freed chain, and there is at most one chain per format (a format seen again reuses its chain).
class DspProcessor {
public:
    // Playback thread. Stages the chain for the input format queued after the next flush(). Returns false
    // when the chain cannot run that format (not mono or stereo, or no rate); flush() then makes the
    // processor pass audio through untouched.
    bool configure(int sampleRate, int channels);

    // Playback thread. Commits what configure() staged, then clears the live chain's state (seek or flush).
    void flush();

    // Playback thread, RT-safe: no allocation, no locks. `capacityFloats` is how many floats `interleaved`
    // holds; `channels` is the channel count the block was queued in.
    void process(float* interleaved, size_t capacityFloats, size_t frames, int channels);

    void setEnabled(bool enabled);
    void setBand(int band, int millibels);
    void setBassBoost(int permille);
    void setLoudnessMb(int millibels);
    void setGainDb(float db);
    void setCrossfeed(bool enabled);
    void setLimiter(bool enabled);

    // For tests: the live chain's format, 0 when none.
    int liveChannels() const;
    int liveSampleRate() const;

private:
    struct Settings {
        bool enabled = false;
        std::array<int, Equalizer::kBands> bandsMb{};
        int bassBoost = 0;
        int loudnessMb = 0;
        float gainDb = 0.0f;
        bool crossfeed = false;
        bool limiter = true;
    };

    static void applyTo(DspChain& chain, const Settings& s);
    template <typename F>
    void forEachChain(F&& f);

    std::mutex mutex_;  // settings_ and chains_; never taken by process() or flush()
    Settings settings_;
    std::vector<std::unique_ptr<DspChain>> chains_;
    std::atomic<DspChain*> live_{nullptr};
    DspChain* pending_ = nullptr;  // playback thread only
    bool staged_ = false;          // playback thread only: a configure() waits for its flush()
};

}  // namespace geode::audio
