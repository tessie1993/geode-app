#include "viz/PerformanceMonitor.hpp"

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
    // Above the rate the target was tuned at (50 of 60), it scales with the cap: a 90 or 120 fps live cap
    // (Settings › Performance on a fast panel) drawn at 55 is not keeping up. Continuous at 60, so every cap
    // of 60 and under reads as before.
    if (paced > kTargetTunedAtFps) return targetFps_ * paced / kTargetTunedAtFps;
    return std::min(targetFps_, paced * kKeepingUpFraction);
}

}  // namespace geode::viz
