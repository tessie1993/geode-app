// Host-side checks for core/viz/frame/Thermal (#440): the process's one thermal state. Built and run by
// CI ("Native host tests" in .github/workflows/android.yml) with the system compiler; no GL.
//
// What is pinned: the owner's fps-first ladder (ADR 0008: fps, then render scale, then march, then post
// FX), the thermal cap reaching the pacer as min(surface cap, thermal cap), two renderers' governors
// reading one state (#387: the second used to read NaN headroom and stay at Full), the once-a-second
// cadence across threads with a headroom asked every ten seconds, the NaN rule (a missed window keeps the
// last real headroom a while), the dwell both ways, and the measured fallback raising the floor for every
// surface.
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

#include "api/geode_api.h"
#include "viz/ThermalGovernor.hpp"
#include "viz/frame/Pacer.hpp"
#include "viz/frame/Thermal.hpp"

using geode::viz::ThermalGovernor;
using geode::viz::ThermalState;
using geode::viz::ThermalTier;
using geode::viz::thermalTierInfo;
namespace frame = geode::viz::frame;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

constexpr ThermalTier kTiers[] = {ThermalTier::Full, ThermalTier::Eased, ThermalTier::Reduced, ThermalTier::Minimal};

// The first tier (0..3) at which a lever has moved from Full's value; 4 if it never does.
template <typename Moved>
int firstMoved(Moved moved) {
    for (int i = 0; i < 4; ++i) {
        if (moved(thermalTierInfo(kTiers[i]))) return i;
    }
    return 4;
}

void ladderIsFpsFirst() {
    const auto& full = thermalTierInfo(ThermalTier::Full);
    check(full.fpsCap == 0.0f && full.renderScale == 1.0f && full.marchScale == 1.0f && full.optionalPasses,
          "a cool phone gives up nothing");
    const int fps = firstMoved([](const auto& t) { return t.fpsCap > 0.0f; });
    const int scale = firstMoved([](const auto& t) { return t.renderScale < 1.0f; });
    const int march = firstMoved([](const auto& t) { return t.marchScale < 1.0f; });
    const int post = firstMoved([](const auto& t) { return !t.optionalPasses; });
    check(fps == 1, "the frame rate is the first thing a warm phone gives up");
    check(scale > fps, "render scale only after the frame rate");
    check(march >= scale, "march quality no sooner than render scale");
    check(post >= march, "post FX last");
    check(post < 4 && march < 4, "the hottest tier does drop march quality and post FX");
    check(thermalTierInfo(ThermalTier::Eased).fpsCap == 30.0f, "fps drops to 30");
    // Each lever only ever gets cheaper as the phone heats.
    for (int i = 1; i < 4; ++i) {
        const auto& cooler = thermalTierInfo(kTiers[i - 1]);
        const auto& hotter = thermalTierInfo(kTiers[i]);
        check(hotter.renderScale <= cooler.renderScale, "render scale never rises with heat");
        check(hotter.marchScale <= cooler.marchScale, "march scale never rises with heat");
        check(!hotter.optionalPasses || cooler.optionalPasses, "post FX never come back with heat");
        check(hotter.fpsCap > 0.0f, "every hot tier caps the frame rate");
    }
}

// Pushes `status` once a second from `t` for `seconds`; returns the time after the last push.
double pushFor(ThermalState& s, int status, float headroom, double t, int seconds) {
    for (int i = 0; i < seconds; ++i) s.push(status, headroom, t + i);
    return t + seconds;
}

void dwellBothWays() {
    ThermalState s;
    s.push(1, NAN, 100.0);
    check(s.tier() == ThermalTier::Full, "one warm reading does not drop the frame rate");
    s.push(0, NAN, 101.0);
    s.push(1, NAN, 102.0);
    s.push(1, NAN, 103.0);
    check(s.tier() == ThermalTier::Full, "a cool reading restarts the wait");
    s.push(1, NAN, 104.0);
    check(s.tier() == ThermalTier::Eased, "three warm readings in a row ease off");
    // Severe escalates straight to Minimal, not a rung at a time.
    double t = pushFor(s, 3, NAN, 105.0, 3);
    check(s.tier() == ThermalTier::Minimal, "a severe phone goes straight to Minimal");
    t = pushFor(s, 0, NAN, t, 59);
    check(s.tier() == ThermalTier::Minimal, "cooling waits a minute");
    t = pushFor(s, 0, NAN, t, 1);
    check(s.tier() == ThermalTier::Reduced, "a minute of cool steps back one rung");
    // A long pause (nothing drawing) is not a minute of watching the phone cool.
    s.push(0, NAN, t + 600.0);
    check(s.tier() == ThermalTier::Reduced, "a pause between readings does not count as cooling");
}

