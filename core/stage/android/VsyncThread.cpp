#include "stage/android/VsyncThread.hpp"

#include "stage/StageCore.hpp"

#include <pthread.h>
#include <sys/resource.h>
#include <unistd.h>

#include <cstdint>
#include <mutex>
#include <thread>

#include "util/Log.hpp"

namespace geode::stage::platform {

namespace {

constexpr const char* kTag = "GeodeStage";

// android.os.Process.THREAD_PRIORITY_DISPLAY, the Kotlin pacer thread's priority, as a Linux nice value.
constexpr int kDisplayNice = -4;

enum VsyncCommand : uint16_t {
    kVsyncRun = 1,     // LastWins: a = 1 on, 0 off
    kVsyncCap = 2,     // LastWins: f = cap fps
};

// A command waits at most this long for room; the queue only fills under a burst of stages.
constexpr std::chrono::milliseconds kPushWithin{200};

int32_t positiveInt32(int64_t ns) { return ns > 0 && ns <= INT32_MAX ? static_cast<int32_t>(ns) : 0; }

// The timeline the system prefers for this vsync. afterNs: from the vsync's frame time to the frame's expected
// presentation; 0 when the callback names none or it is not ahead (a time that has passed asks for nothing).
// intervalNs: between that presentation and the timeline next to it; 0 when the callback names only one.
struct Present {
    int32_t afterNs = 0;
    int32_t intervalNs = 0;
};

Present presentOf(const AChoreographerFrameCallbackData* callbackData, int64_t frameTimeNanos) {
    const size_t count = AChoreographerFrameCallbackData_getFrameTimelinesLength(callbackData);
    if (count == 0) return {};
    const size_t preferred = AChoreographerFrameCallbackData_getPreferredFrameTimelineIndex(callbackData);
    const int64_t present = AChoreographerFrameCallbackData_getFrameTimelineExpectedPresentationTimeNanos(callbackData, preferred);
    Present p;
    p.afterNs = positiveInt32(present - frameTimeNanos);
    if (count > 1) {
        const size_t beside = preferred + 1 < count ? preferred + 1 : preferred - 1;
        const int64_t other = AChoreographerFrameCallbackData_getFrameTimelineExpectedPresentationTimeNanos(callbackData, beside);
        p.intervalNs = positiveInt32(other > present ? other - present : present - other);
    }
    return p;
}

}  // namespace

VsyncThread& VsyncThread::process() {
    // Leaked on purpose: its detached thread runs until the process dies, and a static object would be destroyed
    // at exit under it.
    static VsyncThread* const thread = new VsyncThread;
    return *thread;
}

VsyncThread::VsyncThread() : inbox_(std::make_shared<Inbox>()) {
    std::thread([this] { loop(); }).detach();
}

VsyncThread::Sink* VsyncThread::attach(std::shared_ptr<Inbox> stage, float capFps) {
    return new Sink{StagePacing(capFps), std::move(stage), false, nullptr};
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
    // Not a queued command: a full queue would drop it and leave the sink's callback re-posting for good.
    {
        std::lock_guard<std::mutex> lock(detachLock_);
        sink->nextDetached = detached_;
        detached_ = sink;
    }
    Inbox::wake(inbox_.get());
}

VsyncThread::Sink* VsyncThread::takeDetached() {
    std::lock_guard<std::mutex> lock(detachLock_);
    Sink* taken = detached_;
    detached_ = nullptr;
    return taken;
}

void VsyncThread::retire(Sink* sink) {
    sink->detached = true;
    sink->pacing.stop();
    // A pending callback still names the sink: it frees it when it lapses.
    if (!sink->pacing.posted()) delete sink;
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
        // Taken before the drain and retired after it: a detach is its sink's last call, so every command for the
        // sink is already queued when it is taken, and none is applied to a freed sink.
        Sink* gone = takeDetached();
        // Drained before every sleep, the first time included, so pushes made before bind are not lost.
        for (size_t n = inbox_->queue.drain(batch, Inbox::kCapacity); n > 0; n = inbox_->queue.drain(batch, Inbox::kCapacity)) {
            for (size_t i = 0; i < n; ++i) apply(batch[i]);
        }
        while (gone) {
            Sink* next = gone->nextDetached;
            retire(gone);
            gone = next;
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
                // A frame request lost with the stage's last run leaves no frame to wait for.
                sink->stageBusy.store(false, std::memory_order_relaxed);
                if (sink->pacing.start() && choreographer_) AChoreographer_postVsyncCallback(choreographer_, &VsyncThread::onFrame, sink);
            } else {
                sink->pacing.stop();
            }
            break;
        case kVsyncCap: sink->pacing.setCap(command.f); break;
        default: break;
    }
}

void VsyncThread::onFrame(const AChoreographerFrameCallbackData* callbackData, void* data) {
    auto* sink = static_cast<Sink*>(data);
    if (!sink->pacing.vsyncArrived()) {
        if (sink->detached) delete sink;
        return;
    }
    // Re-posted first: the cadence survives a slow stage, and a skipped vsync still wakes the next one.
    AChoreographer_postVsyncCallback(process().choreographer_, &VsyncThread::onFrame, sink);
    const int64_t frameTimeNanos = AChoreographerFrameCallbackData_getFrameTimeNanos(callbackData);
    const StagePacing::Tick tick = sink->pacing.step(frameTimeNanos, sink->stageBusy.load(std::memory_order_acquire));
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
        // The vsync's own time, for the frame's step; the rest places the swap's stamp (frame::latchStampNs).
        c.t = frameTimeNanos;
        const Present present = presentOf(callbackData, frameTimeNanos);
        c.b = present.afterNs;
        c.a = present.intervalNs;
        // Busy before the push, so a stage that finishes at once clears it after, never before.
        sink->stageBusy.store(true, std::memory_order_release);
        if (sink->stage->queue.push(c, Merge::LastWins) == Pushed::Full) sink->stageBusy.store(false, std::memory_order_release);
    }
}

}  // namespace geode::stage::platform
