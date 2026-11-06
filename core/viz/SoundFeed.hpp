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
    // Whether the ring heard is the player's (or a decoded file's: an export counts as the player); false
    // for a capture ring or none. Read off the ring (Ring::sourceKind), so it can't disagree with the source.
    bool playerIsSource() const {
        if (!ring_) return false;
        const int kind = ring_->sourceKind();
        return kind == audio::ingest::kSourcePlayer || kind == audio::ingest::kSourceFile;
    }

    // Hands the sink everything new; returns the frames handed over (0 with no source or nothing new).
    int pump(Sink& sink);

private:
    const audio::ingest::Ring* ring_ = nullptr;
    std::unique_ptr<audio::ingest::Cursor> cursor_;
    std::vector<float> block_;
};

}  // namespace geode::viz
