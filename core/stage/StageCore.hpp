#pragma once
#include <atomic>
#include <cstdint>
#include <memory>

#include "stage/CommandQueue.hpp"
#include "stage/StageClient.hpp"
#include "stage/StageLifecycle.hpp"

// What a stage thread does with each command and on each turn of its loop (docs/agents/native-stage.md): which
// lifecycle event a command is, when a window hand-over is acknowledged, when a requested frame is drawn, when the
// window's frame-rate preference is restated. Pure, with no EGL and no Android, so the host can test it
// (core/tests/stage_core_test.cpp); core/stage/android carries out the port's calls and nothing else.
namespace geode::stage {

// What a stage thread is told. a, b: sizes or a mode; f, g: rates; t: a window hand-over's sequence number;
// target: a window the sender acquired for the stage.
enum StageCommand : uint16_t {
    kStageWindow = 1,      // Ordered: target = the window, a = width, b = height, t = sequence
    kStageWindowGone = 2,  // Ordered: t = sequence
    kStageSize = 3,        // LastWins: a = width, b = height
    kStageMode = 4,        // LastWins: a = RunMode
    kStagePause = 5,       // LastWins: a = 1 paused, 0 resumed
    kStageRedraw = 6,      // LastWins
    kStageCap = 7,         // LastWins: f = cap fps
    kStageFrameDue = 8,    // LastWins, from the vsync thread: f = paced fps, g = cap fps, t = the vsync's frame time (CLOCK_MONOTONIC ns), b = ns from it to the frame's expected presentation (0: none given)
    kStageFrameRate = 9,   // LastWins, from the vsync thread: f = the window's hint
    kStageQuit = 10,       // Ordered
    kStageService = 11,    // LastWins: the client has work for the stage thread (StageClient::service)
    kStageTrim = 12,       // LastWins: the system wants memory back (Lifecycle::trimRequested)
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
    // The frame just drawn goes to the window: Done, ContextLost, or BadSurface for any other failure.
    virtual Outcome swap() = 0;
    // The reference a hand-over carried, once the stage no longer names that window.
    virtual void releaseWindow(void* window) = 0;
    virtual void setFrameRate(void* window, float fps) = 0;
    virtual void vsyncRun(bool on) = 0;
    virtual void vsyncCap(float capFps) = 0;
    // Every hand-over up to this sequence number is applied: its sender may stop waiting.
    virtual void windowApplied(int64_t sequence) = 0;
    // The next swap's stamp, CLOCK_MONOTONIC ns: the display shows it at the first vsync after this time (0: none, the
    // swap presents when it can). Set just before each swap, so it never outlives the frame it was given for.
    virtual void presentAt(int64_t presentNs) { static_cast<void>(presentNs); }
    // The frame the vsync asked for is finished (drawn, or dropped unseen): the pacer may ask for the next at its
    // first vsync from now. Called once per kStageFrameDue taken, after the swap.
    virtual void frameDone() {}
};

class StageCore {
public:
    StageCore(Kind kind, std::shared_ptr<StageClient> client, float capFps, StagePort& port);
    StageCore(const StageCore&) = delete;
    StageCore& operator=(const StageCore&) = delete;

    // Stage thread. One command from the inbox, in the order drained.
    void apply(const Command& command, int64_t nowMs);
    // Stage thread, after each batch of commands: what the lifecycle owes, the hand-over's acknowledgement, the
    // client's own work, then the frame a vsync asked for.
    void step(int64_t nowMs);
    // Stage thread, when the thread must end out of turn (an exception): quit and free everything in order.
    void abandon();
    // Stage thread, last: a command that will never be applied gives its window back, as does the stage's own.
    void discard(const Command& command);
    void close();

    bool finished() const { return lifecycle_.finished(); }
    // When step is owed with no command arriving, -1 for never.
    int64_t deadlineMs() const { return lifecycle_.deadlineMs(); }
    // The lifecycle asked for more actions than any batch of events can owe: a bug to log, never a hang.
    bool unsettled() const { return unsettled_; }

    // Any thread.
    uint32_t state() const { return state_.load(std::memory_order_relaxed); }
    float pacedFps() const { return paced_.load(std::memory_order_relaxed); }
    uint32_t takeEvents() { return events_.exchange(0, std::memory_order_relaxed); }
    bool hasEvents() const { return events_.load(std::memory_order_relaxed) != 0; }

private:
    void runActions();
    Outcome execute(const Action& action);
    void draw(float pacedFps, float capFps, int64_t frameNs, int64_t presentNs, int64_t stampNs);
    void restateRate();
    // The cap the pacer and the window are given: the surface's own under the governor's (StagePacing::capUnder).
    float pacedCap() const;
    // After a frame: a new cap from the client's governor goes the way a new cap from the shell goes.
    void followGovernor();

    Lifecycle lifecycle_;
    std::shared_ptr<StageClient> client_;
    StagePort& port_;
    void* window_ = nullptr;
    float cap_;
    // The cap "Adapt to frame time" asked for after the last frame, 0 for none (StageClient::governedCapFps).
    float governedCap_ = 0.0f;
    bool frameDue_ = false;
    float dueFps_ = 0.0f;
    int64_t dueFrameNs_ = 0;
    int64_t duePresentNs_ = 0;
    int64_t dueStampNs_ = 0;
    bool serviceDue_ = false;
    int64_t seenSeq_ = 0;
    int64_t appliedSeq_ = 0;
    bool unsettled_ = false;

    std::atomic<uint32_t> state_{0};
    std::atomic<float> paced_;
    std::atomic<uint32_t> events_{0};
};

}  // namespace geode::stage
