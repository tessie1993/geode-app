#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>

#include "viz/frame/LiveFps.hpp"
#include "viz/frame/Pacer.hpp"

// How hot the phone is, once per process (ADR 0008, #440). Pure, with no GL and no Android, so the host
// can test it (core/tests/thermal_test.cpp).
//
// Before, every Renderer owned its own governor and its own copy of the platform's readings, and each
// one asked PowerManager for the headroom once a second. The platform answers NaN to a second ask inside
// its one-second window, so the second renderer alive (the wallpaper beside the app, a cast, the dream)
// read "no forecast" and settled on Full while the first was easing off (#387). Now one state holds the
// readings and the settled tier; every renderer's governor reads it, and the pacer applies its fps cap.
//
// Android keeps only the reads. The status is never polled: PowerManager's status listener pushes it when
// it changes. The headroom is asked every ten seconds: a live renderer's one per-frame call
// (`geode_viz_frame_begin`) claims the slot and says GEODE_FRAME_THERMAL_DUE; only then does
// `ThermalGovernor.kt` ask PowerManager and call `geode_thermal_push`. The cadence, the NaN rule, the dwell
// and the ladder are all decided here.
namespace geode::viz {

enum class ThermalTier { Full, Eased, Reduced, Minimal };

// One rung of the ladder. Every value multiplies what the renderer would otherwise do, under the user's
// own Settings › Performance choices.
struct ThermalTierInfo {
    float fpsCap;         // 0 = none; a cap of 60 or under (and a free-running surface) runs at min(cap, this)
    int fpsRungsDown;     // how many live rungs (frame::kLiveFpsRungs) a cap above 60 steps down
    float renderScale;    // the scene targets' size
    float marchScale;     // ray-march steps (uSteps), on top of the user's Detail and march scale
    bool optionalPasses;  // the post FX a cool phone can afford (ripple overlay)
};

// The owner's ladder (ADR 0008 point 2), cool to hot: fps drops first, then render scale, then march
// quality, then post FX, last. A hot phone's first visible change is smoothness, not sharpness: halving the
// frame rate halves every pass's cost at once, where a lower scale only saves fill.
//
// The frame rate steps a rung a tier (frame::thermalStepCapFps): a 60 cap drops to 30 at Eased, as it
// always has, and a 90 or 120 cap (Settings › Performance on a fast panel) walks down the rungs, 120 to 90,
// 60, then 30, so every hotter tier still costs frames before it costs anything else. fpsCap is the floor
// of that walk, the 30 every hot tier holds a 60 cap and a free-running surface to.
//
// There are three hot tiers for four steps, so march and post FX both go at Minimal: march first in the
// ladder, post FX the very last thing a phone gives up. Liquid mode keeps its own "when hot" choice
// (liquid::modeFor), which reads the same tier.
inline const ThermalTierInfo& thermalTierInfo(ThermalTier tier) {
    static constexpr std::array<ThermalTierInfo, 4> kInfos = {{
        {0.0f, 0, 1.0f, 1.0f, true},    // Full
        {30.0f, 1, 1.0f, 1.0f, true},   // Eased: fps only
        {30.0f, 2, 0.75f, 1.0f, true},  // Reduced: + render scale
        {30.0f, 3, 0.6f, 0.6f, false},  // Minimal: + march quality, + post FX
    }};
    return kInfos[static_cast<size_t>(tier)];
}

// The process's one thermal state. Thread-safe: every renderer's GL thread reads it each frame, the
// pacer thread reads the cap each vsync, one GL thread a second settles it and the main thread pushes a
// new status in.
class ThermalState {
public:
    // The state settles once a second for the whole process, whoever draws.
    static constexpr double kSamplePeriodSeconds = 1.0;
    // PowerManager's headroom tracks slow skin sensors, and Android's Thermal API guide says to ask no more
    // than once every ten seconds (it answers NaN otherwise), so the whole process asks that often.
    static constexpr double kHeadroomPeriodSeconds = 10.0;
    // A NaN between real readings is a missed window, not a cool phone: the last real headroom holds this
    // long, two missed asks, before the state stops trusting it.
    static constexpr double kHeadroomStaleSeconds = 2.5 * kHeadroomPeriodSeconds;
    // Hotter waits three readings in a row, so a single spike does not drop the frame rate; cooler waits a
    // minute per step, so a phone at the edge does not flap between two rates.
    static constexpr double kEscalateDwellSeconds = 3.0;
    static constexpr double kRelaxDwellSeconds = 60.0;
    // The gap between readings counts toward a dwell, but a long pause (no renderer drawing) is not a
    // minute of watching the phone cool.
    static constexpr double kMaxStepSeconds = 2.0 * kSamplePeriodSeconds;
    static constexpr float kHeadroomEased = 0.75f;
    static constexpr float kHeadroomReduced = 0.85f;
    static constexpr float kHeadroomMinimal = 0.95f;
    // Measured frame time alone never forces Minimal: without a platform reading it cannot tell heat from
    // a heavy style, and Minimal drops the post FX.
    static constexpr ThermalTier kMeasuredCeiling = ThermalTier::Reduced;

