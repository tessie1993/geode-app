#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "audio/ingest/Cursor.hpp"

// A renderer's ear on a native ingest ring (#481, #362): what replaces Kotlin's SoundCursor and
// VisualizerRenderer.feedSound for every surface that hears the player.
//
// Each frame, before the sound engine is stepped, pump() hands the engine everything the ring gained since
// the last frame: every sample once, in order, as the ingest's stereo downmix (5.1 and 7.1 included), one
// format block at a time, each with the rate it was written at. A block that begins an epoch the cursor has
// not read before (a seek, a track change: the tap's flush) resets the engine first, so it never mixes two
// songs; a format change inside an epoch (speed, pitch) only changes the rate.
//
// Why not GL: so the feed is a plain object the host test drives (core/tests/sound_feed_test.cpp) with a
// real ring and a real sound engine, while Renderer only wires it to writeAudio / resetAudio.
//
// After a stall longer than kMaxCatchUpFrames (a surface that was off screen, a GL thread that hitched),
// the cursor skips to the newest kMaxCatchUpFrames: what is playing now, in one bounded catch-up, rather
// than seconds of history through the filter bank in one frame. SoundCursor did the same in Kotlin.
//
// Threads: one reader thread (the renderer's GL thread, or an export thread) owns it. The ring must
// outlive the feed; setSource(nullptr) or a new source drops the cursor first.
namespace geode::viz {

class SoundFeed {
public:
    // SoundCursor's old cap (the Kotlin ring's maxWriteFrames, a quarter of 2^16): about 0.34 s at 48 kHz.
    static constexpr int kMaxCatchUpFrames = 1 << 14;
    // Frames read per block; a pump loops until the ring has nothing new.
    static constexpr int kBlockFrames = 1024;

    // Where the samples go: Renderer forwards to writeAudio / resetAudio; the test to a SoundEngine.
    struct Sink {
        virtual ~Sink() = default;
        // The next write begins a new epoch (or a new source): forget what was learned.
        virtual void restart() = 0;
        virtual void write(const float* stereo, int frames, int sampleRateHz) = 0;
    };

    SoundFeed();

    // The ring to hear, or nullptr for none. A different ring starts a new cursor at its newest sample,
    // and its first block restarts the sink (the cursor's first Block is always `restarted`).
    void setSource(const audio::ingest::Ring* ring);
    const audio::ingest::Ring* source() const { return ring_; }
    // The next ring position the cursor reads; -1 with no source.
    int64_t position() const { return cursor_ ? cursor_->position() : -1; }

    // Hands the sink everything new; returns the frames handed over (0 with no source or nothing new).
    int pump(Sink& sink);

    // Speaker sync (#488, sound/interp/SpeakerSync): the same, but only up to [speakerFrame], the playout
    // clock's answer for this frame (where the speaker is, A/V offset applied; negative while the clock knows
    // nothing of the stream yet: hold). Never past it, and never a sample skipped or read twice to get there:
    // at most kMaxCatchUpFrames per call, the rest (a skipped silence's jump, a short hitch) on the next frames.
    // Two exceptions, both of audio this surface never heard: a fresh cursor starts at the speaker (not at the
    // newest sample, which the speaker reaches a whole sink buffer later), and a surface more than
    // kStallFrames behind (off screen, a long hitch) jumps to kMaxCatchUpFrames before the speaker, as pump()
    // does before the newest. The ring's own overrun rule still applies.
    int pumpUntil(Sink& sink, int64_t speakerFrame);
    static constexpr int64_t kStallFrames = int64_t{kMaxCatchUpFrames} * 4;

private:
    // Reads [count] frames from the cursor into the sink, block by block.
    int drain(Sink& sink, int64_t count);

    const audio::ingest::Ring* ring_ = nullptr;
    std::unique_ptr<audio::ingest::Cursor> cursor_;
    std::vector<float> block_;
    // No read since setSource: pumpUntil places the cursor at the speaker first.
    bool fresh_ = false;
};

}  // namespace geode::viz
