#pragma once

// One video export, start to finish: the bit rate that keeps the file under the muxer's limit, the encoder it
// settles on (output/EncoderLadder), the frame count and each frame's time and timestamp (output/FrameClock),
// the drain into the muxer, the already-encoded audio track interleaved by its timestamps, the progress, and
// what a cancel or a failure leaves behind. The picture, the EGL surface and the audio bytes are the Host's;
// the encoder and the muxer are the encode sink's seam (output/EncodeSink.hpp), so the whole job runs on the
// host under core/tests.
//
// Threads: run() blocks on the thread that will hold the GL context. cancel(), progress() and state() are the
// only calls another thread may make while it runs; everything else is read after run() returns.

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "output/EncodeSink.hpp"
#include "output/EncoderLadder.hpp"

namespace geode::output::videoexport {

enum class State { Idle = 0, Running = 1, Rendered = 2, Cancelled = 3, Failed = 4 };

// Why a render failed. The shell words each one; detail() carries the text a reason is shown with, if any.
enum class Failure {
    None = 0,
    EncoderRejected = 1,  // no codec took the frame at any rung of the ladder
    NoOutput = 2,         // the encoder never announced its format
    Stalled = 3,          // the encoder never reached its end of stream
    EncoderFailed = 4,    // the encoder refused its surface, its start or its end of input
    EmptyBuffer = 5,      // the encoder handed out a sample with no buffer
    Stopped = 6,          // anything else; detail() is the message
};

// The most idle polls the last drain makes before the encoder is called stalled, and how long the Host waits
// on each: ten seconds in all.
inline constexpr int kMaxIdlePolls = 1000;
inline constexpr int64_t kIdleWaitUs = 10'000;

// The share of the bar the audio's transcode fills before the first frame, and the share the frames fill
// after it; the muxer's finish takes it to 1.
inline constexpr float kLeadInSpan = 0.1f;
inline constexpr float kFrameSpan = 0.85f;

// The bar while the audio is transcoded, `done` 0..1 of it.
float leadInProgress(float done);
// The bar once frame `frame` of `total` is presented.
float frameProgress(int64_t frame, int64_t total);

// One already-encoded audio sample: where its bytes sit in the Host's scratch, how many, its timestamp and
// flags (MediaCodec's).
struct AudioSample {
    int64_t offset = 0;
    int32_t size = 0;
    int64_t ptsUs = 0;
    uint32_t flags = 0;
};

// How long the audio runs: one AAC frame past its last sample at the rate its format names; 0 with no sample.
int64_t audioDurationUs(const std::vector<AudioSample>& samples, const std::vector<sink::FormatEntry>& format);

// What the shell asks for. The codecs are the ones to try in order (the requested one, then H.264) with the top
// frame rate the device lists for each at this size (<= 0: it did not say). The audio is the transcoder's
// output: its samples, its format as the encoder named it and the bytes its scratch holds.
struct Request {
    int width = 0;
    int height = 0;
    int bitRate = 0;
    int fps = 0;
    std::string mimes[kAttemptMaxCodecs];
    int topFps[kAttemptMaxCodecs] = {};
    int codecCount = 0;
    std::vector<AudioSample> audio;
    std::vector<sink::FormatEntry> audioFormat;
    int64_t audioBytes = 0;
};

// The render's scene: the grid it is drawn on and how long it runs (the engine settings are pinned for that
// long, and an end card counts back from it).
struct Scene {
    int fps = 0;
    int64_t durationMs = 0;
};

// Everything the job cannot do on the host. A call that returns text returns nullptr when it worked, else why it
// did not (shown to the user as is).
class Host {
public:
    virtual ~Host() = default;

    // The codecs, and the muxer writing into the output file.
    virtual std::unique_ptr<sink::Codecs> codecs() = 0;
    // `sample`'s bytes into out (sample.size of room): how many were read, < 0 on an error.
    virtual int readAudio(const AudioSample& sample, uint8_t* out) = 0;
    // Sleeps kIdleWaitUs while the encoder has nothing to hand out.
    virtual void idleWait() = 0;

    // A GL context current on the encoder's input window.
    virtual const char* openSurface(void* window) = 0;
    // The renderer, its scene, the look and a warm sound engine.
    virtual const char* prepareScene(const Scene& scene) = 0;
    // The wordmark's pass, when these frames carry one.
    virtual const char* prepareMark(int64_t lengthMs) = 0;
    // Draws frame `frame` of the range; false when the track's audio could not be decoded.
    virtual bool renderFrame(int64_t frame) = 0;
    // The wordmark over the frame at timeMs, its presentation time, then the swap into the encoder.
    virtual const char* present(int64_t ptsNs, int64_t timeMs) = 0;
    // Frees the mark and the renderer; then, after the encoder is gone, the surface.
    virtual void closeScene() = 0;
    virtual void closeSurface() = 0;
};

class Job {
public:
    Job(const Request& request, Host& host);
    Job(const Job&) = delete;
    Job& operator=(const Job&) = delete;

    void run();
    void cancel() { cancelled_.store(true, std::memory_order_relaxed); }
    float progress() const { return progress_.load(std::memory_order_relaxed); }
    // A cancel that comes after the file was finished still discards it.
    State state() const;

    Failure failure() const { return failure_; }
    const std::string& detail() const { return detail_; }
    // What the render settled on: the rate and bit rate the encoder took, the frames drawn, the audio's length.
    int fps() const { return fps_; }
    int bitRate() const { return bitRate_; }
    int64_t frames() const { return frames_; }
    int64_t durationUs() const { return durationUs_; }

private:
    enum class End { Done, Cancelled, Failed };

    class Writer;

    End render(Writer& writer);
    End fail(Failure failure, const char* detail = nullptr);

    const Request request_;
    Host& host_;
    std::atomic<bool> cancelled_{false};
    std::atomic<float> progress_{0.0f};
    std::atomic<State> state_{State::Idle};
    Failure failure_ = Failure::None;
    std::string detail_;
    int fps_ = 0;
    int bitRate_ = 0;
    int64_t frames_ = 0;
    int64_t durationUs_ = 0;
};

}  // namespace geode::output::videoexport
