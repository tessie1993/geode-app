#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

#include "audio/ingest/CapturePlan.hpp"
#include "audio/ingest/PcmRing.hpp"

// Live input: the microphone as a device input stream whose callback writes the mic's ring. No JVM thread,
// no lock and no allocation on the audio thread. The device stream is behind MicStreams (Oboe on Android,
// audio/ingest/android/OboeMic; a fake in core/tests/mic_input_test.cpp), so all of this runs on the host.
//
// The stream asked for is the one AudioRecord was: mono, the formats of capturePlan in its order (the
// device's rate first, float before 16-bit), the UNPROCESSED source where the device has it, else
// VOICE_RECOGNITION, the shared normal-latency path. A start the device refuses is a failure, as it was.
//
// Threads: start, stop and active on the caller's (the main thread); onAudio on the stream's audio thread;
// onClosed on the platform's error thread once it has closed a stream. lock_ is never taken on the audio
// thread.
namespace geode::audio::ingest {

struct MicRequest {
    CaptureFormat format;
    bool unprocessed = false;  // the UNPROCESSED input preset, else VOICE_RECOGNITION
    int capacityFrames = 0;
};

// One open device stream; its destructor stops and closes it, after which no callback runs.
class MicStream {
public:
    virtual ~MicStream() = default;
    virtual bool start() = 0;
    // What the platform names this stream by in onClosed.
    virtual const void* id() const = 0;
};

class MicInput;

class MicStreams {
public:
    virtual ~MicStreams() = default;
    // Null unless the device gives a stream that is exactly the request's rate, channels and encoding. The
    // stream calls input.onAudio and input.onClosed.
    virtual std::unique_ptr<MicStream> open(const MicRequest& request, MicInput& input) = 0;
};

// A stream closed on a disconnect (a headset mic plugged or unplugged) is opened again, at most kMax
// times in kWindowMs, so a device that keeps dropping ends the input instead of looping.
class ReopenBudget {
public:
    static constexpr int kMax = 3;
    static constexpr int64_t kWindowMs = 10'000;

    ReopenBudget() { reset(); }
    void reset();
    // True, and counted, when fewer than kMax reopens happened in the kWindowMs before nowMs.
    bool take(int64_t nowMs);

private:
    static constexpr int64_t kNever = INT64_MIN;
    int64_t at_[kMax];
    int next_ = 0;
};

class MicInput {
public:
    // Four reads of AudioRecord's buffer rule (CapturePlan), without the platform minimum the NDK has not.
    static constexpr int kCapacityFrames = kCaptureReadFrames * kCaptureBufferMinimums;

    explicit MicInput(MicStreams& streams) : streams_(streams) {}
    MicInput(const MicInput&) = delete;
    MicInput& operator=(const MicInput&) = delete;
    ~MicInput() { stop(); }

    // Opens the first format the device gives and starts it, writing `ring`. deviceRateHz <= 0 is unknown.
    // True when running (also when it already was).
    bool start(std::shared_ptr<PcmRing> ring, int deviceRateHz, bool unprocessed);
    // Stops and closes the stream; when it returns no block is written any more.
    void stop();
    // False after stop, or once a closed stream was not opened again.
    bool active() const { return running_.load(std::memory_order_acquire); }

    // The audio thread: frames of the stream's format. False when the stream should stop.
    bool onAudio(const void* data, int frames);
    // The platform closed stream `id` on an error. A disconnect opens a new stream (the ladder again: a new
    // device may not have the old rate) within the budget; anything else ends the input.
    void onClosed(const void* id, bool disconnected, int64_t nowMs);

private:
    bool openLocked();
    void endLocked();

    MicStreams& streams_;
    std::mutex lock_;
    std::unique_ptr<MicStream> stream_;
    std::shared_ptr<PcmRing> ring_;
    int deviceRateHz_ = 0;
    bool unprocessed_ = false;
    ReopenBudget reopens_;

    std::atomic<bool> running_{false};
    // The audio thread's view: the ring while running (ring_ keeps it alive) and the stream's format.
    std::atomic<PcmRing*> writing_{nullptr};
    std::atomic<int> rateHz_{0};
    std::atomic<int> channels_{1};
    std::atomic<bool> asFloat_{true};
};

}  // namespace geode::audio::ingest
