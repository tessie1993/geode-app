#pragma once
#include <array>
#include <cstdint>
#include <mutex>

namespace geode::viz {

struct FrameRatePolicy {
    bool native = false;
    float cappedFps = 60.0f;
    static FrameRatePolicy nativeRate() { return {true, 0.0f}; }
    static FrameRatePolicy capped(float fps) { return {false, fps}; }
};

struct FrameStats {
    int frames = 0;
    float meanMs = 0.0f;
    float p95Ms = 0.0f;
    float p99Ms = 0.0f;
};

// Port of FramePacer.kt minus the Choreographer: the host feeds every vsync in and draws when told to.
class FramePacer {
public:
    static constexpr float kDefaultTargetFps = 60.0f;
    static constexpr float kMaxDtSeconds = 1.0f / 15.0f;
    static constexpr float kMinDtSeconds = 0.001f;

    explicit FramePacer(FrameRatePolicy policy = FrameRatePolicy::capped(kDefaultTargetFps)) : policy_(policy) {}

    void setPolicy(FrameRatePolicy policy);
    FrameRatePolicy policy() const;
    float dtSeconds() const;
    // What the display should be asked for: 0 means no preference.
    float requestedFps() const;

    void start();
    void stop();
    bool running() const { return running_; }
    // One call per vsync; true when this vsync is a drawn frame, with dtSeconds() updated for it.
    bool onVsync(int64_t frameTimeNanos);
    FrameStats stats();

private:
    static constexpr float kDefaultDtSeconds = 1.0f / 60.0f;
    static constexpr int kWindowFrames = 240;
    static constexpr float kNanosPerSecond = 1000000000.0f;
    static constexpr float kNanosPerMs = 1000000.0f;
    static constexpr int64_t kMinVsyncNanos = 4000000;
    static constexpr int64_t kMaxVsyncNanos = 22000000;
    static constexpr int64_t kVsyncSmoothing = 8;
    static constexpr int kVsyncSettleFrames = 8;
    static constexpr float kDivisorEpsilon = 0.05f;
    static constexpr int kMaxDivisor = 8;

    void trackVsyncPeriod(int64_t frameTimeNanos);
    void recomputeDivisor();
    int cappedDivisor(float fps) const;
    void record(int64_t elapsedNanos);
    void resetStats();
    float percentileMs(int n, float fraction) const;

    mutable std::mutex policyLock_;
    FrameRatePolicy policy_;
    float dt_ = kDefaultDtSeconds;
    bool running_ = false;
    int64_t lastDrawnNanos_ = 0;
    int64_t lastVsyncNanos_ = 0;
    int64_t vsyncPeriodNanos_ = 0;
    int vsyncSamples_ = 0;
    int vsyncsSinceDrawn_ = 0;
    int divisor_ = 1;

    mutable std::mutex statsLock_;
    std::array<int64_t, kWindowFrames> frameNanos_{};
    std::array<int64_t, kWindowFrames> sortScratch_{};
    int writeIndex_ = 0;
    int sampleCount_ = 0;
    int64_t sumNanos_ = 0;
};

}  // namespace geode::viz
