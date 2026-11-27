// Host-side checks for core/stage/VsyncRule: the stage's whole vsync rule. Built and run by CI ("Native host tests")
// with the system compiler; no EGL, no Android.
//
// What is pinned: a vsync draws unless the last frame is still in flight, on either side of the stage (the CPU
// side the vsync thread knows, the GPU side the fence says); the rate hint states the user's rate, 0 for Follow device, no cap
// or a cap that is not a number; and the presentation rate is read from the interval between presentations.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

#include "stage/VsyncRule.hpp"

using namespace geode::stage;
namespace frame = geode::viz::frame;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

// Red against: a rule that also skips when idle, one that ignores the fence (the GPU queue then stuffs), one that
// ignores the CPU flag (a second request lands on a stage still drawing), and `||` written `&&`.
void aVsyncDrawsUnlessAFrameIsInFlight() {
    check(!skipVsync(false, false), "nothing in flight: draw");
    check(skipVsync(true, false), "the stage is still on the last frame: skip");
    check(skipVsync(false, true), "the GPU has not finished the last frame: skip");
    check(skipVsync(true, true), "both: skip");
}

// Red against: Follow device (-1) stated as a rate (a negative hint), a NaN or infinite cap passed on to the
// platform, and a cap that is changed (rounded, clamped to a table) on the way.
void theHintIsTheUsersRateAndNothingElse() {
    check(frameRateHint(60.0f) == 60.0f && frameRateHint(30.0f) == 30.0f && frameRateHint(120.0f) == 120.0f, "a cap is stated as it is");
    check(frameRateHint(59.94f) == 59.94f, "an odd cap is not rounded");
    check(frameRateHint(static_cast<float>(frame::kLiveFpsFollow)) == 0.0f, "Follow device states no rate");
    check(frameRateHint(0.0f) == 0.0f && frameRateHint(-7.0f) == 0.0f, "no cap states no rate");
    check(frameRateHint(std::numeric_limits<float>::quiet_NaN()) == 0.0f, "a NaN cap states no rate");
    check(frameRateHint(std::numeric_limits<float>::infinity()) == 0.0f, "an infinite cap states no rate");
}

// By hand: 1e9 / 16'666'667 = 59.99999..., 1e9 / 8'333'333 = 120.00000.., 1e9 / 11'111'111 = 90.
void thePresentRateIsReadFromTheInterval() {
    check(std::fabs(presentRateFps(16'666'667) - 60.0f) < 1e-3f, "16.67 ms is 60 fps");
    check(std::fabs(presentRateFps(8'333'333) - 120.0f) < 1e-3f, "8.33 ms is 120 fps");
    check(std::fabs(presentRateFps(11'111'111) - 90.0f) < 1e-3f, "11.11 ms is 90 fps");
    check(presentRateFps(0) == 0.0f && presentRateFps(-1) == 0.0f, "no interval is no rate");
}

}  // namespace

int main() {
    aVsyncDrawsUnlessAFrameIsInFlight();
    theHintIsTheUsersRateAndNothingElse();
    thePresentRateIsReadFromTheInterval();
    if (failures) {
        std::fprintf(stderr, "%d vsync rule check(s) failed\n", failures);
        return 1;
    }
    std::printf("vsync rule: all checks passed\n");
    return 0;
}
