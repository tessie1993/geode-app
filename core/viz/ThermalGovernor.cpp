#include "viz/ThermalGovernor.hpp"

#include <algorithm>

namespace geode::viz {

const ThermalTierInfo& thermalTierInfo(ThermalTier tier) {
    static const std::array<ThermalTierInfo, 4> kInfos = {{
        {1.0f, true, 0.0f},
        {0.85f, true, 0.0f},
        {0.7f, false, 0.0f},
        {0.6f, false, 30.0f},
    }};
    return kInfos[static_cast<int>(tier)];
}

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
    return severity > 0.35f ? 2 : 1;
}

void PerformanceMonitor::reset() {
    samples_.fill(0.0f);
    count_ = 0;
    index_ = 0;
    deficitSeconds_ = 0.0f;
}

float PerformanceMonitor::effectiveTargetFps() const {
    const float paced = pacedFps_.load();
    if (paced <= 0.0f) return targetFps_;
    return std::min(targetFps_, paced * kKeepingUpFraction);
}

void ThermalGovernor::onFrame(float dtSeconds) {
    if (offscreenDepth_.load() > 0) return;
    if (!platformKnows_.load()) trackMeasuredTrend(dtSeconds);
    sampleClock_ += dtSeconds;
    if (sampleClock_ < kSamplePeriodSeconds) return;
    sampleClock_ = 0.0f;
    settle(observe());
}

void ThermalGovernor::onSurfaceRecreated() {
    monitor_.reset();
    sampleClock_ = 0.0f;
    hotDwellSeconds_ = 0.0f;
    coolDwellSeconds_ = 0.0f;
}

void ThermalGovernor::endOffscreenRender() {
    int depth = offscreenDepth_.load();
    while (depth > 0 && !offscreenDepth_.compare_exchange_weak(depth, depth - 1)) {}
}

void ThermalGovernor::trackMeasuredTrend(float dtSeconds) {
    monitor_.setPacedFps(pacedFps_.load());
    const int relief = monitor_.onFrame(dtSeconds);
    if (relief <= 0) return;
    const int ceiling = static_cast<int>(kMeasuredCeiling);
    measuredFloor_ = static_cast<ThermalTier>(std::min(static_cast<int>(measuredFloor_) + relief, ceiling));
    monitor_.reset();
}

ThermalTier ThermalGovernor::observe() const {
    const ThermalTier platform = std::max(statusTier(osStatus_.load()), headroomTier());
    return platformKnows_.load() ? platform : std::max(platform, measuredFloor_);
}

void ThermalGovernor::settle(ThermalTier observed) {
    const ThermalTier current = settled_.load();
    if (observed > current) {
        coolDwellSeconds_ = 0.0f;
        hotDwellSeconds_ += kSamplePeriodSeconds;
        if (hotDwellSeconds_ >= kEscalateDwellSeconds) {
            settled_.store(observed);
            hotDwellSeconds_ = 0.0f;
        }
    } else if (observed < current) {
        hotDwellSeconds_ = 0.0f;
        coolDwellSeconds_ += kSamplePeriodSeconds;
        if (coolDwellSeconds_ >= kRelaxDwellSeconds) {
            settled_.store(static_cast<ThermalTier>(static_cast<int>(current) - 1));
            coolDwellSeconds_ = 0.0f;
        }
    } else {
        hotDwellSeconds_ = 0.0f;
        coolDwellSeconds_ = 0.0f;
    }
}

ThermalTier ThermalGovernor::statusTier(int status) {
    if (status <= kStatusNone) return ThermalTier::Full;
    if (status == kStatusLight) return ThermalTier::Eased;
    if (status == kStatusModerate) return ThermalTier::Reduced;
    return ThermalTier::Minimal;
}

ThermalTier ThermalGovernor::headroomTier() const {
    const float headroom = headroom_.load();
    if (std::isnan(headroom)) return ThermalTier::Full;
    if (headroom >= kHeadroomMinimal) return ThermalTier::Minimal;
    if (headroom >= kHeadroomReduced) return ThermalTier::Reduced;
    if (headroom >= kHeadroomEased) return ThermalTier::Eased;
    return ThermalTier::Full;
}

}  // namespace geode::viz
