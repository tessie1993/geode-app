// Host checks for the rate parameters' phases (core/viz/params/RatePhases): a rate integrates, so a change of
// speed mid-run never moves the phase, and a ramp's phase is its integral at any frame rate.
#include <cmath>
#include <cstdio>
#include <string>

#include "viz/params/RatePhases.hpp"

using namespace geode::viz;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

bool isRate(ParamSlot s) {
    for (ParamSlot r : RatePhases::slots()) {
        if (r == s) return true;
    }
    return false;
}

void theRateParamsAreMarked() {
    check(isRate(ParamSlot::speed) && isRate(ParamSlot::rotation) && isRate(ParamSlot::endlessZoomSpeed) &&
              isRate(ParamSlot::camFly) && isRate(ParamSlot::fluidStirrerSpeed) && isRate(ParamSlot::fluidPaletteCycleSpeed),
          "Speed, Rotation, Dive speed, Fly speed, Stirrer speed and Palette cycle are rates");
    check(!isRate(ParamSlot::zoom) && !isRate(ParamSlot::warp), "an offset is no rate");
    RatePhases r;
    check(r.phase(ParamSlot::zoom) == 0.0, "a slot that is no rate has no phase");
}

// Speed jumps from 1 to 4 after ten seconds: the phase carries on from where it was, at the new rate. `clock * speed`
// would have jumped by 10 * 3 = 30 in the frame of the change.
void aChangeOfSpeedNeverJumpsThePhase() {
    RatePhases r;
    SceneParams p;
    p.speed = 1.0f;
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 600; ++i) r.step(p, dt);
    const double before = r.phase(ParamSlot::speed);
    check(std::fabs(before - 10.0) < 1e-3, "ten seconds at speed 1 is a phase of 10");
    p.speed = 4.0f;
    r.step(p, dt);
    const double step = r.phase(ParamSlot::speed) - before;
    check(std::fabs(step - 4.0 * dt) < 1e-6, "the frame of the change moves the phase by speed * dt, not by uptime * change");
    p.speed = 0.05f;
    const double mid = r.phase(ParamSlot::speed);
    r.step(p, dt);
    const double slow = r.phase(ParamSlot::speed) - mid;
    check(slow > 0.0 && std::fabs(slow - 0.05 * dt) < 1e-6, "a lower speed slows the phase and never runs it back");
}

// A ramp 0 -> 2 over 4 s (slope 0.5) has the integral 0.25 * 4^2 = 4, at 30, 60, 90 and 144 fps.
void aRampsPhaseIsItsIntegral() {
    for (int fps : {30, 60, 90, 144}) {
        RatePhases r;
        SceneParams p;
        const float dt = 1.0f / static_cast<float>(fps);
        const int frames = 4 * fps;
        for (int i = 0; i < frames; ++i) {
            // The ramp at the frame's midpoint: exact for a line.
            const double t = (static_cast<double>(i) + 0.5) / fps;
            p.rotation = static_cast<float>(0.5 * t);
            r.step(p, dt);
        }
        check(std::fabs(r.phase(ParamSlot::rotation) - 4.0) < 1e-3, "the ramp's phase is its integral at " + std::to_string(fps) + " fps");
    }
}

void aNegativeRateRunsBackAndTheWrapStaysInRange() {
    RatePhases r;
    SceneParams p;
    p.camFly = -1.5f;
    for (int i = 0; i < 120; ++i) r.step(p, 1.0f / 60.0f);
    check(std::fabs(r.phase(ParamSlot::camFly) + 3.0) < 1e-3, "a negative rate runs the phase back");
    const double w = r.wrapped(ParamSlot::camFly, 2.0);
    check(w >= 0.0 && w < 2.0 && std::fabs(w - 1.0) < 1e-3, "wrapped lands in [0, period)");
}

void badInputsAddNothing() {
    RatePhases r;
    SceneParams p;
    p.speed = 1.0f;
    r.step(p, 0.0f);
    r.step(p, NAN);
    check(r.phase(ParamSlot::speed) == 0.0, "a zero or non-finite dt adds nothing");
    r.step(p, -0.5f);
    check(std::fabs(r.phase(ParamSlot::speed) + 0.5) < 1e-9, "a negative dt (Clock control's reverse) runs the phase back");
    r.reset();
    p.speed = NAN;
    r.step(p, 0.1f);
    check(r.phase(ParamSlot::speed) == 0.0, "a non-finite rate adds nothing");
    r.step(SceneParams{}, 0.5f);
    r.reset();
    check(r.phase(ParamSlot::speed) == 0.0, "reset clears the phases");
}

}  // namespace

int main() {
    theRateParamsAreMarked();
    aChangeOfSpeedNeverJumpsThePhase();
    aRampsPhaseIsItsIntegral();
    aNegativeRateRunsBackAndTheWrapStaysInRange();
    badInputsAddNothing();
    if (failures == 0) std::printf("rate_phases_test: ok\n");
    return failures == 0 ? 0 : 1;
}
