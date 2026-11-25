// Host checks for the rate parameters' phases (core/viz/params/RatePhases): a rate integrates, so a change of
// speed mid-run never moves the phase, and a ramp's phase is its integral at any frame rate.
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

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

// Every kRate slot is one the renderer reads, and every slot it reads is flagged: a flag cannot drift from its reader.
void everyRateSlotHasAReader() {
    for (ParamSlot s : RatePhases::slots()) {
        bool read = false;
        for (ParamSlot r : kReadRates) read = read || r == s;
        check(read, std::string("a kRate slot nobody reads: ") + SceneParams::fieldNames()[static_cast<size_t>(s)]);
    }
    for (ParamSlot r : kReadRates) check(isRate(r), "a slot the renderer reads is flagged kRate");
}

void theRateParamsAreMarked() {
    check(isRate(ParamSlot::speed) && isRate(ParamSlot::rotation), "Speed and Rotation are rates");
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
    p.rotation = -1.5f;
    for (int i = 0; i < 120; ++i) r.step(p, 1.0f / 60.0f);
    check(std::fabs(r.phase(ParamSlot::rotation) + 3.0) < 1e-3, "a negative rate runs the phase back");
    const double w = r.wrapped(ParamSlot::rotation, 2.0);
    check(w >= 0.0 && w < 2.0 && std::fabs(w - 1.0) < 1e-3, "wrapped lands in [0, period)");
    // A remainder just below zero must not round up to the period.
    RatePhases tiny;
    p.rotation = -1e-20f;
    tiny.step(p, 1.0f);
    const double t = tiny.wrapped(ParamSlot::rotation, 2.0);
    check(t >= 0.0 && t < 2.0, "a tiny negative phase wraps inside [0, period)");
}

// A day and more of Speed at a fast rate: the wrap is exact where a float clock would have stalled long before.
void aHugePhaseWrapsExactly() {
    RatePhases r;
    SceneParams p;
    p.rotation = 1e7f;  // exact in a float
    r.step(p, 1.0f);
    const double w = r.wrapped(ParamSlot::rotation, 7100.0);
    check(std::fabs(w - 3200.0) < 1e-6, "1e7 wrapped at 7100 is 3200");
    check(w >= 0.0 && w < 7100.0, "a huge phase stays in [0, period)");
}

// Two runs from reset give the same phases bit for bit (a new surface starts the phases over).
void twoRunsFromResetAgree() {
    RatePhases r;
    SceneParams p;
    auto run = [&]() {
        r.reset();
        for (int i = 0; i < 500; ++i) {
            p.speed = 0.5f + 0.001f * static_cast<float>(i);
            p.rotation = 0.3f - 0.0007f * static_cast<float>(i);
            r.step(p, 1.0f / 60.0f);
        }
        return std::make_pair(r.phase(ParamSlot::speed), r.phase(ParamSlot::rotation));
    };
    const auto a = run();
    const auto b = run();
    check(a.first == b.first && a.second == b.second, "two runs from reset give identical phases");
}

void badInputsAddNothing() {
    RatePhases r;
    SceneParams p;
    p.speed = 1.0f;
    r.step(p, 0.0f);
    r.step(p, -1.0f);
    r.step(p, NAN);
    check(r.phase(ParamSlot::speed) == 0.0, "a zero, negative or non-finite dt adds nothing");
    p.speed = NAN;
    r.step(p, 0.1f);
    check(r.phase(ParamSlot::speed) == 0.0, "a non-finite rate adds nothing");
    r.step(SceneParams{}, 0.5f);
    r.reset();
    check(r.phase(ParamSlot::speed) == 0.0, "reset clears the phases");
}

}  // namespace

int main() {
    everyRateSlotHasAReader();
    theRateParamsAreMarked();
    aChangeOfSpeedNeverJumpsThePhase();
    aRampsPhaseIsItsIntegral();
    aNegativeRateRunsBackAndTheWrapStaysInRange();
    aHugePhaseWrapsExactly();
    twoRunsFromResetAgree();
    badInputsAddNothing();
    if (failures == 0) std::printf("rate_phases_test: ok\n");
    return failures == 0 ? 0 : 1;
}
