// Host-side checks for core/viz/frame/Pacer.hpp: the times a frame is read and shown at (the dt clamp, the cap a
// surface runs at, where the sound is read, the swap's stamp). Built and run by CI ("Native host tests" in
// .github/workflows/android.yml) with the system compiler; no GL.
//
// The vsync divisor, the period estimate and the stall rules this file used to pin are gone with the pacer: every
// vsync the system delivers draws (core/tests/vsync_rule_test.cpp).
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "viz/frame/Pacer.hpp"

using namespace geode::viz::frame;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

bool near(double a, double b, double eps) { return std::fabs(a - b) <= eps; }

// Kills: `std::isfinite` -> `!std::isnan` on either cap, and a negative surface cap taken as a cap.
void aCapThatIsNotANumberIsNoCap() {
    check(effectiveCapFps(60.0f, NAN) == 60.0f, "a NaN second cap leaves the first");
    check(effectiveCapFps(INFINITY, 0.0f) == 0.0f, "an infinite cap is free-running");
    check(effectiveCapFps(60.0f, INFINITY) == 60.0f, "an infinite second cap is none");
    check(effectiveCapFps(-30.0f, 0.0f) == 0.0f, "a negative cap is none");
    check(effectiveCapFps(-30.0f, 30.0f) == 30.0f, "and the other cap still applies");
}

// Red against: kMaxDtSeconds back at the deleted Kotlin's 1/15 s (a 12 fps frame loses 17 ms) or raised (a stall
// steps more than 100 ms), kMinDtSeconds at 0 or raised past 4 ms (a 250 Hz frame is stretched), clampDt's arguments
// swapped (a 250 ms hitch passes whole).
void dtRuleSpansTenToTwoFiftyFps() {
    check(near(clampDt(1.0 / 12.0), 0.0833333333, 1e-9), "a 12 fps frame steps its whole 83.3 ms");
    check(near(clampDt(1.0 / 10.0), 0.1, 1e-12), "a 10 fps frame steps its whole 100 ms");
    check(near(clampDt(0.25), 0.1, 1e-12), "a 250 ms hitch steps 100 ms");
    check(near(clampDt(3600.0), 0.1, 1e-12), "an hour-long stall steps 100 ms");
    check(near(clampDt(0.004), 0.004, 1e-15), "a 250 Hz frame steps its whole 4 ms");
    check(clampDt(0.0) >= 0.001 && clampDt(0.0) <= 0.002, "a zero interval still steps about 1 ms, never 0");
    check(clampDt(-0.5) >= 0.001 && clampDt(-0.5) <= 0.002, "a clock that steps back still steps about 1 ms forward");
}

// Red against: sound read at the draw's start (present ahead ignored), a lead past the dt clamp, and a
// presentation time already past reading ahead.
void soundIsReadWhereTheFrameIsShown() {
    const int64_t now = 5'000'000'000;
    check(listenAtNs(now, now + 33'000'000) == now + 33'000'000, "a frame shown 33 ms on hears 33 ms on");
    check(listenAtNs(now, 0) == now, "no presentation time reads at now");
    check(listenAtNs(now, now - 5'000'000) == now, "a presentation time in the past reads at now");
    check(listenAtNs(now, now + 900'000'000) == now + 100'000'000, "a lead is held to the dt clamp");
}

// Red against: a swap stamped on its own presentation time when the interval is known (Android's compositor holds a
// buffer until a refresh it expects to present later than the stamp, so that frame can slip one).
void aSwapIsStampedBetweenItsVsyncAndTheOneBefore() {
    check(latchStampNs(1'020'000'000, 8'000'000) == 1'016'000'000, "a stamp is half an interval before its presentation");
    check(latchStampNs(1'020'000'000, 8'333'333) > 1'020'000'000 - 8'333'333 && latchStampNs(1'020'000'000, 8'333'333) < 1'020'000'000,
          "an odd interval still lands strictly between the two");
    check(latchStampNs(1'020'000'000, 0) == 1'020'000'000 && latchStampNs(1'020'000'000, -5) == 1'020'000'000,
          "no interval: the presentation time itself, as before");
    check(latchStampNs(0, 8'000'000) == 0, "no presentation time, no stamp");
}

}  // namespace

int main() {
    aCapThatIsNotANumberIsNoCap();
    dtRuleSpansTenToTwoFiftyFps();
    soundIsReadWhereTheFrameIsShown();
    aSwapIsStampedBetweenItsVsyncAndTheOneBefore();
    if (failures == 0) std::printf("pacer_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
