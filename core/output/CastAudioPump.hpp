#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "audio/dsp/PcmResampler.hpp"
#include "audio/ingest/PcmRing.hpp"
#include "viz/frame/CastAudio.hpp"

namespace geode::output {

// Cast's sound, one video frame at a time (#1150): what the player's ring gained, read through its own
// Cursor and rounded to 16 bits, resampled to the stream's rate by the export's PcmResampler, then held and
// padded to the frame clock's audio cut by viz/frame/CastAudio. The shell only hands the block to the AAC
// and Opus encoders.
//
// Only the player's music leaves the phone: while another ring feeds the visuals (the mic, another app's
// audio) nothing is taken and the cut is padded with silence, so the room's sound never reaches the TV and
// the mic never hears the TV. A ring with another channel count than the stream's, or a take with no rate,
// is read and dropped. The resampler is rebuilt (fresh state) when the ring's rate changes.
//
// One thread. Nothing allocates per frame: the buffers are sized at construction, the resampler's when the
// rate changes, and the block grows only past a second of audio (a render-loop stall that long).
class CastAudioPump {
public:
    // music: the player's ring, null for none (silence throughout). rateHz and channels: the stream's.
    CastAudioPump(std::shared_ptr<const audio::ingest::PcmRing> music, int rateHz, int channels);

    // One frame: fromMusic says the player's ring is what the visuals hear now; untilUs is the frame
    // clock's cut. Returns the frames of the block to queue, held frames first and silence after them
    // (0: the cut has not moved past what was queued).
    int pump(bool fromMusic, int64_t untilUs);

    // The last pump's block: blockFrames() * channels() interleaved samples, valid until the next pump.
    const int16_t* block() const { return block_.data(); }
    int blockFrames() const { return blockFrames_; }
    int channels() const { return channels_; }
    const viz::frame::CastAudio& counts() const { return counts_; }

private:
    // Takes what the ring gained into resampled_; returns the frames there.
    int64_t gain();
    size_t samples(int64_t frames) const { return static_cast<size_t>(frames) * static_cast<size_t>(channels_); }

    std::shared_ptr<const audio::ingest::PcmRing> music_;
    std::unique_ptr<audio::ingest::Cursor> cursor_;
    const int channels_;
    viz::frame::CastAudio counts_;
    std::unique_ptr<audio::PcmResampler> resampler_;
    std::vector<int16_t> taken_;      // the cursor's take as s16
    std::vector<int16_t> resampled_;  // that take at the stream's rate
    std::vector<int16_t> hold_;       // frames waiting for their cut, capacity() of them
    std::vector<int16_t> block_;
    int blockFrames_ = 0;
};

}  // namespace geode::output
