#pragma once
#include <cstdint>

namespace geode::viz::frame {

// The Cast stream's audio pump, as counts (#772). Beside FixedRateClock: each frame the clock hands out
// an audio cut (the end of the frame taken, in µs of stream time) and the pump queues 48 kHz stereo up
// to exactly that cut, so the TV's player never waits for audio and sound runs as far as the picture.
// Pure maths, no samples: CastStream (Kotlin) keeps the PCM, resamples what the ring gained with
// PcmResampler and calls the encoder; this decides how many of those frames to keep, how many to queue
// from the hold, how much silence to pad, and where the stream's sample clock stands. The whole rule is
// testable on the host (core/tests/cast_audio_test.cpp).
//
// The hold is a FIFO of resampled frames waiting for their cut, capped at half a second: more than that
// and the TV's sound would drift behind its picture (the ring ran ahead, say after a stall in the
// render loop), so the oldest frames go first. A feed that runs ahead of the cut is held for the next
// frame; a feed that falls short (paused, between tracks, the mic owns the ring) is padded with silence,
// so the stream's audio never has a gap and its timeline never slips.
//
// The position is a frame count, never a time, so it cannot drift: the cut is turned into frames by
// rounding to the nearest frame. The clock's cut is truncated to whole µs, which at 30 fps lands up to a
// µs short of the slot's true end; truncating that again would queue 1599 frames where 1600 are due and
// jitter the block sizes. Rounding is exact for every rate whose slot is a whole number of frames
// (48 kHz at 24, 25, 30, 50 or 60 fps) and within half a frame for any other.
struct CastAudio {
    static constexpr int64_t kMicrosPerSec = 1'000'000;
    static constexpr int kDefaultRateHz = 48'000;

    int64_t produced = 0;   // frames queued to the encoder so far: the stream's sample clock
    int64_t held = 0;       // frames in the hold, waiting for their cut
    int rateHz = kDefaultRateHz;

    static CastAudio started(int rateHz) {
        CastAudio a;
        a.rateHz = rateHz > 0 ? rateHz : kDefaultRateHz;
        return a;
    }

    // The hold's capacity: half a second of frames.
    int64_t capacity() const { return rateHz / 2; }

    // The cut in frames: the nearest frame to untilUs.
    int64_t cutFrames(int64_t untilUs) const {
        if (untilUs <= 0) return 0;
        return (untilUs * rateHz + kMicrosPerSec / 2) / kMicrosPerSec;
    }

    // A frame position's PTS, in µs: where a block starting there plays.
    int64_t ptsUs(int64_t frames) const { return frames * kMicrosPerSec / rateHz; }

    // What one frame's pump does, in the order the caller applies it.
    struct Step {
        int64_t evict = 0;   // 1. drop this many of the oldest held frames
        int64_t skip = 0;    // 2. skip this many frames at the head of what the ring gained
        int64_t keep = 0;    // 3. append the next this many gained frames to the hold
        int64_t take = 0;    // 4. queue this many frames from the head of the hold
        int64_t pad = 0;     //    then this many frames of silence after them
        int64_t ptsUs = 0;   //    stamped here (the block's first frame)
    };

    // One frame: gained frames (resampled to rateHz, 0 when nothing came) join the hold, then the
    // block up to the cut is queued. take + pad is 0 when the cut has not moved past the position;
    // whatever was gained then waits in the hold.
    Step step(int64_t gained, int64_t untilUs) {
        Step s;
        const int64_t cap = capacity();
        if (gained > 0) {
            s.keep = gained;
            if (s.keep > cap) {
                s.skip = s.keep - cap;
                s.keep = cap;
            }
            const int64_t overflow = held + s.keep - cap;
            if (overflow > 0) s.evict = overflow;
            held += s.keep - s.evict;
        }
        const int64_t need = cutFrames(untilUs) - produced;
        if (need <= 0) return s;
        s.take = need < held ? need : held;
        s.pad = need - s.take;
        s.ptsUs = ptsUs(produced);
        held -= s.take;
        produced += need;
        return s;
    }
};

// What the AAC encoder has taken of the blocks CastAudio queued (#772). A block goes in whole, but the
// codec takes input buffers of its own size and has none free while it is behind; this keeps the
// frames it has not taken and says how many the next buffer takes and its PTS, so every frame reaches
// the encoder once, in order, at the position CastAudio counted it at. The backlog is capped at a
// second: past that the encoder has stalled and the oldest frames are dropped, with the position moved
// past them, so the PTS that follow still sit on the frame clock's cut.
struct CastAudioFeed {
    int64_t queued = 0;    // frames handed to the codec, or dropped: the next one's position
    int64_t pending = 0;   // frames waiting for an input buffer
    int rateHz = CastAudio::kDefaultRateHz;

    static CastAudioFeed started(int rateHz) {
        CastAudioFeed f;
        f.rateHz = rateHz > 0 ? rateHz : CastAudio::kDefaultRateHz;
        return f;
    }

    int64_t capacity() const { return rateHz; }

    // A block of frames joins the backlog; returns how many of the oldest to drop first (the caller
    // drops them from its PCM before appending the block's tail). A block bigger than the cap keeps
    // its newest frames.
    int64_t add(int64_t frames) {
        if (frames <= 0) return 0;
        const int64_t over = pending + frames - capacity();
        const int64_t drop = over > 0 ? over : 0;
        queued += drop;
        pending += frames - drop;
        return drop;
    }

    // The next input buffer, holding up to bufferFrames: how many frames it takes from the head of the
    // backlog (0 when none wait), stamped ptsUs.
    int64_t take(int64_t bufferFrames, int64_t& ptsUs) {
        const int64_t n = bufferFrames < pending ? bufferFrames : pending;
        ptsUs = queued * CastAudio::kMicrosPerSec / rateHz;
        if (n <= 0) return 0;
        queued += n;
        pending -= n;
        return n;
    }
};

}  // namespace geode::viz::frame
