#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "audio/dsp/DspChain.hpp"
#include "audio/dsp/Equalizer.hpp"
#include "audio/dsp/GainStage.hpp"

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
// Bit-perfect USB (issue #353): while the DAC takes the stream bit-perfect, the chain is pure pass-through, so
// the DAC gets the file's exact samples whatever gain, ramp, equalizer, crossfeed or limiter is set. The shell
// only says whether the platform granted the bit-perfect path for the stream it is about to open
// (setBitPerfect, any thread); this latches it at flush(), the moment a new AudioTrack follows (Media3 flushes
// the pipeline whenever it builds one), so the chain and the platform switch together. The live chain is
// reset at that flush either way, so leaving bit-perfect starts from clean filter state and the current
// targets, not from a half-finished ramp.
//
// The playback gain (GainStage: ReplayGain, fades, crossfade, sleep) belongs to the processor, not to a chain,
// and runs on every block a live chain processes, before it. While bit-perfect is live it stands still too.
//
// Threads: configure, flush and process (and the gain's beginStream and endStream) run on the playback
// thread; the setters may run on any thread. Every chain ever built stays owned here until destruction,
// so a setter racing a flush never reaches a
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
    // Playback thread, RT-safe: the same on 16-bit PCM in place, converted to float and back in fixed chunks
    // on the stack (full scale 32768, clipped, truncated toward zero, as the Kotlin shell did). A block the
    // chain does not run, including every block while bit-perfect is live, is never touched: not a byte.
    void processPcm16(int16_t* interleaved, size_t capacitySamples, size_t frames, int channels);

    // Any thread. Whether the platform takes the next AudioTrack bit-perfect; applies from the next flush().
    void setBitPerfect(bool on);

    void setEnabled(bool enabled);
    void setBand(int band, int millibels);
    void setBassBoost(int permille);
    void setLoudnessMb(int millibels);
    void setCrossfeed(bool enabled);
    void setLimiter(bool enabled);

    // ReplayGain, fades, crossfade and sleep (GainStage.hpp); its setters are thread-safe themselves.
    GainStage& gain() { return gain_; }

    // For tests: the live chain's format, 0 when none.
    int liveChannels() const;
    int liveSampleRate() const;
    bool bitPerfectLive() const;

private:
    struct Settings {
        bool enabled = false;
        std::array<int, Equalizer::kBands> bandsMb{};
        int bassBoost = 0;
        int loudnessMb = 0;
        bool crossfeed = false;
        bool limiter = true;
    };

    static void applyTo(DspChain& chain, const Settings& s);
    template <typename F>
    void forEachChain(F&& f);

    GainStage gain_;
    std::mutex mutex_;  // settings_ and chains_; never taken by process() or flush()
    Settings settings_;
    std::vector<std::unique_ptr<DspChain>> chains_;
    std::atomic<DspChain*> live_{nullptr};
    DspChain* pending_ = nullptr;  // playback thread only
    bool staged_ = false;          // playback thread only: a configure() waits for its flush()
    std::atomic<bool> bitPerfectWanted_{false};
    std::atomic<bool> bitPerfectLive_{false};  // written at flush(), read by process()

    // The chain process() may run on a block of `channels`, or null for pass-through.
    DspChain* runnable(int channels) const;
};

}  // namespace geode::audio