void headroomAndNaN() {
    ThermalState s;
    double t = pushFor(s, 0, 0.9f, 100.0, 3);
    check(s.tier() == ThermalTier::Reduced, "headroom 0.9 is Reduced");
    // Missed windows (NaN) keep the last real headroom: no cool dwell starts.
    t = pushFor(s, 0, NAN, t, 9);
    check(s.tier() == ThermalTier::Reduced, "a NaN is a missed window, not a cool phone");
    // Past the stale limit the headroom is no longer trusted; the cool dwell then runs a minute.
    t = pushFor(s, 0, NAN, t, 77);
    check(s.tier() == ThermalTier::Eased, "a long run of NaN lets the tier relax, a rung a minute");
    check(ThermalState::headroomTier(NAN) == ThermalTier::Full, "no forecast reads Full");
    check(ThermalState::headroomTier(0.8f) == ThermalTier::Eased, "0.8 eases");
    check(ThermalState::headroomTier(1.2f) == ThermalTier::Minimal, "past 1.0 is Minimal");
    check(ThermalState::statusTier(-1) == ThermalTier::Full, "no thermal API reads Full");
    check(ThermalState::statusTier(2) == ThermalTier::Reduced, "MODERATE is Reduced");
    check(ThermalState::statusTier(6) == ThermalTier::Minimal, "SHUTDOWN is Minimal");
    check(!ThermalState().platformKnows(), "no reading yet: the platform is unknown");
    ThermalState old;
    old.push(-1, NAN, 1.0);
    check(!old.platformKnows(), "a phone without a thermal API stays unknown");
}

void cadenceOncePerSecond() {
    ThermalState s;
    check(s.claimSample(100.0), "the first ask is due");
    check(!s.claimSample(100.4), "a second ask inside the second is not");
    check(!s.claimSample(100.99), "nor just before it ends");
    check(s.claimSample(101.0), "a second later it is due again");
    // Two GL threads asking in the same instant: exactly one reads PowerManager.
    std::atomic<int> won{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&] {
            if (s.claimSample(200.0)) won.fetch_add(1);
        });
    }
    for (auto& th : threads) th.join();
    check(won.load() == 1, "one renderer of many wins each sample");
}

// Kills: kHeadroomPeriodSeconds 10 -> 9 or 11, `>=` -> `>` on it, an ask on every second, and a settle
// dropped from the seconds between asks. Android's Thermal API guide: no more than once every 10 s.
void headroomIsAskedEveryTenSeconds() {
    ThermalState s;
    check(s.headroomDue(100.0), "the first second asks for a headroom");
    check(!s.headroomDue(100.5), "nothing is due inside the second");
    int asks = 0;
    for (int i = 1; i <= 9; ++i) asks += s.headroomDue(100.0 + i) ? 1 : 0;
    check(asks == 0, "nor on the nine seconds after an ask");
    check(s.headroomDue(110.0), "ten seconds after the last ask the next is due");
    check(!s.headroomDue(111.0) && !s.headroomDue(119.0), "and not again before 120");
    check(s.headroomDue(120.0), "then at 120");
    // The status arrives from the platform's listener, not on an ask; the seconds between asks settle it.
    ThermalState hot;
    check(hot.headroomDue(200.0), "an ask at 200");
    hot.push(-1, NAN, 200.0);  // the shell's answer: no forecast
    hot.push(3, NAN, 200.2);   // the listener: SEVERE
    check(hot.tier() == ThermalTier::Full && hot.platformKnows(), "one SEVERE status does not drop the rate");
    hot.headroomDue(201.0);
    hot.headroomDue(202.0);
    check(hot.tier() == ThermalTier::Full, "2 s of SEVERE is short of the dwell");
    hot.headroomDue(203.0);
    hot.headroomDue(204.0);
    check(hot.tier() == ThermalTier::Minimal, "the settles between asks carry the dwell to Minimal");
    // A headroom answered once holds across the settles until the next ask.
    ThermalState warm;
    warm.headroomDue(300.0);
    warm.push(-1, 0.9f, 300.0);
    for (int i = 1; i <= 9; ++i) warm.headroomDue(300.0 + i);
    check(warm.tier() == ThermalTier::Reduced, "one forecast of 0.9 holds for the ten seconds to the next ask");
    check(!warm.platformKnows(), "and a headroom alone is not a status");
}

