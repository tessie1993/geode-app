#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>

#include "viz/fluid/FluidQuality.hpp"
#include "viz/frame/LiveFps.hpp"
#include "viz/frame/Quality.hpp"

// Settings › Performance's "Adapt to frame time" (owner, 2026-11-18): a live surface whose frames measurably miss
// their budget gives up quality one step at a time, and the frame rate only last, so the rate the user picked holds
// as long as anything else can give. Frame time is the only input: no temperature, no thermal status, no headroom.
// Pure, with no GL and no Android, so the host can test it (core/tests/frame_governor_test.cpp).
//
// The ladder, from the user's own settings down (never above any of them):
//   1. render scale down through the tab's scales (kScaleChoices) under the user's;
//   2. march scale the same way;
//   3. post FX off, when the user has it on;
//   4. only then the frame rate, down the live rungs (kLiveFpsRungs) under the user's fps maximum.
// It climbs back in the reverse order: the frame rate first, then post FX, march scale, render scale.
//
// The measurement and the hysteresis are the fluid scenes' own, which held on phones before (#929, #1042, #1054):
// fluid::quality::FrameMonitor judges the frames against the rate the pacer really paces to (a 60 cap on a 90 Hz
// panel draws 45, which is on time, not slow) and asks for a step after 2.5 s of sustained deficit (two steps when
// frames run more than 35 % under); fluid::quality::AutoDowngrade gives a step back after 45 s of kept-up frames, and
// a step back up that fails within 15 s doubles the next wait, up to 12 minutes, so a style too heavy for the phone
// settles instead of flapping. Frames that build or blend a style (Renderer::settling) are not counted, and a new
// style starts again from the user's own settings with the backoff cleared: one style's cost says nothing about the
// next one's.
//
// Live surfaces only: an export, a recording (Record copies the stage's frames into a fixed-rate video) and a
// renderer the shell never switched it on for pass the user's settings through unchanged.
namespace geode::viz::frame {

// What the user set in Settings › Performance, and the fastest the surface may draw.
struct GovernorUser {
    float renderScale = 1.0f;
    float marchScale = 1.0f;
    bool postFx = true;
    // The user's fps maximum as the surface runs it: its cap, or the panel's rate under Follow device; 0 unknown.
    float ceilingFps = 0.0f;
};

// The user's settings under the governor's steps.
struct Governed {
    float renderScale = 1.0f;
    float marchScale = 1.0f;
    bool postFx = true;
    // The cap the stage paces to instead of the user's; 0 none (the user's own cap stands).
    float capFps = 0.0f;
};

namespace governor {

inline constexpr float kScaleEpsilon = 1e-3f;
// A rung within this of the ceiling is the ceiling itself (59.94 Hz panels).
inline constexpr float kFpsEpsilon = 1.0f;

// How many of the tab's scales lie under `user`: the steps a scale can go down.
inline int scaleStepsUnder(float user) {
    int n = 0;
    for (const float c : kScaleChoices) {
        if (c < user - kScaleEpsilon) ++n;
    }
    return n;
}

// `user` taken `steps` of the tab's scales down; kScaleChoices is ascending.
inline float scaleStepped(float user, int steps) {
    const int n = scaleStepsUnder(user);
    if (steps <= 0 || n == 0) return user;
    return kScaleChoices[static_cast<size_t>(n - std::min(steps, n))];
}

// How many live rungs lie under the fps maximum: the steps the frame rate can go down.
inline int fpsStepsUnder(float ceilingFps) {
    if (!std::isfinite(ceilingFps) || ceilingFps <= 0.0f) return 0;
    int n = 0;
    for (const int r : kLiveFpsRungs) {
        if (static_cast<float>(r) < ceilingFps - kFpsEpsilon) ++n;
    }
    return n;
}

// The cap `steps` rungs under the fps maximum; 0 for none.
inline float fpsStepped(float ceilingFps, int steps) {
    const int n = fpsStepsUnder(ceilingFps);
    if (steps <= 0 || n == 0) return 0.0f;
    return static_cast<float>(kLiveFpsRungs[static_cast<size_t>(n - std::min(steps, n))]);
}

// Every step the ladder has for these settings.
inline int ladderLength(const GovernorUser& u) {
    return scaleStepsUnder(u.renderScale) + scaleStepsUnder(u.marchScale) + (u.postFx ? 1 : 0) +
        fpsStepsUnder(u.ceilingFps);
}

// The user's settings `steps` down the ladder: render scale, march scale, post FX, then the frame rate.
inline Governed governed(const GovernorUser& u, int steps) {
    Governed g{u.renderScale, u.marchScale, u.postFx, 0.0f};
    int left = std::max(steps, 0);
    const int render = std::min(left, scaleStepsUnder(u.renderScale));
    g.renderScale = scaleStepped(u.renderScale, render);
    left -= render;
    const int march = std::min(left, scaleStepsUnder(u.marchScale));
    g.marchScale = scaleStepped(u.marchScale, march);
    left -= march;
    if (left > 0 && u.postFx) {
        g.postFx = false;
        --left;
    }
    g.capFps = fpsStepped(u.ceilingFps, left);
    return g;
}

}  // namespace governor

// The step the surface that drew last is on, for Settings › Performance's "Now: …" line (geode_perf_adapt_now).
// Process-wide: the surface drawing now is the one on screen. steps < 0: no live frame yet.
struct AdaptNow {
    std::atomic<float> fps{0.0f};
    std::atomic<float> renderScale{1.0f};
    std::atomic<float> marchScale{1.0f};
    std::atomic<bool> postFx{true};
    std::atomic<int> steps{-1};
};

inline AdaptNow& adaptNow() {
    static AdaptNow now;
    return now;
}

// One live surface's governor. setEnabled and capFps are any thread; the rest is the GL thread's.
class FrameGovernor {
public:
    // Settings › Performance's switch. Off (the default until the shell says), every step goes on the next frame.
    void setEnabled(bool on) { enabled_.store(on, std::memory_order_relaxed); }
    bool enabled() const { return enabled_.load(std::memory_order_relaxed); }

