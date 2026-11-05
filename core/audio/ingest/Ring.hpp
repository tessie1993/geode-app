#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

// The native ingest ring (ADR 0006, #478, #480): one per audio source (the player's tap, the mic, another
// app's audio, the wallpaper's output-mix capture, a decoded file for export). The source writes raw PCM
// bytes straight from its buffer; every renderer and analyzer reads it through its own Cursor (Cursor.hpp).
//
// Why it replaces SampleRing, SoundCursor, SoundInput and PcmRingBuffer (Kotlin):
// - The rate travelled beside the ring as a separate volatile, so a reader could pair new samples with the
//   old rate across a format change, and an epoch could begin off the writer thread (#361). Here the format
//   and the epoch are written by the writer, in-band, as a record that starts at a sample position: a
//   sample's rate, layout and epoch are whatever record covers its position, so they can never disagree.
// - The int16 -> float loops ran per sample on the playback and capture threads in Kotlin. Conversion is
//   here, once, in C++.
// - Only front left and right were kept (#362). Every channel is kept now, with its layout; readers hear
//   the ingest's one stereo downmix (Downmix.hpp).
//
// Threads: one writer (beginEpoch, setFormat, write), any number of readers, no locks. The writer publishes
// a format record before the samples it covers, then the write position with release order, so a reader
// that sees a position also sees the record for every sample under it. Readers never write to the ring;
// each Cursor is one reader thread's own state.
//
// Overrun: the writer never waits. A write is split into chunks of at most runway() frames and the
// position is published after each; a reader that fell more than capacity - runway frames behind skips to
// the oldest sample still safe (oldestSafe) and is told how many it dropped. Samples are plain floats read
// under a seqlock-style check: a reader copies, then re-reads the write position and throws away anything
// the writer may have overwritten meanwhile.
//
// Format records live in a small ring of their own (kRecords). Each is guarded by a sequence number, so a
// reader that held one while the writer reused its slot (kRecords format changes without a read, which
// only a test does) notices and resynchronises like any overrun.
namespace geode::audio::ingest {

// android.media.AudioFormat ENCODING_* values, which Media3 and AudioRecord hand the tap.
enum Encoding : int {
    kPcm16 = 2,
    kPcm8 = 3,
    kPcmFloat = 4,
    kPcm24Packed = 21,
    kPcm32 = 22,
};

// What feeds a ring (geode_api.h GEODE_SOURCE_*). Set once by whoever creates it; the player's is the default.
// A renderer reads it off the ring it hears, so "is the player the source" (#460: no silence re-warm while
// the song is only paused) follows the ring itself, with no flag of its own to drift from the source switch.
enum SourceKind : int {
    kSourcePlayer = 0,
    kSourceMic = 1,
    kSourceCapture = 2,
    kSourceOutputMix = 3,
    kSourceFile = 4,
};

// Bytes per sample of an encoding; 0 for one the ring cannot read.
int bytesPerSample(int encoding);

// The format that covers ring positions from `start` until the next record's start.
struct Record {
    int64_t start = 0;
    int sampleRate = 0;
    int channels = 0;         // channels kept per frame (the source's, capped at the ring's max)
    int sourceChannels = 0;   // channels per frame in the bytes the writer hands in
    uint32_t mask = 0;        // effective speaker mask of the kept channels (0 = unknown, more than eight)
    uint32_t epoch = 0;
};

class Ring {
public:
    static constexpr int kMaxChannels = 24;   // AudioTrack's own limit (FCC_24)
    static constexpr int kRecords = 128;

    // capacityFrames is rounded up to a power of two (at least 256); maxChannels is clamped to 1..24.
    Ring(int capacityFrames, int maxChannels);
    Ring(const Ring&) = delete;
    Ring& operator=(const Ring&) = delete;

    // --- Writer thread only ---

