#include "viz/ThermalGovernor.hpp"

#include <algorithm>

namespace geode::viz {

PerformanceMonitor::PerformanceMonitor(float targetFps, float sustainSeconds, int windowSize)
    : targetFps_(targetFps), sustainSeconds_(sustainSeconds), windowSize_(std::min(windowSize, static_cast<int>(samples_.size()))) {}

float PerformanceMonitor::averageFps() const {
    if (count_ < windowSize_ / 2) return 0.0f;
    float sum = 0.0f;
    const int n = std::min(count_, windowSize_);
    for (int i = 0; i < n; i++) sum += samples_[i];
    const float meanDt = sum / n;
    return meanDt > 1e-6f ? 1.0f / meanDt : 0.0f;
}

int PerformanceMonitor::onFrame(float dtSeconds) {
    const float target = effectiveTargetFps();
    if (target != lastTargetFps_) {
        lastTargetFps_ = target;
        deficitSeconds_ = 0.0f;
    }
    const float fps = dtSeconds > 1e-6f ? 1.0f / dtSeconds : 1000.0f;
    if (fps < 5.0f || fps > 180.0f) return 0;
    samples_[index_] = dtSeconds;
    index_ = (index_ + 1) % windowSize_;
    count_ = std::min(count_ + 1, windowSize_);
    const float avg = averageFps();
    if (avg <= 0.0f) return 0;
    if (avg < target) deficitSeconds_ += dtSeconds; else deficitSeconds_ = 0.0f;
    if (deficitSeconds_ < sustainSeconds_) return 0;
    const float severity = (target - avg) / target;
    return severity > 0.35f ? kSevereRelief : kMildRelief;
}

void PerformanceMonitor::reset() {
    samples_.fill(0.0f);
    count_ = 0;
    index_ = 0;
    deficitSeconds_ = 0.0f;
}

bool PerformanceMonitor::keepingUp() const {
    if (count_ < windowSize_) return false;
    return averageFps() >= effectiveTargetFps();
}

float PerformanceMonitor::effectiveTargetFps() const {
    const float paced = pacedFps_.load();
    if (paced <= 0.0f) return targetFps_;
    return std::min(targetFps_, paced * kKeepingUpFraction);
}

void ThermalGovernor::setAdaptive(bool on) {
    // Switching the mode off where the platform answers hands the tier back to the platform alone, as
    // it was before the mode existed. The setting is app-wide, so every governor would do the same.
    if (adaptive_.exchange(on) && !on && shared_.platformKnows()) shared_.clearMeasuredFloor();
}

void ThermalGovernor::onFrame(float dtSeconds) {
    if (offscreenDepth_.load() > 0) return;
    // The platform's readings arrive through ThermalState::push, settled once a second for the process;
    // per frame there is only this surface's own frame time to watch.
    trackMeasuredTrend(dtSeconds);
}

void ThermalGovernor::onSurfaceRecreated() {
    monitor_.reset();
    keptUpSeconds_ = 0.0f;
    relaxSeconds_ = kMeasuredRelaxSeconds;
    sinceRelaxSeconds_ = kRelaxProbeSeconds;
}

void ThermalGovernor::endOffscreenRender() {
    int depth = offscreenDepth_.load();
    while (depth > 0 && !offscreenDepth_.compare_exchange_weak(depth, depth - 1)) {}
}

void ThermalGovernor::trackMeasuredTrend(float dtSeconds) {
    // The effective cap, not the surface's own: under a 30 fps thermal cap, 30 fps is keeping up.
    monitor_.setPacedFps(pacedFps());
    int relief = monitor_.onFrame(dtSeconds);
    const bool adaptive = adaptive_.load();
    const bool platformKnows = shared_.platformKnows();
    // Adapt off on a phone that reports thermals (the High preset): the platform owns the tier and a
    // mild deficit is the price of the quality the user asked for. Frames far under the cap are not:
    // those step down, and from then on the surface adapts until the floor is back at Full.
    if (!adaptive && platformKnows && relief < PerformanceMonitor::kSevereRelief && shared_.measuredFloor() == ThermalTier::Full) {
        relief = 0;
    }
    // Any surface's raise restarts every surface's wait to step back down, so a cheap surface keeping
    // up (a wallpaper preview) cannot undo the raise a busy one just made.
    const uint64_t raises = shared_.raises();
    if (raises != seenRaises_) {
        seenRaises_ = raises;
        keptUpSeconds_ = 0.0f;
    }
    if (sinceRelaxSeconds_ < kRelaxProbeSeconds) {
        sinceRelaxSeconds_ += dtSeconds;
        // The step back up held: the next one waits the usual time again.
        if (sinceRelaxSeconds_ >= kRelaxProbeSeconds) relaxSeconds_ = kMeasuredRelaxSeconds;
    }
    if (relief <= 0) {
        // Without a thermal API and without Adapt, the measured floor keeps its old promise of never
        // upgrading: frame time is that phone's only signal. Everywhere else it steps back up.
        const bool stepsBackUp = adaptive || platformKnows;
        if (!stepsBackUp || shared_.measuredFloor() == ThermalTier::Full || !monitor_.keepingUp()) {
            keptUpSeconds_ = 0.0f;
            return;
        }
        keptUpSeconds_ += dtSeconds;
        if (keptUpSeconds_ < relaxSeconds_) return;
        keptUpSeconds_ = 0.0f;
        shared_.lowerMeasuredFloor();
        sinceRelaxSeconds_ = 0.0f;
        monitor_.reset();
        return;
    }
    // A raise while a step back up is still on probation: that tier is more than the phone can draw.
    if (sinceRelaxSeconds_ < kRelaxProbeSeconds) {
        relaxSeconds_ = std::min(relaxSeconds_ * 2.0f, kMeasuredRelaxMaxSeconds);
        sinceRelaxSeconds_ = kRelaxProbeSeconds;
    }
    keptUpSeconds_ = 0.0f;
    shared_.raiseMeasuredFloor(relief);
    seenRaises_ = shared_.raises();
    monitor_.reset();
}

}  // namespace geode::viz
