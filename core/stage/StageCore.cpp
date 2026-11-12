#include "stage/StageCore.hpp"

#include <algorithm>
#include <utility>

#include "stage/StagePacing.hpp"

namespace geode::stage {

namespace {

// More actions than this for one batch of events means the lifecycle is not converging: a bug, not a hang.
constexpr int kMaxActions = 64;

}  // namespace

StageCore::StageCore(Kind kind, std::shared_ptr<StageClient> client, float capFps, StagePort& port)
    : lifecycle_(kind), client_(std::move(client)), port_(port), cap_(capFps), paced_(capFps) {}

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
            paced_.store(command.f, std::memory_order_relaxed);
            port_.vsyncCap(command.f);
            restateRate();
            break;
        case kStageFrameDue:
            frameDue_ = true;
            dueFps_ = command.f;
            dueCapFps_ = command.g;
            paced_.store(command.f, std::memory_order_relaxed);
            break;
        case kStageFrameRate:
            if (window_) port_.setFrameRate(window_, command.f);
            break;
        case kStageQuit: lifecycle_.quit(); break;
        case kStageService: serviceDue_ = true; break;
        default: break;
    }
}

void StageCore::step(int64_t nowMs) {
    lifecycle_.tick(nowMs);
    runActions();
    // Acknowledged once the lifecycle has acted on it: a window that went has no EGL surface left on it here.
    if (seenSeq_ > appliedSeq_) {
        appliedSeq_ = seenSeq_;
        port_.windowApplied(appliedSeq_);
    }
    if (serviceDue_) {
        serviceDue_ = false;
        if (client_) client_->service(lifecycle_.canDraw());
    }
    if (frameDue_) {
        frameDue_ = false;
        // A request already in flight when the vsync stopped is dropped here.
        if (lifecycle_.vsyncOn() && lifecycle_.canDraw()) {
            draw(dueFps_, dueCapFps_);
            runActions();
        }
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

// The hint is the cap's, lowered with the pacer's thermal rungs, so the panel is asked for what will be drawn.
void StageCore::restateRate() {
    if (window_) port_.setFrameRate(window_, StagePacing::frameRateFor(cap_, port_.fpsRungsDown()));
}

void StageCore::runActions() {
    for (int i = 0; i < kMaxActions; ++i) {
        const Action action = lifecycle_.next();
        if (action.act == Act::None) {
            unsettled_ = false;
            return;
        }
        lifecycle_.done(action, execute(action));
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
        case Act::StartVsync: port_.vsyncRun(true); return Outcome::Done;
        case Act::StopVsync: port_.vsyncRun(false); return Outcome::Done;
        case Act::DrawOnce: draw(paced_.load(std::memory_order_relaxed), cap_); return Outcome::Done;
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

// One frame and its swap; the swap's outcome is the lifecycle's next event.
void StageCore::draw(float pacedFps, float capFps) {
    if (!client_) return;
    events_.fetch_or(client_->frame(pacedFps, capFps), std::memory_order_relaxed);
    switch (port_.swap()) {
        case Outcome::Done: lifecycle_.frameDrawn(); break;
        case Outcome::ContextLost: lifecycle_.contextLost(); break;
        default: lifecycle_.surfaceBad(); break;
    }
}

}  // namespace geode::stage
