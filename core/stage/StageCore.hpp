#pragma once
#include <atomic>
#include <cstdint>
#include <memory>

#include "stage/CommandQueue.hpp"
#include "stage/FramePacer.hpp"
#include "stage/StageClient.hpp"
#include "stage/StageLifecycle.hpp"
#include "stage/VsyncRule.hpp"

// What a stage thread does with each command and on each turn of its loop: which
// lifecycle event a command is, when a window hand-over is acknowledged, when a requested frame is drawn, when the
// window's frame-rate preference is restated. Pure, with no EGL and no Android, so the host can test it
// (core/tests/stage_core_test.cpp); core/stage/android carries out the port's calls and nothing else.
namespace geode::stage {

// What a stage thread is told. a, b: sizes, a mode or a frame's intervals; f: a rate; t: a window hand-over's sequence number or a vsync's time;
// target: a window the sender acquired for the stage.
enum StageCommand : uint16_t {
    kStageWindow = 1,      // Ordered: target = the window, a = width, b = height, t = sequence
    kStageWindowGone = 2,  // Ordered: t = sequence
    kStageSize = 3,        // LastWins: a = width, b = height
    kStageMode = 4,        // LastWins: a = RunMode
    kStagePause = 5,       // LastWins: a = 1 paused, 0 resumed
    kStageRedraw = 6,      // LastWins
    kStageCap = 7,         // LastWins: f = cap fps
    kStageFrameDue = 8,    // LastWins, from the vsync thread: t = the vsync's frame time (CLOCK_MONOTONIC ns), b = ns from it to the frame's expected presentation (0: none given), a = ns between presentations (0: not known)
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
    // Every hand-over up to this sequence number is applied: its sender may stop waiting.
    virtual void windowApplied(int64_t sequence) = 0;
    // The next swap's stamp, CLOCK_MONOTONIC ns (frame::latchStampNs): the frame is not shown before it (0: none, the
    // swap presents when it can). Set just before each swap, so it never outlives the frame it was given for.
    virtual void presentAt(int64_t stampNs) { static_cast<void>(stampNs); }
    // The last frame swapped is still in the GPU's queue: its fence (inserted after the draw, before the swap) has not
    // signalled. Asked with no wait. False when no frame was swapped, or the platform has no fence.
    virtual bool gpuBusy() { return false; }
    // The frame the vsync asked for is finished (drawn, or dropped unseen): the vsync thread may ask for the next at its
    // first vsync from now. Called once per kStageFrameDue taken, after the swap.
    virtual void frameDone() {}
    // One vsync frame's whole work on the stage thread, the client's draw and the swap, took `actualNs` of the
    // `budgetNs` the frame clock gave it (ADPF's target and actual work duration). Only frames a vsync asked for.
    virtual void frameWork(int64_t budgetNs, int64_t actualNs) {
        static_cast<void>(budgetNs);
        static_cast<void>(actualNs);
    }
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
    // The rate the display last presented at, from the vsync's own intervals; 0 until one says.
    float pacedFps() const { return paced_.load(std::memory_order_relaxed); }
    uint32_t takeEvents() { return events_.exchange(0, std::memory_order_relaxed); }
    bool hasEvents() const { return events_.load(std::memory_order_relaxed) != 0; }

private:
    void runActions();
    void acknowledge();
    Outcome execute(const Action& action);
    void draw(float pacedFps, float capFps, int64_t frameNs, int64_t presentNs, int64_t stampNs);
    void restateRate();
    Lifecycle lifecycle_;
    std::shared_ptr<StageClient> client_;
    StagePort& port_;
    void* window_ = nullptr;
    float cap_;
    // The frame clock: which vsyncs draw, and the budget each frame has (stage/FramePacer).
    FramePacer pacer_;
    bool frameDue_ = false;
    float dueFps_ = 0.0f;
    int64_t dueFrameNs_ = 0;
    int64_t duePeriodNs_ = 0;
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
