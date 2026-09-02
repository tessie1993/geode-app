#pragma once
#include <array>
#include <atomic>
#include <cmath>

namespace geode::viz {

enum class ThermalTier { Full, Eased, Reduced, Minimal };

struct ThermalTierInfo {
    float renderScale;
    bool optionalPasses;
    float fpsCap;  // 0 = none
};
const ThermalTierInfo& thermalTierInfo(ThermalTier tier);

// Port of PerformanceMonitor.kt: downgrades on a sustained deficit, never upgrades.
class PerformanceMonitor {
public:
    static constexpr float kFreeRunning = 0.0f;

    explicit PerformanceMonitor(float targetFps = 50.0f, float sustainSeconds = 2.5f, int windowSize = 30);
    void setPacedFps(float fps) { pacedFps_.store(fps); }
    float averageFps() const;
    int onFrame(float dtSeconds);
    void reset();

private:
    static constexpr float kKeepingUpFraction = 0.85f;
    float effectiveTargetFps() const;

    float targetFps_;
    float sustainSeconds_;
    int windowSize_;
    std::array<float, 64> samples_{};
    int count_ = 0;
    int index_ = 0;
    float deficitSeconds_ = 0.0f;
    float lastTargetFps_ = 0.0f;
    std::atomic<float> pacedFps_{kFreeRunning};
};

// Port of ThermalGovernor.kt; the platform readings (status, headroom) are pushed in by the host.
class ThermalGovernor {
public:
    ThermalTier tier() const { return offscreenDepth_.load() > 0 ? ThermalTier::Full : settled_.load(); }
    void setPacedFps(float fps) { pacedFps_.store(fps); }
    float pacedFps() const { return pacedFps_.load(); }

    // Marks the platform as answering; called once with the first real status.
    void setPlatformStatus(int status) { osStatus_.store(status); platformKnows_.store(true); }
    // NaN means no forecast; sampled by the host at most once a second.
    void setThermalHeadroom(float headroom) { headroom_.store(headroom); }

    void onFrame(float dtSeconds);
    void onSurfaceRecreated();
    void beginOffscreenRender() { offscreenDepth_.fetch_add(1); }
    void endOffscreenRender();

private:
    static constexpr int kStatusNone = 0;
    static constexpr int kStatusLight = 1;
    static constexpr int kStatusModerate = 2;
    static constexpr float kSamplePeriodSeconds = 1.25f;
    static constexpr float kEscalateDwellSeconds = 3.0f;
    static constexpr float kRelaxDwellSeconds = 60.0f;
    static constexpr float kHeadroomEased = 0.75f;
    static constexpr float kHeadroomReduced = 0.85f;
    static constexpr float kHeadroomMinimal = 0.95f;

    void trackMeasuredTrend(float dtSeconds);
    ThermalTier observe() const;
    void settle(ThermalTier observed);
    static ThermalTier statusTier(int status);
    ThermalTier headroomTier() const;

    std::atomic<ThermalTier> settled_{ThermalTier::Full};
    std::atomic<int> osStatus_{kStatusNone};
    std::atomic<bool> platformKnows_{false};
    std::atomic<float> headroom_{NAN};
    std::atomic<int> offscreenDepth_{0};
    std::atomic<float> pacedFps_{0.0f};
    PerformanceMonitor monitor_;
    ThermalTier measuredFloor_ = ThermalTier::Full;
    static constexpr ThermalTier kMeasuredCeiling = ThermalTier::Reduced;
    float sampleClock_ = 0.0f;
    float hotDwellSeconds_ = 0.0f;
    float coolDwellSeconds_ = 0.0f;
};

}  // namespace geode::viz
