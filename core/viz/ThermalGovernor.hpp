#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "viz/frame/Thermal.hpp"

namespace geode::viz {

// The performance monitor: downgrades on a sustained deficit, never upgrades.
class PerformanceMonitor {
public:
    static constexpr float kFreeRunning = 0.0f;
    // What onFrame answers for a sustained deficit: a mild one (one rung of relief), or frames more
    // than 35 % under the target (two rungs).
    static constexpr int kMildRelief = 1;
    static constexpr int kSevereRelief = 2;

    explicit PerformanceMonitor(float targetFps = 50.0f, float sustainSeconds = 2.5f, int windowSize = 30);
    void setPacedFps(float fps) { pacedFps_.store(fps); }
    float averageFps() const;
    // A full window at or above the effective target: the headroom an adaptive step back up waits for.
    bool keepingUp() const;
    int onFrame(float dtSeconds);
    void reset();

private:
    static constexpr float kKeepingUpFraction = 0.85f;
    // The paced rate the default target (50) was tuned against.
    static constexpr float kTargetTunedAtFps = 60.0f;
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

// One renderer's view of the process's thermal state (frame/Thermal.hpp, #440). The platform readings
// and the settled tier are shared, so every surface reads the same tier; what stays per renderer is what
// only it can see: its own frame times (the measured fallback), whether it is an export, and its own cap.
class ThermalGovernor {
public:
    explicit ThermalGovernor(ThermalState& shared = ThermalState::process()) : shared_(shared) {}

    // An export always renders at Full: it runs as fast as the encoder takes frames, and its video must
    // not depend on how warm the phone was.
    ThermalTier tier() const { return offscreenDepth_.load() > 0 ? ThermalTier::Full : shared_.tier(); }
    const ThermalTierInfo& tierInfo() const { return thermalTierInfo(tier()); }
    // The surface's own cap (Settings › Performance, the wallpaper's, the dream's, Cast's fixed rate), 0 when
    // free-running, as the rate it is paced at too.
    void setPacedFps(float fps) { setPacedFps(fps, fps); }
    // The rate the surface's pacer last drew at (the thermal step already in it, or the next whole divide of
    // the panel under the cap) and the surface's own cap apart. The thermal step walks a 120 cap down a rung a
    // tier, which is not idempotent, so it is taken from the own cap and only min'd with the paced rate: a
    // 120 cap paced at the Eased 90 stays 90, where stepping the 90 again would read 60.
    void setPacedFps(float pacedFps, float ownCapFps) {
        pacedFps_.store(pacedFps);
        ownCapFps_.store(ownCapFps);
    }
    // The rate this surface really draws at: its cap under the thermal one, as the pacer applies it.
    // The fluid scenes and the measured fallback read this, so a 30 fps thermal cap is never mistaken
    // for a phone that cannot keep up with 60.
    float pacedFps() const {
        return frame::effectiveCapFps(pacedFps_.load(), frame::thermalStepCapFps(ownCapFps_.load(), tierInfo().fpsRungsDown));
    }
    // Settings › Performance "Adapt to frame time": any sustained frame-time deficit steers the tier
    // even when the platform reports thermals, and a sustained run of kept-up frames steps it back up.
    // Off (the High preset) full quality is the user's choice, so a mild deficit is left alone; only
    // frames far under the cap step down, since nobody chooses 20 fps. Once stepped down, the surface
    // adapts as if the switch were on until it is back at Full.
    void setAdaptive(bool on);

    // This frame builds or blends a style (a staged build, a transition, a new MilkDrop preset): its frame
    // time says nothing about what the phone can draw, so the measured fallback sits out until a moment
    // after the last such frame. Same thread as onFrame.
    void noteSettling() { settleHoldSeconds_ = kSettleHoldSeconds; }
    bool settling() const { return settleHoldSeconds_ > 0.0f; }

    void onFrame(float dtSeconds);
    void onSurfaceRecreated();
    void beginOffscreenRender() { offscreenDepth_.fetch_add(1); }
    void endOffscreenRender();

private:
    static constexpr float kMeasuredRelaxSeconds = 45.0f;
    // A step back up is a probe: a raise within this long of it means the phone could not hold the
    // better tier, and the next step up waits twice as long (up to the maximum), so a style too heavy
    // for the phone settles on the tier it can draw instead of flapping every 45 s. A probe that
    // holds puts the wait back.
    static constexpr float kRelaxProbeSeconds = 15.0f;
    static constexpr float kMeasuredRelaxMaxSeconds = 720.0f;
    // The new style's first frames still upload textures and load its preset.
    static constexpr float kSettleHoldSeconds = 1.5f;

    void trackMeasuredTrend(float dtSeconds);

    ThermalState& shared_;
    std::atomic<int> offscreenDepth_{0};
    std::atomic<float> pacedFps_{0.0f};
    std::atomic<float> ownCapFps_{0.0f};
    std::atomic<bool> adaptive_{false};
    float settleHoldSeconds_ = 0.0f;
    float keptUpSeconds_ = 0.0f;
    float relaxSeconds_ = kMeasuredRelaxSeconds;
    float sinceRelaxSeconds_ = kRelaxProbeSeconds;  // at kRelaxProbeSeconds: no probe in flight
    uint64_t seenRaises_ = 0;
    PerformanceMonitor monitor_;
};

}  // namespace geode::viz
