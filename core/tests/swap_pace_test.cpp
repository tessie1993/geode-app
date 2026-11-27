// Host-side checks for core/stage/SwapPace: how the stage paces its swaps. Built and run by CI ("Native host tests")
// with the system compiler; no GL.
//
// What is pinned: the rate the window is voted (the cap, none for Follow device or a cap that is not a rate), the swap
// interval Swappy is given for a cap, the frame's length in whole refresh periods with Swappy's own rounding, and the
// presentation time each frame is drawn for (the pacer's time snapped to whole refresh periods, never two frames on one
// refresh, two frames ahead where the pacer says none).
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "stage/SwapPace.hpp"

using namespace geode::stage;

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

// Red against: Follow device stated as a rate, a cap that is not a number stated as one, and a stated rate that is not
// the cap.
void theVoteIsTheCapOrNothing() {
    check(frameRateHint(60.0f) == 60.0f && frameRateHint(45.0f) == 45.0f, "a cap is the rate the window is voted");
    check(frameRateHint(static_cast<float>(geode::viz::frame::kLiveFpsFollow)) == 0.0f, "Follow device states none");
    check(frameRateHint(0.0f) == 0.0f && frameRateHint(-5.0f) == 0.0f, "no cap, or a negative one, states none");
    check(frameRateHint(NAN) == 0.0f && frameRateHint(INFINITY) == 0.0f, "a cap that is not a number states none");
}

// Red against: an interval rounded up (a 60 cap would be 59.99), Follow device given an interval (Swappy would pace
// it), and a cap that is not a rate given one.
void theSwapIntervalIsOneFrameAtTheCap() {
    check(swapIntervalNs(60.0f) == 16'666'666, "60: one frame, rounded down");
    check(swapIntervalNs(30.0f) == 33'333'333 && swapIntervalNs(120.0f) == 8'333'333, "30 and 120");
    check(swapIntervalNs(static_cast<float>(geode::viz::frame::kLiveFpsFollow)) == 0, "Follow device: none, so Swappy's pacing is off");
    check(swapIntervalNs(0.0f) == 0 && swapIntervalNs(NAN) == 0, "no cap: none");
}

// Red against: Swappy's margin lost (a 60 interval that is 1 ns short of a period counted as less than one), a frame
// that spans fewer periods than the interval needs (a 60 cap on a 90 Hz panel shown at 90), and an unknown period
// guessed.
void aFrameSpansWholeRefreshPeriods() {
    check(periodsPerFrame(swapIntervalNs(60.0f), k60) == 1, "60 on 60 Hz: one period");
    check(periodsPerFrame(swapIntervalNs(60.0f), 16'666'666) == 1, "60 on a period just under: still one");
    check(periodsPerFrame(swapIntervalNs(60.0f), k120) == 2, "60 on 120 Hz: two periods");
    check(periodsPerFrame(swapIntervalNs(60.0f), k90) == 2, "60 on 90 Hz: two periods, 45 fps, never over the cap");
    check(periodsPerFrame(swapIntervalNs(45.0f), k90) == 2, "45 on 90 Hz: two");
    check(periodsPerFrame(swapIntervalNs(30.0f), k90) == 3, "30 on 90 Hz: three");
    check(periodsPerFrame(swapIntervalNs(40.0f), k120) == 3, "40 on 120 Hz: three");
    check(periodsPerFrame(swapIntervalNs(120.0f), k60) == 1, "a cap above the display is every refresh");
    check(periodsPerFrame(0, k90) == 1, "no interval: every refresh");
    check(periodsPerFrame(swapIntervalNs(60.0f), 0) == 1, "no period known: one");
    check(frameLengthNs(swapIntervalNs(60.0f), k90) == 2 * k90, "a frame's length is its periods");
    check(frameLengthNs(0, k90) == k90, "Follow device: one refresh period of the mode the system runs");
    check(frameLengthNs(swapIntervalNs(60.0f), 0) == 0, "no period known: no length");
}

