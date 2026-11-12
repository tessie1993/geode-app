#include "stage/android/StageThread.hpp"

#include <pthread.h>
#include <sys/resource.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <climits>

#include "util/Log.hpp"
#include "viz/frame/Thermal.hpp"

namespace geode::stage::platform {

namespace {

constexpr const char* kTag = "GeodeStage";

// GLSurfaceView's GLThread sets no priority: a Java thread's default, nice 0.
constexpr int kStageNice = 0;

// More actions than this for one batch of events means the lifecycle is not converging: a bug, logged, not a hang.
constexpr int kMaxActions = 64;

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

Outcome outcomeOf(EGLint error) {
    if (error == EGL_SUCCESS) return Outcome::Done;
    return error == EGL_CONTEXT_LOST ? Outcome::ContextLost : Outcome::BadSurface;
}

}  // namespace

std::shared_ptr<StageThread> StageThread::start(Kind kind, std::unique_ptr<StageClient> client, float capFps) {
    auto stage = std::make_shared<StageThread>(kind, std::move(client), capFps);
    // The thread holds the stage too, so a thread left to finish after destroy's bound still has it.
    stage->thread_ = std::thread([stage] { stage->run(); });
    return stage;
}

StageThread::StageThread(Kind kind, std::unique_ptr<StageClient> client, float capFps)
    : lifecycle_(kind), client_(std::move(client)), inbox_(std::make_shared<Inbox>()), cap_(capFps), paced_(capFps) {}

StageThread::~StageThread() {
    // Normally the thread has released these; a thread that died on an exception may not have.
    if (window_) ANativeWindow_release(window_);
    if (thread_.joinable()) thread_.detach();
}

void StageThread::push(const Command& command, Merge merge) {
    if (!inbox_->pushWithin(command, merge, kWindowWait)) GEODE_LOGW(kTag, "stage command %u dropped", static_cast<unsigned>(command.kind));
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
    if (!inbox_->pushWithin(c, Merge::Ordered, kWindowWait)) {
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

void StageThread::quitAndWait() {
    Command c;
    c.kind = kStageQuit;
    push(c, Merge::Ordered);
    bool finished = false;
    {
        std::unique_lock<std::mutex> lock(ackLock_);
        finished = ackCv_.wait_for(lock, kQuitWait, [&] { return finished_; });
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
        if (setpriority(PRIO_PROCESS, static_cast<id_t>(gettid()), kStageNice) != 0) {
            GEODE_LOGW(kTag, "stage thread keeps its inherited priority");
        }
        ALooper* looper = ALooper_prepare(0);
        inbox_->bind(looper);
        sink_ = VsyncThread::process().attach(inbox_, cap_);
        loop();
    } catch (...) {
        GEODE_LOGE(kTag, "stage thread ended on an exception");
    }
    VsyncThread::process().detach(sink_);
    sink_ = nullptr;
    std::lock_guard<std::mutex> lock(ackLock_);
    finished_ = true;
    // Hand-overs that arrived after quit: their window references are released here, nobody else reads them.
    Command batch[Inbox::kCapacity];
    for (size_t n = inbox_->queue.drain(batch, Inbox::kCapacity); n > 0; n = inbox_->queue.drain(batch, Inbox::kCapacity)) {
        for (size_t i = 0; i < n; ++i) {
            if (batch[i].kind == kStageWindow && batch[i].target) ANativeWindow_release(static_cast<ANativeWindow*>(batch[i].target));
        }
    }
    if (window_) ANativeWindow_release(window_);
    window_ = nullptr;
    ackCv_.notify_all();
}

void StageThread::loop() {
    Command batch[Inbox::kCapacity];
    for (;;) {
        // Drained before every sleep, the first time included, so commands sent before the looper was bound count.
        for (size_t n = inbox_->queue.drain(batch, Inbox::kCapacity); n > 0; n = inbox_->queue.drain(batch, Inbox::kCapacity)) {
            for (size_t i = 0; i < n; ++i) apply(batch[i]);
        }
        lifecycle_.tick(nowMs());
        runActions();
        acknowledge();
        if (frameDue_) {
            frameDue_ = false;
            // A request already in flight when the vsync stopped is dropped here.
            if (lifecycle_.vsyncOn() && lifecycle_.canDraw()) {
                draw(dueFps_, dueCapFps_);
                runActions();
            }
        }
        if (lifecycle_.finished()) return;
        int timeout = -1;
        if (const int64_t deadline = lifecycle_.deadlineMs(); deadline >= 0) {
            timeout = static_cast<int>(std::clamp<int64_t>(deadline - nowMs(), 0, INT_MAX));
        }
        // Every return is treated as a wake: the queue is drained either way.
        ALooper_pollOnce(timeout, nullptr, nullptr, nullptr);
    }
}

void StageThread::apply(const Command& command) {
    switch (command.kind) {
        case kStageWindow:
            if (window_) ANativeWindow_release(window_);
            window_ = static_cast<ANativeWindow*>(command.target);
            seenSeq_ = std::max(seenSeq_, command.t);
            lifecycle_.windowSet(command.a, command.b);
            // A new window is a new Surface, and the rate preference lives on the surface.
            restateRate();
            break;
        case kStageWindowGone:
            if (window_) ANativeWindow_release(window_);
            window_ = nullptr;
            seenSeq_ = std::max(seenSeq_, command.t);
            lifecycle_.windowGone();
            break;
        case kStageSize:
            lifecycle_.windowChanged(command.a, command.b);
            restateRate();
            break;
        case kStageMode: lifecycle_.setMode(static_cast<RunMode>(command.a), nowMs()); break;
        case kStagePause:
            if (command.a != 0) {
                lifecycle_.pause();
            } else {
                lifecycle_.resume();
            }
            break;
        case kStageRedraw: lifecycle_.redrawNeeded(); break;
        case kStageCap:
            cap_ = command.f;
            paced_.store(command.f, std::memory_order_relaxed);
            VsyncThread::process().setCap(sink_, command.f);
            restateRate();
            break;
        case kStageFrameDue:
            frameDue_ = true;
            dueFps_ = command.f;
            dueCapFps_ = command.g;
            paced_.store(command.f, std::memory_order_relaxed);
            break;
        case kStageFrameRate:
            if (window_) ANativeWindow_setFrameRate(window_, command.f, ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_DEFAULT);
            break;
        case kStageQuit: lifecycle_.quit(); break;
        default: break;
    }
}

// The two-argument form is only-if-seamless, as the two-argument Surface.setFrameRate was: a panel that blanks for
// a moment mid-track is worse than the rate it was asked for.
void StageThread::restateRate() {
    if (!window_) return;
    const float fps = StagePacing::frameRateFor(cap_, viz::ThermalState::process().fpsRungsDown());
    ANativeWindow_setFrameRate(window_, fps, ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_DEFAULT);
}

void StageThread::acknowledge() {
    std::lock_guard<std::mutex> lock(ackLock_);
    if (seenSeq_ <= appliedSeq_) return;
    appliedSeq_ = seenSeq_;
    ackCv_.notify_all();
}

void StageThread::runActions() {
    for (int i = 0; i < kMaxActions; ++i) {
        const Action action = lifecycle_.next();
        if (action.act == Act::None) return;
        lifecycle_.done(action, execute(action));
    }
    GEODE_LOGE(kTag, "the stage lifecycle did not settle in %d actions", kMaxActions);
}

Outcome StageThread::execute(const Action& action) {
    switch (action.act) {
        case Act::None: return Outcome::Done;
        case Act::CreateContext: {
            if (!egl_.ensureDisplay()) return Outcome::Refused;
            lifecycle_.surfacelessSupported(egl_.surfaceless());
            return egl_.createContext() ? Outcome::Done : Outcome::Refused;
        }
        case Act::CreateSurface: return egl_.createSurface(window_) ? Outcome::Done : Outcome::BadSurface;
        case Act::MakeCurrent: return outcomeOf(egl_.makeCurrent());
        case Act::MakeCurrentSurfaceless: return outcomeOf(egl_.makeCurrentSurfaceless());
        case Act::ClientSurfaceCreated:
            if (client_) client_->surfaceCreated();
            return Outcome::Done;
        case Act::ClientSurfaceChanged:
            if (client_) client_->surfaceChanged(action.width, action.height);
            return Outcome::Done;
        case Act::StartVsync: VsyncThread::process().run(sink_, true); return Outcome::Done;
        case Act::StopVsync: VsyncThread::process().run(sink_, false); return Outcome::Done;
        case Act::DrawOnce: draw(paced_.load(std::memory_order_relaxed), cap_); return Outcome::Done;
        case Act::ClientTrimSpares:
            if (client_) client_->trimSpares();
            return Outcome::Done;
        case Act::ClientContextLost:
            if (client_) client_->contextLost();
            return Outcome::Done;
        case Act::ReleaseCurrent: egl_.releaseCurrent(); return Outcome::Done;
        case Act::DestroySurface: egl_.destroySurface(); return Outcome::Done;
        case Act::DestroyContext: egl_.destroyContext(); return Outcome::Done;
        case Act::ClientRelease:
            if (client_) client_->release(action.glCurrent);
            client_.reset();
            return Outcome::Done;
        case Act::ReportState: state_.store(action.state, std::memory_order_relaxed); return Outcome::Done;
    }
    return Outcome::Done;
}

// One frame and its swap; the swap's error is the lifecycle's next event.
void StageThread::draw(float pacedFps, float capFps) {
    if (!client_) return;
    events_.fetch_or(client_->frame(pacedFps, capFps), std::memory_order_relaxed);
    const EGLint error = egl_.swap();
    if (error == EGL_SUCCESS) {
        lifecycle_.frameDrawn();
    } else if (error == EGL_CONTEXT_LOST) {
        lifecycle_.contextLost();
    } else {
        lifecycle_.surfaceBad();
    }
}

}  // namespace geode::stage::platform
