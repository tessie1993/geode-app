#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "stage/CommandQueue.hpp"
#include "stage/FrameRecorder.hpp"
#include "stage/StageClient.hpp"
#include "stage/StageLifecycle.hpp"
#include "stage/SwapPace.hpp"

// What a stage thread does with each command and on each turn of its loop: which
// lifecycle event a command is, when a window hand-over is acknowledged, when a paced frame is drawn, when the
// window's frame-rate preference is restated. Pure, with no EGL and no Android, so the host can test it
// (core/tests/stage_core_test.cpp); core/stage/android carries out the port's calls and nothing else.
//
// While the stage paces, every turn of the loop draws one frame and the swap holds it to the frame rate (Android
// Frame Pacing: the swap blocks until the frame's slot), so the thread takes its commands between frames.
namespace geode::stage {

// What a stage thread is told. a, b: sizes or a mode; f: a rate; t: a window hand-over's sequence number;
// target: a window the sender acquired for the stage.
enum StageCommand : uint16_t {
    kStageWindow = 1,      // Ordered: target = the window, a = width, b = height, t = sequence
    kStageWindowGone = 2,  // Ordered: t = sequence
    kStageSize = 3,        // LastWins: a = width, b = height
    kStageMode = 4,        // LastWins: a = RunMode
    kStagePause = 5,       // LastWins: a = 1 paused, 0 resumed
    kStageRedraw = 6,      // LastWins
    kStageCap = 7,         // LastWins: f = cap fps
    kStageQuit = 10,       // Ordered
    kStageService = 11,    // LastWins: the client has work for the stage thread (StageClient::service)
    kStageTrim = 12,       // LastWins: the system wants memory back (Lifecycle::trimRequested)
    kStageVsync = 13,      // LastWins, from the stage's own Choreographer: the vsync StagePort::wakeAtVsync asked for
};

// What the pacer knows before a frame (StagePort::paceTimes).
struct PaceTimes {
    // The presentation time the pacer aims the next frame at, CLOCK_MONOTONIC ns (Swappy's startFrame); 0: none.
    int64_t desiredNs = 0;
    // The refresh period of the display mode running, as the platform reports it; 0: not known.
    int64_t periodNs = 0;
    // Swappy holds each frame to its slot inside the swap. False: nothing in the swap does, so the platform's vsync
    // (Choreographer, wakeAtVsync) paces the stage, one frame per vsync, and desiredNs is that vsync's presentation time.
    bool swapPaces = false;
};

// The platform a stage runs on, called on the stage thread only. A window is opaque here.
class StagePort {
public:
    virtual ~StagePort() = default;
    // Opens the display too; says whether a context may be current with no surface.
    virtual Outcome createContext(bool& surfaceless) = 0;
    virtual Outcome createSurface(void* window) = 0;
    virtual Outcome makeCurrent() = 0;
    virtual Outcome makeCurrentSurfaceless() = 0;
    virtual void releaseCurrent() = 0;
    virtual void destroySurface() = 0;
    virtual void destroyContext() = 0;
    // The frame just drawn goes to the window through the pacer: Done, ContextLost, or BadSurface for any other
    // failure. Blocks until the pacer lets the frame go (Swappy's swap interval, or the window's own back-pressure).
    virtual Outcome swap() = 0;
    // No swap holds the next frame back (the frame swapped nothing, or the pacer is not Swappy): the platform's next vsync
    // (Choreographer) is the next frame's turn, and the loop sleeps until it comes (kStageVsync).
    virtual void wakeAtVsync() = 0;
    // The reference a hand-over carried, once the stage no longer names that window.
    virtual void releaseWindow(void* window) = 0;
    virtual void setFrameRate(void* window, float fps) = 0;
    // The pacing starts or takes a new frame budget (on: the swap interval in it is Swappy's), or stops (off).
    virtual void pace(bool on, const FrameBudget& budget) = 0;
    virtual PaceTimes paceTimes() = 0;
    // The GPU time of the frame before the last one swapped, as the pacer measured it; 0 or less: not known.
    virtual int64_t lastGpuNs() { return 0; }
    // CLOCK_MONOTONIC ns.
    virtual int64_t nowNs() = 0;
    // Every hand-over up to this sequence number is applied: its sender may stop waiting.
    virtual void windowApplied(int64_t sequence) = 0;
    // One paced frame's work against the `targetNs` budget (ADPF's target and actual work duration): the draw on the stage
    // thread took `cpuNs`, never the swap's wait; `gpuNs` is the GPU time the pacer measured for the frame before (0: not
    // known); `startNs` is when the draw began, CLOCK_MONOTONIC.
    virtual void frameWork(int64_t targetNs, int64_t cpuNs, int64_t gpuNs, int64_t startNs) {
        static_cast<void>(targetNs);
        static_cast<void>(cpuNs);
        static_cast<void>(gpuNs);
        static_cast<void>(startNs);
    }
    // A chunk of the frame recorder (stage/FrameRecorder), as its CSV: when the ring is full and when the pacing
    // stops. Only while a debug flag has it recording.
    virtual void framesRecorded(const std::string& csv) { static_cast<void>(csv); }
};

class StageCore {
public:
    StageCore(Kind kind, std::shared_ptr<StageClient> client, float capFps, StagePort& port);
    StageCore(const StageCore&) = delete;
    StageCore& operator=(const StageCore&) = delete;

