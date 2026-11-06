// Host-side checks for what #509 (#441, #389) adds to the wallpaper's and the dream's run rule
// (core/viz/SurfaceRun), through the C ABI the shells call: reduced motion holds a still frame, the still
// frame's native settle, and each surface's own fps choice as the frame pacer (core/viz/frame/Pacer)
// caps to it. Built and run by CI ("Native host tests" in .github/workflows/android.yml); no GL, no device.
//
// surface_run_test.cpp pins the rule's spot cases; this file walks every flag combination with the new
// bit, every pair of run modes, the fps snap over its whole input range, and the pacer at each choice.
// Expected values come from the PR's stated rule ("reduced motion holds a still frame, stopped still beats
// still"; "the 2 s settle on the way into a still frame"; "15, 30 or 60, the nearest, the lower on a
// tie, <= 0 the default 30"), never from the code's own arithmetic.
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>

#include "api/geode_api.h"
#include "viz/frame/Pacer.hpp"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

void checkAt(bool ok, const char* what, long long a, long long b) {
    if (ok) return;
    char line[220];
    std::snprintf(line, sizeof line, "%s (%lld, %lld)", what, a, b);
    check(false, line);
}

constexpr int kStopped = 0;
constexpr int kStill = 1;
constexpr int kAnimated = 2;

constexpr unsigned kReduced = GEODE_SURFACE_REDUCED_MOTION;
constexpr unsigned kOnScreen = GEODE_SURFACE_HAS_SURFACE | GEODE_SURFACE_VISIBLE;
constexpr unsigned kDefaults = GEODE_SURFACE_PAUSE_IN_BATTERY_SAVER | GEODE_SURFACE_REST_WHEN_SILENT;
constexpr unsigned kOtherBits = 0xFFu;  // the eight GEODE_SURFACE_* bits before reduced motion

// A real wall clock (Nov 2026, ms since the epoch) and the trial's hour.
constexpr long long kWall = 1'793'000'000'000LL;
constexpr long long kHour = 3'600'000;
constexpr long long kPro = GEODE_SURFACE_FOREVER;

// Pro, free, a trial with time left, a trial over on this very millisecond.
constexpr long long kEntitlements[] = {kPro, 0, kWall + kHour, kWall};

// ---- Reduced motion over every other input ---------------------------------------------------------

// The stated rule as a property: reduced motion turns anything that would draw into a still frame and
// leaves a stopped surface stopped (min(mode, still)), whatever the cover, preview, saver, silence,
// options or entitlement; and it releases the capture. Kills: reduced motion checked only when a
// rest reason is absent (e.g. `&& !battery_saver`, `&& !silent`), the preview exempted from it, the
// check placed before the sight test (would draw on a hidden surface) or before the cover (a covered
// home screen drawn), returning Stopped instead of Still, and listening() masking the bit out.
void reducedMotionIsAtMostStillEverywhere() {
    for (const long long until : kEntitlements) {
        for (unsigned f = 0; f <= kOtherBits; ++f) {
            const int without = geode_surface_run_mode(f, until, kWall);
            const int with = geode_surface_run_mode(f | kReduced, until, kWall);
            const int expected = without == kStopped ? kStopped : kStill;
            checkAt(with == expected, "reduced motion: stopped stays stopped, everything else a still frame", f, until);
            checkAt(geode_surface_listening(f | kReduced, until, kWall) == 0, "reduced motion hears nothing", f,
                    until);
        }
    }
}

// The bit is one input of its own: turning it on never stops or wakes a surface the other bits decide,
// and on screen with nothing else to rest for it alone makes the difference. Kills: the new bit given
// a value that overlaps an older one in both the header and the rule (1u << 7 would read as "rest when
// silent" and leave this surface moving; 1u << 2 as the preview).
void reducedMotionIsItsOwnBit() {
    // On screen, Pro, every rest option off: only reduced motion can still it.
    check(geode_surface_run_mode(kOnScreen, kPro, kWall) == kAnimated, "on screen, nothing to rest for: moving");
    check(geode_surface_run_mode(kOnScreen | kReduced, kPro, kWall) == kStill, "the same with reduced motion: still");
    check(geode_surface_listening(kOnScreen, kPro, kWall) == 1, "moving, it hears");
}

// ---- The settle -----------------------------------------------------------------------------------