    // A discontinuity: a seek, a track or source change, a format change across a gap. Readers see the
    // next sample with restarted set and reset what they learned. A rate of 0 or no channels makes later
    // writes drop until a usable format arrives.
    void beginEpoch(int sampleRate, int channels, int channelMask);
    // The same stream in a new format (a flush for speed, pitch or skip-silence, #358): no restart.
    // Repeating the current format publishes nothing.
    void setFormat(int sampleRate, int channels, int channelMask);
    // Converts `frames` interleaved frames of the current format from `bytes` (little-endian) and publishes
    // them. Returns the frames taken: 0 before a format, for an unknown encoding or a null pointer.
    size_t write(const void* bytes, size_t frames, int encoding);
    // The same, sized in bytes: the frames that fit whole. For callers that only know a buffer's length.
    size_t writeBytes(const void* bytes, size_t byteCount, int encoding);

    // --- Any thread ---

    // Frames written since creation, over every epoch; never goes back.
    int64_t written() const { return written_.load(std::memory_order_acquire); }
    // Format records published so far; record(i) for i in [records() - kRecords, records()).
    int64_t records() const { return recordCount_.load(std::memory_order_acquire); }
    // Reads record `index`; false when it is not published yet or its slot has been reused since.
    bool record(int64_t index, Record& out) const;

    // What feeds this ring (SourceKind). Set once after create; any thread reads it. An unknown kind reads
    // as the player.
    void setSourceKind(int kind) { sourceKind_.store(kind, std::memory_order_release); }
    int sourceKind() const { return sourceKind_.load(std::memory_order_acquire); }

    int capacity() const { return capacity_; }
    int maxChannels() const { return stride_; }
    int runway() const { return runway_; }
    // The oldest position a reader may still copy when the write position is `written`: anything older
    // may be under the writer's pen.
    int64_t oldestSafe(int64_t written) const {
        const int64_t oldest = written + runway_ - capacity_;
        return oldest > 0 ? oldest : 0;
    }
    // The kept channels of the frame at `position`, valid while position >= oldestSafe(written()).
    const float* frame(int64_t position) const {
        return data_.data() + static_cast<size_t>(position & mask_) * static_cast<size_t>(stride_);
    }

    // The core/sound/Silence rule, fed by this ring: 1 when nothing above about -42 dBFS has been written
    // for the hold (6 s). No writes at all counts as silence, so a source that stops is resting after the
    // same hold. The first poll counts as hearing something, so a surface never rests the instant it
    // starts. Any thread; meant for one poller, and two pollers only ever move the heard time forward.
    bool resting(int64_t nowMs);

private:
    struct RecordSlot {
        std::atomic<uint64_t> seq{0};   // 2 * index + 2 once record `index` is complete; odd while written
        std::atomic<int64_t> start{0};
        std::atomic<int> sampleRate{0};
        std::atomic<int> channels{0};
        std::atomic<int> sourceChannels{0};
        std::atomic<uint32_t> mask{0};
        std::atomic<uint32_t> epoch{0};
    };

    void publish(const Record& r);

    int capacity_;
    int stride_;
    int runway_;
    int64_t mask_;
    std::vector<float> data_;
    RecordSlot slots_[kRecords];

    std::atomic<int64_t> written_{0};
    std::atomic<int64_t> recordCount_{0};
    std::atomic<int64_t> audibleAt_{-1};   // write position just past the newest audible chunk
    std::atomic<int> sourceKind_{kSourcePlayer};

    // Writer-only state.
    Record current_{};
    int64_t pen_ = 0;
    bool hasFormat_ = false;

    // Poller state for resting().
    static constexpr int64_t kNeverPolled = INT64_MIN;
    std::atomic<int64_t> heardAtMs_{kNeverPolled};
    std::atomic<int64_t> seenAudible_{-1};
};

}  // namespace geode::audio::ingest

// The C ABI's handle (core/api/geode_api.h): the ring itself. core/viz and core/sound take `&h->ring`.
struct geode_ingest {
    geode_ingest(int capacityFrames, int maxChannels) : ring(capacityFrames, maxChannels) {}
    geode::audio::ingest::Ring ring;
};
