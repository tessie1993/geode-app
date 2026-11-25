// Host-side checks for core/stage/VsyncRule: the stage's whole vsync rule. Built and run by CI ("Native host tests")
// with the system compiler; no EGL, no Android.
//
// What is pinned: a vsync draws unless the last frame is still in flight, on either side of the stage (the CPU
// side the vsync thread knows, the GPU side the fence says); the user's cap kept by a whole-number divide when the
// platform ignores the hint, and left alone when it does not; the rate hint states the user's rate, 0 for Follow device, no cap
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

// Counts the frames CapGuard lets through of `frames` frames at `rate` against `cap`, after `warm` frames that are not counted.
int guardedDraws(CapGuard& g, float rate, float cap, int frames, int warm = 0) {
    for (int i = 0; i < warm; ++i) g.draws(rate, cap);
    int drawn = 0;
    for (int i = 0; i < frames; ++i) drawn += g.draws(rate, cap) ? 1 : 0;
    return drawn;
}

// Red against: a cap that is only a hint (a display that ignores it draws every vsync, over what the user chose), a
// divide that is not a whole number of vsyncs (uneven frames), one that starts before the rate has stayed over the cap
// (a mode switch in flight would drop frames).
void aCapThePlatformIgnoredIsKept() {
    const int warm = CapGuard::kSustainFrames - 1;
    CapGuard sixtyOn120;
    check(guardedDraws(sixtyOn120, 120.0f, 60.0f, 120, warm) == 60, "120 Hz shown under a 60 cap draws every second vsync");
    CapGuard thirtyOn120;
    check(guardedDraws(thirtyOn120, 120.0f, 30.0f, 120, warm) == 30, "120 Hz under a 30 cap draws every fourth");
    CapGuard thirtyOn90;
    check(guardedDraws(thirtyOn90, 90.0f, 30.0f, 90, warm) == 30, "90 Hz under a 30 cap draws every third");
    CapGuard early;
    check(guardedDraws(early, 120.0f, 60.0f, CapGuard::kSustainFrames - 1) == CapGuard::kSustainFrames - 1,
          "before the rate has stayed over the cap for a while, every vsync draws");
}

// Red against: a guard that lowers what the user did not ask to lower: a display at the cap (the hint was honoured), one
// within the over factor, Follow device, no cap, an unknown rate, and one that stays on after the hint is honoured.
void aCapTheUserDidNotBreakIsLeftAlone() {
    CapGuard honoured;
    check(guardedDraws(honoured, 60.0f, 60.0f, 300) == 300, "a display that follows the hint draws every vsync");
    CapGuard close;
    check(guardedDraws(close, 90.0f, 60.0f, 300) == 300, "90 Hz under a 60 cap is within the over factor: every vsync");
    CapGuard follow;
    check(guardedDraws(follow, 120.0f, static_cast<float>(frame::kLiveFpsFollow), 300) == 300, "Follow device is never cut");
    CapGuard none;
    check(guardedDraws(none, 120.0f, 0.0f, 300) == 300, "no cap is never cut");
    CapGuard unknown;
    check(guardedDraws(unknown, 0.0f, 30.0f, 300) == 300, "an unknown rate is never cut");
    CapGuard nan;
    check(guardedDraws(nan, std::numeric_limits<float>::quiet_NaN(), 30.0f, 300) == 300, "a NaN rate is never cut");
    CapGuard followed;
    guardedDraws(followed, 120.0f, 60.0f, 100);
    check(guardedDraws(followed, 60.0f, 60.0f, 100) == 100, "the moment the display follows the hint, every vsync draws again");
    check(guardedDraws(followed, 120.0f, 60.0f, 20) == 20, "and a rate that goes back over the cap must stay over it again first");
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
    aCapThePlatformIgnoredIsKept();
    aCapTheUserDidNotBreakIsLeftAlone();
    theHintIsTheUsersRateAndNothingElse();
    thePresentRateIsReadFromTheInterval();
    if (failures) {
        std::fprintf(stderr, "%d vsync rule check(s) failed\n", failures);
        return 1;
    }
    std::printf("vsync rule: all checks passed\n");
    return 0;
}
