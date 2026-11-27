#pragma once
#include <cmath>
#include <cstdint>

// The live stage's frame clock (Android's frame pacing guidance, developer.android.com/games/sdk/frame-pacing): one
// target rate the user picked from the display's own rates, and the display's vsync period, give a swap interval (the
// whole vsyncs one frame spans) and the frame budget (interval x period). Every frame is drawn on a vsync that interval
// apart, so the frames reach the screen evenly, and the budget is what the stage's work is sized and reported against
// (ADPF). Pure, with no EGL and no Android, so the host can test it (core/tests/frame_pacer_test.cpp).
//
// The period is read from each vsync's own frame timelines, so a display that changes mode changes the interval on
// the next vsync: no measured rate, no settling, no guess.
namespace geode::stage {

class FramePacer {
public:
    // How far a refresh rate may sit over a whole multiple of the target and still be that multiple (59.94 Hz panels).
    static constexpr float kToleranceHz = 1.0f;
    // The longest interval: a target this far under the refresh rate is not a frame rate.
    static constexpr int kMaxInterval = 8;

    // The whole vsyncs one frame spans at `refreshHz` for `targetFps`: the fewest that keep the drawn rate at or
    // under the target. 1 when either is not a positive rate (Follow device states no target: every vsync).
    static int intervalFor(float refreshHz, float targetFps) {
        if (!(std::isfinite(refreshHz) && refreshHz > 0.0f && std::isfinite(targetFps) && targetFps > 0.0f)) return 1;
        const float over = (refreshHz - kToleranceHz) / targetFps;
        if (over <= 1.0f) return 1;
        const int n = static_cast<int>(std::ceil(over));
        return n < kMaxInterval ? n : kMaxInterval;
    }

    // The user's rate; 0 or less (Follow device, no cap) draws every vsync.
    void setTarget(float targetFps) { target_ = std::isfinite(targetFps) && targetFps > 0.0f ? targetFps : 0.0f; }
    float target() const { return target_; }

    // A vsync at `vsyncNs` (CLOCK_MONOTONIC, 0 when not known) on a display `periodNs` apart (0 when not known: the last
    // period stands). True when a frame is due on it. Asks only: drawn() says the frame really was drawn.
    bool due(int64_t vsyncNs, int64_t periodNs) {
        if (periodNs > 0) periodNs_ = periodNs;
        interval_ = periodNs_ > 0 ? intervalFor(static_cast<float>(1.0e9 / static_cast<double>(periodNs_)), target_) : 1;
        if (interval_ <= 1) return true;
        if (vsyncNs > 0 && lastNs_ > 0 && vsyncNs > lastNs_) {
            // By time: a vsync the stage was too busy to be asked for still counts towards the interval.
            return vsyncNs - lastNs_ >= interval_ * periodNs_ - periodNs_ / 2;
        }
        // No times: by count.
        return seen_ + 1 >= interval_;
    }

    // The vsync asked about last was drawn on (or, `false`, passed by).
    void drawn(int64_t vsyncNs) {
        lastNs_ = vsyncNs;
        seen_ = 0;
    }
    void passed() { ++seen_; }

    int interval() const { return interval_; }
    int64_t periodNs() const { return periodNs_; }
    // The time one frame has, interval x period; 0 until a vsync says the period.
    int64_t budgetNs() const { return periodNs_ > 0 ? interval_ * periodNs_ : 0; }
    // The rate frames are drawn at, 1 / budget; 0 until known.
    float frameFps() const {
        const int64_t budget = budgetNs();
        return budget > 0 ? static_cast<float>(1.0e9 / static_cast<double>(budget)) : 0.0f;
    }

private:
    float target_ = 0.0f;
    int interval_ = 1;
    int64_t periodNs_ = 0;
    int64_t lastNs_ = 0;  // the vsync the last frame was drawn on, 0 for none
    int seen_ = kMaxInterval;  // vsyncs passed by since then; the first vsync is always due
};

}  // namespace geode::stage
