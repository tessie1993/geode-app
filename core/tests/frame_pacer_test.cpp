// Host-side checks for core/stage/FramePacer: the live stage's frame clock. Built and run by CI ("Native host tests")
// with the system compiler; no EGL, no Android.
//
// What is pinned: the swap interval is the fewest whole vsyncs that keep the drawn rate at or under the user's rate
// (60 on a 90 Hz panel is every second vsync, 45 fps, never 90); Follow device and an unknown period draw every vsync;
// the budget is the interval times the period; frames land the interval apart by vsync time, so a vsync the stage was
// too busy to be asked for still counts; without times they are counted; and a display that changes mode changes the
// interval on its next vsync, with no settling.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

#include "stage/FramePacer.hpp"

using geode::stage::FramePacer;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

constexpr int64_t k60 = 16'666'667;
constexpr int64_t k90 = 11'111'111;
constexpr int64_t k120 = 8'333'333;

// Frames drawn over `vsyncs` vsyncs `period` apart, starting at `start`, at `target`.
int drawnOver(FramePacer& p, int vsyncs, int64_t period, int64_t start = 1'000'000'000) {
    int drawn = 0;
    for (int i = 0; i < vsyncs; ++i) {
        const int64_t t = start + i * period;
        if (p.due(t, period)) {
            p.drawn(t);
            ++drawn;
        } else {
            p.passed();
        }
    }
    return drawn;
}

// Red against: a divide that rounds (60 on 90 Hz drawn at 90, the old 1.5x rule), one that does not reach whole
// vsyncs, and a 59.94 Hz panel read as over a 60 cap.
void theIntervalIsTheFewestWholeVsyncsUnderTheRate() {
    check(FramePacer::intervalFor(60.0f, 60.0f) == 1, "60 on 60 Hz: every vsync");
    check(FramePacer::intervalFor(59.94f, 60.0f) == 1, "60 on a 59.94 Hz panel: every vsync");
    check(FramePacer::intervalFor(90.0f, 60.0f) == 2, "60 on 90 Hz: every second vsync, 45 fps, never over 60");
    check(FramePacer::intervalFor(90.0f, 45.0f) == 2, "45 on 90 Hz: every second vsync");
    check(FramePacer::intervalFor(90.0f, 30.0f) == 3, "30 on 90 Hz: every third");
    check(FramePacer::intervalFor(120.0f, 40.0f) == 3, "40 on 120 Hz: every third");
    check(FramePacer::intervalFor(120.0f, 60.0f) == 2, "60 on 120 Hz: every second");
    check(FramePacer::intervalFor(120.0f, 90.0f) == 2, "90 on 120 Hz: every second, under the rate");
    check(FramePacer::intervalFor(60.0f, 90.0f) == 1, "a rate over the panel draws every vsync");
    check(FramePacer::intervalFor(240.0f, 1.0f) == FramePacer::kMaxInterval, "never past the longest interval");
    check(FramePacer::intervalFor(90.0f, 0.0f) == 1 && FramePacer::intervalFor(90.0f, -1.0f) == 1, "no rate, Follow: every vsync");
    check(FramePacer::intervalFor(0.0f, 60.0f) == 1, "an unknown refresh rate: every vsync");
    check(FramePacer::intervalFor(std::numeric_limits<float>::quiet_NaN(), 60.0f) == 1, "a NaN refresh rate: every vsync");
}

// Red against: a pacer that draws more often than the user's rate, one that drifts off whole vsyncs, and a budget
// that is the display's period instead of the frame's.
void framesLandTheIntervalApart() {
    FramePacer p;
    p.setTarget(60.0f);
    check(drawnOver(p, 90, k90) == 45, "a second of 90 Hz under a 60 rate draws 45 frames");
    check(p.interval() == 2 && p.budgetNs() == 2 * k90, "the budget is two vsyncs");
    check(std::fabs(p.frameFps() - 45.0f) < 0.01f, "and the frame rate 45");

    FramePacer honoured;
    honoured.setTarget(60.0f);
    check(drawnOver(honoured, 60, k60) == 60, "a display at the rate draws every vsync");
    check(honoured.budgetNs() == k60, "the budget is one vsync");

    FramePacer follow;
    follow.setTarget(-1.0f);
    check(drawnOver(follow, 120, k120) == 120, "Follow device draws every vsync");
    check(std::fabs(follow.frameFps() - 120.0f) < 0.01f, "at the display's own rate");

    FramePacer thirty;
    thirty.setTarget(30.0f);
    check(drawnOver(thirty, 120, k120) == 30, "30 on 120 Hz draws every fourth");
}

// Red against: a pacer that counts only the vsyncs it is asked about (a busy stage would then draw late frames every
// interval after the miss, drifting), and one that cannot pace without vsync times.
void aMissedVsyncCountsByTime() {
    FramePacer p;
    p.setTarget(45.0f);
    const int64_t t0 = 1'000'000'000;
    check(p.due(t0, k90), "the first vsync draws");
    p.drawn(t0);
    // The stage was busy for the next vsync: it was never asked. The one after is two periods on: due.
    check(p.due(t0 + 2 * k90, k90), "two periods on is due, though only one vsync was asked about");
    p.drawn(t0 + 2 * k90);
    check(!p.due(t0 + 3 * k90, k90), "one period after a frame is not");

    FramePacer counted;
    counted.setTarget(30.0f);
    int drawn = 0;
    for (int i = 0; i < 90; ++i) {
        if (counted.due(0, k90)) {
            counted.drawn(0);
            ++drawn;
        } else {
            counted.passed();
        }
    }
    check(drawn == 30, "with no vsync times, every third vsync is counted out");
}

// Red against: a pacer that waits for a new rate to settle (the old 30-frame rule), and one that keeps the old period
// once the display changes mode.
void aModeChangeTakesTheNextVsync() {
    FramePacer p;
    p.setTarget(60.0f);
    drawnOver(p, 30, k60);
    check(p.interval() == 1, "60 Hz under a 60 rate: every vsync");
    p.due(2'000'000'000, k90);
    check(p.interval() == 2 && p.budgetNs() == 2 * k90, "the first 90 Hz vsync takes the interval to two");
    p.due(2'000'000'000 + k90, 0);
    check(p.periodNs() == k90, "a vsync with no period keeps the last one");
    p.setTarget(90.0f);
    p.due(2'000'000'000 + 2 * k90, k90);
    check(p.interval() == 1, "a new rate takes the next vsync too");
}

}  // namespace

int main() {
    theIntervalIsTheFewestWholeVsyncsUnderTheRate();
    framesLandTheIntervalApart();
    aMissedVsyncCountsByTime();
    aModeChangeTakesTheNextVsync();
    if (failures) {
        std::fprintf(stderr, "%d frame pacer check(s) failed\n", failures);
        return 1;
    }
    std::printf("frame pacer: all checks passed\n");
    return 0;
}
