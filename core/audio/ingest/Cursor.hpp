#pragma once

#include <cstdint>

#include "audio/ingest/Ring.hpp"

// One reader's place in an ingest Ring (the native SoundCursor, #480).
//
// The sound engine runs a filter bank over every sample as it arrives, and Group D's hop analyzer cuts the
// stream into fixed hops, so each reader must get every sample exactly once, in order: a skipped stretch
// reads as a hole in the drums, a repeated one as a double hit. A cursor remembers the next position it owes
// and only moves forward. Each renderer (screen, cast, wallpaper, dream, export) and each analyzer thread
// owns its own, so none of them steals samples from another.
//
// What a read returns: up to maxFrames frames of the ingest's stereo downmix (Downmix.hpp), interleaved
// L R, all from one format record. A read stops at a format or epoch boundary, so every frame of a Block
// shares the Block's rate, layout and epoch, exactly as they were written.
//
// restarted: the Block's first frame begins an epoch this cursor has not read before (a seek, a track or
// source change: Ring::beginEpoch), or it is the cursor's first Block. The reader resets what it learned
// (tempo, key, sections) before feeding it. A format change inside an epoch (Ring::setFormat: speed,
// pitch, skip-silence) changes sampleRate but never sets restarted (#358).
//
// dropped: frames skipped before this Block because the cursor fell behind by more than the ring holds
// (or was told to skip). The position then jumps to the oldest sample still safe; nothing is ever read
// twice and nothing torn is ever handed out.
//
// Threads: a cursor belongs to one reader thread. The ring's writer runs concurrently; the ring must outlive
// the cursor. No allocation, no locks, no syscalls: safe on a GL or analysis thread every frame.
namespace geode::audio::ingest {

class Cursor {
public:
    enum class Start {
        Newest,   // hear from now on: the first read returns what is written after the cursor was made
        Oldest,   // everything the ring still safely holds (tests, export, a late analyzer)
    };

    struct Block {
        int frames = 0;              // stereo frames written to `out`
        int64_t position = 0;        // ring position of the first frame
        int sampleRate = 0;
        int sourceChannels = 0;      // the layout it was downmixed from
        uint32_t channelMask = 0;    // effective speaker mask (0 = unknown, more than eight channels)
        uint32_t epoch = 0;
        bool restarted = false;
        int64_t dropped = 0;         // frames skipped (overrun or skipToNewest) since the previous Block
    };

    explicit Cursor(const Ring& ring, Start start = Start::Newest);

    // Copies up to maxFrames stereo frames (2 * maxFrames floats) into out. frames == 0 when nothing new
    // is written; then only `dropped` may be set.
    Block read(float* stereoOut, int maxFrames);

    // The newest n frames this cursor has read, as mono ((L + R) / 2 of the downmix), oldest first: the raw
    // PCM tail projectM takes. It ends at the cursor's position, so the tail is what the engine just heard,
    // and it never reaches back across the current epoch's start. Returns how many it wrote (fewer than n
    // early in an epoch or when the ring no longer holds them).
    int rawMonoTail(float* out, int n) const;

    // The next ring position this cursor will read.
    int64_t position() const { return position_; }
    // Frames written but not read yet.
    int64_t behind() const;
    // Skips all but the newest keepFrames unread frames (a live surface that stalled catches up on what is
    // playing now rather than seconds of history). Counted in the next Block's dropped; an epoch crossed
    // on the way still restarts the reader. Returns the frames skipped.
    int64_t skipToNewest(int64_t keepFrames);

private:
    // The record covering `position`, found from the newest back; `end` is where it stops covering
    // (the next record's start, or `written`). False when the record ring no longer holds it.
    bool locate(int64_t position, int64_t written, int64_t records, Record& rec, int64_t& end) const;
    // The oldest position any held record covers, for a cursor older than every record.
    int64_t oldestCovered(int64_t records) const;
    void loadGains(const Record& rec);

    const Ring& ring_;
    int64_t position_ = 0;
    int64_t pendingDropped_ = 0;
    uint32_t epoch_ = 0;
    bool hasEpoch_ = false;
    int64_t epochStart_ = 0;   // first position of the current epoch this cursor saw

    uint32_t gainsMask_ = 0;
    int gainsChannels_ = -1;
    float left_[Ring::kMaxChannels] = {};
    float right_[Ring::kMaxChannels] = {};
};

}  // namespace geode::audio::ingest
