#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "viz/frame/LiveFps.hpp"

namespace geode::viz {

// The frame-time monitor the fluid scenes' "lower the quality when the phone struggles" reads
// (fluid::quality::AutoDowngrade): it reports a sustained deficit against the surface's paced rate.
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

// One renderer's pace: the rate its surface really draws at, and whether a style is being built or blended in.
// The fluid scenes' frame-time monitor and MilkDrop's fps read it (SceneHost::pacedFps, SceneHost::settling).
// There is no heat throttle: nothing here lowers the frame rate or the quality.
class SurfacePace {
public:
    // The surface's own cap (Settings › Performance, the wallpaper's, the dream's, Cast's fixed rate), 0 when
    // free-running, as the rate it is paced at too.
    void setPacedFps(float fps) { setPacedFps(fps, fps); }
    // The rate the surface's pacer last drew at (the next whole divide of the panel under the cap) and the
    // surface's own cap apart.
    void setPacedFps(float pacedFps, float ownCapFps) {
        pacedFps_.store(pacedFps);
        ownCapFps_.store(ownCapFps);
    }
    // The rate this surface really draws at: the lower of the paced rate and its own cap, whichever is set.
    // Follow device has no cap of its own: the paced rate is the rate, and until a vsync says one the surface
    // reads as free-running (0).
    float pacedFps() const {
        const float paced = pacedFps_.load();
        const float own = ownCapFps_.load();
        if (frame::isLiveFpsFollow(own)) return paced > 0.0f ? paced : 0.0f;
        const bool hasPaced = std::isfinite(paced) && paced > 0.0f;
        const bool hasOwn = std::isfinite(own) && own > 0.0f;
        if (hasPaced && hasOwn) return std::min(paced, own);
        if (hasPaced) return paced;
        return hasOwn ? own : 0.0f;
    }

    // This frame builds or blends a style (a staged build, a transition, a new MilkDrop preset): its frame
    // time says nothing about what the phone can draw, so the frame-time monitors sit out until a moment
    // after the last such frame. Same thread as onFrame.
    void noteSettling() { settleHoldSeconds_ = kSettleHoldSeconds; }
    bool settling() const { return settleHoldSeconds_ > 0.0f; }
    // One live frame: the settling hold runs down.
    void onFrame(float dtSeconds) {
        if (settleHoldSeconds_ > 0.0f) settleHoldSeconds_ -= dtSeconds;
    }
    // A new surface (back from the background, a wallpaper shown again) rebuilds every program and texture on
    // its first frames, so it starts with the settling hold.
    void onSurfaceRecreated() { noteSettling(); }

private:
    // The new style's first frames still upload textures and load its preset.
    static constexpr float kSettleHoldSeconds = 1.5f;

    std::atomic<float> pacedFps_{0.0f};
    std::atomic<float> ownCapFps_{0.0f};
    float settleHoldSeconds_ = 0.0f;
};

}  // namespace geode::viz
