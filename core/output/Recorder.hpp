#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "audio/dsp/PcmResampler.hpp"
#include "output/EncodePlan.hpp"

namespace geode::output::record {

// Record's clock (Visuals › Record): a video of the visual plus the sound, captured live. The stage's GL
// thread tees each finished composite into a hardware encoder and the PCM its sound engine just heard into
// this; an encoder thread takes the audio back out. Everything that times the file is decided here and
// tested on the host (core/tests/recorder_test.cpp): which stage frames are kept and their PTS, where each
// heard sample sits on the file's timeline, when the recording ends and how long it is. Kotlin owns only
// MediaCodec, MediaMuxer and MediaStore; the GL copy is core/viz/frame/RecordTap.
//
// Picture. Time 0 is the first frame kept. The frame rate is a ceiling: one frame per 1/fps slot. A frame
// up to half a stage frame before a slot's start already belongs to that slot, so vsync jitter never doubles
// or skips one; a frame near the slot's start is stamped on the grid, any other with its real time. So a 60
// or 120 fps stage records the same frames at a 30 fps ceiling as a 30 fps stage does, and a stage the heat
// slowed records what it drew. A frame is dropped,
// never waited for, while the encoder holds kMaxPending frames it has not put out (the GL thread must not
// block in the encoder's queue); after kProbeNs without one it is tried again, so an encoder that buffers
// more than that cannot starve the file.
//
// Sound is what the visual heard, where it heard it: each block the renderer's engine takes (the song, the
// mic, another app's audio, the mic layered over the song) lands so its last sample sits at the stream time
// the engine heard it. The renderer reads the ring as it is written, ahead of the speaker, so the room hears
// a beat after the picture shows it; the file does not. Blocks arrive in bursts (a decoder buffer at a
// time), so the writer runs kLookbackMs behind the picture: within that window a block is laid back to
// where it belongs, and only a real gap (a pause, no source) becomes silence. Alignment is checked on every
// block: audio more than kSyncToleranceMs late (a seek's burst, a faster clock) loses its oldest frames,
// first from silence still waiting, then from the block; audio that stayed early for kEarlyHoldNs, or
// arrives after silence was written, is moved up with silence. So a sample is never further than
// kSyncToleranceMs from the picture it drove, and a pause records the still picture and silence.
//
// The end: the user, the maximum length (Settings › Export › Record) or the entitlement's cap, whichever is
// shorter, a stage that changed size (a rotation; the encoder's frame is fixed), a file near the muxer's
// 4 GB line, or a failure. The audio then runs exactly to the stop time, the lookback included.
//
// Threads: frame() and hear() on the stage's GL thread; takeAudio(), videoEncoded() and wrote() on the
// encoder thread; stop() and the readers anywhere. One short mutex guards the audio hold.
enum class Stop : int {
    None = 0,
    User = 1,
    MaxLength = 2,
    Resized = 3,
    FileFull = 4,
    Failed = 5,
};

struct Config {
    int stageWidth = 0;    // the stage's surface, in pixels, when the recording starts
    int stageHeight = 0;
    int fps = 30;          // the frame-rate ceiling
    int maxSec = 0;        // Settings › Export › Record's maximum length; 0 = none
    int64_t capMs = 0;     // the entitlement's cap (a trial's minute); 0 = none
};

class Recorder {
public:
    static constexpr int kAudioRateHz = 48'000;
    static constexpr int kChannels = 2;
    static constexpr int64_t kLookbackMs = 400;
    static constexpr int64_t kSyncToleranceMs = 80;
    static constexpr int64_t kEarlyHoldNs = 1'000'000'000;
    static constexpr int kMaxPending = 4;
    static constexpr int64_t kProbeNs = 250'000'000;
    // A frame within this share of a slot of the slot's start is stamped on the grid.
    static constexpr int kSlotToleranceDiv = 8;
    static constexpr int kStageSmoothing = 8;
    // The most audio kept waiting for the encoder thread; a block past it loses its oldest frames.
    static constexpr int64_t kHoldMaxMs = 2'000;
    // Where the file stops itself: under what a render's bit rate aims for, with the index still to write.
    static constexpr int64_t kFileLimitBytes = encode::kTargetBytes;

    explicit Recorder(const Config& config);

