#pragma once

// One loop render, start to finish: the encoder it settles on, the seam it can afford, each drift stop's frames
// and their presentation times, the video-only file each stop is written to, and what a cancel or a failure
// leaves behind. The picture, the EGL surface and the files are the Host's; the encoder and the muxer are the
// encode sink's seam (output/EncodeSink.hpp), so the whole job runs on the host under core/tests.
//
// A long-form video is never drawn in full. This draws one loop plus a tail per drift stop, dissolves the tail
// into the stashed opening frames (viz/frame/LoopSeam.hpp) and encodes the result once; the extend step repeats
// the encoded loops. Every stop lands in one track of one file later, so the frame rate and the codec are
// settled before the first stop starts, and the reel reports the shortest seam any of its stops kept.
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
#include "output/LoopPlan.hpp"

namespace geode::output::looprender {

enum class State { Idle = 0, Running = 1, Rendered = 2, Cancelled = 3, Failed = 4 };

// Why a render failed. The shell words each one; detail() carries the text a reason is shown with, if any.
enum class Failure {
    None = 0,
    EncoderRejected = 1,  // no codec took the frame at any rung of the ladder
    ShaderCompile = 2,    // the seam pass did not build; detail() is the compiler's log
    LowGpuMemory = 3,     // not one seam frame could be allocated
    NoOutput = 4,         // the encoder never announced its format
    Stalled = 5,          // the encoder never reached its end of stream
    EncoderFailed = 6,    // the encoder refused to open, start or hand out a buffer
    CacheWrite = 7,       // the stop's file could not be made or opened; detail() is the reason
    Stopped = 8,          // anything else; detail() is the message, or "" for the shell's general one
};

// The most idle polls a stop's last drain makes before the encoder is called stalled, and how long the Host
// waits on each: ten seconds in all.
inline constexpr int kMaxIdlePolls = 1000;
inline constexpr int64_t kIdleWaitUs = 10'000;

// What the shell asks for. The codecs are the ones to try in order (the requested one, then H.264) with the
// top frame rate the device lists for each at this size (<= 0: it did not say).
struct Request {
    int width = 0;
    int height = 0;
    int bitRate = 0;
    int64_t loopMs = 0;
    int64_t crossfadeMs = 0;
    int fps = 0;
    int64_t longFormMs = 0;  // how long the extended file will run, 0 when unknown; caps the bit rate
    float hueTurns = 0.0f;
    float warmth = 0.0f;
    int stops = 1;
    std::string mimes[kAttemptMaxCodecs];
    int topFps[kAttemptMaxCodecs] = {};
    int codecCount = 0;
};

// One stop's scene: the grid it is drawn on, how long the engine settings are pinned for, and the palette
// offset the stop adds to the look.
struct StopScene {
    int fps = 0;
    int64_t pinDurationMs = 0;
    float hueOffset = 0.0f;
    float warmthOffset = 0.0f;
};

// Everything the job cannot do on the host. A call that returns text returns nullptr when it worked, else why
// it did not (shown to the user as is).
class Host {
public:
    virtual ~Host() = default;

    // The codecs writing into fd (the muxer duplicates it); fd < 0 gives encoders only, for the probe.
    virtual std::unique_ptr<sink::Codecs> codecs(int fd) = 0;
    // A new, empty scratch file for stop `stop`, open for reading and writing: its descriptor and *path, or -1
    // with *path the reason.
    virtual int createFile(int stop, std::string* path) = 0;
    virtual void closeFile(int fd) = 0;
    virtual void removeFile(const std::string& path) = 0;
    // Sleeps kIdleWaitUs while the encoder has nothing to hand out.
    virtual void idleWait() = 0;

    // A GL context current on the encoder's input window.
    virtual const char* openSurface(void* window) = 0;
    // The seam stash: how many frames it holds of `requested` (0: none fit), or -1 with *error when the pass
    // did not build.
    virtual int allocateSeam(int requested, std::string* error) = 0;
    // The renderer, its scene, the look with the stop's drift and a warm sound engine.
    virtual const char* prepareScene(const StopScene& scene) = 0;
    // The wordmark's pass, when these frames carry one.
    virtual const char* prepareMark() = 0;
    // Draws source frame `frame`; false when the track's audio could not be decoded.
    virtual bool renderFrame(int frame) = 0;
    virtual void captureSeam(int index) = 0;
    virtual void blendSeam(int index, int tail, int seamFrames) = 0;
    // The wordmark over the frame, its presentation time, then the swap into the encoder.
    virtual void present(int64_t ptsNs) = 0;
    // Frees the mark, the renderer and the stash, in that order; then, after the encoder is gone, the surface.
    virtual void closeScene() = 0;
    virtual void closeSurface() = 0;
};

// The look's params frame with a stop's drift on it: the colour shift moved and wrapped into a turn, the
// temperature moved and held to its range (applyDrift), at the frame's two slots. A slot outside the frame is
// left alone.
void driftParams(std::vector<float>& params, int colorShiftSlot, int temperatureSlot, float hueOffset,
                 float warmthOffset);

class Job {
public:
    Job(const Request& request, Host& host);
    Job(const Job&) = delete;
    Job& operator=(const Job&) = delete;

    void run();
    void cancel() { cancelled_.store(true, std::memory_order_relaxed); }
    float progress() const { return progress_.load(std::memory_order_relaxed); }
    State state() const { return state_.load(std::memory_order_acquire); }

    Failure failure() const { return failure_; }
    const std::string& detail() const { return detail_; }
    // The rendered loops, one per stop in order; empty unless state() is Rendered.
    const std::vector<std::string>& files() const { return files_; }
    // The shape the reel was rendered to: the rate the encoder took and the seam the stops kept.
    int fps() const { return fps_; }
    int bitRate() const { return bitRate_; }
    int64_t crossfadeMs() const { return crossfadeMs_; }

private:
    enum class End { Done, Cancelled, Failed };

    class Writer;

    bool negotiate();
    End renderStop(const DriftStop& stop, int index, int count, int fd, int crossfadeFrames, int* seamFrames);
    End drawStop(Writer& writer, const DriftStop& stop, int index, int count, int crossfadeFrames, int* seamFrames);
    End fail(Failure failure, const char* detail = nullptr);
    void finish(State state);

    const Request request_;
    Host& host_;
    std::atomic<bool> cancelled_{false};
    std::atomic<float> progress_{0.0f};
    std::atomic<State> state_{State::Idle};
    Failure failure_ = Failure::None;
    std::string detail_;
    std::vector<std::string> files_;
    std::string mime_;
    int fps_ = 0;
    int bitRate_ = 0;
    int64_t crossfadeMs_ = 0;
    LoopSpec spec_;
};

}  // namespace geode::output::looprender