    // The one per process. A function-local static in an inline function is one object for the whole
    // shared library, whichever translation unit asks.
    static ThermalState& process() {
        static ThermalState state;
        return state;
    }

    // The cadence: true for one caller at most once per sample period, across every renderer.
    bool claimSample(double nowSeconds) {
        double last = lastClaimS_.load(std::memory_order_relaxed);
        if (nowSeconds - last < kSamplePeriodSeconds) return false;
        // Two GL threads can both see the slot open; only one wins it.
        return lastClaimS_.compare_exchange_strong(last, nowSeconds, std::memory_order_relaxed);
    }

    // The slot's winner, once a second: true when a new headroom is due from the platform, which the caller
    // asks for and pushes. On the other seconds the state settles on the readings it holds, so the dwells
    // keep running between asks.
    bool headroomDue(double nowSeconds) {
        if (!claimSample(nowSeconds)) return false;
        if (nowSeconds - lastHeadroomAskS_.load(std::memory_order_relaxed) >= kHeadroomPeriodSeconds) {
            lastHeadroomAskS_.store(nowSeconds, std::memory_order_relaxed);
            return true;
        }
        push(-1, std::numeric_limits<float>::quiet_NaN(), nowSeconds);
        return false;
    }

    // One reading. `status` < 0: none this time (the listener pushes it apart from the headroom). A NaN or
    // negative `headroom`: no forecast this time (a status push, a settle, or asked inside the window).
    void push(int status, float headroom, double nowSeconds) {
        std::lock_guard<std::mutex> lock(lock_);
        if (status >= 0) {
            status_ = status;
            platformKnows_.store(true, std::memory_order_relaxed);
        }
        if (std::isfinite(headroom) && headroom >= 0.0f) {
            headroom_ = headroom;
            headroomAtS_ = nowSeconds;
        } else if (nowSeconds - headroomAtS_ > kHeadroomStaleSeconds) {
            headroom_ = std::numeric_limits<float>::quiet_NaN();
        }
        const double step = pushed_ ? std::clamp(nowSeconds - lastPushS_, 0.0, kMaxStepSeconds) : kSamplePeriodSeconds;
        pushed_ = true;
        lastPushS_ = nowSeconds;
        settle(std::max(statusTier(status_), headroomTier(headroom_)), step);
    }

    // True once PowerManager has answered with a real status. Until then (and on old phones for good)
    // the governors fall back on measured frame time.
    bool platformKnows() const { return platformKnows_.load(std::memory_order_relaxed); }

    // The tier every renderer reads: the platform's settled tier, raised by the measured floor.
    ThermalTier tier() const {
        return std::max(settled_.load(std::memory_order_relaxed), measuredFloor_.load(std::memory_order_relaxed));
    }
    ThermalTier platformTier() const { return settled_.load(std::memory_order_relaxed); }
    float fpsCap() const { return thermalTierInfo(tier()).fpsCap; }
    // What the pacer applies and Surface.setFrameRate asks for: the surface's own cap stepped down a rung per
    // hot tier (frame::thermalStepCapFps; for a cap of 60 or under that is min(cap, fpsCap())). Handed the
    // surface's own cap, never a rate this already produced: the step is not idempotent.
    float capFor(float surfaceCapFps) const { return frame::thermalStepCapFps(surfaceCapFps, fpsRungsDown()); }
    // How many live rungs the settled tier steps a cap down (0 cool): what Follow device steps its panel rate by.
    int fpsRungsDown() const { return thermalTierInfo(tier()).fpsRungsDown; }

