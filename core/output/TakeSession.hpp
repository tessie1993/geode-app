#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

#include "output/TakeClock.hpp"

struct ANativeWindow;

// A capture take (ADR 0008): the live stage's frames and the heard sound, encoded to one MP4 while the user
// performs. Process-wide, because its two feeds are: the record tap at the end of the one playback chain, and
// the one in-app stage renderer (geode_viz_set_take_stage). Wallpaper, dream and cast renderers never feed it.
//
// The device half: AVC and AAC encoders (AMediaCodec), the muxer (AMediaMuxer) and the take's own encoder
// thread, which pulls the sound from TakeAudio, drains both encoders and writes the file. Every decision those
// steps need (sizes, rates, which frame, which timestamp, where silence goes) is TakeClock's, tested on the
// host; nothing here decides anything a test would want to check.
namespace geode::output {

class Take;

class TakeSession {
public:
    static TakeSession& shared();

    // Starts a take into fd (open for reading and writing; the caller closes it after end()), at the stage's
    // last size and the display's rate. False when no stage has drawn yet, a take is already running, or no
    // encoder would take any size TakeClock offered.
    bool begin(int fd, float displayFps);
    // Ends the take: the stage stops feeding it, both encoders drain and the MP4 is finished. Returns the
    // take's length in ms, or -1 when it holds no picture (the stage never drew while it ran). Blocks for the
    // drain, so never on the main thread.
    int64_t end();
    bool active() const;

    // The record tap, on the playback thread: interleaved 16-bit PCM as the performer hears it.
    void writeAudio(const int16_t* pcm, int frames, int channels, int rateHz);

    // The stage renderer's surface size, noted every frame so begin() knows the screen's size.
    void noteStageSize(int width, int height);

    // The take the stage renderer feeds, or null. GL thread.
    std::shared_ptr<Take> current() const;

private:
    TakeSession() = default;

    mutable std::mutex lock_;
    std::shared_ptr<Take> take_;
    std::atomic<int> stageW_{0};
    std::atomic<int> stageH_{0};
    uint64_t generations_ = 0;
};

// One running take. The GL thread presents frames through window(); the playback thread writes sound; the
// take's own thread encodes and muxes.
class Take {
public:
    ~Take();

    uint64_t generation() const { return generation_; }
    ANativeWindow* window() const { return window_; }
    TakeSize size() const { return size_; }

    // GL thread, around one frame into window(): the presentation time to give it, or nothing when TakeClock
    // skips this frame or the take is closing. Every frame that gets a time must be followed by endFrame().
    std::optional<int64_t> beginFrame(int64_t frameNs);
    void endFrame();

private:
    friend class TakeSession;
    Take() = default;

    bool open(int fd, int screenW, int screenH, float displayFps, uint64_t generation);
    int64_t finish();
    void run();

    struct Impl;
    std::unique_ptr<Impl> impl_;
    uint64_t generation_ = 0;
    ANativeWindow* window_ = nullptr;
    TakeSize size_{};
    std::atomic<bool> closing_{false};
    std::atomic<int> inFlight_{0};
};

}  // namespace geode::output
