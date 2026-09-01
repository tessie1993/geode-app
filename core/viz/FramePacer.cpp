#include "viz/FramePacer.hpp"

#include <algorithm>
#include <cmath>

namespace geode::viz {

void FramePacer::setPolicy(FrameRatePolicy policy) {
    std::lock_guard<std::mutex> lock(policyLock_);
    policy_ = policy;
}

FrameRatePolicy FramePacer::policy() const {
    std::lock_guard<std::mutex> lock(policyLock_);
    return policy_;
}

float FramePacer::dtSeconds() const {
    std::lock_guard<std::mutex> lock(policyLock_);
    return dt_;
}

float FramePacer::requestedFps() const {
    const FrameRatePolicy p = policy();
    return p.native ? 0.0f : p.cappedFps;
}

void FramePacer::start() {
    if (running_) return;
    running_ = true;
    lastDrawnNanos_ = 0;
    lastVsyncNanos_ = 0;
    vsyncsSinceDrawn_ = 0;
    resetStats();
}

void FramePacer::stop() { running_ = false; }

bool FramePacer::onVsync(int64_t frameTimeNanos) {
    if (!running_) return false;
    trackVsyncPeriod(frameTimeNanos);
    recomputeDivisor();
    vsyncsSinceDrawn_++;
    if (vsyncsSinceDrawn_ < divisor_) return false;
    vsyncsSinceDrawn_ = 0;
    const int64_t previous = lastDrawnNanos_;
    lastDrawnNanos_ = frameTimeNanos;
    float dt = kDefaultDtSeconds;
    if (previous != 0) {
        const int64_t elapsed = frameTimeNanos - previous;
        record(elapsed);
        dt = std::clamp(static_cast<float>(elapsed) / kNanosPerSecond, kMinDtSeconds, kMaxDtSeconds);
    }
    std::lock_guard<std::mutex> lock(policyLock_);
    dt_ = dt;
    return true;
}

FrameStats FramePacer::stats() {
    std::lock_guard<std::mutex> lock(statsLock_);
    const int n = sampleCount_;
    if (n == 0) return FrameStats{};
    std::copy(frameNanos_.begin(), frameNanos_.begin() + n, sortScratch_.begin());
    std::sort(sortScratch_.begin(), sortScratch_.begin() + n);
    return FrameStats{n, static_cast<float>(sumNanos_ / n) / kNanosPerMs, percentileMs(n, 0.95f), percentileMs(n, 0.99f)};
}

void FramePacer::trackVsyncPeriod(int64_t frameTimeNanos) {
    const int64_t previous = lastVsyncNanos_;
    lastVsyncNanos_ = frameTimeNanos;
    if (previous == 0) return;
    const int64_t delta = frameTimeNanos - previous;
    if (delta < kMinVsyncNanos || delta > kMaxVsyncNanos) return;
    vsyncPeriodNanos_ = vsyncSamples_ == 0 ? delta : vsyncPeriodNanos_ + (delta - vsyncPeriodNanos_) / kVsyncSmoothing;
    if (vsyncSamples_ < kVsyncSettleFrames) vsyncSamples_++;
}

void FramePacer::recomputeDivisor() {
    const FrameRatePolicy current = policy();
    divisor_ = current.native ? 1 : cappedDivisor(current.cappedFps);
}

int FramePacer::cappedDivisor(float fps) const {
    if (fps <= 0.0f || vsyncPeriodNanos_ <= 0 || vsyncSamples_ < kVsyncSettleFrames) return 1;
    const float panelHz = kNanosPerSecond / static_cast<float>(vsyncPeriodNanos_);
    return std::clamp(static_cast<int>(panelHz / fps + kDivisorEpsilon), 1, kMaxDivisor);
}

void FramePacer::record(int64_t elapsedNanos) {
    std::lock_guard<std::mutex> lock(statsLock_);
    if (sampleCount_ == kWindowFrames) sumNanos_ -= frameNanos_[writeIndex_];
    frameNanos_[writeIndex_] = elapsedNanos;
    sumNanos_ += elapsedNanos;
    writeIndex_ = (writeIndex_ + 1) % kWindowFrames;
    if (sampleCount_ < kWindowFrames) sampleCount_++;
}

void FramePacer::resetStats() {
    std::lock_guard<std::mutex> lock(statsLock_);
    writeIndex_ = 0;
    sampleCount_ = 0;
    sumNanos_ = 0;
}

float FramePacer::percentileMs(int n, float fraction) const {
    const int rank = std::clamp(static_cast<int>(std::ceil(fraction * n)), 1, n);
    return static_cast<float>(sortScratch_[rank - 1]) / kNanosPerMs;
}

}  // namespace geode::viz