    // The measured fallback: a governor that saw a sustained frame-time deficit raises the floor for the
    // whole process (the surfaces share one GPU), never past kMeasuredCeiling.
    ThermalTier measuredFloor() const { return measuredFloor_.load(std::memory_order_relaxed); }
    void raiseMeasuredFloor(int steps) {
        std::lock_guard<std::mutex> lock(lock_);
        const int floor = static_cast<int>(measuredFloor_.load(std::memory_order_relaxed));
        const int raised = std::min(floor + std::max(steps, 0), static_cast<int>(kMeasuredCeiling));
        measuredFloor_.store(static_cast<ThermalTier>(raised), std::memory_order_relaxed);
        raises_.fetch_add(1, std::memory_order_relaxed);
    }
    void lowerMeasuredFloor() {
        std::lock_guard<std::mutex> lock(lock_);
        const int floor = static_cast<int>(measuredFloor_.load(std::memory_order_relaxed));
        measuredFloor_.store(static_cast<ThermalTier>(std::max(floor - 1, 0)), std::memory_order_relaxed);
    }
    void clearMeasuredFloor() { measuredFloor_.store(ThermalTier::Full, std::memory_order_relaxed); }
    // How many times any governor raised the floor. A governor steps back down only after a long run of
    // kept-up frames with no raise anywhere, so a cheap surface keeping up cannot undo a busy one's raise.
    uint64_t raises() const { return raises_.load(std::memory_order_relaxed); }

    static ThermalTier statusTier(int status) {
        // PowerManager.THERMAL_STATUS_*: NONE 0, LIGHT 1, MODERATE 2, SEVERE 3 and up.
        if (status <= 0) return ThermalTier::Full;
        if (status == 1) return ThermalTier::Eased;
        if (status == 2) return ThermalTier::Reduced;
        return ThermalTier::Minimal;
    }

    // 1.0 is where the platform throttles at SEVERE; the tiers start easing well before it.
    static ThermalTier headroomTier(float headroom) {
        if (std::isnan(headroom)) return ThermalTier::Full;
        if (headroom >= kHeadroomMinimal) return ThermalTier::Minimal;
        if (headroom >= kHeadroomReduced) return ThermalTier::Reduced;
        if (headroom >= kHeadroomEased) return ThermalTier::Eased;
        return ThermalTier::Full;
    }

private:
    // Caller holds lock_. Escalates straight to the observed tier once the dwell is met (a phone that is
    // hot now should not climb one rung a reading); relaxes one rung per dwell.
    void settle(ThermalTier observed, double step) {
        const ThermalTier current = settled_.load(std::memory_order_relaxed);
        if (observed > current) {
            coolDwellS_ = 0.0;
            hotDwellS_ += step;
            if (hotDwellS_ >= kEscalateDwellSeconds) {
                settled_.store(observed, std::memory_order_relaxed);
                hotDwellS_ = 0.0;
            }
        } else if (observed < current) {
            hotDwellS_ = 0.0;
            coolDwellS_ += step;
            if (coolDwellS_ >= kRelaxDwellSeconds) {
                settled_.store(static_cast<ThermalTier>(static_cast<int>(current) - 1), std::memory_order_relaxed);
                coolDwellS_ = 0.0;
            }
        } else {
            hotDwellS_ = 0.0;
            coolDwellS_ = 0.0;
        }
    }

    std::mutex lock_;
    std::atomic<double> lastClaimS_{-std::numeric_limits<double>::infinity()};
    std::atomic<double> lastHeadroomAskS_{-std::numeric_limits<double>::infinity()};
    std::atomic<ThermalTier> settled_{ThermalTier::Full};
    std::atomic<ThermalTier> measuredFloor_{ThermalTier::Full};
    std::atomic<bool> platformKnows_{false};
    std::atomic<uint64_t> raises_{0};
    // Under lock_.
    int status_ = 0;
    float headroom_ = std::numeric_limits<float>::quiet_NaN();
    double headroomAtS_ = -std::numeric_limits<double>::infinity();
    bool pushed_ = false;
    double lastPushS_ = 0.0;
    double hotDwellS_ = 0.0;
    double coolDwellS_ = 0.0;
};

}  // namespace geode::viz
