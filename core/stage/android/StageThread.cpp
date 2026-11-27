#include "stage/android/StageThread.hpp"

#include <fcntl.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include <sys/system_properties.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <climits>

#include "util/Log.hpp"
#include "util/ThreadNice.hpp"

namespace geode::stage::platform {

namespace {

constexpr const char* kTag = "GeodeStage";

// The frame recorder's debug flag (stage/FrameRecorder): `adb shell setprop debug.geode.frames 1`, read when a stage
// starts. Its chunks go to logcat under kFramesTag as CSV, so a tester on another brand can send them.
constexpr const char* kFramesProperty = "debug.geode.frames";
constexpr const char* kFramesTag = "GeodeFrames";

bool framesFlag() {
    char value[PROP_VALUE_MAX] = {};
    return __system_property_get(kFramesProperty, value) > 0 && value[0] == '1';
}

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

Outcome outcomeOf(EGLint error) {
    if (error == EGL_SUCCESS) return Outcome::Done;
    return error == EGL_CONTEXT_LOST ? Outcome::ContextLost : Outcome::BadSurface;
}

}  // namespace

std::shared_ptr<StageThread> StageThread::start(Kind kind, std::shared_ptr<StageClient> client, float capFps) {
    auto stage = std::make_shared<StageThread>(kind, std::move(client), capFps);
    // The thread holds the stage too, so a thread left to finish after destroy's bound still has it.
    stage->thread_ = std::thread([stage] { stage->run(); });
    return stage;
}

StageThread::StageThread(Kind kind, std::shared_ptr<StageClient> client, float capFps)
    : inbox_(std::make_shared<Inbox>()), core_(kind, std::move(client), capFps, *this) {
    eventFd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
}

StageThread::~StageThread() {
    if (thread_.joinable()) thread_.detach();
    if (eventFd_ >= 0) close(eventFd_);
}

uint32_t StageThread::takeEvents() {
    if (eventFd_ >= 0) {
        uint64_t count = 0;
        if (read(eventFd_, &count, sizeof(count)) < 0) count = 0;  // EAGAIN: nothing was signalled
        eventSignalled_.store(false);
    }
    return core_.takeEvents();
}

int StageThread::eventFd() const {
    return eventFd_ >= 0 ? fcntl(eventFd_, F_DUPFD_CLOEXEC, 0) : -1;
}

void StageThread::push(const Command& command, Merge merge) {
    if (!inbox_->pushWithin(command, merge, kPushWait)) GEODE_LOGW(kTag, "stage command %u dropped", static_cast<unsigned>(command.kind));
}

void StageThread::setWindow(ANativeWindow* window, int width, int height) {
    std::unique_lock<std::mutex> lock(ackLock_);
    if (finished_) return;
    const int64_t seq = nextSeq_.fetch_add(1) + 1;
    Command c;
    c.kind = window ? kStageWindow : kStageWindowGone;
    c.a = width;
    c.b = height;
    c.t = seq;
    c.target = window;
    // The reference travels with the command; the stage releases it when the window is replaced or gone. Pushed
    // under the lock, so a thread that has finished never leaves a hand-over in a queue nobody drains.
    if (window) ANativeWindow_acquire(window);
    if (!inbox_->pushWithin(c, Merge::Ordered, kPushWait)) {
        if (window) ANativeWindow_release(window);
        GEODE_LOGE(kTag, "window hand-over dropped");
        return;
    }
    if (!ackCv_.wait_for(lock, kWindowWait, [&] { return appliedSeq_ >= seq || finished_; })) {
        GEODE_LOGW(kTag, "the stage took over %lld ms to take a window", static_cast<long long>(kWindowWait.count()));
    }
}

void StageThread::setSize(int width, int height) {
    Command c;
    c.kind = kStageSize;
    c.a = width;
    c.b = height;
    push(c, Merge::LastWins);
}

void StageThread::setMode(RunMode mode) {
    Command c;
    c.kind = kStageMode;
    c.a = static_cast<int32_t>(mode);
    push(c, Merge::LastWins);
}

void StageThread::setPaused(bool paused) {
    Command c;
    c.kind = kStagePause;
    c.a = paused ? 1 : 0;
    push(c, Merge::LastWins);
}

void StageThread::redraw() {
    Command c;
    c.kind = kStageRedraw;
    push(c, Merge::LastWins);
}

void StageThread::setCap(float capFps) {
    Command c;
    c.kind = kStageCap;
    c.f = capFps;
    push(c, Merge::LastWins);
}

void StageThread::service() {
    Command c;
    c.kind = kStageService;
    push(c, Merge::LastWins);
}

void StageThread::trim() {
    Command c;
    c.kind = kStageTrim;
    push(c, Merge::LastWins);
}

void StageThread::quitAndWait() {
    const auto until = std::chrono::steady_clock::now() + kQuitWait;
    quit_.store(true);
    Command c;
    c.kind = kStageQuit;
    if (!inbox_->pushWithin(c, Merge::Ordered, kQuitWait)) Inbox::wake(inbox_.get());
    bool finished = false;
    {
        std::unique_lock<std::mutex> lock(ackLock_);
        finished = ackCv_.wait_until(lock, until, [&] { return finished_; });
    }
    if (!thread_.joinable()) return;
    // Past the bound (a wedged driver) the thread finishes on its own and frees what it owns; it holds the stage.
    if (finished && thread_.get_id() != std::this_thread::get_id()) {
        thread_.join();
    } else {
        GEODE_LOGW(kTag, "the stage thread outlived destroy's wait; it finishes on its own");
        thread_.detach();
    }
}

void StageThread::run() {
    try {
        pthread_setname_np(pthread_self(), "GeodeStage");
        // GLSurfaceView's GLThread set none (a Java thread's default, nice 0).
        if (!util::setThreadNice(util::kDisplayNice)) {
            GEODE_LOGW(kTag, "stage thread keeps its inherited priority");
        }
        ALooper* looper = ALooper_prepare(0);
        inbox_->bind(looper);
        sink_ = VsyncThread::process().attach(inbox_);
        if (framesFlag()) {
            core_.recordFrames(true);
            GEODE_LOGI(kFramesTag, "recording frames (%s)", kFramesProperty);
        }
        loop();
    } catch (...) {
        GEODE_LOGE(kTag, "stage thread ended on an exception");
        // Still torn down here, in the lifecycle's order: the client released and the EGL objects freed on the
        // thread that made them.
        try {
            core_.abandon();
        } catch (...) {
            GEODE_LOGE(kTag, "the stage's teardown ended on an exception too");
        }
    }
    VsyncThread::process().detach(sink_);
    sink_ = nullptr;
    std::lock_guard<std::mutex> lock(ackLock_);
    finished_ = true;
    // Hand-overs that arrived after quit: their window references are released here, nobody else reads them.
    Command batch[Inbox::kCapacity];
    for (size_t n = inbox_->queue.drain(batch, Inbox::kCapacity); n > 0; n = inbox_->queue.drain(batch, Inbox::kCapacity)) {
        for (size_t i = 0; i < n; ++i) core_.discard(batch[i]);
    }
    core_.close();
    ackCv_.notify_all();
}

void StageThread::loop() {
    Command batch[Inbox::kCapacity];
    for (;;) {
        // Drained before every sleep, the first time included, so commands sent before the looper was bound count.
        for (size_t n = inbox_->queue.drain(batch, Inbox::kCapacity); n > 0; n = inbox_->queue.drain(batch, Inbox::kCapacity)) {
            for (size_t i = 0; i < n; ++i) core_.apply(batch[i], nowMs());
        }
        if (quit_.load()) {
            Command quit;
            quit.kind = kStageQuit;
            core_.apply(quit, nowMs());
        }
        core_.step(nowMs());
        if (eventFd_ >= 0 && core_.hasEvents() && !eventSignalled_.exchange(true)) {
            const uint64_t one = 1;
            if (write(eventFd_, &one, sizeof(one)) < 0) eventSignalled_.store(false);
        }
        if (core_.unsettled()) GEODE_LOGE(kTag, "the stage lifecycle did not settle");
        if (core_.finished()) return;
        int timeout = -1;
        if (const int64_t deadline = core_.deadlineMs(); deadline >= 0) {
            timeout = static_cast<int>(std::clamp<int64_t>(deadline - nowMs(), 0, INT_MAX));
        }
        // Every return is treated as a wake: the queue is drained either way.
        ALooper_pollOnce(timeout, nullptr, nullptr, nullptr);
    }
}

Outcome StageThread::createContext(bool& surfaceless) {
    if (!egl_.ensureDisplay()) return Outcome::Refused;
    surfaceless = egl_.surfaceless();
    return egl_.createContext() ? Outcome::Done : Outcome::Refused;
}

Outcome StageThread::createSurface(void* window) {
    return egl_.createSurface(static_cast<ANativeWindow*>(window)) ? Outcome::Done : Outcome::BadSurface;
}

Outcome StageThread::makeCurrent() { return outcomeOf(egl_.makeCurrent()); }

Outcome StageThread::makeCurrentSurfaceless() { return outcomeOf(egl_.makeCurrentSurfaceless()); }

void StageThread::releaseCurrent() { egl_.releaseCurrent(); }

void StageThread::destroySurface() { egl_.destroySurface(); }

void StageThread::destroyContext() { egl_.destroyContext(); }

Outcome StageThread::swap() { return outcomeOf(egl_.swap()); }

void StageThread::releaseWindow(void* window) { ANativeWindow_release(static_cast<ANativeWindow*>(window)); }

// The surface's one vote, the user's rate (developer.android.com/media/optimize/performance/frame-rate): default
// compatibility (not video), and only a seamless switch, since a panel that blanks for a moment mid-track is worse than
// the rate. The window asks for the matching display mode itself (the shell's preferredDisplayModeId), so the two agree.
void StageThread::setFrameRate(void* window, float fps) {
    ANativeWindow_setFrameRateWithChangeStrategy(static_cast<ANativeWindow*>(window), fps, ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_DEFAULT,
                                                 ANATIVEWINDOW_CHANGE_FRAME_RATE_ONLY_IF_SEAMLESS);
}

void StageThread::vsyncRun(bool on) { VsyncThread::process().run(sink_, on); }

bool StageThread::gpuBusy() { return egl_.previousFrameBusy(); }

void StageThread::frameDone() { VsyncThread::frameDone(sink_); }

void StageThread::presentAt(int64_t stampNs) { egl_.presentAt(stampNs); }

// ADPF (developer.android.com/games/optimize/adpf): the session's target is the frame clock's budget, and each paced
// frame reports its whole time on this thread, the draw and the swap.
void StageThread::frameWork(int64_t budgetNs, int64_t actualNs) { workHint_.report(budgetNs, actualNs); }

// One log line per row: a chunk is too long for one logcat entry.
void StageThread::framesRecorded(const std::string& csv) {
    size_t from = 0;
    for (size_t end = csv.find('\n'); end != std::string::npos; end = csv.find('\n', from)) {
        GEODE_LOGI(kFramesTag, "%.*s", static_cast<int>(end - from), csv.c_str() + from);
        from = end + 1;
    }
}

void StageThread::windowApplied(int64_t sequence) {
    std::lock_guard<std::mutex> lock(ackLock_);
    appliedSeq_ = sequence;
    ackCv_.notify_all();
}

}  // namespace geode::stage::platform
