#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "audio/dsp/PcmResampler.hpp"

// The pure half of a capture take (ADR 0008): every timing and sizing decision the take makes, with no
// GL, EGL or MediaCodec in it, so core/tests/take_clock_test.cpp can prove it on the host. The device half,
// output/TakeSession (codecs, muxer, worker thread) and viz/TakeSurface (the encoder's EGL surface), only
// carries out what these say.
//
// One clock for both tracks: CLOCK_BOOTTIME nanoseconds, the clock the live renderer's frame time is on
// (VisualizerRenderer passes elapsedRealtimeNanos, which is BOOTTIME). Time zero of the take is the moment
// it began; a video frame's time is the engine frame time it was rendered for, an audio block's the moment
// the record tap wrote it. The record tap sits at the end of the same chain as the analysis tap, so a block
// is written when the analysis tap hears it: the MP4 pairs each frame with the sound that frame reacted to.
namespace geode::output {

// Where a take's sound comes from, fixed when it begins: the player's record tap (a track take), or the
// mic or another app's audio, whichever owns the sound ring then (#458). Only that source is heard; the other
// writing at the same time (the player starting under a mic take) is not mixed in.
enum class TakeSource : int {
    Player = 0,
    Input = 1,
};

// The mic's and the capture's float PCM (-1..1) as 16-bit: clamped, then rounded to nearest.
void toPcm16(const float* in, int samples, int16_t* out);

struct TakeSize {
    int width = 0;
    int height = 0;
};

// The encoder size for attempt `attempt` (0, 1, 2 …) at a stage of screenW × screenH. Attempt 0 is the
// screen itself (rounded down to even sizes, which every AVC encoder needs); each later attempt is 3/4 of
// the one before, aligned down to 16, for an encoder that refused the size above it (a software encoder
// caps the long side near 2048). Neither side ever exceeds kMaxSide. {0, 0} past kMaxAttempts or for a
// stage with no size: the take cannot start.
TakeSize takeSize(int screenW, int screenH, int attempt);
inline constexpr int kMaxAttempts = 5;
inline constexpr int kMaxSide = 4096;

// The capture's frame rate: the display's, held to 24..60. A take is as smooth as the screen up to 60 fps;
// above that (90 and 120 Hz panels) the encoder at screen size is the limit, so every other frame goes.
float takeFps(float displayFps);

// AVC bit rate for a size and rate: about 0.15 bits a pixel, which keeps a busy visual free of blocking at
// phone sizes, held to 2..40 Mbit/s.
int takeBitrate(TakeSize size, float fps);

// Which rendered frames the take keeps, and the presentation time of each (µs since the take began). A frame
// rendered before the take began, or sooner than 3/5 of a frame period after the last one kept, is skipped:
// a 120 Hz screen records at 60, and a frame's jitter never drops a frame at the display's own rate. Times
// only go forward, so the encoder never sees a timestamp twice. GL thread only.
//
// A trial take is limited exactly like a trial render (FeatureGate.renderLimits): capUs > 0 is the longest it
// may run, and no frame past it is kept. reachedCap() says so, from any thread, and the shell then stops the
// take; the file ends at the cap whenever the stop arrives.
class TakeFramePicker {
public:
    TakeFramePicker(int64_t startNs, float fps, int64_t capUs = 0);

    std::optional<int64_t> admit(int64_t frameNs);

    bool reachedCap() const { return reachedCap_.load(std::memory_order_relaxed); }

    // A held take (TakeRules: pause holds the capture) keeps no frame, and its clock stops: the time held is
    // taken out of every later frame's time, so the file runs on without a gap. Any thread.
    void hold(int64_t nowNs);
    void release(int64_t nowNs);

    // The last kept frame's time, or -1 before the first. Read after the GL thread stops presenting.
    int64_t lastPtsUs() const { return lastUs_; }
    int64_t frames() const { return frames_; }

private:
    int64_t startNs_;
    int64_t minGapUs_;
    int64_t capUs_;
    std::atomic<bool> reachedCap_{false};
    std::atomic<int64_t> heldNs_{0};       // every hold so far, added up
    std::atomic<int64_t> holdSinceNs_{0};  // when the current hold began; 0 while running
    int64_t lastUs_ = -1;
    int64_t frames_ = 0;
};

// The take's sound: whatever the record tap writes, as 48 kHz stereo 16-bit, contiguous from time zero.
//
// - The stream starts at time zero. The first block is placed at the moment it was written, with silence
//   before it, so the audio and video share one time zero.
// - After that, blocks follow one another sample by sample (the sink writes ahead in bursts, so placing each
//   by its own arrival time would jitter). The audio clock and BOOTTIME drift by parts per million, far under
//   a frame over any take.
// - A gap of more than kGapNs with nothing written (a pause, a stall, no source at all: a take with nothing
//   playing) is filled with silence, so the track never stops while the picture runs. The fill keeps up as
//   read() is called, and the first block after it lands exactly at its own time again.
// - Any rate, mono, stereo or more (the first two channels) is accepted; a rate change mid-take (the next
//   song) starts a new resampler.
//
// - A capped take's sound stops at the cap with its picture: read() never hands out a frame past capUs.
//
// write() runs on the playback thread, read() on the take's encoder thread; one mutex, held for a copy.
class TakeAudio {
public:
    static constexpr int kRate = 48000;
    static constexpr int kChannels = 2;
    static constexpr int64_t kGapNs = 200'000'000;

    explicit TakeAudio(int64_t startNs, int64_t capUs = 0, TakeSource source = TakeSource::Player);

    // A write from a source other than the take's is dropped.
    void write(const int16_t* pcm, int frames, int channels, int rateHz, int64_t wallNs, TakeSource from = TakeSource::Player);
    TakeSource source() const { return source_; }

    // Up to maxFrames stereo frames into out, padding silence first when nothing has been written for longer
    // than kGapNs before nowNs. Returns the frames written; 0 when nothing is due yet.
    int read(int16_t* out, int maxFrames, int64_t nowNs);

    // The take is ending: silence up to endUs (the last video frame), so the sound lasts as long as the picture.
    void padTo(int64_t endUs);

    // Held with the picture (TakeRules): nothing is written or filled, and the time held is taken out of the
    // take's clock, so the first block after release follows the last one before the hold.
    void hold(int64_t nowNs);
    void release(int64_t nowNs);

    // The presentation time (µs) of the next frame read() hands out.
    int64_t nextReadPtsUs() const;
    // Frames buffered and not yet read.
    int64_t pending() const;

private:
    void padFramesLocked(int64_t frames);
    int64_t pendingLocked() const;
    int64_t endNsLocked() const;

    mutable std::mutex lock_;
    int64_t startNs_;
    int64_t capFrames_;  // 0: no cap
    TakeSource source_;
    std::vector<int16_t> buffer_;  // interleaved stereo, frames from readAt_ / 2 onward unread
    size_t readAt_ = 0;            // sample index into buffer_
    int64_t framesIn_ = 0;         // every frame placed in the stream, silence included
    int64_t framesOut_ = 0;        // every frame read() handed out
    bool anchored_ = false;
    bool lastWasPad_ = false;
    bool holding_ = false;
    int64_t holdSinceNs_ = 0;
    std::unique_ptr<audio::PcmResampler> resampler_;
    int resamplerRate_ = 0;
    std::vector<int16_t> stereo_;
    std::vector<int16_t> resampled_;
};

}  // namespace geode::output
