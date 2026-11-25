#pragma once
#include <algorithm>
#include <cmath>

#include "viz/clock/SceneClock.hpp"
#include "viz/modulation/Responder.hpp"

namespace geode::viz::sceneclock {

// The Event Responder's Clock control kind (responder::kClock): a response of this kind drives the scene clock.
//
// Its kind-params (Response::params):
//   P0 mode        0 Freeze, 1 Stutter, 2 Reverse
//   P1 loop        Stutter: the length looped back over (see P4)
//   P2 repeats     Stutter: how many times it loops
//   P3 rate        Reverse: the speed backwards (0 reads as 1)
//   P4 loop unit   0 seconds, 1 beats: a beat loop needs a heard tempo and does nothing without one
//   P5 hold frame  Freeze: above 0.5, the renderer re-presents the last frame while the clock stands at 0
// The envelope is the response's own (fade in, hold, fade out; While holds until its event ends). Follow drives a
// Freeze or Reverse by the event's level; a Stutter has nothing to follow.
class ClockKind final : public responder::ResponseKindHandler {
public:
    enum Mode { kFreeze = 0, kStutter = 1, kReverse = 2 };
    enum Param { kMode = 0, kLoop = 1, kRepeats = 2, kRate = 3, kLoopUnit = 4, kHoldFrame = 5 };

    explicit ClockKind(SceneClock& clock) : clock_(clock) {}

    void fire(const Play& play) override {
        if (play.response == nullptr) return;
        const responder::Response& r = *play.response;
        owner_ = play.instance;
        const float hold = r.drive == responder::DriveMode::While ? -1.0f : play.holdSeconds;
        switch (modeOf(r)) {
            case kStutter: {
                float loop = param(r, kLoop);
                if (param(r, kLoopUnit) > 0.5f) loop *= play.beatSeconds;
                clock_.stutter(loop, static_cast<int>(std::lround(std::clamp(param(r, kRepeats), 0.0f, 1000.0f))));
                break;
            }
            case kReverse:
                clock_.reverse(param(r, kRate), play.fadeInSeconds, hold, play.fadeOutSeconds);
                break;
            default:
                clock_.freeze(play.fadeInSeconds, hold, play.fadeOutSeconds, param(r, kHoldFrame) > 0.5f);
                break;
        }
    }

    void follow(int, const responder::Response& r, float level) override {
        switch (modeOf(r)) {
            case kReverse: {
                const float rate = param(r, kRate);
                clock_.follow(level, -(rate > 0.0f ? rate : 1.0f));
                break;
            }
            case kFreeze:
                clock_.follow(level, 0.0f, param(r, kHoldFrame) > 0.5f);
                break;
            default:
                break;
        }
    }

    void release(int instance) override {
        if (instance == owner_) clock_.release();
    }

private:
    static float param(const responder::Response& r, Param p) { return r.params[static_cast<size_t>(p)]; }
    static int modeOf(const responder::Response& r) {
        return std::clamp(static_cast<int>(std::lround(param(r, kMode))), 0, static_cast<int>(kReverse));
    }

    SceneClock& clock_;
    int owner_ = -1;
};

}  // namespace geode::viz::sceneclock