    // Stage thread. One command from the inbox, in the order drained.
    void apply(const Command& command, int64_t nowMs);
    // Stage thread, after each batch of commands: what the lifecycle owes, the hand-over's acknowledgement, the
    // client's own work, then one paced frame when the stage paces (unless it waits for a vsync).
    void step(int64_t nowMs);
    // Stage thread, when the thread must end out of turn (an exception): quit and free everything in order.
    void abandon();
    // Stage thread, last: a command that will never be applied gives its window back, as does the stage's own.
    void discard(const Command& command);
    void close();
    // Stage thread, from a debug flag: every paced frame goes to the frame recorder. Off by default.
    void recordFrames(bool on) { recorder_.setEnabled(on); }

    bool finished() const { return lifecycle_.finished(); }
    // How long the loop may sleep before the next step, in ms: 0 for at once (the next paced frame: its swap does the
    // waiting), -1 for until a command (or the vsync asked for) comes, else until the lifecycle's deadline.
    int64_t waitMs(int64_t nowMs) const;
    // The lifecycle asked for more actions than any batch of events can owe: a bug to log, never a hang.
    bool unsettled() const { return unsettled_; }

    // Any thread.
    uint32_t state() const { return state_.load(std::memory_order_relaxed); }
    // The rate frames are paced at, 1 / the frame's length; 0 until the pacer says.
    float pacedFps() const { return paced_.load(std::memory_order_relaxed); }
    // The published mirror of the frame budget, ns: the setting's frame length on the display mode now running; 0 until
    // the platform has said the mode. Read by the UI without waiting.
    int64_t frameBudgetNs() const { return mirror_.load(std::memory_order_relaxed); }
    uint32_t takeEvents() { return events_.exchange(0, std::memory_order_relaxed); }
    bool hasEvents() const { return events_.load(std::memory_order_relaxed) != 0; }

private:
    bool paces() const;
    void runActions();
    void acknowledge();
    Outcome execute(const Action& action);
    bool draw(float pacedFps, float capFps, int64_t presentNs, int64_t& drawNs);
    void restateRate();
    void deriveBudget(int64_t periodNs, bool swapPaces);
    void drawPaced();
    void handOverFrames();
    Lifecycle lifecycle_;
    std::shared_ptr<StageClient> client_;
    StagePort& port_;
    void* window_ = nullptr;
    float cap_;
    PresentClock clock_;
    // The last paced frame swapped nothing: the next one waits for the vsync asked for (kStageVsync).
    bool vsyncWait_ = false;
    // The one frame budget: derived when the cap or the display mode changes (deriveBudget), then only read.
    FrameBudget budget_;
    bool swapPaces_ = false;
    // The last drawn frame's presentation time, 0 since a start.
    int64_t lastShownNs_ = 0;
    FrameRecorder recorder_;
    bool serviceDue_ = false;
    int64_t seenSeq_ = 0;
    int64_t appliedSeq_ = 0;
    bool unsettled_ = false;

    std::atomic<uint32_t> state_{0};
    std::atomic<float> paced_;
    std::atomic<int64_t> mirror_{0};
    std::atomic<uint32_t> events_{0};
};

}  // namespace geode::stage