// Red against: the pacer's time used as it comes (the picture's step jitters with Swappy's wake-up), two frames shown on
// one refresh, a frame after a start counted from a frame before the gap, a time already past used, and no time from the
// pacer taken as now.
void theClockCountsRefreshPeriodsAndLocksItsPhase() {
    const int64_t t0 = 5'000'000'000;
    PresentClock clock;
    check(clock.next(t0, 1'000, k60, k60) == t0, "the first frame is shown when the pacer says");
    const int64_t a = clock.next(t0 + k60 + 400, 2'000, k60, k60);
    check(a - (t0 + k60) >= 0 && a - (t0 + k60) < 100, "a fraction of a millisecond of jitter is divided by the lock's eight frames");
    const int64_t b = clock.next(t0 + 2 * k60 - 300, 3'000, k60, k60);
    check(std::llabs((b - a) - k60) < 100, "in either direction the step is one period");
    const int64_t c = clock.next(t0 + 6 * k60 + 20'000, 4'000, k60, k60);
    check(std::llabs((c - b) - 4 * k60) < 10'000, "a late frame steps by the periods it really took, once");
    const int64_t d = clock.next(t0, 5'000, k60, k60);
    check(d - c > k60 / 2 && d - c < 3 * k60 / 2, "a time at or before the last frame's is the next refresh: never two frames on one");
    clock.restart();
    check(clock.next(t0 + 100 * k60 + 5, 6'000, k60, k60) == t0 + 100 * k60 + 5, "a start is not counted from a frame before the gap");
    PresentClock none;
    check(none.next(0, 7'000'000'000, 2 * k60, k60) == 7'000'000'000 + 4 * k60, "no time from the pacer: two frames of the frame's length ahead");
    PresentClock bare;
    check(bare.next(0, 9'000, 0, 0) == 9'000, "no time and no length known: now");
    check(bare.next(9'500, 9'000, 0, 0) == 9'500, "no period: no count");
}

// A small deterministic noise, so the test is the same on every run.
struct Noise {
    uint64_t s = 0x9e3779b97f4a7c15ULL;
    int64_t within(int64_t half) {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<int64_t>((s >> 33) % static_cast<uint64_t>(2 * half + 1)) - half;
    }
};

// Red against: a count that accumulates the nominal period and never looks at the pacer again. A panel whose real period is
// 0.05 % off the nominal one gains 8 us a frame on it, so after about a thousand frames the offset passes half a period and
// the count steps twice: the picture skips a frame every 16 s at 60 Hz and the sound's read point wanders by up to half a
// period meanwhile.
void aPeriodOffFromNominalNeitherDriftsNorSkips() {
    for (const int64_t nominal : {k60, k90, k120}) {
        const int64_t real = nominal + nominal / 2000;  // 0.05 % off
        Noise noise;
        PresentClock clock;
        const int64_t base = 40'000'000'000;
        int64_t previous = 0;
        int64_t worstSkew = 0;
        double sumSquares = 0.0;
        int steps = 0, odd = 0;
        for (int n = 0; n < 10'000; ++n) {
            const int64_t truth = base + n * real;
            const int64_t desired = truth + noise.within(500'000);  // +-0.5 ms
            const int64_t t = clock.next(desired, desired - 30'000'000, nominal, nominal);
            if (n > 0) {
                const int64_t step = t - previous;
                ++steps;
                if (std::llabs(step - nominal) > nominal / 8) ++odd;
            }
            if (n >= 100) {
                worstSkew = std::max<int64_t>(worstSkew, std::llabs(t - truth));
                sumSquares += static_cast<double>(t - truth) * static_cast<double>(t - truth);
            }
            previous = t;
        }
        check(odd == 0 && steps == 9'999, "every step is one period: no frame is skipped or doubled over 10 000 frames");
        check(worstSkew < 400'000, "the time stays within 0.4 ms of the real one, inside the pacer's own +-0.5 ms jitter");
        check(std::sqrt(sumSquares / 9'900.0) < 120'000.0, "and is smoother than the pacer: its typical error is under half the jitter's");
    }
}

// Red against: the first frame after a display mode change counted against the old period (22.2 ms rounds to one 16.7 ms
// period, and the picture steps too little), and a step that is not the new frame length.
void aNewModeStepsByTheNewFrameLength() {
    const int64_t base = 10'000'000'000;
    PresentClock clock;
    int64_t last = 0;
    for (int n = 0; n < 50; ++n) last = clock.next(base + n * k60, base + n * k60 - 30'000'000, k60, k60);
    // The user's 60 on a panel that now runs 90 Hz: a frame is two of its periods.
    const int64_t desired = base + 49 * k60 + 2 * k90;
    const int64_t first = clock.next(desired, desired - 30'000'000, 2 * k90, k90);
    check(std::llabs((first - last) - 2 * k90) < 50'000, "the first frame after the change steps by the new frame length");
    int64_t previous = first;
    int odd = 0;
    for (int n = 1; n < 100; ++n) {
        const int64_t d = desired + n * 2 * k90;
        const int64_t t = clock.next(d, d - 30'000'000, 2 * k90, k90);
        if (std::llabs((t - previous) - 2 * k90) > 50'000) ++odd;
        previous = t;
    }
    check(odd == 0, "and every step after it");
    PresentClock faster;
    for (int n = 0; n < 20; ++n) last = faster.next(base + n * 2 * k60, base + n * 2 * k60 - 30'000'000, 2 * k60, k60);
    // The cap raised on the same mode: a frame is one period now, not two.
    const int64_t d2 = base + 19 * 2 * k60 + k60;
    const int64_t shorter = faster.next(d2, d2 - 30'000'000, k60, k60);
    check(std::llabs((shorter - last) - k60) < 50'000, "a cap change that shortens the frame steps by one period at once");
    // The pacer's base changed (Swappy to the vsync): the time still moves forward, by the new time's own distance.
    PresentClock switched;
    int64_t before = 0;
    for (int n = 0; n < 20; ++n) before = switched.next(base + n * k60, base + n * k60 - 30'000'000, k60, k60);
    switched.reanchor();
    const int64_t other = switched.next(before - 5 * k60 / 4, before - 5 * k60 / 4 - 30'000'000, k60, k60);
    check(other > before, "a pacer whose clock reads behind never steps the picture back");
}
// Red against: a budget worked out twice (the swap interval, the vote and the work target disagreeing), a vote that follows the
// display mode (the platform's choice) instead of the setting, an interval handed to a stage Swappy does not pace, and a
// budget before the period is known.
void theBudgetIsOneDescriptor() {
    const FrameBudget at60 = frameBudget(60.0f, k90, true);
    check(at60.periodNs == k90 && at60.swapNs == swapIntervalNs(60.0f) && at60.budgetNs == 2 * k90 && at60.voteFps == 60.0f,
          "60 on a 90 Hz mode: its swap interval, two periods, the setting's own vote");
    const FrameBudget moved = frameBudget(60.0f, k120, true);
    check(moved.voteFps == at60.voteFps && moved.swapNs == at60.swapNs && moved.budgetNs == 2 * k120, "a new mode moves the budget, never the vote or the interval");
    const FrameBudget follow = frameBudget(static_cast<float>(geode::viz::frame::kLiveFpsFollow), k90, true);
    check(follow.swapNs == 0 && follow.voteFps == 0.0f && follow.budgetNs == k90, "Follow device: no interval, no vote, one period of whatever mode runs");
    const FrameBudget vsync = frameBudget(30.0f, k60, false);
    check(vsync.swapNs == 0 && vsync.budgetNs == k60 && vsync.voteFps == 30.0f, "no Swappy: no interval, one frame per refresh, the vote still the setting's");
    const FrameBudget unknown = frameBudget(60.0f, 0, true);
    check(unknown.budgetNs == 0 && unknown.swapNs == swapIntervalNs(60.0f), "no period known: no budget yet, the interval and vote already");
    check(frameBudget(60.0f, k90, true) == at60 && !(moved == at60), "equal settings on equal modes are equal descriptors");
}

}  // namespace

int main() {
    theBudgetIsOneDescriptor();
    theVoteIsTheCapOrNothing();
    theSwapIntervalIsOneFrameAtTheCap();
    aFrameSpansWholeRefreshPeriods();
    theClockCountsRefreshPeriodsAndLocksItsPhase();
    aPeriodOffFromNominalNeitherDriftsNorSkips();
    aNewModeStepsByTheNewFrameLength();
    if (failures) {
        std::fprintf(stderr, "%d swap pace check(s) failed\n", failures);
        return 1;
    }
    std::printf("swap pace: all checks passed\n");
    return 0;
}
