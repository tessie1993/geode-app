#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

// The PCM ingest ring (ADR 0006, core/audio/ingest/README.md): one ring per audio source (the player's
// tap, the mic, another app's audio, the wallpaper's output-mix capture). Ported from the Kotlin
// SampleRing and SoundCursor, with the rate inside the ring instead of in a volatile beside it.
//
// One writer at a time, any number of readers. The writer copies a block in, then publishes the new
// head; a reader loads the head once and copies frames below it. Readers never write to the ring (a
// Cursor holds it const), so a second reader, such as an audio output callback, reads the same samples.
// No locks and no allocation after construction on either side.
//
// The runway keeps a slow reader off the slots being written: the writer publishes at most
// maxWriteFrames at a time and a reader never starts below head + maxWriteFrames - capacity, so a copy
// that starts at the oldest safe frame is only overtaken after the writer publishes a whole runway
// more. A reader that slow is caught: it reads the head again after its copy and throws the copy away
// when the writer could have reached those slots, so torn audio is never handed on. The samples are
// relaxed atomics (plain loads and stores on every target), which makes that discarded read defined.
//
// One writer is the contract, and the shell keeps it (PcmTap.owns). Two writers at once still cannot
// move the head backwards or tear a format mark: a block whose publish finds the head moved is dropped
// and its writer carries on from the new head.
//
// The clock is the sample count. No wall time.
namespace geode::audio::ingest {

// Sample encodings of a raw byte block, numbered as Android's AudioFormat.ENCODING_PCM_* so the shell
// passes its own constant through. Bytes are in the machine's order (little-endian on every target).
enum Encoding : int {
    kSigned16 = 2,   // x / 32768
    kUnsigned8 = 3,  // (x - 128) / 128
    kFloat32 = 4,
};

class Cursor;

class PcmRing {
public:
    // The runway when none is given: a quarter of the ring, as SampleRing had it.
    static constexpr int kRunwayDivisor = 4;

    // capacityFrames a power of two, channelCount > 0, maxWriteFrames in 1..capacityFrames (0 = a quarter).
    static bool valid(int capacityFrames, int channelCount, int maxWriteFrames);

    // Only for arguments valid() accepts.
    PcmRing(int capacityFrames, int channelCount, int maxWriteFrames = 0);

    PcmRing(const PcmRing&) = delete;
    PcmRing& operator=(const PcmRing&) = delete;

    int capacityFrames() const { return capacity_; }
    int channelCount() const { return channels_; }
    int maxWriteFrames() const { return maxWrite_; }

    // Frames written since the last beginEpoch().
    int64_t writtenFrames() const;

    // Frames ever written, across epochs: the position a playout clock's anchors and a cursor's limit
    // count in (sound/interp/PlayoutClock).
    int64_t headFrames() const { return head_.load(); }

    // A new stream (track change, seek, source switch): every cursor restarts at its next take and hands
    // out nothing written before this. Any thread, also several at once.
    void beginEpoch();

    struct Written {
        int frames = 0;     // 0 when the block was refused (null, no frames, a bad channel count or rate)
        float peak = 0.0f;  // the largest magnitude stored, for the capture's level
    };

    // Interleaved frames of sourceChannels at sampleRateHz. The ring keeps its own channelCount: missing
    // channels are stored silent, extra ones dropped. The rate and the source's channel count travel with
    // the samples, so a reader gets every frame with the rate it was written at. Any block length: it is
    // published a runway at a time. Writer thread only.
    Written writeFloat(const float* interleaved, int frames, int sourceChannels, int sampleRateHz);
    Written writeInt16(const int16_t* interleaved, int frames, int sourceChannels, int sampleRateHz);
    // Whole frames of `encoding` in `bytes` bytes; a trailing part of a frame is ignored. No alignment needed.
    Written writeBytes(const void* data, size_t bytes, int encoding, int sourceChannels, int sampleRateHz);

    // The newest maxFrames whole frames of `encoding` in `bytes` bytes, as interleaved float at sourceChannels,
    // converted as the writers convert. Returns the frames decoded: 0 for an unknown encoding or no frames.
    static int decode(const void* data, size_t bytes, int encoding, int sourceChannels, float* out, int maxFrames);

