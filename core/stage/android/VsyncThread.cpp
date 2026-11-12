#include "stage/android/VsyncThread.hpp"

#include <pthread.h>
#include <sys/resource.h>
#include <unistd.h>

#include <thread>

#include "util/Log.hpp"
#include "viz/frame/Thermal.hpp"

namespace geode::stage::platform {

namespace {

constexpr const char* kTag = "GeodeStage";

// android.os.Process.THREAD_PRIORITY_DISPLAY, the Kotlin pacer thread's priority, as a Linux nice value.
constexpr int kDisplayNice = -4;

enum VsyncCommand : uint16_t {
    kVsyncRun = 1,     // LastWins: a = 1 on, 0 off
    kVsyncCap = 2,     // LastWins: f = cap fps
    kVsyncDetach = 3,  // Ordered
};

// Ordered commands wait at most this long for room; the queue only fills under a burst of stages.
constexpr std::chrono::milliseconds kPushWithin{200};

}  // namespace

VsyncThread& VsyncThread::process() {
    static VsyncThread thread;
    return thread;
}

VsyncThread::VsyncThread() : inbox_(std::make_shared<Inbox>()) {
    std::thread([this] { loop(); }).detach();
}

VsyncThread::Sink* VsyncThread::attach(std::shared_ptr<Inbox> stage, float capFps) {
    auto* sink = new Sink{StagePacing(capFps), std::move(stage), false};
    return sink;
}

void VsyncThread::run(Sink* sink, bool on) {
    if (!sink) return;
    Command c;
    c.kind = kVsyncRun;
    c.a = on ? 1 : 0;
    c.target = sink;
    if (!inbox_->pushWithin(c, Merge::LastWins, kPushWithin)) GEODE_LOGW(kTag, "vsync run dropped");
}

void VsyncThread::setCap(Sink* sink, float capFps) {
    if (!sink) return;
    Command c;
    c.kind = kVsyncCap;
    c.f = capFps;
    c.target = sink;
    if (!inbox_->pushWithin(c, Merge::LastWins, kPushWithin)) GEODE_LOGW(kTag, "vsync cap dropped");
}

void VsyncThread::detach(Sink* sink) {
    if (!sink) return;
    Command c;
    c.kind = kVsyncDetach;
    c.target = sink;
    // Never queued: the sink is leaked rather than freed under a callback that may still name it.
    if (!inbox_->pushWithin(c, Merge::Ordered, kPushWithin)) GEODE_LOGE(kTag, "vsync detach dropped; one sink leaks");
}

void VsyncThread::loop() {
    pthread_setname_np(pthread_self(), "GeodeFramePacer");
    if (setpriority(PRIO_PROCESS, static_cast<id_t>(gettid()), kDisplayNice) != 0) {
        GEODE_LOGW(kTag, "vsync thread keeps the default priority");
    }
    ALooper* looper = ALooper_prepare(0);
    // This thread's own instance: a choreographer is per looper and hears the same display vsync on any of them.
    choreographer_ = AChoreographer_getInstance();
    if (!choreographer_) GEODE_LOGE(kTag, "no choreographer on the vsync thread: no stage will draw");
    inbox_->bind(looper);
    Command batch[Inbox::kCapacity];
    for (;;) {
        // Drained before every sleep, the first time included, so pushes made before bind are not lost.
        for (size_t n = inbox_->queue.drain(batch, Inbox::kCapacity); n > 0; n = inbox_->queue.drain(batch, Inbox::kCapacity)) {
            for (size_t i = 0; i < n; ++i) apply(batch[i]);
        }
        // Choreographer callbacks run inside this call; every return is treated as a wake.
        ALooper_pollOnce(-1, nullptr, nullptr, nullptr);
    }
}

void VsyncThread::apply(const Command& command) {
    auto* sink = static_cast<Sink*>(command.target);
    if (!sink) return;
    switch (command.kind) {
        case kVsyncRun:
            if (command.a != 0) {
                if (sink->pacing.start() && choreographer_) AChoreographer_postFrameCallback64(choreographer_, &VsyncThread::onFrame, sink);
            } else {
                sink->pacing.stop();
            }
            break;
        case kVsyncCap: sink->pacing.setCap(command.f); break;
        case kVsyncDetach:
            sink->detached = true;
            sink->pacing.stop();
            // A pending callback still names the sink: it frees it when it lapses.
            if (!sink->pacing.posted()) delete sink;
            break;
        default: break;
    }
}

void VsyncThread::onFrame(int64_t frameTimeNanos, void* data) {
    auto* sink = static_cast<Sink*>(data);
    if (!sink->pacing.vsyncArrived()) {
        if (sink->detached) delete sink;
        return;
    }
    // Re-posted first: the cadence survives a slow stage, and a skipped vsync still wakes the next one.
    AChoreographer_postFrameCallback64(process().choreographer_, &VsyncThread::onFrame, sink);
    const StagePacing::Tick tick = sink->pacing.step(frameTimeNanos, viz::ThermalState::process().fpsRungsDown());
    if (tick.restateRate) {
        Command c;
        c.kind = kStageFrameRate;
        c.f = tick.rateFps;
        sink->stage->queue.push(c, Merge::LastWins);
    }
    if (tick.draw) {
        // Coalesced like requestRender: a stage behind the vsync takes one request, not a backlog. A full queue
        // drops it, and the next drawn vsync asks again.
        Command c;
        c.kind = kStageFrameDue;
        c.f = sink->pacing.fps();
        c.g = sink->pacing.capFps();
        sink->stage->queue.push(c, Merge::LastWins);
    }
}

}  // namespace geode::stage::platform