void capReachesThePacer() {
    ThermalState s;
    check(s.capFor(60.0f) == 60.0f, "a cool phone leaves the surface's cap");
    check(s.capFor(0.0f) == 0.0f, "a cool free-running surface stays free");
    pushFor(s, 1, NAN, 100.0, 3);
    check(s.fpsCap() == 30.0f, "an eased phone caps at 30");
    check(s.capFor(60.0f) == 30.0f, "min(surface, thermal)");
    check(s.capFor(24.0f) == 24.0f, "a lower surface cap stays");
    check(s.capFor(0.0f) == 30.0f, "a free-running surface is capped too");
    // The pacer on a 120 Hz panel, fed the effective cap as geode_pacer_on_vsync feeds it.
    frame::PacerState p;
    const double period = 1e9 / 120.0;
    int drawn = 0;
    for (int i = 0; i < 240; ++i) {
        const auto t = 1'000'000'000 + static_cast<int64_t>(std::llround(period * i));
        if (frame::Pacer::onVsync(p, t, s.capFor(60.0f)) > 0.0 && i >= 120) drawn++;
    }
    check(p.divisor == 4, "120 Hz divides by 4 under the thermal 30");
    check(drawn == 30, "30 frames a second drawn");
    check(p.capMilliHz == 30'000, "the shell reads 30 fps back for setFrameRate");
}

void twoRenderersShareOneState() {
    ThermalState shared;
    ThermalGovernor live(shared);
    ThermalGovernor wallpaper(shared);
    live.setPacedFps(60.0f);
    wallpaper.setPacedFps(30.0f);
    // Only one of them is ever handed a reading; both see its tier (#387).
    pushFor(shared, 0, 0.9f, 100.0, 3);
    check(live.tier() == ThermalTier::Reduced && wallpaper.tier() == ThermalTier::Reduced, "both renderers read one tier");
    check(live.pacedFps() == 30.0f, "the live surface draws at the thermal 30");
    check(wallpaper.pacedFps() == 30.0f, "the wallpaper's own 30 stays");
    // An export on the same state renders at Full and does not cool the live surface.
    ThermalGovernor exportRenderer(shared);
    exportRenderer.beginOffscreenRender();
    check(exportRenderer.tier() == ThermalTier::Full, "an export renders at Full");
    check(live.tier() == ThermalTier::Reduced, "and the live surface stays hot");
    exportRenderer.endOffscreenRender();
    check(exportRenderer.tier() == ThermalTier::Reduced, "after the export it reads the shared tier");
}

// Cast's renderer draws a fixed 30 fps beside the stage's 60, on one thermal state, with Adapt on. Each is
// told its own paced rate (the shell used to hand both the stage's, the process's last-started pacer), so
// both are keeping up and nothing steps down. Worked by hand: the monitor's target is min(50, 0.85 × paced),
// 25.5 for the 30 fps surface and 50 for the 60 one. The second half is what the mislabel did: 30 fps read
// against a 60 cap is 40 % under the 50 target, a severe deficit after 2.5 s, so the floor rose two rungs
// and the stage itself was capped to 30.
void eachSurfaceIsReadAgainstItsOwnPacedRate() {
    ThermalState shared;
    shared.push(0, NAN, 100.0);
    ThermalGovernor stage(shared);
    ThermalGovernor cast(shared);
    stage.setAdaptive(true);
    cast.setAdaptive(true);
    stage.setPacedFps(60.0f);
    cast.setPacedFps(30.0f);
    for (int i = 0; i < 60 * 20; ++i) {  // 20 s: the stage every 60th of a second, Cast every other one
        stage.onFrame(1.0f / 60.0f);
        if (i % 2 == 0) cast.onFrame(1.0f / 30.0f);
    }
    check(shared.raises() == 0, "a 30 fps surface and a 60 fps one, both keeping up, raise nothing");
    check(shared.measuredFloor() == ThermalTier::Full && shared.fpsCap() == 0.0f, "so no surface is capped");
    check(stage.pacedFps() == 60.0f && cast.pacedFps() == 30.0f, "and each keeps its own rate");

    ThermalState mislabelled;
    mislabelled.push(0, NAN, 100.0);
    ThermalGovernor castAsStage(mislabelled);
    castAsStage.setAdaptive(true);
    castAsStage.setPacedFps(60.0f);  // the stage's rate on a renderer that is driven at 30
    for (int i = 0; i < 30 * 10; ++i) castAsStage.onFrame(1.0f / 30.0f);
    check(mislabelled.measuredFloor() == ThermalTier::Reduced, "read against another surface's 60, the same frames step down");
    check(mislabelled.fpsCap() == 30.0f, "which is the cap the stage then drew under");
}

void measuredFloorIsShared() {
    // A phone without a thermal API: frame time is the only signal.
    ThermalState shared;
    shared.push(-1, NAN, 100.0);
    ThermalGovernor busy(shared);
    ThermalGovernor idle(shared);
    busy.setPacedFps(60.0f);
    for (int i = 0; i < 200; ++i) busy.onFrame(0.05f);  // 20 fps for 10 s
    check(shared.measuredFloor() > ThermalTier::Full, "a sustained deficit raises the floor");
    check(shared.measuredFloor() <= ThermalState::kMeasuredCeiling, "never past the measured ceiling");
    check(idle.tier() == busy.tier(), "the other surface reads the raised floor");
    check(shared.fpsCap() == 30.0f, "and the pacer caps every surface");
    // A cheap surface keeping up cannot undo the busy one's raise while the busy one keeps raising.
    idle.setAdaptive(true);
    const ThermalTier raised = shared.tier();
    for (int i = 0; i < 60 * 30; ++i) {
        idle.onFrame(1.0f / 30.0f);
        if (i % 30 == 0) shared.raiseMeasuredFloor(0);  // the busy surface still struggling
    }
    check(shared.tier() == raised, "a fresh raise anywhere restarts the wait to step down");
    // With nobody raising, the adaptive surface steps the floor down after its long kept-up run.
    for (int i = 0; i < 50 * 30; ++i) idle.onFrame(1.0f / 30.0f);
    check(shared.tier() < raised, "a long kept-up run steps the floor back down");
}

// ---- Mutation tests (#548). Each names the mutant of the PR's code it kills. Times are the injected
// clock in seconds; expected tiers and times are worked by hand from the PR's rules: hotter after 3 s of
// readings, cooler one rung per 60 s, a real headroom trusted 25 s, a gap counted at most 2 s.

// A ThermalState climbed to Reduced by three readings of headroom 0.9 at t = 100, 101, 102.
void climbToReducedByHeadroom(ThermalState& s) { pushFor(s, 0, 0.9f, 100.0, 3); }

// Kills: `>=` -> `>` at each headroom threshold (0.75, 0.85, 0.95), a forecast of 1.0 not at Minimal, and
// a reordered status ladder.
void headroomThresholdsAreInclusive() {
    check(ThermalState::headroomTier(0.75f) == ThermalTier::Eased, "headroom exactly 0.75 eases");
    check(ThermalState::headroomTier(std::nextafter(0.75f, 0.0f)) == ThermalTier::Full, "just under 0.75 is Full");
    check(ThermalState::headroomTier(0.85f) == ThermalTier::Reduced, "headroom exactly 0.85 is Reduced");
    check(ThermalState::headroomTier(std::nextafter(0.85f, 0.0f)) == ThermalTier::Eased, "just under 0.85 is Eased");
    check(ThermalState::headroomTier(0.95f) == ThermalTier::Minimal, "headroom exactly 0.95 is Minimal");
    check(ThermalState::headroomTier(std::nextafter(0.95f, 0.0f)) == ThermalTier::Reduced, "just under 0.95 is Reduced");
    // Android throttles at a forecast of 1.0 (SEVERE); by then the ladder is on its last rung.
    check(ThermalState::headroomTier(1.0f) == ThermalTier::Minimal, "a forecast of 1.0 is Minimal");
    // PowerManager.THERMAL_STATUS_NONE 0, LIGHT 1, SEVERE 3.
    check(ThermalState::statusTier(0) == ThermalTier::Full, "THERMAL_STATUS_NONE is Full");
    check(ThermalState::statusTier(1) == ThermalTier::Eased, "THERMAL_STATUS_LIGHT eases");
    check(ThermalState::statusTier(3) == ThermalTier::Minimal, "THERMAL_STATUS_SEVERE is Minimal");
}

// Kills: `status >= 0` -> `status > 0` in push (a cool phone with a thermal API read as one without), and
// a dropped platformKnows() or `relief < kSevereRelief` in the High rule of trackMeasuredTrend.
void aCoolPhoneWithAThermalApiIgnoresAMildDeficit() {
    ThermalState shared;
    shared.push(0, NAN, 100.0);  // THERMAL_STATUS_NONE: the platform answers, and the phone is cool
    check(shared.platformKnows(), "THERMAL_STATUS_NONE is a real answer");
    ThermalGovernor heavyStyle(shared);
    heavyStyle.setPacedFps(60.0f);
    for (int i = 0; i < 400; ++i) heavyStyle.onFrame(0.025f);  // 40 fps for 10 s: a heavy style, not heat
    check(shared.measuredFloor() == ThermalTier::Full, "without Adapt, 40 fps under a 60 cap keeps Full");
    check(heavyStyle.pacedFps() == 60.0f, "and its own 60 fps cap");
}

// The QA trace's phone: the High preset (Adapt off), a marched style at 20 fps under a 60 fps cap.
// Kills: the High rule swallowing a severe deficit, a step back up refused without Adapt, a mild deficit
// ignored once the floor is raised, and a failed probe not doubling the next wait. Worked by hand: the
// monitor needs half its 30-frame window, then 2.5 s of deficit; a step up waits 45 s of kept-up frames.
void withoutAdaptFramesFarUnderTheCapStepDown() {
    ThermalState shared;
    shared.push(0, NAN, 100.0);
    ThermalGovernor g(shared);
    g.setPacedFps(60.0f);
    for (int i = 0; i < 200; ++i) g.onFrame(0.05f);  // 20 fps for 10 s
    check(shared.measuredFloor() == ThermalTier::Reduced, "20 fps under a 60 cap steps down two rungs, Adapt or not");
    check(g.pacedFps() == 30.0f, "and the surface draws at the tier's 30");
    for (int i = 0; i < 50 * 30; ++i) g.onFrame(1.0f / 30.0f);  // keeping up at 30 for 50 s
    check(shared.measuredFloor() == ThermalTier::Eased, "a kept-up run steps back up one rung without Adapt too");
    for (int i = 0; i < 5 * 24; ++i) g.onFrame(1.0f / 24.0f);  // 24 fps under the 30 cap for 5 s
    check(shared.measuredFloor() == ThermalTier::Reduced, "once stepped down, not keeping up counts");
    for (int i = 0; i < 50 * 30; ++i) g.onFrame(1.0f / 30.0f);
    check(shared.measuredFloor() == ThermalTier::Reduced, "a step up that did not hold waits twice as long");
    for (int i = 0; i < 50 * 30; ++i) g.onFrame(1.0f / 30.0f);
    check(shared.measuredFloor() == ThermalTier::Eased, "and then tries again");
}

// Kills: `std::isfinite(headroom)` -> `!std::isnan(headroom)` (an infinite forecast read as Minimal).
void anInfiniteForecastIsNoForecast() {
    ThermalState s;
    pushFor(s, 0, INFINITY, 100.0, 5);
    check(s.tier() == ThermalTier::Full, "an infinite headroom is no forecast");
}

// Kills: dropping `headroom >= 0.0f` (a negative "no forecast" taken as a cool reading).
void aNegativeForecastIsAMissedWindow() {
    ThermalState s;
    climbToReducedByHeadroom(s);
    // Last real reading at 102, trusted to 127; cool readings count from 128, so 103..162 is only 35 s.
    pushFor(s, 0, -1.0f, 103.0, 60);
    check(s.tier() == ThermalTier::Reduced, "a negative headroom keeps the last real one, like NaN");
}

// Kills: `headroom >= 0.0f` -> `headroom > 0.0f` (a real 0.0, a cold phone, read as no forecast).
void aZeroForecastIsARealCoolReading() {
    ThermalState s;
    climbToReducedByHeadroom(s);
    pushFor(s, 0, 0.0f, 103.0, 59);  // 103..161: 59 s of cool
    check(s.tier() == ThermalTier::Reduced, "59 s of headroom 0 is not yet a minute");
    s.push(0, 0.0f, 162.0);
    check(s.tier() == ThermalTier::Eased, "a minute of headroom 0 relaxes one rung");
}

// Kills: the stale rule's `>` -> `>=`, kHeadroomStaleSeconds 25 -> 24 or 26, and the relax dwell's
// `>=` -> `>`. By hand: last real 102, trusted through 127, cool from 128, the 60th cool reading at 187.
void aMissedWindowHoldsTheLastHeadroomTwoAsks() {
    ThermalState s;
    climbToReducedByHeadroom(s);
    pushFor(s, 0, NAN, 103.0, 84);  // 103..186
    check(s.tier() == ThermalTier::Reduced, "at 186 the phone has been trusted cool for only 59 s");
    s.push(0, NAN, 187.0);
    check(s.tier() == ThermalTier::Eased, "at 187 it has been a minute: one rung down");
}

// Kills: not resetting the hot dwell after a climb (the next hotter tier would come at once).
void eachClimbWaitsItsOwnThreeSeconds() {
    ThermalState s;
    pushFor(s, 1, NAN, 100.0, 3);  // Eased at 102
    s.push(3, NAN, 103.0);
    check(s.tier() == ThermalTier::Eased, "one SEVERE reading right after easing does not jump to Minimal");
    s.push(3, NAN, 104.0);
    check(s.tier() == ThermalTier::Eased, "nor two");
    s.push(3, NAN, 105.0);
    check(s.tier() == ThermalTier::Minimal, "three do");
}

// Kills: the cool branch not resetting the hot dwell (a hot run broken by a cool reading would still climb).
void aCoolReadingBreaksAHotRun() {
    ThermalState s;
    pushFor(s, 1, NAN, 100.0, 3);  // Eased at 102
    s.push(3, NAN, 103.0);
    s.push(3, NAN, 104.0);
    s.push(0, NAN, 105.0);
    s.push(3, NAN, 106.0);
    check(s.tier() == ThermalTier::Eased, "hot, hot, cool, hot is not three hot readings in a row");
    pushFor(s, 3, NAN, 107.0, 2);
    check(s.tier() == ThermalTier::Minimal, "106, 107, 108 are");
}

// Kills: the hot branch not resetting the cool dwell, and the equal branch not resetting it (a phone that
// warms back up mid-minute would still relax on the old count).
void anyNonCoolReadingRestartsTheMinute() {
    for (const int interrupt : {2, 3}) {  // as hot as the settled tier, and hotter
        ThermalState s;
        pushFor(s, 2, NAN, 100.0, 3);   // MODERATE: Reduced at 102
        pushFor(s, 0, NAN, 103.0, 30);  // 30 s cool
        s.push(interrupt, NAN, 133.0);
        pushFor(s, 0, NAN, 134.0, 59);  // 134..192: 59 s cool
        check(s.tier() == ThermalTier::Reduced, "a warm reading mid-minute restarts the cool wait");
        s.push(0, NAN, 193.0);
        check(s.tier() == ThermalTier::Eased, "a full minute after the warm reading it relaxes");
    }
}

// Kills: dropping the step clamp, raising kMaxStepSeconds past 2 s, or lowering it under 2 s.
void aGapBetweenReadingsCountsAtMostTwoSeconds() {
    ThermalState late;
    late.push(0, NAN, 100.0);
    late.push(1, NAN, 102.9);   // a 2.9 s gap counts 2 s
    late.push(1, NAN, 103.85);  // + 0.95 s = 2.95 s, short of 3
    check(late.tier() == ThermalTier::Full, "a long gap is not more than 2 s of watching");
    ThermalState sparse;
    sparse.push(0, NAN, 200.0);
    sparse.push(1, NAN, 202.0);  // 2 s, all of it counts
    sparse.push(1, NAN, 203.0);  // + 1 s = 3 s
    check(sparse.tier() == ThermalTier::Eased, "readings 2 s apart still count in full");
}

// Kills: any dwell cut to a reading or two, so a headroom hovering at the Eased line flips the frame rate
// back and forth. Seeded: the same ten minutes every run.
void aPhoneAtTheEdgeDoesNotFlap() {
    ThermalState s;
    uint32_t seed = 0x440u;
    ThermalTier last = s.tier();
    int changes = 0;
    for (int i = 0; i < 600; ++i) {
        seed = seed * 1664525u + 1013904223u;
        const float jitter = (static_cast<float>(seed >> 8) / 16777216.0f - 0.5f) * 0.02f;  // +-0.01
        s.push(0, 0.75f + jitter, 100.0 + i);
        if (s.tier() != last) {
            changes++;
            last = s.tier();
        }
    }
    check(changes <= 1, "ten minutes at the edge change the tier at most once");
}

// Feeds `vsyncs` callbacks of a 120 Hz panel to a 60 fps surface under the state's thermal cap, from
// vsync `index` on; returns how many were drawn.
int pace120(frame::PacerState& p, int& index, int vsyncs, const ThermalState& s) {
    const double period = 1e9 / 120.0;
    int drawn = 0;
    for (int k = 0; k < vsyncs; ++k, ++index) {
        const auto t = 1'000'000'000 + static_cast<int64_t>(std::llround(period * index));
        if (frame::Pacer::onVsync(p, t, s.capFor(60.0f)) > 0.0) drawn++;
    }
    return drawn;
}

// Kills: a relax that never lifts the fps cap, and a relax past or short of Full from Eased.
void theCapLiftsWhenThePhoneCools() {
    ThermalState s;
    pushFor(s, 1, NAN, 100.0, 3);  // Eased at 102
    frame::PacerState p;
    int index = 0;
    pace120(p, index, 120, s);
    check(pace120(p, index, 120, s) == 30, "warm: 30 frames a second on a 120 Hz panel");
    pushFor(s, 0, NAN, 103.0, 60);  // a minute cool
    check(s.tier() == ThermalTier::Full, "a minute of cool from Eased is Full");
    pace120(p, index, 120, s);
    check(pace120(p, index, 120, s) == 60, "cool again: the surface's own 60");
    check(p.divisor == 2 && p.capMilliHz == 60'000, "120 Hz halves, and the shell is told 60 fps");
}

// Kills: dropping max(steps, 0) in raiseMeasuredFloor, dropping its min with the ceiling, and dropping
// max(floor - 1, 0) in lowerMeasuredFloor.
void theMeasuredFloorStaysOnTheLadder() {
    ThermalState s;
    s.raiseMeasuredFloor(1);
    s.raiseMeasuredFloor(-3);
    check(s.measuredFloor() == ThermalTier::Eased, "a raise never lowers the floor");
    s.raiseMeasuredFloor(3);
    check(s.measuredFloor() == ThermalTier::Reduced, "frame time alone stops at Reduced, keeping post FX");
    s.lowerMeasuredFloor();
    check(s.measuredFloor() == ThermalTier::Eased, "one step down is one rung");
    s.lowerMeasuredFloor();
    s.lowerMeasuredFloor();
    check(s.measuredFloor() == ThermalTier::Full, "and the floor bottoms out at Full");
    check(s.tier() == ThermalTier::Full && s.fpsCap() == 0.0f, "a bottomed-out floor caps nothing");
}

// Kills: `&&` -> `||` in setAdaptive (turning Adapt on again would clear the floor), a dropped clear, and a
// dropped or negated platformKnows() check there.
void adaptOffHandsTheTierBackToThePlatform() {
    ThermalState known;
    known.push(0, NAN, 100.0);
    ThermalGovernor g(known);
    g.setAdaptive(true);
    known.raiseMeasuredFloor(2);
    g.setAdaptive(true);
    check(known.tier() == ThermalTier::Reduced, "turning Adapt on again keeps the measured floor");
    g.setAdaptive(false);
    check(known.tier() == ThermalTier::Full, "turning it off leaves the cool platform's Full");

    ThermalState unknown;
    unknown.push(-1, NAN, 100.0);
    ThermalGovernor old(unknown);
    old.setAdaptive(true);
    unknown.raiseMeasuredFloor(2);
    old.setAdaptive(false);
    check(unknown.tier() == ThermalTier::Reduced, "without a thermal API frame time is the only signal: kept");
}

// Kills: dropping `!adaptive_` from the step-down guard (without Adapt the floor never comes down).
void withoutAdaptTheFloorNeverStepsDown() {
    ThermalState shared;
    shared.push(-1, NAN, 100.0);
    ThermalGovernor g(shared);
    g.setPacedFps(60.0f);
    for (int i = 0; i < 200; ++i) g.onFrame(0.05f);  // 20 fps for 10 s
    const ThermalTier raised = shared.measuredFloor();
    check(raised > ThermalTier::Full, "a sustained deficit raised the floor");
    for (int i = 0; i < 120 * 60; ++i) g.onFrame(1.0f / 60.0f);  // two minutes keeping up
    check(shared.measuredFloor() == raised, "without Adapt the floor never steps back down");
}

// Kills: kMeasuredRelaxSeconds well under or over 45 s, and a step down of more than one rung.
void adaptStepsDownOneRungAfterAKeptUpRun() {
    ThermalState shared;
    shared.push(-1, NAN, 100.0);
    ThermalGovernor g(shared);
    g.setPacedFps(30.0f);
    g.setAdaptive(true);
    shared.raiseMeasuredFloor(2);  // another surface struggled: Reduced
    for (int i = 0; i < 44 * 30; ++i) g.onFrame(1.0f / 30.0f);
    check(shared.measuredFloor() == ThermalTier::Reduced, "44 s of keeping up is not yet enough");
    for (int i = 0; i < 6 * 30; ++i) g.onFrame(1.0f / 30.0f);
    check(shared.measuredFloor() == ThermalTier::Eased, "by 50 s it steps down, one rung only");
}

// Kills: dropping the export's early return in ThermalGovernor::onFrame (an export's slow frames, drawn as
// fast as the encoder takes them, would cap the live surfaces).
void anExportsFrameTimesNeverRaiseTheFloor() {
    ThermalState shared;
    shared.push(-1, NAN, 100.0);
    ThermalGovernor exporter(shared);
    exporter.beginOffscreenRender();
    for (int i = 0; i < 200; ++i) exporter.onFrame(0.05f);
    check(shared.measuredFloor() == ThermalTier::Full, "an export's frame times are not a deficit");
}

// Kills: endOffscreenRender going below zero (an unmatched end would cancel the next export's pin to Full).
void anUnmatchedExportEndDoesNotUnpinTheNext() {
    ThermalState shared;
    pushFor(shared, 3, NAN, 100.0, 3);  // Minimal
    ThermalGovernor g(shared);
    g.endOffscreenRender();
    g.beginOffscreenRender();
    check(g.tier() == ThermalTier::Full, "an export still renders at Full");
}

// Kills: the measured fallback watching the surface's own cap instead of the effective one (a phone drawing
// at the thermal 30 would read as failing 60 and raise the floor).
void theThermalCapIsNotADeficit() {
    ThermalState shared;
    pushFor(shared, 1, NAN, 100.0, 3);  // LIGHT: Eased, 30 fps
    ThermalGovernor g(shared);
    g.setPacedFps(60.0f);
    g.setAdaptive(true);
    for (int i = 0; i < 300; ++i) g.onFrame(1.0f / 30.0f);  // 10 s at the thermal 30
    check(shared.measuredFloor() == ThermalTier::Full, "30 fps under a 30 fps thermal cap is keeping up");
    check(g.tier() == ThermalTier::Eased, "the tier stays the platform's");
}

// The owner's report: the app stays slow after quick style changes. Stepping styles is slow frames because
// scenes build (blocking compiles, dt clamped to 0.1 s, so every stall counts) and blend two scenes at once,
// not because the phone cannot draw. Read as a deficit, 3 s of it at 25 fps raised the process-wide floor two
// rungs (30 fps, 0.75 scale, one spare), and each rung took 45 s of kept-up frames to come back.
void aBurstOfStyleChangesUsedToRaiseTheFloor() {
    ThermalState shared;
    shared.push(0, NAN, 100.0);
    ThermalGovernor g(shared);
    g.setPacedFps(60.0f);
    for (int i = 0; i < 4 * 25; ++i) g.onFrame(0.04f);  // 4 s of builds and blends, no settling noted
    check(shared.measuredFloor() == ThermalTier::Reduced, "unmarked, a burst of style changes reads as a severe deficit");
    for (int i = 0; i < 80 * 60; ++i) g.onFrame(1.0f / 60.0f);
    check(shared.measuredFloor() != ThermalTier::Full, "and 80 s of smooth frames later the phone is still capped");
}

// Kills: onFrame measuring while settling, the hold ending with the last switch (the new style's first frames
// load textures and presets), and settling hiding a style that stays slow once it is built.
void aBurstOfStyleChangesIsNotADeficit() {
    ThermalState shared;
    shared.push(0, NAN, 100.0);
    ThermalGovernor g(shared);
    g.setPacedFps(60.0f);
    g.setAdaptive(true);
    for (int i = 0; i < 6 * 25; ++i) {
        g.noteSettling();
        g.onFrame(0.04f);  // 6 s of style changes at 25 fps
    }
    check(shared.measuredFloor() == ThermalTier::Full, "frames slow from building and blending styles keep Full");
    for (int i = 0; i < 20; ++i) g.onFrame(0.04f);
    check(g.settling(), "the hold outlasts the last switch by more than 20 frames");
    check(shared.measuredFloor() == ThermalTier::Full, "and the new style's first frames are not a deficit either");
    for (int i = 0; i < 60; ++i) g.onFrame(1.0f / 60.0f);
    check(!g.settling(), "the hold ends within a second or two");
    for (int i = 0; i < 200; ++i) g.onFrame(0.05f);  // the new style itself draws at 20 fps
    check(shared.measuredFloor() == ThermalTier::Reduced, "a style that stays slow once built still steps down");
}

// Kills: GEODE_PACER_STATE_LONGS left at 6 (the pacer writes past the shell's array) or GEODE_PACER_CAP_SLOT
// naming another slot (the shell would re-ask setFrameRate from the wrong number).
void theShellsPacerArrayHoldsTheCap() {
    ThermalState s;
    pushFor(s, 1, NAN, 100.0, 3);  // Eased
    frame::PacerState p;
    int index = 0;
    pace120(p, index, 120, s);
    constexpr int64_t kSentinel = -12345;
    int64_t wire[GEODE_PACER_STATE_LONGS + 1];
    for (auto& w : wire) w = kSentinel;
    frame::pack(p, wire);
    check(wire[GEODE_PACER_STATE_LONGS] == kSentinel, "the pacer state fits the array the shell allocates");
    check(wire[GEODE_PACER_CAP_SLOT] == 30'000, "the shell reads 30 fps from the cap slot");
    for (int i = 0; i < GEODE_PACER_STATE_LONGS; ++i) {
        check(wire[i] != kSentinel, "every slot of the shell's array is the pacer's");
    }
}

// Kills: a per-thread or per-renderer state behind ThermalState::process() (#387: the wallpaper read its
// own copy and stayed at Full). Uses the real process state, so main runs it last.
void everySurfaceInTheProcessReadsOneState() {
    ThermalGovernor live;  // the process's state, the default
    ThermalTier wallpaperTier = ThermalTier::Full;
    float wallpaperCap = 0.0f;
    pushFor(ThermalState::process(), 3, NAN, 1000.0, 3);  // only the live surface's thread pushes
    std::thread wallpaperGl([&] {
        ThermalGovernor wallpaper;  // built on its own GL thread
        wallpaper.setPacedFps(60.0f);
        wallpaperTier = wallpaper.tier();
        wallpaperCap = wallpaper.pacedFps();
    });
    wallpaperGl.join();
    check(live.tier() == ThermalTier::Minimal, "the pushing surface is at Minimal");
    check(wallpaperTier == ThermalTier::Minimal, "a surface on another thread reads the same tier");
    check(wallpaperCap == 30.0f, "and draws at the thermal 30");
}

}  // namespace

int main() {
    ladderIsFpsFirst();
    dwellBothWays();
    headroomAndNaN();
    cadenceOncePerSecond();
    headroomIsAskedEveryTenSeconds();
    capReachesThePacer();
    twoRenderersShareOneState();
    eachSurfaceIsReadAgainstItsOwnPacedRate();
    measuredFloorIsShared();
    headroomThresholdsAreInclusive();
    aCoolPhoneWithAThermalApiIgnoresAMildDeficit();
    withoutAdaptFramesFarUnderTheCapStepDown();
    anInfiniteForecastIsNoForecast();
    aNegativeForecastIsAMissedWindow();
    aZeroForecastIsARealCoolReading();
    aMissedWindowHoldsTheLastHeadroomTwoAsks();
    eachClimbWaitsItsOwnThreeSeconds();
    aCoolReadingBreaksAHotRun();
    anyNonCoolReadingRestartsTheMinute();
    aGapBetweenReadingsCountsAtMostTwoSeconds();
    aPhoneAtTheEdgeDoesNotFlap();
    theCapLiftsWhenThePhoneCools();
    theMeasuredFloorStaysOnTheLadder();
    adaptOffHandsTheTierBackToThePlatform();
    withoutAdaptTheFloorNeverStepsDown();
    adaptStepsDownOneRungAfterAKeptUpRun();
    anExportsFrameTimesNeverRaiseTheFloor();
    anUnmatchedExportEndDoesNotUnpinTheNext();
    theThermalCapIsNotADeficit();
    aBurstOfStyleChangesUsedToRaiseTheFloor();
    aBurstOfStyleChangesIsNotADeficit();
    theShellsPacerArrayHoldsTheCap();
    everySurfaceInTheProcessReadsOneState();  // last: it uses the real process state
    if (failures == 0) std::printf("thermal_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
