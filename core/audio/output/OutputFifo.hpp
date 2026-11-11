#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

// The playback output's FIFO (core/audio/output/README.md): what sits between Media3's sink, which
// hands over PCM whenever its playback thread runs, and the audio device's callback, which pulls a
// burst every few milliseconds on a real-time thread. It also keeps the count the sink asks for: how
// many of the frames it wrote the listener has heard. No Oboe here, so the host tests reach all of it;
// OboeOutput owns one and feeds it the stream's timestamps.
//
// One producer, one consumer, PcmRing's conventions: positions are frame counts that only grow (so a
// slot index never repeats early), the producer copies a block in and then publishes the new write
// position, the consumer copies frames below it and then publishes its read position. No locks and no
// allocation after construction on either side. The samples are relaxed atomics (plain loads and
// stores on every target) for the one case two threads can meet on a slot: a flush while the consumer
// is copying. The producer may reuse the flushed slots at once; the consumer sees the flush after its
// copy and hands out silence instead of that copy, so a torn frame is never played.
//
// Threads:
//   - the control thread (Media3's playback thread): write*, flush, play, pause, stop, positionUs,
//     takeEvents, stalled, ended. One thread, the producer.
//   - the consumer (the device's audio callback): read. Nothing else.
//   - any thread: setVolume and the counters.
//   - streamRestarted: whoever reopened the stream, while no callback runs.
//
// Units: frames (one sample per channel), monotonic nanoseconds (CLOCK_MONOTONIC, System.nanoTime),
// microseconds for the position the sink reads.
namespace geode::audio::output {

// Sample encodings of a raw byte block, numbered as Android's AudioFormat.ENCODING_PCM_* (and Media3's
// C.ENCODING_PCM_*), so the shell passes its own constant through. Bytes are in the machine's order.
enum Encoding : int {
    kSigned16 = 2,  // x / 32768, PcmRing's scaling
    kFloat32 = 4,
};

// What the device stream last said about itself: the frame at framePosition (counted from the
// stream's start, silence included) reaches the speaker at timeNs. Oboe's getTimestamp.
struct StreamTime {
    bool valid = false;
    int64_t framePosition = 0;
    int64_t timeNs = 0;
};

// takeEvents' bits.
enum Event : int {
    kEventAdvancing = 1,  // the position moved for the first time since play()
    kEventUnderrun = 2,   // the consumer ran dry mid-stream since the last take
};

class OutputFifo {
public:
    // What an output can be opened for. Rates outside this are left to the platform's own track.
    static constexpr int kMinRateHz = 8000;
    static constexpr int kMaxRateHz = 192000;
    static constexpr int kMaxChannels = 2;

    // The FIFO's length in time. A music player wants glitch-free before low latency: the default is
    // what Media3 gives an AudioTrack for PCM (250 ms), so the sink's pacing and the gain ramps' lag
    // (PlaybackSession.SINK_LAG_MS) stay what they are with the platform track.
    static constexpr int kDefaultLatencyMs = 250;
    static constexpr int kMinLatencyMs = 100;
    static constexpr int kMaxLatencyMs = 1000;

    // A full-scale volume change takes this long, so setVolume never clicks.
    static constexpr int kRampMs = 10;

    // A timestamp older than this is not run on from; the frames-read estimate takes over.
    static constexpr int64_t kMaxExtrapolateNs = 1'000'000'000;

    // Playing, frames waiting, and the consumer has not asked for this long: the stream is stuck.
    static constexpr int64_t kStallNs = 2'000'000'000;

    // 16-bit or float PCM, mono or stereo, kMinRateHz..kMaxRateHz.
    static bool supports(int encoding, int channelCount, int sampleRateHz);

    // Bytes of one frame; 0 for an encoding or channel count supports() refuses.
    static int bytesPerFrame(int encoding, int channelCount);

    // The capacity for a sink buffer of bufferBytes in `encoding` (Media3's OutputConfig.bufferSize),
    // kept within kMinLatencyMs..kMaxLatencyMs of audio; kDefaultLatencyMs when the size says nothing.
    static int capacityFrames(int sampleRateHz, int channelCount, int encoding, int bufferBytes);

    // Only for a format supports() accepts and capacityFrames > 0.
    OutputFifo(int sampleRateHz, int channelCount, int capacityFrames);

    OutputFifo(const OutputFifo&) = delete;
    OutputFifo& operator=(const OutputFifo&) = delete;

    int sampleRateHz() const { return rate_; }
    int channelCount() const { return channels_; }
    int capacityFrames() const { return capacity_; }

