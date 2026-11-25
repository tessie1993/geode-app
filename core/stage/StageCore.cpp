#include "stage/StageCore.hpp"

#include <algorithm>
#include <utility>

#include "stage/VsyncRule.hpp"
#include "viz/frame/PresentTime.hpp"

namespace geode::stage {

namespace {

// More actions than this for one batch of events means the lifecycle is not converging: a bug, not a hang.
constexpr int kMaxActions = 64;

}  // namespace

StageCore::StageCore(Kind kind, std::shared_ptr<StageClient> client, float capFps, StagePort& port)
    : lifecycle_(kind), client_(std::move(client)), port_(port), cap_(capFps), paced_(0.0f) {}

void StageCore::apply(const Command& command, int64_t nowMs) {
    switch (command.kind) {
        case kStageWindow:
            if (window_) port_.releaseWindow(window_);
            window_ = command.target;
            seenSeq_ = std::max(seenSeq_, command.t);
            lifecycle_.windowSet(command.a, command.b);
            // A new window is a new Surface, and the rate preference lives on the surface.
            restateRate();
            break;
        case kStageWindowGone:
            if (window_) port_.releaseWindow(window_);
            window_ = nullptr;
            seenSeq_ = std::max(seenSeq_, command.t);
            lifecycle_.windowGone();
            break;
        case kStageSize:
            lifecycle_.windowChanged(command.a, command.b);
            restateRate();
            break;
        case kStageMode: lifecycle_.setMode(static_cast<RunMode>(command.a), nowMs); break;
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
            restateRate();
            break;
        case kStageFrameDue:
            frameDue_ = true;
            dueFps_ = presentRateFps(command.a);
            dueFrameNs_ = command.t;
            duePresentNs_ = command.b > 0 ? command.t + command.b : 0;
            dueStampNs_ = viz::frame::latchStampNs(duePresentNs_, command.a);
            paced_.store(dueFps_, std::memory_order_relaxed);
            break;
        case kStageQuit: lifecycle_.quit(); break;
        case kStageService: serviceDue_ = true; break;
        case kStageTrim: lifecycle_.trimRequested(); break;
        default: break;
    }
}

void StageCore::step(int64_t nowMs) {
    lifecycle_.tick(nowMs);
    runActions();
    // Already acknowledged inside runActions when the surface moved; this is for a hand-over that moved none.
    acknowledge();
    if (serviceDue_) {
        serviceDue_ = false;
        if (client_) client_->service(lifecycle_.canDraw());
    }
    if (frameDue_) {
        frameDue_ = false;
        // A request already in flight when the vsync stopped is dropped here, and so is one whose predecessor the GPU
        // has not finished (stage/VsyncRule).
        if (lifecycle_.vsyncOn() && lifecycle_.canDraw() && !skipVsync(false, port_.gpuBusy())) {
            draw(dueFps_, cap_, dueFrameNs_, duePresentNs_, dueStampNs_);
            runActions();
        }
        port_.frameDone();
    }
}

void StageCore::abandon() {
    lifecycle_.quit();
    runActions();
}

void StageCore::discard(const Command& command) {
    if (command.kind == kStageWindow && command.target) port_.releaseWindow(command.target);
}

void StageCore::close() {
    if (window_) port_.releaseWindow(window_);
    window_ = nullptr;
}

// The window is asked for the user's rate, as a hint; what is drawn does not depend on the answer (stage/VsyncRule).
void StageCore::restateRate() {
    if (window_) port_.setFrameRate(window_, frameRateHint(cap_));
}

// A window that went has no EGL surface left on it; a new one is held by its surface. Nothing after that touches the
// window the shell handed over, so the shell need not wait for the client's calls, the vsync or a frame.
void StageCore::acknowledge() {
    if (seenSeq_ <= appliedSeq_) return;
    appliedSeq_ = seenSeq_;
    port_.windowApplied(appliedSeq_);
}

void StageCore::runActions() {
    for (int i = 0; i < kMaxActions; ++i) {
        const Action action = lifecycle_.next();
        if (action.act == Act::None) {
            unsettled_ = false;
            return;
        }
        lifecycle_.done(action, execute(action));
        if ((action.act == Act::CreateSurface && window_) || (action.act == Act::DestroySurface && !window_)) acknowledge();
    }
    unsettled_ = true;
}

Outcome StageCore::execute(const Action& action) {
    switch (action.act) {
        case Act::None: return Outcome::Done;
        case Act::CreateContext: {
            bool surfaceless = false;
            const Outcome made = port_.createContext(surfaceless);
            lifecycle_.surfacelessSupported(surfaceless);
            return made;
        }
        case Act::CreateSurface: return port_.createSurface(window_);
        case Act::MakeCurrent: return port_.makeCurrent();
        case Act::MakeCurrentSurfaceless: return port_.makeCurrentSurfaceless();
        case Act::ClientSurfaceCreated:
            if (client_) client_->surfaceCreated();
            return Outcome::Done;
        case Act::ClientSurfaceChanged:
            if (client_) client_->surfaceChanged(action.width, action.height);
            return Outcome::Done;
        case Act::StartVsync:
            // A start does not inherit the gap it was stopped across.
            if (client_) client_->restartClock();
            port_.vsyncRun(true);
            return Outcome::Done;
        case Act::StopVsync: port_.vsyncRun(false); return Outcome::Done;
        case Act::DrawOnce:
            // No vsync asked for it: the time since the last frame is not a step.
            if (client_) client_->restartClock();
            draw(paced_.load(std::memory_order_relaxed), cap_, 0, 0, 0);
            return Outcome::Done;
        case Act::ClientTrimSpares:
            if (client_) client_->trimSpares();
            return Outcome::Done;
        case Act::ClientContextLost:
            if (client_) client_->contextLost();
            return Outcome::Done;
        case Act::ReleaseCurrent: port_.releaseCurrent(); return Outcome::Done;
        case Act::DestroySurface: port_.destroySurface(); return Outcome::Done;
        case Act::DestroyContext: port_.destroyContext(); return Outcome::Done;
        case Act::ClientRelease: {
            // Dropped before the call returns to the lifecycle: release is the client's last call from here.
            const std::shared_ptr<StageClient> client = std::move(client_);
            client_.reset();
            if (client) client->release(action.glCurrent);
            return Outcome::Done;
        }
        case Act::ReportState: state_.store(action.state, std::memory_order_relaxed); return Outcome::Done;
    }
    return Outcome::Done;
}

// One frame and its swap; the swap's outcome is the lifecycle's next event. frameNs is the vsync's frame time and
// presentNs the frame's expected presentation (0 for a frame no vsync asked for: the client's own clock, no time);
// stampNs is what the swap is stamped with for it (frame::latchStampNs, 0 for no stamp).
void StageCore::draw(float pacedFps, float capFps, int64_t frameNs, int64_t presentNs, int64_t stampNs) {
    if (!client_) return;
    client_->frameTime(frameNs);
    client_->framePresent(presentNs);
    const uint32_t events = client_->frame(pacedFps, capFps);
    events_.fetch_or(events & ~kFrameNothingNew, std::memory_order_relaxed);
    // The picture is the one already on screen: no new buffer, so no swap, and the display keeps the last.
    if ((events & kFrameNothingNew) != 0) return;
    port_.presentAt(stampNs);
    switch (port_.swap()) {
        case Outcome::Done: lifecycle_.frameDrawn(); break;
        case Outcome::ContextLost: lifecycle_.contextLost(); break;
        default: lifecycle_.surfaceBad(); break;
    }
}

}  // namespace geode::stage
