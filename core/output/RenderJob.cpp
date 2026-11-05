#include "output/RenderJob.hpp"

#include <algorithm>
#include <cmath>

namespace geode::output {

RenderReport reportFor(RenderOutcome outcome) {
    switch (outcome) {
        case RenderOutcome::Saved:
            return RenderReport::Done;
        case RenderOutcome::Failed:
        case RenderOutcome::NothingToRender:
            return RenderReport::Failed;
        case RenderOutcome::Locked:
            return RenderReport::OfferPro;
        case RenderOutcome::Cancelled:
        case RenderOutcome::None:
            break;
    }
    return RenderReport::Clear;
}

bool keepsOutput(RenderOutcome outcome) { return outcome == RenderOutcome::Saved; }

RenderJob& RenderJob::process() {
    static RenderJob job;
    return job;
}

bool RenderJob::begin(RenderKind kind) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.live()) return false;
    RenderJobSnapshot next;
    next.phase = RenderPhase::Preparing;
    next.kind = kind;
    next.generation = state_.generation + 1;
    state_ = next;
    eta_ = RenderEtaState{};
    return true;
}

bool RenderJob::publish(float progress, int64_t atMs, bool steady) {
    if (std::isnan(progress)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!state_.live()) return false;
    const float clamped = std::clamp(progress, 0.0f, 1.0f);
    const int64_t left = renderEtaSample(eta_, clamped, atMs, steady);
    state_.hasProgress = true;
    state_.progress = clamped;
    if (state_.phase == RenderPhase::Preparing) state_.phase = RenderPhase::Rendering;
    state_.secondsLeft = state_.phase == RenderPhase::Cancelling ? -1 : left;
    return true;
}

bool RenderJob::requestCancel() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!state_.live() || state_.cancelRequested) return false;
    state_.cancelRequested = true;
    state_.phase = RenderPhase::Cancelling;
    state_.secondsLeft = -1;
    return true;
}

RenderOutcome RenderJob::finish(RenderOutcome reported) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!state_.live()) return RenderOutcome::None;
    RenderOutcome resolved = reported;
    if (resolved == RenderOutcome::None) resolved = RenderOutcome::Failed;
    if (state_.cancelRequested && resolved == RenderOutcome::Failed) resolved = RenderOutcome::Cancelled;
    state_.phase = RenderPhase::Ended;
    state_.outcome = resolved;
    state_.secondsLeft = -1;
    eta_ = RenderEtaState{};
    return resolved;
}

void RenderJob::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = RenderJobSnapshot{};
    eta_ = RenderEtaState{};
}

RenderJobSnapshot RenderJob::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

bool RenderJob::cancelRequested() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_.cancelRequested;
}

}  // namespace geode::output
