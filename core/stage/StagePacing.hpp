#pragma once
#include <cstdint>

#include "viz/frame/LiveFps.hpp"
#include "viz/frame/Pacer.hpp"

// One stage's pacer on the vsync thread (docs/agents/native-stage.md): which vsyncs draw, the paced rate the frame
// is handed, and when the window's frame-rate hint must be restated. The clock itself is core/viz/frame/Pacer,
// unchanged; this owns its nine-int64 state natively and applies the process thermal step the way
// geode_pacer_on_vsync does. Pure, with no Android, so the host can test it (core/tests/stage_pacing_test.cpp).
//
// Confined to the vsync thread: start, stop and setCap arrive there as commands, and the choreographer callback
// calls vsyncArrived, then (when it re-posted) step.
namespace geode::stage {

class StagePacing {
public:
    explicit StagePacing(float capFps = static_cast<float>(viz::frame::kLiveFpsDefault)) : cap_(capFps) {}

    // The surface's own cap (Settings › Performance's, the wallpaper's or the dream's; 0 every vsync,
    // frame::kLiveFpsFollow for Follow device). Read on the next vsync; until then fps() answers the cap.
    void setCap(float capFps) {
        cap_ = capFps;
        pacedMilliHz_ = -1;
    }
    float capFps() const { return cap_; }

    // The rate this surface draws at: what the last vsync paced it to, or its cap until a vsync says.
    float fps() const { return pacedMilliHz_ < 0 ? cap_ : static_cast<float>(pacedMilliHz_) / 1000.0f; }

    bool running() const { return running_; }
    // A callback is posted and not yet delivered.
    bool posted() const { return posted_; }

    // True when the caller must post a callback now. A resume does not inherit the gap it was paused across. With
    // a callback still pending (a stop and a start between two vsyncs) nothing is posted: there is no call that
    // removes a posted callback, and a second one would fire twice per vsync and halve every dt.
    bool start() {
        if (running_) return false;
        running_ = true;
        viz::frame::Pacer::resume(state_);
        if (posted_) return false;
        posted_ = true;
        return true;
    }

    // The pending callback lapses on its next vsync: it sees the pacer stopped and neither re-posts nor draws.
    void stop() { running_ = false; }

    // The posted callback arrived. True: the caller re-posts at once, before stepping, so the cadence survives a
    // slow frame and a skipped vsync still wakes the next one. False: stopped, nothing is pending any more.
    bool vsyncArrived() {
        if (!running_) {
            posted_ = false;
            return false;
        }
        return true;
    }

    struct Tick {
        bool draw = false;
        double dt = 0.0;
        // The cap the pacer paces to moved (the first vsync, a new cap, the thermal step): restate the window's hint.
        bool restateRate = false;
        float rateFps = 0.0f;
    };

    // One vsync, after vsyncArrived said true. rungsDown is the process thermal tier's step
    // (ThermalState::fpsRungsDown).
    Tick step(int64_t frameTimeNs, int rungsDown);

    // The rate the window is asked for: the cap under the thermal step, 0 (no preference) for Follow device or
    // no cap. geode_pacer_effective_cap's rule.
    static float frameRateFor(float capFps, int rungsDown);

    const viz::frame::PacerState& state() const { return state_; }

private:
    viz::frame::PacerState state_{};
    float cap_;
    bool running_ = false;
    bool posted_ = false;
    int64_t askedCapMilliHz_ = -1;
    int64_t pacedMilliHz_ = -1;
};

}  // namespace geode::stage