    // After a step, its own change (a render scale reallocates the targets) hitches the next frames: they are not
    // counted, like a settling style's.
    static constexpr float kStepHoldSeconds = 1.0f;

    // A new style: back to the user's own settings, the backoff and the measurement with it.
    void reset() {
        downgrade_.reset();
        monitor_.reset();
        holdSeconds_ = 0.0f;
    }

    // One live frame. dt: its measured step. pacedFps: the rate the pacer really paces to (0 free-running).
    // ownCapFps: the user's own cap (kLiveFpsFollow for Follow device, <= 0 none). settling: a style is building or
    // blending. held: an export or a recording takes these frames. True when the step changed.
    bool onFrame(float dt, float pacedFps, float ownCapFps, GovernorUser user, bool settling, bool held) {
        user.ceilingFps = ceilingFor(ownCapFps, pacedFps);
        const int before = downgrade_.steps;
        const int length = governor::ladderLength(user);
        if (!enabled() || held) {
            reset();
        } else {
            // Settings changed under the steps: never more steps than the ladder has now.
            if (downgrade_.steps > length) downgrade_.steps = length;
            if (settling || holdSeconds_ > 0.0f) {
                // A deficit begun before the switch does not carry over it; a kept-up run only pauses.
                monitor_.reset();
                if (!settling) holdSeconds_ -= dt;
            } else {
                monitor_.setPacedFps(pacedFps);
                const int relief = monitor_.onFrame(dt);
                if (downgrade_.onFrame(relief, monitor_.keepingUp(), dt, length)) {
                    monitor_.reset();
                    holdSeconds_ = kStepHoldSeconds;
                }
            }
        }
        governed_ = governor::governed(user, downgrade_.steps);
        capFps_.store(governed_.capFps, std::memory_order_relaxed);
        if (!held) publish(pacedFps);
        return downgrade_.steps != before;
    }

    int steps() const { return downgrade_.steps; }
    // The user's settings under the steps, as of the last frame.
    const Governed& governed() const { return governed_; }
    // The cap the stage paces to instead of the user's, 0 for none. Any thread.
    float capFps() const { return capFps_.load(std::memory_order_relaxed); }

private:
    // The user's fps maximum: the cap, or under Follow device (or no cap) the panel rate the pacer ran at while the
    // governor capped nothing.
    float ceilingFor(float ownCapFps, float pacedFps) {
        if (std::isfinite(ownCapFps) && ownCapFps > 0.0f) return ownCapFps;
        if (governed_.capFps <= 0.0f && std::isfinite(pacedFps) && pacedFps > 0.0f) followCeiling_ = pacedFps;
        return followCeiling_;
    }

    void publish(float pacedFps) const {
        AdaptNow& now = adaptNow();
        now.fps.store(pacedFps, std::memory_order_relaxed);
        now.renderScale.store(governed_.renderScale, std::memory_order_relaxed);
        now.marchScale.store(governed_.marchScale, std::memory_order_relaxed);
        now.postFx.store(governed_.postFx, std::memory_order_relaxed);
        now.steps.store(downgrade_.steps, std::memory_order_relaxed);
    }

    std::atomic<bool> enabled_{false};
    std::atomic<float> capFps_{0.0f};
    fluid::quality::FrameMonitor monitor_;
    fluid::quality::AutoDowngrade downgrade_;
    Governed governed_;
    float followCeiling_ = 0.0f;
    float holdSeconds_ = 0.0f;
};

}  // namespace geode::viz::frame