    // One stage frame about to be drawn at nowNs (monotonic) on a stage of this size: its PTS in ns when
    // it goes into the file, -1 when it does not (too soon for the ceiling, the encoder is behind, or the
    // recording has ended, which this may decide).
    int64_t frame(int64_t nowNs, int stageWidth, int stageHeight);

    // A block the renderer's sound engine just took, interleaved float at rateHz. Nothing before the first
    // frame or after the end is kept.
    void hear(const float* interleaved, int frames, int channels, int rateHz, int64_t nowNs);

    // The ring started over (a seek, another source): the next block is placed exactly.
    void restarted();

    // The encoder put one video frame out.
    void videoEncoded() { encoded_.fetch_add(1, std::memory_order_relaxed); }

    // The muxer wrote this many bytes more; ends the recording once the file is full.
    void wrote(int64_t bytes, int64_t nowNs);

    // Up to capacityFrames of 48 kHz stereo for the AAC encoder, stamped *ptsUs: everything due kLookbackMs
    // behind nowNs, or, once stopped, everything to the end. 0 when nothing is due; after the end, 0 means
    // the audio is complete.
    int takeAudio(int64_t nowNs, int16_t* out, int capacityFrames, int64_t* ptsUs);

    // Ends the recording at nowNs (or at its limit, if that came first). The first reason wins.
    void stop(Stop why, int64_t nowNs);

    Stop stopped() const { return static_cast<Stop>(stop_.load(std::memory_order_acquire)); }
    bool started() const { return started_.load(std::memory_order_acquire); }
    // The length so far, or the final length once stopped.
    int64_t recordedMs(int64_t nowNs) const;
    int64_t framesTaken() const { return taken_.load(std::memory_order_relaxed); }
    int64_t framesDropped() const { return dropped_.load(std::memory_order_relaxed); }
    int64_t audioFrames() const;

    // The longest this recording may run, in ns: the shorter of the two limits, 0 for neither.
    int64_t limitNs() const { return limitNs_; }

private:
    int64_t position(int64_t elapsedNs) const;
    int64_t held() const { return static_cast<int64_t>(hold_.size()) / kChannels; }
    void stopLocked(Stop why, int64_t nowNs);
    void place(int64_t frames, int64_t nowNs);
    int64_t eatPad(int64_t frames);
    void fillGap(int64_t frames);

    const Config config_;
    const int64_t limitNs_;

    std::atomic<bool> started_{false};
    std::atomic<int> stop_{0};
    std::atomic<int64_t> startNs_{0};
    std::atomic<int64_t> stopNs_{0};
    std::atomic<int64_t> encoded_{0};
    std::atomic<int64_t> taken_{0};
    std::atomic<int64_t> dropped_{0};
    std::atomic<int64_t> bytes_{0};

    // GL thread only.
    int64_t lastSlot_ = -1;
    int64_t lastFrameNs_ = -1;
    int64_t stageFrameNs_ = -1;
    int64_t lastPtsNs_ = -1;
    int64_t lastSubmitNs_ = 0;

    mutable std::mutex audioLock_;
    std::vector<int16_t> hold_;       // stream frames [emitted_, emitted_ + held()), interleaved
    int64_t emitted_ = 0;
    int64_t padStart_ = 0;            // the last silence laid into the hold: stream frames [padStart_, padEnd_)
    int64_t padEnd_ = 0;
    bool exact_ = true;               // the next block is placed exactly (the first, after silence, a restart)
    int64_t earlySinceNs_ = -1;
    int64_t earlyLeast_ = 0;
    std::unique_ptr<audio::PcmResampler> resampler_;
    int resamplerRateHz_ = 0;
    std::vector<int16_t> source_;     // the block as stereo s16 at its own rate
    std::vector<int16_t> block_;      // the block at kAudioRateHz
};

// The encoder's frame for a stage: the stage's shape, its short side the chosen one or the stage's own when
// that is smaller (the file cannot be sharper than the screen), each side even and inside H.264's limits.
encode::FrameSize frameSize(int stageWidth, int stageHeight, int shortSide);

// The video bit rate for that frame at fps: 12 Mbit/s for 1080p30, by area, half as much again above
// 30 fps, inside 2 to 40 Mbit/s, and lowered when a maximum length would pass the muxer's target.
int videoBps(int width, int height, int fps, int64_t limitMs);

}  // namespace geode::output::record