    // The newest `frames` frames, interleaved at channelCount, for a level check. False when this epoch
    // has not written that many yet, the ring cannot hold them, or the writer reached them during the copy.
    bool snapshotLatest(float* interleaved, int frames) const;

private:
    friend class Cursor;

    // A stretch of frames written with one format, from `start` on.
    struct Mark {
        std::atomic<int64_t> start{0};
        std::atomic<uint64_t> format{0};
    };
    struct Run {
        int sampleRateHz = 0;
        int sourceChannels = 0;
        int64_t end = 0;  // the first frame past `pos` that has another format, or the limit
    };

    // Format changes kept: a reader further behind than this many changes reads the oldest kept format.
    static constexpr uint32_t kMarks = 16;
    // The epoch word is the epoch's number over the head it began at, so both change in one store.
    static constexpr int kEpochShift = 48;
    static constexpr uint64_t kStartMask = (uint64_t{1} << kEpochShift) - 1;

    template <typename Read>
    Written put(const unsigned char* data, int frames, int sourceChannels, int sampleRateHz, size_t width,
                Read read);
    void mark(int64_t head, uint64_t format);
    Run runAt(int64_t pos, int64_t limit) const;
    void copyOut(int64_t first, int count, bool monoSource, float* interleaved) const;
    // After a copy of frames from `first`: false when the writer could have reached their slots meanwhile.
    bool stillIntact(int64_t first) const;
    static int64_t startOf(uint64_t epochWord) { return static_cast<int64_t>(epochWord & kStartMask); }

    const int capacity_;
    const int channels_;
    const int maxWrite_;
    const int64_t mask_;
    std::vector<std::atomic<float>> samples_;  // capacity_ frames, interleaved at channels_
    std::atomic<int64_t> head_{0};  // frames ever written; never reset, so a slot index never repeats early
    std::atomic<uint64_t> epoch_{0};
    Mark marks_[kMarks];
    std::atomic<uint32_t> markCount_{0};
    std::atomic_flag markBusy_;  // held for the few stores of one append, so two writers cannot tear a slot
};

// One reader's place in a ring: every frame once, in order (SoundCursor.take's rules). The sound engine
// runs a filter bank over every sample, so a skipped stretch reads as a hole and a repeated one as a
// double hit. One thread per cursor; the ring must outlive it (the C ABI's handle shares ownership).
class Cursor {
public:
    struct Take {
        int frames = 0;
        bool restarted = false;  // a new epoch or a first take: reset the consumer before using the frames
        bool skipped = false;    // a stall: frames between the last take and this one were dropped
        int sampleRateHz = 0;    // the rate these frames were written at; 0 with no frames
    };

    // take()'s limit for a reader with no sink between the ring and the listener: the newest frame.
    static constexpr int64_t kNewest = INT64_MAX;

    // maxFrames is the most one take hands out, so a long stall costs one bounded catch-up; 0 = the runway.
    explicit Cursor(const PcmRing& ring, int maxFrames = 0);

    // What the ring gained since the last take, into interleaved(). After a stall it keeps the newest
    // audio. A take stops where the format changes, so one take has one rate; the rest comes next take.
    // A take the writer lapped during the copy hands out no frames, and the next one restarts.
    //
    // limit (a headFrames() position, exclusive) is for a reader behind a sink: the frame the speaker
    // plays now, so the take stops there and "newest" above reads "newest below the limit". A limit at or
    // behind the last take hands out nothing and never rewinds. A new epoch restarts the cursor once the
    // limit reaches it; until then the listener still hears the old stream's tail (a gapless track
    // change) and the cursor reads it. A limit the ring no longer keeps is raised to a runway above the
    // oldest frame it does: such a reader runs ahead of the speaker rather than stand still.
    Take take(int64_t limit = kNewest);

    // The last take's frames, frames * channels() floats. A mono source is handed out on every channel.
    const float* interleaved() const { return out_.data(); }
    int channels() const { return ring_.channelCount(); }
    int maxFrames() const { return maxFrames_; }

    // Forget the position: the next take starts from the newest audio and reports a restart.
    void reset() { pending_ = true; }

private:
    const PcmRing& ring_;
    const int maxFrames_;
    int64_t read_ = 0;
    uint64_t epoch_ = 0;
    bool pending_ = true;
    std::vector<float> out_;
};

}  // namespace geode::audio::ingest