    // ---- the producer -------------------------------------------------------------------------

    // Interleaved frames at channelCount. Never blocks: takes as many whole frames as fit and returns
    // how many; the caller offers the rest again.
    int writeFloat(const float* interleaved, int frames);
    int writeInt16(const int16_t* interleaved, int frames);
    // Whole frames of `encoding` in `bytes` bytes, no alignment needed; a trailing part of a frame is
    // never taken. Returns the frames taken, 0 for an encoding it does not know.
    int writeBytes(const void* data, size_t bytes, int encoding);

    // Drops what is waiting and starts the counts and the position again from 0. The consumer may be
    // running: it hands out silence for a burst the flush cut through.
    void flush();

    // ---- control ------------------------------------------------------------------------------

    // The device stream starts or resumes. Timestamps taken before nowNs are not trusted after it.
    void play(int64_t nowNs);
    // Freezes the position where positionUs last put it; call positionUs first for an exact stop.
    void pause();
    // The last frame was written: what is waiting plays out, then ended() holds.
    void stop(int64_t nowNs);
    // 0..1, clamped. Reached over kRampMs in the consumer; the first frames ever read start at it.
    void setVolume(float volume);
    // A new device stream replaced the old one (a route change): its frame count starts at 0 again.
    void streamRestarted();

    // ---- the consumer -------------------------------------------------------------------------

    // Fills out with `frames` interleaved frames at the volume: what is waiting, then silence. Never
    // blocks, locks or allocates.
    void read(float* out, int frames);

    // ---- counts -------------------------------------------------------------------------------

    int64_t framesWritten() const;  // since the last flush
    int64_t framesPlayed() const;   // handed to the device since the last flush
    int64_t pendingFrames() const { return framesWritten() - framesPlayed(); }
    // Times the consumer ran dry after it had played something and before stop().
    int64_t underruns() const { return underruns_.load(std::memory_order_relaxed); }

    // How much of what was written the listener has heard at nowNs, in microseconds of audio. From the
    // stream's timestamp when it has a fresh one (the frame it names, run on to nowNs, less the silence
    // the stream was given), else the frames read less latencyFrames (the stream's own buffer). Never
    // goes back, never passes the frames read, stands still while paused, 0 after a flush. After stop()
    // it also runs on by the clock, so the end is reached even when the stream stops reporting.
    int64_t positionUs(int64_t nowNs, const StreamTime& stream, int64_t latencyFrames);

    // positionUs' last answer in frames.
    int64_t positionFrames() const { return lastPosition_; }

    // stop() was called and positionUs has reached the last frame written.
    bool ended() const;

    // Event bits since the last call.
    int takeEvents();

    // True once the consumer has not asked for kStallNs while playing with frames waiting.
    bool stalled(int64_t nowNs);

private:
    template <typename Sample>
    int put(const unsigned char* data, int frames, size_t width, Sample sample);
    int64_t framesIn(int64_t ns) const;

    const int rate_;
    const int channels_;
    const int capacity_;
    const int64_t mask_;      // the storage is the next power of two at or above capacity_
    const float rampStep_;    // gain per frame
    std::vector<std::atomic<float>> samples_;

    std::atomic<int64_t> write_{0};  // frames ever written; the producer's
    std::atomic<int64_t> read_{0};   // frames ever read; the consumer's
    std::atomic<int64_t> floor_{0};  // the last flush: frames below it are gone; the producer's

    // The consumer's.
    std::atomic<int64_t> delivered_{0};  // frames handed to the current device stream, silence included
    std::atomic<int64_t> lead_{0};       // the silence the stream was given ahead of the last frame read:
                                         // a stream frame position less this is a position in what was
                                         // written (past the last frame read, all of it was heard)
    std::atomic<int64_t> underruns_{0};
    bool underrun_ = false;
    float gain_ = -1.0f;  // below 0: nothing read yet, take the target as it is

    std::atomic<float> gainTarget_{1.0f};
    std::atomic<bool> stopped_{false};

    // The control thread's.
    bool playing_ = false;
    int64_t playStartNs_ = 0;
    int64_t lastPosition_ = 0;
    bool advancePending_ = false;
    int64_t stopPosition_ = 0;
    int64_t stopNs_ = 0;
    int events_ = 0;
    int64_t underrunsSeen_ = 0;
    bool stallWatching_ = false;
    int64_t stallDelivered_ = 0;
    int64_t stallSinceNs_ = 0;
};

}  // namespace geode::audio::output
