#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "audio/ingest/AiffReader.hpp"

// An export's track audio, handed to its renderer's sound engine exactly up to each video frame (ADR 0003:
// export equals live). The shell decodes (MediaExtractor and MediaCodec, or an AiffReader for AIFF) and
// pushes each decoded chunk here; this converts it to float, drops what lies before the range start, keeps
// what the frame has not reached yet, and hands a sink exactly the frames up to the target. Ported from the
// Kotlin render/offscreen/TrackAudio, whose per-sample loops and pre-roll cut these are, with the same
// arithmetic: 16-bit / 32768, the codec's pre-roll cut at (start - pts) * rate / 1e6 frames, the AIFF one
// at start * rate / 1000 frames, the target at endMs * rate / 1000 frames of the latest chunk's rate.
//
// One chunk is held at a time, as in the Kotlin: push only once deliver() has emptied it. One thread.
namespace geode::audio::ingest {

class TrackFeed {
public:
    // interleaved, frames, channels, rate
    using Sink = void (*)(void* context, const float* interleaved, int frames, int channels, int sampleRate);

    static constexpr int kAiffChunk = 8192;  // samples per AIFF read

    explicit TrackFeed(int64_t startMs) : startMs_(startMs) {}

    // A decoder's output buffer: `bytes` of interleaved float (native order) or 16-bit little-endian PCM whose
    // first frame plays at ptsUs. Frames before the range start are dropped.
    void pushDecoded(const uint8_t* data, size_t bytes, bool isFloat, int channels, int sampleRate, int64_t ptsUs);
    // One read of the AIFF; false once it has run out. Frames before the range start are dropped.
    bool pushAiff(AiffReader& aiff);

    // Whether a chunk has told the rate yet (it is kept once known).
    bool hasRate() const { return rate_ != 0; }
    // Counts the target from the range start: endMs into the range at the latest chunk's rate.
    void setTargetMs(int64_t endMs) { target_ = endMs * rate_ / 1000; }
    // Hands `sink` the held frames up to the target; true once the target is reached, false when the held
    // chunk ran out first (push the next one, then call again).
    bool deliver(Sink sink, void* context);

    int64_t delivered() const { return delivered_; }
    int heldFrames() const { return frames_; }

private:
    float* hold(size_t samples);
    void held(int frames, int channels, int rate);

    int64_t startMs_ = 0;
    std::vector<float> pending_;
    int at_ = 0;
    int frames_ = 0;
    int channels_ = 0;
    int rate_ = 0;
    int64_t delivered_ = 0;
    int64_t target_ = 0;
    int64_t aiffSkip_ = -1;  // frames still to drop; -1 until the AIFF's rate is known
    std::vector<int16_t> aiffBuf_;
};

}  // namespace geode::audio::ingest