// Every pair of run modes against the table the PR states: 2 s only on the way into a still frame
// from somewhere else, 0 for every other restate. Kills: `now == Still` -> `now <= Still` (the way
// into Stopped would settle: Animated -> Stopped and Stopped -> Stopped), `previous != Still` ->
// `previous != now` with `now` unchecked, a swapped (previous, mode) pair at the ABI, any other value.
void settleOnlyOnTheWayIntoStill() {
    const long long table[3][3] = {
        // to:  stopped  still   moving        from:
        {0, 2'000, 0},  // stopped
        {0, 0, 0},      // still
        {0, 2'000, 0},  // moving
    };
    for (int from = kStopped; from <= kAnimated; ++from) {
        for (int to = kStopped; to <= kAnimated; ++to) {
            checkAt(geode_surface_settle_ms(from, to) == table[from][to], "the settle table", from, to);
        }
    }
}

// The shells' real sequence, through the rule and the settle together, on a trial: the dream is shown
// with reduced motion on, the trial ends under it, silence comes and goes, reduced motion turns off.
// The trial's end must not start a second burst (nothing moves to stop), and turning reduced motion
// off on a quiet surface must start listening again so sound can wake it. Kills: listening() masking
// reduced motion out (a still frame that keeps the capture), and a settle on Still -> Still.
void trialAndSilenceUnderReducedMotion() {
    const unsigned f = kOnScreen | kDefaults;
    const long long ends = kWall + kHour;
    int prev = kStopped;
    auto restate = [&](unsigned flags, long long now, long long expectSettle, int expectMode, const char* what) {
        const int m = geode_surface_run_mode(flags, ends, now);
        check(m == expectMode, what);
        check(geode_surface_settle_ms(prev, m) == expectSettle, what);
        prev = m;
    };
    restate(f | kReduced, kWall, 2'000, kStill, "shown under reduced motion in a trial: still, settling 2 s");
    check(geode_surface_listening(f | kReduced, ends, kWall) == 0, "and its capture released");
    const long long wait = geode_surface_recheck_in_ms(ends, kWall);
    check(wait > kHour, "the trial's restate is still asked for under reduced motion");
    restate(f | kReduced, kWall + wait, 0, kStill, "the trial's end under reduced motion: still, no new burst");
    restate(f | kReduced | GEODE_SURFACE_SILENT, kWall + wait + 1, 0, kStill, "silence under it: no new burst");

    // A second dream, Pro this time: reduced motion turned off while the last poll said silent.
    const unsigned quiet = f | GEODE_SURFACE_SILENT;
    check(geode_surface_run_mode(quiet, kPro, kWall) == kStill, "reduced motion off, still quiet: a still frame");
    check(geode_surface_listening(quiet, kPro, kWall) == 1, "reduced motion off: listening again, so sound wakes it");
    check(geode_surface_run_mode(f, kPro, kWall) == kAnimated, "sound back: moving");
    check(geode_surface_settle_ms(kStill, kAnimated) == 0, "waking needs no settle");
}

// ---- The fps choice -------------------------------------------------------------------------------

// Hand-worked midpoints of the listed rates. Linear nearest: 15..30 splits at 22.5, 30..60 at 45 (a
// tie, the lower). Kills: a nearest by ratio (the geometric midpoints are 21.2 and 42.4, so 22 -> 30 and
// 44 -> 60), a tie that goes up, a missing abs on one side.
void fpsSnapsToTheNearestRate() {
    check(geode_surface_fps_choice(22) == 15, "22 is nearer 15");
    check(geode_surface_fps_choice(23) == 30, "23 is nearer 30");
    check(geode_surface_fps_choice(44) == 30, "44 is nearer 30");
    check(geode_surface_fps_choice(46) == 60, "46 is nearer 60");
    check(geode_surface_fps_choice(16) == 15 && geode_surface_fps_choice(29) == 30 && geode_surface_fps_choice(31) == 30
              && geode_surface_fps_choice(59) == 60 && geode_surface_fps_choice(61) == 60,
          "one off a listed rate snaps back to it");
    // Whatever an older or newer build wrote, at the int's ends. Kills: the <= 0 guard dropped (abs of
    // a distance to INT_MIN overflows) or narrowed to < 0.
    check(geode_surface_fps_choice(INT_MAX) == 60, "INT_MAX: the highest");
    check(geode_surface_fps_choice(INT_MIN) == GEODE_SURFACE_DEFAULT_FPS, "INT_MIN: the default");
    check(geode_surface_fps_choice(1) == 15, "1: the lowest, not the default");
}

// Over every request 1..10 000 and a seeded spread of any int: the answer is listed, applying it again
// changes nothing (a value read back from prefs is stable), and a higher request never gives a lower
// rate. Kills: a farthest-not-nearest comparison, a best that starts outside the list, a loop that
// skips the last rate.
void fpsChoiceIsListedStableAndMonotone() {
    int out[3] = {};
    const int n = geode_surface_fps_choices(out, 3);
    auto listed = [&](int v) {
        for (int i = 0; i < n; ++i) {
            if (out[i] == v) return true;
        }
        return false;
    };
    int last = 0;
    for (int r = 1; r <= 10'000; ++r) {
        const int c = geode_surface_fps_choice(r);
        checkAt(listed(c), "the choice is a listed rate", r, c);
        checkAt(geode_surface_fps_choice(c) == c, "a choice is its own choice", r, c);
        checkAt(c >= last, "a higher request never gives a lower rate", r, c);
        last = c;
    }
    std::mt19937 gen(441);
    for (int i = 0; i < 100'000; ++i) {
        const int r = static_cast<int>(gen());  // raw output: any int, negative included (C++20 wraps)
        const int c = geode_surface_fps_choice(r);
        checkAt(listed(c), "any int: a listed rate", r, c);
        if (r <= 0) checkAt(c == GEODE_SURFACE_DEFAULT_FPS, "any non-positive int: the default", r, c);
    }
}

// The list call writes no more than it is given room for and never through a null buffer, and always
// counts the whole list. Kills: `i < capacity` -> `i <= capacity` (one write past the end), `&&` -> `||`
// (writes up to the list's length into a shorter buffer), the null check dropped.
void fpsChoicesRespectsTheBuffer() {
    constexpr int kSentinel = -7;
    int buf[6] = {kSentinel, kSentinel, kSentinel, kSentinel, kSentinel, kSentinel};
    check(geode_surface_fps_choices(buf, 2) == 3, "room for two: the count is still three");
    check(buf[0] == 15 && buf[1] == 30, "room for two: the two lowest");
    check(buf[2] == kSentinel && buf[3] == kSentinel, "room for two: nothing written past them");
    int none[2] = {kSentinel, kSentinel};
    check(geode_surface_fps_choices(none, 0) == 3 && none[0] == kSentinel, "no room: nothing written");
    check(geode_surface_fps_choices(none, -1) == 3 && none[0] == kSentinel, "negative room: nothing written");
    check(geode_surface_fps_choices(nullptr, 8) == 3, "a null buffer only counts, whatever the room");
    int roomy[6] = {kSentinel, kSentinel, kSentinel, kSentinel, kSentinel, kSentinel};
    check(geode_surface_fps_choices(roomy, 6) == 3 && roomy[2] == 60 && roomy[3] == kSentinel,
          "more room than rates: three written, the rest untouched");
}

// ---- The fps choice as the pacer's cap ------------------------------------------------------------

// Draws per second the pacer gives a surface whose stored choice is `stored`, on a steady `hz` panel:
// warm the period estimate up, then count the draws in two seconds of vsyncs and keep the last dt.
struct Paced {
    int perSecond = 0;
    double dt = 0.0;
};

Paced paceStored(int stored, int hz) {
    using geode::viz::frame::Pacer;
    using geode::viz::frame::PacerState;
    PacerState s;
    const auto cap = static_cast<float>(geode_surface_fps_choice(stored));
    const double period = 1e9 / hz;
    const int64_t t0 = 1'000'000'000;
    const int warm = 32;
    int drawn = 0;
    double dt = 0.0;
    for (int i = 0; i < warm + 2 * hz; ++i) {
        const auto t = t0 + static_cast<int64_t>(std::llround(period * i));
        const double d = Pacer::onVsync(s, t, cap);
        if (i >= warm && d > 0.0) {
            ++drawn;
            dt = d;
        }
    }
    return Paced{drawn / 2, dt};
}

// Each listed rate is one the pacer reaches exactly on 60 and 120 Hz panels (the PR's reason for the
// list), and 15 and 30 on 90 Hz, each frame stepping the engine by 1/rate; an unset choice paces at
// 30. Expected rates are the owner's 15, 30 and 60 (ADR 0008). Kills: a choice the pacer cannot divide
// down to (24 or 45 in the list), the snap handing the pacer the raw request instead of a listed rate
// (a stored 0 or -1 would cap to nothing and draw every vsync, 20 would draw
// at 20 on 120 Hz, 40 at 60 on 60 Hz), the default not 30.
void everyChoiceIsWhatThePacerDraws() {
    struct Case {
        int stored;
        int hz;
        int fps;
    };
    const Case cases[] = {{15, 60, 15},  {30, 60, 30},  {60, 60, 60},  {15, 120, 15}, {30, 120, 30},
                          {60, 120, 60}, {15, 90, 15},  {30, 90, 30},  {0, 60, 30},   {0, 120, 30},
                          {59, 120, 60}, {-1, 90, 30},  {20, 120, 15}, {40, 60, 30}};
    for (const Case& c : cases) {
        const Paced p = paceStored(c.stored, c.hz);
        checkAt(p.perSecond == c.fps, "the pacer draws at the chosen rate", c.stored, c.hz);
        checkAt(std::fabs(p.dt - 1.0 / c.fps) < 1e-6, "each drawn frame steps by 1/rate", c.stored, c.hz);
    }
}

}  // namespace

int main() {
    reducedMotionIsAtMostStillEverywhere();
    reducedMotionIsItsOwnBit();
    settleOnlyOnTheWayIntoStill();
    trialAndSilenceUnderReducedMotion();
    fpsSnapsToTheNearestRate();
    fpsChoiceIsListedStableAndMonotone();
    fpsChoicesRespectsTheBuffer();
    everyChoiceIsWhatThePacerDraws();
    if (failures == 0) std::printf("surface_motion_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
