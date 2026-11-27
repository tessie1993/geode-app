#include "stage/StageCore.hpp"

#include <algorithm>
#include <utility>

#include "util/Trace.hpp"

namespace geode::stage {

namespace {

// More actions than this for one batch of events means the lifecycle is not converging: a bug, not a hang.
constexpr int kMaxActions = 64;

}  // namespace

StageCore::StageCore(Kind kind, std::shared_ptr<StageClient> client, float capFps, StagePort& port)
    : lifecycle_(kind), client_(std::move(client)), port_(port), cap_(capFps), budget_(frameBudget(capFps, 0, false)), paced_(0.0f) {}

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
            // The setting is a void event: the budget is derived from it here, once, and handed on whole.
            if (command.f == cap_) break;
            cap_ = command.f;
            deriveBudget(budget_.periodNs, swapPaces_);
            break;
        case kStageQuit: lifecycle_.quit(); break;
        case kStageService: serviceDue_ = true; break;
        case kStageTrim: lifecycle_.trimRequested(); break;
        case kStageVsync: vsyncWait_ = false; break;
        default: break;
    }
}

bool StageCore::paces() const { return client_ && lifecycle_.vsyncOn() && lifecycle_.canDraw(); }

void StageCore::step(int64_t nowMs) {
    lifecycle_.tick(nowMs);
    runActions();
    // Already acknowledged inside runActions when the surface moved; this is for a hand-over that moved none.
    acknowledge();
    if (serviceDue_) {
        serviceDue_ = false;
        if (client_) client_->service(lifecycle_.canDraw());
    }
    if (paces() && !vsyncWait_) {
        drawPaced();
        // What the frame's swap made of the lifecycle (a lost context, a bad surface) is settled before the loop sleeps.
        runActions();
    }
}

int64_t StageCore::waitMs(int64_t nowMs) const {
    if (paces()) return vsyncWait_ ? -1 : 0;
    const int64_t deadline = lifecycle_.deadlineMs();
    return deadline >= 0 ? std::max<int64_t>(deadline - nowMs, 0) : -1;
}

// One paced frame: drawn for the presentation time the pacer gives it, swapped through the pacer (which holds it to
// its slot), its draw (not the swap's wait) reported for ADPF and the recorder.
void StageCore::drawPaced() {
    const PaceTimes p = port_.paceTimes();
    deriveBudget(p.periodNs, p.swapPaces);
    const int64_t start = port_.nowNs();
    const int64_t presentNs = clock_.next(p.desiredNs, start, budget_.budgetNs, budget_.periodNs);
    const float fps = budget_.budgetNs > 0 ? static_cast<float>(1.0e9 / static_cast<double>(budget_.budgetNs)) : 0.0f;
    paced_.store(fps, std::memory_order_relaxed);
    int64_t drawNs = 0;
    const bool swapped = draw(fps, cap_, presentNs, drawNs);
    port_.frameWork(budget_.budgetNs, drawNs, swapped && p.swapPaces ? port_.lastGpuNs() : 0, start);
    util::traceCounter("geode.cpuUs", drawNs / 1000);
    if (const int64_t gpuNs = port_.lastGpuNs(); gpuNs > 0) util::traceCounter("geode.gpuUs", gpuNs / 1000);
    if (!swapped || !p.swapPaces) {
        // No swap held this frame back, so none paces the next: it waits for the platform's next vsync.
        vsyncWait_ = true;
        port_.wakeAtVsync();
        if (!swapped) return;
    }
    // Presentation to presentation, so a late frame shows as the longer interval it made on screen.
    const int64_t intervalNs = lastShownNs_ > 0 && presentNs > lastShownNs_ ? presentNs - lastShownNs_ : 0;
    lastShownNs_ = presentNs;
    if (intervalNs > 0) util::traceCounter("geode.frameIntervalUs", intervalNs / 1000);
    if (recorder_.enabled()) {
        if (recorder_.full()) handOverFrames();
        recorder_.drawn(drawNs, intervalNs);
        recorder_.gpuOfPrevious(port_.lastGpuNs());
    }
}

// The one place the frame budget is worked out: the setting and the display mode in, the descriptor out. A new one is
// published for the UI, traced, handed to the pacer (the swap interval) and, when the rate it votes changed, to the window.
// The swap, the vote, the work target and the UI only read it.
void StageCore::deriveBudget(int64_t periodNs, bool swapPaces) {
    swapPaces_ = swapPaces;
    const FrameBudget next = frameBudget(cap_, periodNs, swapPaces);
    if (next == budget_) return;
    const bool voteChanged = next.voteFps != budget_.voteFps;
    budget_ = next;
    mirror_.store(budget_.budgetNs, std::memory_order_relaxed);
    util::traceCounter("geode.frameBudgetUs", budget_.budgetNs / 1000);
    if (lifecycle_.vsyncOn()) port_.pace(true, budget_);
    if (voteChanged) restateRate();
}

void StageCore::handOverFrames() {
    if (recorder_.size() > 0) port_.framesRecorded(recorder_.csv());
    recorder_.clear();
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

// The window is asked for the user's rate (one vote, restated only for a new surface, size or rate); the pacer keeps
// the rate whether or not the display follows it.
void StageCore::restateRate() {
    if (window_) port_.setFrameRate(window_, budget_.voteFps);
}

// A window that went has no EGL surface left on it; a new one is held by its surface. Nothing after that touches the
// window the shell handed over, so the shell need not wait for the client's calls or a frame.
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
            clock_.restart();
            lastShownNs_ = 0;
            vsyncWait_ = false;
            port_.pace(true, budget_);
            return Outcome::Done;
        case Act::StopVsync:
            port_.pace(false, budget_);
            if (recorder_.enabled()) handOverFrames();
            return Outcome::Done;
        case Act::DrawOnce: {
            // No pacer asked for it: the time since the last frame is not a step.
            if (client_) client_->restartClock();
            int64_t drawNs = 0;
            draw(paced_.load(std::memory_order_relaxed), cap_, 0, drawNs);
            return Outcome::Done;
        }
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

// One frame and its swap; the swap's outcome is the lifecycle's next event. presentNs is the frame's presentation
// time (0 for a frame no pacer asked for: the client's own clock). drawNs is set to the client's draw time. True
// when a new frame was swapped.
bool StageCore::draw(float pacedFps, float capFps, int64_t presentNs, int64_t& drawNs) {
    if (!client_) return false;
    client_->framePresent(presentNs);
    uint32_t events = 0;
    const int64_t start = port_.nowNs();
    {
        const util::TraceSection section("geode.draw");
        events = client_->frame(pacedFps, capFps);
    }
    drawNs = port_.nowNs() - start;
    events_.fetch_or(events & ~kFrameNothingNew, std::memory_order_relaxed);
    // The picture is the one already on screen: no new buffer, so no swap, and the display keeps the last.
    if ((events & kFrameNothingNew) != 0) return false;
    Outcome swapped = Outcome::Done;
    {
        const util::TraceSection section("geode.swap");
        swapped = port_.swap();
    }
    switch (swapped) {
        case Outcome::Done: lifecycle_.frameDrawn(); return true;
        case Outcome::ContextLost: lifecycle_.contextLost(); return false;
        default: lifecycle_.surfaceBad(); return false;
    }
}

}  // namespace geode::stage
