// Host checks for the modulation cable's smoothing step (core/viz/modulation/Slew.hpp): the same input gives the same
// value at equal times at 30, 60 and 120 fps, no smoothing follows the target, and a huge dt lands on it without overshoot.
#include <cmath>
#include <cstdio>
#include <string>

#include "viz/modulation/Slew.hpp"

using namespace geode::viz;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

// A target that steps 0 -> 1 at 0.5 s and back to 0.25 at 1.5 s, slewed with tau = 0.2 s, read at 2 s and at the steps.
float run(int fps, float seconds, float tau) {
    float y = 0.0f;
    const float dt = 1.0f / static_cast<float>(fps);
    const int frames = static_cast<int>(std::lround(seconds * static_cast<float>(fps)));
    for (int i = 0; i < frames; ++i) {
        // The steps sit on a frame boundary at every rate, so each rate holds the target exactly as long as the others do.
        const float target = i < fps / 2 ? 0.0f : (i < 3 * fps / 2 ? 1.0f : 0.25f);
        y = slew::step(y, target, dt, tau);
    }
    return y;
}

void theSameInputGivesTheSameValueAtAnyFrameRate() {
    for (float at : {0.75f, 1.0f, 1.5f, 2.0f}) {
        const float a = run(30, at, 0.2f);
        const float b = run(60, at, 0.2f);
        const float c = run(120, at, 0.2f);
        check(std::fabs(a - b) < 1e-4f && std::fabs(b - c) < 1e-4f, "30, 60 and 120 fps agree at " + std::to_string(at) + " s");
    }
    // One time constant of a held target covers 1 - 1/e of the gap, at any rate.
    for (int fps : {30, 60, 120}) {
        float y = 0.0f;
        for (int i = 0; i < fps / 5; ++i) y = slew::step(y, 1.0f, 1.0f / static_cast<float>(fps), 0.2f);
        check(std::fabs(y - (1.0f - std::exp(-1.0f))) < 1e-4f, "one tau covers 63% of the gap at " + std::to_string(fps) + " fps");
    }
}

void noSmoothingFollowsTheTarget() {
    check(slew::step(0.3f, 0.9f, 1.0f / 60.0f, 0.0f) == 0.9f, "tau 0 follows the target");
    check(slew::step(0.3f, 0.9f, 1.0f / 60.0f, -1.0f) == 0.9f, "a negative tau counts as none");
    check(slew::step(0.3f, 0.9f, 1.0f / 60.0f, NAN) == 0.9f, "a non-finite tau counts as none");
}

void aHugeDtSnapsAndNeverOvershoots() {
    check(slew::step(0.0f, 1.0f, 1000.0f, 0.2f) == 1.0f, "a huge dt lands on the target");
    check(slew::step(0.0f, 1.0f, INFINITY, 0.2f) == 1.0f, "an infinite dt lands on the target");
    float y = 0.0f;
    bool over = false;
    for (float dt : {0.001f, 0.5f, 3.0f, 60.0f, 1e6f}) {
        y = slew::step(y, 1.0f, dt, 0.2f);
        over = over || y > 1.0f;
    }
    check(!over, "no dt takes y past the target");
}

void aStoppedClockHolds() {
    check(slew::step(0.3f, 0.9f, 0.0f, 0.2f) == 0.3f, "dt 0 holds");
    check(slew::step(0.3f, 0.9f, -1.0f, 0.2f) == 0.3f, "a negative dt holds");
    check(slew::step(0.3f, 0.9f, NAN, 0.2f) == 0.3f, "a NaN dt holds");
}

}  // namespace

int main() {
    theSameInputGivesTheSameValueAtAnyFrameRate();
    noSmoothingFollowsTheTarget();
    aHugeDtSnapsAndNeverOvershoots();
    aStoppedClockHolds();
    if (failures == 0) std::printf("slew_test: ok\n");
    return failures == 0 ? 0 : 1;
}
