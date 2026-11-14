// Host-side checks for core/viz/frame/Thermal (#440): the process's one thermal state. Built and run by
// CI ("Native host tests" in .github/workflows/android.yml) with the system compiler; no GL.
//
// What is pinned: the owner's fps-first ladder (ADR 0008: fps, then render scale, then march, then post
// FX), the thermal cap reaching the pacer as min(surface cap, thermal cap) for a cap of 60 and under and as a
// walk down the rungs for a live 90 or 120 (core/viz/frame/LiveFps), the governor stepping the surface's own
// cap and not the rate already paced, two renderers' governors
// reading one state (#387: the second used to read NaN headroom and stay at Full), the once-per-process
// "Phone is warm" note (#994), the once-a-second
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
    s.push(2, NAN, 100.0);
    check(s.tier() == ThermalTier::Full, "one MODERATE reading does not drop the frame rate");
    pushFor(s, 2, NAN, 101.0, 8);  // 100..108: 9 s
    check(s.tier() == ThermalTier::Full, "nine seconds of MODERATE is short of the dwell");
    s.push(0, NAN, 109.0);
    pushFor(s, 2, NAN, 110.0, 9);  // 110..118: 9 s
    check(s.tier() == ThermalTier::Full, "a cool reading restarts the wait");
    s.push(2, NAN, 119.0);
    check(s.tier() == ThermalTier::Eased, "ten seconds of MODERATE in a row ease off");
    // CRITICAL escalates straight to Minimal, not a rung at a time.
    double t = pushFor(s, 4, NAN, 120.0, 10);
    check(s.tier() == ThermalTier::Minimal, "a critical phone goes straight to Minimal");
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
    double t = pushFor(s, 0, 0.95f, 100.0, 10);
    check(s.tier() == ThermalTier::Reduced, "headroom 0.95 is Reduced");
    // Missed windows (NaN) keep the last real headroom: no cool dwell starts.
    t = pushFor(s, 0, NAN, t, 9);
    check(s.tier() == ThermalTier::Reduced, "a NaN is a missed window, not a cool phone");
    // Past the stale limit the headroom is no longer trusted; the cool dwell then runs a minute.
    t = pushFor(s, 0, NAN, t, 77);
    check(s.tier() == ThermalTier::Eased, "a long run of NaN lets the tier relax, a rung a minute");
    check(ThermalState::headroomTier(NAN) == ThermalTier::Full, "no forecast reads Full");
    check(ThermalState::headroomTier(0.8f) == ThermalTier::Full, "0.8 is warm, not yet near throttling: Full");
    check(ThermalState::headroomTier(0.88f) == ThermalTier::Eased, "0.88 eases");
    check(ThermalState::headroomTier(1.2f) == ThermalTier::Minimal, "past 1.0 is Minimal");
    check(ThermalState::statusTier(-1) == ThermalTier::Full, "no thermal API reads Full");
    check(ThermalState::statusTier(2) == ThermalTier::Eased, "MODERATE eases");
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
    for (int i = 1; i <= 9; ++i) hot.headroomDue(200.0 + i);
    check(hot.tier() == ThermalTier::Full, "9 s of SEVERE is short of the dwell");
    check(hot.headroomDue(210.0), "the next ask is due at 210");
    hot.push(-1, NAN, 210.0);  // its answer: still no forecast
    check(hot.tier() == ThermalTier::Reduced, "the settles between asks carry the dwell to SEVERE's Reduced");
    // A headroom answered once holds across the settles until the next ask.
    ThermalState warm;
    warm.headroomDue(300.0);
    warm.push(-1, 0.95f, 300.0);
    for (int i = 1; i <= 9; ++i) warm.headroomDue(300.0 + i);
    check(warm.tier() == ThermalTier::Reduced, "one forecast of 0.95 holds for the ten seconds to the next ask");
    check(!warm.platformKnows(), "and a headroom alone is not a status");
}

void capReachesThePacer() {
    ThermalState s;
    check(s.capFor(60.0f) == 60.0f, "a cool phone leaves the surface's cap");
    check(s.capFor(0.0f) == 0.0f, "a cool free-running surface stays free");
    pushFor(s, 2, NAN, 100.0, 10);
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
    pushFor(shared, 0, 0.95f, 100.0, 10);
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
// clock in seconds; expected tiers and times are worked by hand from the PR's rules: hotter after 10 s of
// readings, cooler one rung per 60 s, a real headroom trusted 25 s, a gap counted at most 2 s.

// A ThermalState climbed to Reduced by ten readings of headroom 0.95 at t = 100..109.
void climbToReducedByHeadroom(ThermalState& s) { pushFor(s, 0, 0.95f, 100.0, 10); }

// Kills: `>=` -> `>` at each headroom threshold (0.85, 0.92, 0.98), a forecast of 1.0 not at Minimal, and
// a reordered status ladder.
void headroomThresholdsAreInclusive() {
    check(ThermalState::headroomTier(0.85f) == ThermalTier::Eased, "headroom exactly 0.85 eases");
    check(ThermalState::headroomTier(std::nextafter(0.85f, 0.0f)) == ThermalTier::Full, "just under 0.85 is Full");
    check(ThermalState::headroomTier(0.92f) == ThermalTier::Reduced, "headroom exactly 0.92 is Reduced");
    check(ThermalState::headroomTier(std::nextafter(0.92f, 0.0f)) == ThermalTier::Eased, "just under 0.92 is Eased");
    check(ThermalState::headroomTier(0.98f) == ThermalTier::Minimal, "headroom exactly 0.98 is Minimal");
    check(ThermalState::headroomTier(std::nextafter(0.98f, 0.0f)) == ThermalTier::Reduced, "just under 0.98 is Reduced");
    // Android throttles at a forecast of 1.0 (SEVERE); by then the ladder is on its last rung.
    check(ThermalState::headroomTier(1.0f) == ThermalTier::Minimal, "a forecast of 1.0 is Minimal");
    // AThermalStatus: NONE 0, LIGHT 1, MODERATE 2, SEVERE 3, CRITICAL 4, EMERGENCY 5, SHUTDOWN 6.
    check(ThermalState::statusTier(0) == ThermalTier::Full, "THERMAL_STATUS_NONE is Full");
    check(ThermalState::statusTier(1) == ThermalTier::Full, "THERMAL_STATUS_LIGHT is Full");
    check(ThermalState::statusTier(2) == ThermalTier::Eased, "THERMAL_STATUS_MODERATE eases");
    check(ThermalState::statusTier(3) == ThermalTier::Reduced, "THERMAL_STATUS_SEVERE is Reduced");
    check(ThermalState::statusTier(4) == ThermalTier::Minimal, "THERMAL_STATUS_CRITICAL is Minimal");
    check(ThermalState::statusTier(5) == ThermalTier::Minimal, "THERMAL_STATUS_EMERGENCY is Minimal");
}

// The owner's report: the app read the phone as warm too soon. Kills: LIGHT easing again, and an Eased
// threshold back under 0.85. A minute of either keeps the surface's own rate and shows no note.
void aMerelyWarmPhoneKeepsItsFrameRate() {
    ThermalState light;
    pushFor(light, 1, NAN, 100.0, 60);
    check(light.tier() == ThermalTier::Full && light.capFor(60.0f) == 60.0f, "a minute of LIGHT keeps Full and 60");
    check(light.takeWarmNote(60.0f, 30.0f) == 0, "and says nothing about heat");
    ThermalState warm;
    pushFor(warm, 0, std::nextafter(0.85f, 0.0f), 100.0, 60);
    check(warm.tier() == ThermalTier::Full && warm.capFor(120.0f) == 120.0f, "a minute just under 0.85 keeps Full");
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
    // Last real reading at 109, trusted to 134; cool readings count from 135, so 110..169 is only 35 s.
    pushFor(s, 0, -1.0f, 110.0, 60);
    check(s.tier() == ThermalTier::Reduced, "a negative headroom keeps the last real one, like NaN");
}

// Kills: `headroom >= 0.0f` -> `headroom > 0.0f` (a real 0.0, a cold phone, read as no forecast).
void aZeroForecastIsARealCoolReading() {
    ThermalState s;
    climbToReducedByHeadroom(s);
    pushFor(s, 0, 0.0f, 110.0, 59);  // 110..168: 59 s of cool
    check(s.tier() == ThermalTier::Reduced, "59 s of headroom 0 is not yet a minute");
    s.push(0, 0.0f, 169.0);
    check(s.tier() == ThermalTier::Eased, "a minute of headroom 0 relaxes one rung");
}

// Kills: the stale rule's `>` -> `>=`, kHeadroomStaleSeconds 25 -> 24 or 26, and the relax dwell's
// `>=` -> `>`. By hand: last real 109, trusted through 134, cool from 135, the 60th cool reading at 194.
void aMissedWindowHoldsTheLastHeadroomTwoAsks() {
    ThermalState s;
    climbToReducedByHeadroom(s);
    pushFor(s, 0, NAN, 110.0, 84);  // 110..193
    check(s.tier() == ThermalTier::Reduced, "at 193 the phone has been trusted cool for only 59 s");
    s.push(0, NAN, 194.0);
    check(s.tier() == ThermalTier::Eased, "at 194 it has been a minute: one rung down");
}

// Kills: not resetting the hot dwell after a climb (the next hotter tier would come at once).
void eachClimbWaitsItsOwnTenSeconds() {
    ThermalState s;
    pushFor(s, 2, NAN, 100.0, 10);  // MODERATE: Eased at 109
    s.push(4, NAN, 110.0);
    check(s.tier() == ThermalTier::Eased, "one CRITICAL reading right after easing does not jump to Minimal");
    pushFor(s, 4, NAN, 111.0, 8);  // 110..118: nine
    check(s.tier() == ThermalTier::Eased, "nor nine");
    s.push(4, NAN, 119.0);
    check(s.tier() == ThermalTier::Minimal, "ten do");
}

// Kills: the cool branch not resetting the hot dwell (a hot run broken by a cool reading would still climb).
void aCoolReadingBreaksAHotRun() {
    ThermalState s;
    pushFor(s, 2, NAN, 100.0, 10);  // Eased at 109
    pushFor(s, 4, NAN, 110.0, 9);   // 110..118
    s.push(0, NAN, 119.0);
    s.push(4, NAN, 120.0);
    check(s.tier() == ThermalTier::Eased, "nine hot, a cool, a hot is not ten hot readings in a row");
    pushFor(s, 4, NAN, 121.0, 9);
    check(s.tier() == ThermalTier::Minimal, "120 through 129 are");
}

// Kills: the hot branch not resetting the cool dwell, and the equal branch not resetting it (a phone that
// warms back up mid-minute would still relax on the old count).
void anyNonCoolReadingRestartsTheMinute() {
    for (const int interrupt : {3, 4}) {  // as hot as the settled tier, and hotter
        ThermalState s;
        pushFor(s, 3, NAN, 100.0, 10);  // SEVERE: Reduced at 109
        pushFor(s, 0, NAN, 110.0, 30);  // 30 s cool
        s.push(interrupt, NAN, 140.0);
        pushFor(s, 0, NAN, 141.0, 59);  // 141..199: 59 s cool
        check(s.tier() == ThermalTier::Reduced, "a warm reading mid-minute restarts the cool wait");
        s.push(0, NAN, 200.0);
        check(s.tier() == ThermalTier::Eased, "a full minute after the warm reading it relaxes");
    }
}

// Kills: dropping the step clamp, raising kMaxStepSeconds past 2 s, or lowering it under 2 s.
void aGapBetweenReadingsCountsAtMostTwoSeconds() {
    ThermalState late;
    late.push(0, NAN, 100.0);
    late.push(2, NAN, 102.9);                                  // a 2.9 s gap counts 2 s
    for (int i = 0; i < 7; ++i) late.push(2, NAN, 103.9 + i);  // + 7 s = 9 s
    late.push(2, NAN, 110.85);                                 // + 0.95 s = 9.95 s, short of 10
    check(late.tier() == ThermalTier::Full, "a long gap is not more than 2 s of watching");
    ThermalState sparse;
    sparse.push(0, NAN, 200.0);
    sparse.push(2, NAN, 202.0);         // 2 s, all of it counts
    pushFor(sparse, 2, NAN, 203.0, 8);  // + 8 s = 10 s
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
        s.push(0, 0.85f + jitter, 100.0 + i);
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
    pushFor(s, 2, NAN, 100.0, 10);  // Eased at 109
    frame::PacerState p;
    int index = 0;
    pace120(p, index, 120, s);
    check(pace120(p, index, 120, s) == 30, "warm: 30 frames a second on a 120 Hz panel");
    pushFor(s, 0, NAN, 110.0, 60);  // a minute cool
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

// The owner's report: the app slows down the longer it is used. Without a thermal API and without Adapt the
// floor used to only rise, so one slow stretch (a resume, a heavy style) capped the phone until it was killed.
// Kills: a step-up guard that again asks for Adapt or the platform. Worked by hand: 45 s of kept-up frames a
// rung, plus the monitor's 30-frame window after each step.
void withoutAdaptOrAPlatformTheFloorStillStepsBackUp() {
    ThermalState shared;
    shared.push(-1, NAN, 100.0);
    ThermalGovernor g(shared);
    g.setPacedFps(60.0f);
    for (int i = 0; i < 200; ++i) g.onFrame(0.05f);  // 20 fps for 10 s
    check(shared.measuredFloor() == ThermalTier::Reduced, "a sustained deficit raised the floor");
    for (int i = 0; i < 50 * 60; ++i) g.onFrame(1.0f / 60.0f);
    check(shared.measuredFloor() == ThermalTier::Eased, "50 s of keeping up steps it back one rung");
    for (int i = 0; i < 50 * 60; ++i) g.onFrame(1.0f / 60.0f);
    check(shared.measuredFloor() == ThermalTier::Full, "and 50 s more is full quality again");
}

// Kills: onSurfaceRecreated without the settling hold (a resume's first frames rebuild every program and
// texture, and read as a deficit they raised the floor for the whole process).
void aNewSurfaceStartsSettling() {
    ThermalState shared;
    shared.push(0, NAN, 100.0);
    ThermalGovernor g(shared);
    g.setPacedFps(60.0f);
    g.setAdaptive(true);
    g.onSurfaceRecreated();
    check(g.settling(), "a new surface starts with the settling hold");
    for (int i = 0; i < 70; ++i) g.onFrame(0.05f);  // 3.5 s at 20 fps: unheld, a raise at about 3.25 s
    check(shared.measuredFloor() == ThermalTier::Full, "so its first slow frames are not a deficit");
}

// Kills: onStyleChanged leaving the doubled wait (a light style after a heavy one stayed capped for up to
// 12 minutes on the waits the heavy one's failed step-ups earned). The same frames as
// withoutAdaptFramesFarUnderTheCapStepDown, which waits 90 s there without the change.
void aNewStyleForgetsTheOldStylesLongWait() {
    ThermalState shared;
    shared.push(0, NAN, 100.0);
    ThermalGovernor g(shared);
    g.setPacedFps(60.0f);
    for (int i = 0; i < 200; ++i) g.onFrame(0.05f);
    for (int i = 0; i < 50 * 30; ++i) g.onFrame(1.0f / 30.0f);
    for (int i = 0; i < 5 * 24; ++i) g.onFrame(1.0f / 24.0f);
    check(shared.measuredFloor() == ThermalTier::Reduced, "the heavy style's step up failed");
    g.onStyleChanged();
    for (int i = 0; i < 55 * 30; ++i) g.onFrame(1.0f / 30.0f);
    check(shared.measuredFloor() == ThermalTier::Eased, "the new style steps up after the usual 45 s, not 90");
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
    pushFor(shared, 4, NAN, 100.0, 10);  // CRITICAL: Minimal
    ThermalGovernor g(shared);
    g.endOffscreenRender();
    g.beginOffscreenRender();
    check(g.tier() == ThermalTier::Full, "an export still renders at Full");
}

// Kills: the measured fallback watching the surface's own cap instead of the effective one (a phone drawing
// at the thermal 30 would read as failing 60 and raise the floor).
void theThermalCapIsNotADeficit() {
    ThermalState shared;
    pushFor(shared, 2, NAN, 100.0, 10);  // MODERATE: Eased, 30 fps
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
    pushFor(s, 2, NAN, 100.0, 10);  // Eased
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

// The owner's "Offer 90 and 120 in Performance" (2026-11-12): a live cap above 60 walks down a rung per hot
// tier, 120 to 90, 60, 30, so fps still goes first. Kills: a hot tier that drops a 120 cap straight to 30
// (the old flat cap) or not at all, and a 60 cap that no longer drops to 30 when warm.
void aHighCapWalksDownTheLadder() {
    const float expected120[] = {120.0f, 90.0f, 60.0f, 30.0f};
    const float expected90[] = {90.0f, 60.0f, 30.0f, 30.0f};
    const int statuses[] = {0, 2, 3, 4};  // NONE, MODERATE, SEVERE, CRITICAL: Full .. Minimal
    for (int i = 0; i < 4; ++i) {
        ThermalState s;
        pushFor(s, statuses[i], NAN, 100.0, 10);
        check(s.tier() == kTiers[i], "ten seconds of readings settle on the status's tier");
        check(s.capFor(120.0f) == expected120[i], "a 120 cap walks 120, 90, 60, 30");
        check(s.capFor(90.0f) == expected90[i], "a 90 cap walks 90, 60, 30");
        check(s.capFor(60.0f) == (i == 0 ? 60.0f : 30.0f), "a 60 cap still drops to 30 when warm");
        check(s.capFor(0.0f) == (i == 0 ? 0.0f : 30.0f), "and a free-running surface to the floor");
    }
    // Each rung of the ladder is still smoothness first: the render scale only goes once fps has gone twice.
    check(thermalTierInfo(ThermalTier::Eased).fpsRungsDown == 1 && thermalTierInfo(ThermalTier::Eased).renderScale == 1.0f,
          "Eased costs one rung of fps and nothing else");
}

// The shell hands the rate the pacer last drew at (the thermal step already in it) beside the surface's own cap.
// Kills: the governor stepping the paced rate again (a 120 cap paced at the Eased 90 read as 60, so MilkDrop
// presets ran at the wrong rate and the measured fallback watched the wrong target).
void theGovernorStepsTheOwnCapNotThePacedRate() {
    ThermalState shared;
    pushFor(shared, 2, NAN, 100.0, 10);  // Eased
    ThermalGovernor g(shared);
    g.setPacedFps(90.0f, 120.0f);
    check(g.pacedFps() == 90.0f, "a 120 cap paced at the Eased 90 draws at 90, not 60");
    g.setPacedFps(60.0f, 120.0f);
    check(g.pacedFps() == 60.0f, "a 120 Hz panel that kept its mode divides the 90 down to 60, and 60 it reads");
    g.setPacedFps(120.0f, 120.0f);
    check(g.pacedFps() == 90.0f, "before the pacer says, the own cap's step caps the paced rate");
    g.setPacedFps(60.0f);
    check(g.pacedFps() == 30.0f, "one value is both, as Cast and the tests hand it: a 60 cap draws the warm 30");
    ThermalState cool;
    cool.push(0, NAN, 100.0);
    ThermalGovernor c(cool);
    c.setPacedFps(45.0f, 60.0f);
    check(c.pacedFps() == 45.0f, "cool, the pacer's 45 for a 60 cap on a 90 Hz panel stands");
}

// Follow device has no cap of its own (GEODE_LIVE_FPS_FOLLOW): the pacer stepped the panel rate it follows,
// so the governor reads the paced rate as it is. Kills: the -1 read as a cap (a warm phone's floor 30 under
// a stage drawing 60), or as a rate (MilkDrop told -1 fps).
void followDeviceReadsThePacedRate() {
    ThermalState shared;
    pushFor(shared, 2, NAN, 100.0, 10);  // Eased
    ThermalGovernor g(shared);
    const float follow = static_cast<float>(GEODE_LIVE_FPS_FOLLOW);
    g.setPacedFps(60.0f, follow);
    check(g.pacedFps() == 60.0f, "warm, Follow paced at 60 reads 60");
    g.setPacedFps(-1.0f, follow);
    check(g.pacedFps() == 30.0f, "before a vsync says, warm Follow reads as a warm free-running surface");
    ThermalState cool;
    cool.push(0, NAN, 100.0);
    ThermalGovernor c(cool);
    c.setPacedFps(144.0f, follow);
    check(c.pacedFps() == 144.0f, "cool, Follow on a 144 Hz panel reads 144");
    c.setPacedFps(-1.0f, follow);
    check(c.pacedFps() == 0.0f, "and before a vsync, free-running");
    check(frame::isLiveFpsFollow(follow) && !frame::isLiveFpsFollow(-2.0f), "only -1 means Follow");
}

// Kills: a frame-time target that stays at 50 above a 60 cap (a 120 cap drawn at 70 would never step down),
// and one that moved for caps of 60 and under. Worked by hand: under a 120 cap the target is 50 x 120 / 60 =
// 100, so 90 fps is a 10 % deficit, a mild one: with Adapt on it raises the floor a rung after 2.5 s, which
// steps the cap to 90, whose target (75) 90 fps meets.
void aHighCapThatCannotKeepUpStepsDownARung() {
    ThermalState shared;
    shared.push(0, NAN, 100.0);
    ThermalGovernor g(shared);
    g.setAdaptive(true);
    g.setPacedFps(120.0f, 120.0f);
    for (int i = 0; i < 90 * 5; ++i) g.onFrame(1.0f / 90.0f);  // 90 fps under a 120 cap for 5 s
    check(shared.measuredFloor() == ThermalTier::Eased, "90 fps under a 120 cap steps one rung");
    check(g.pacedFps() == 90.0f && shared.capFor(120.0f) == 90.0f, "and the cap walks to 90");
    for (int i = 0; i < 90 * 20; ++i) g.onFrame(1.0f / 90.0f);
    check(shared.measuredFloor() == ThermalTier::Eased, "90 fps under the 90 it now asks for keeps up");

    ThermalState sixty;
    sixty.push(0, NAN, 100.0);
    ThermalGovernor s(sixty);
    s.setAdaptive(true);
    s.setPacedFps(60.0f);
    for (int i = 0; i < 52 * 10; ++i) s.onFrame(1.0f / 52.0f);  // 52 fps under a 60 cap: over the old 50
    check(sixty.measuredFloor() == ThermalTier::Full, "a 60 cap's target is still 50");
}

// The Visuals stage's "Phone is warm" note (#994): once per process, only once heat has really cost frames.
void theWarmNoteShowsOnceWhenHeatFirstDropsTheRate() {
    ThermalState s;
    check(s.takeWarmNote(60.0f, 60.0f) == 0, "a cool phone shows no note");
    pushFor(s, 2, NAN, 100.0, 10);
    check(s.fpsRungsDown() == 1, "ten seconds of MODERATE ease off");
    check(s.takeWarmNote(60.0f, 60.0f) == 0, "not before the pacer has stepped down");
    check(s.takeWarmNote(30.0f, 30.0f) == 0, "a cap already at the floor lost nothing");
    check(s.takeWarmNote(60.0f, 0.0f) == 0, "nor while nothing has been paced");
    check(s.takeWarmNote(60.0f, 30.0f) == 30, "the first drop names the rate it runs at");
    check(s.takeWarmNote(60.0f, 30.0f) == 0, "and is said once");
    pushFor(s, 4, NAN, 110.0, 10);
    check(s.takeWarmNote(120.0f, 30.0f) == 0, "a hotter tier later says nothing more");

    ThermalState fast;
    pushFor(fast, 2, NAN, 100.0, 10);
    check(fast.takeWarmNote(120.0f, 120.0f) == 0, "a 120 cap still at 120 has not dropped yet");
    check(fast.takeWarmNote(120.0f, 90.0f) == 90, "120 stepped to 90 names 90");

    ThermalState follow;
    pushFor(follow, 2, NAN, 100.0, 10);
    check(follow.takeWarmNote(static_cast<float>(frame::kLiveFpsFollow), 60.0f) == 60, "Follow device names its paced rate");
}

// Kills: a per-thread or per-renderer state behind ThermalState::process() (#387: the wallpaper read its
// own copy and stayed at Full). Uses the real process state, so main runs it last.
void everySurfaceInTheProcessReadsOneState() {
    ThermalGovernor live;  // the process's state, the default
    ThermalTier wallpaperTier = ThermalTier::Full;
    float wallpaperCap = 0.0f;
    pushFor(ThermalState::process(), 4, NAN, 1000.0, 10);  // only the live surface's thread pushes
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
    aMerelyWarmPhoneKeepsItsFrameRate();
    aCoolPhoneWithAThermalApiIgnoresAMildDeficit();
    withoutAdaptFramesFarUnderTheCapStepDown();
    anInfiniteForecastIsNoForecast();
    aNegativeForecastIsAMissedWindow();
    aZeroForecastIsARealCoolReading();
    aMissedWindowHoldsTheLastHeadroomTwoAsks();
    eachClimbWaitsItsOwnTenSeconds();
    aCoolReadingBreaksAHotRun();
    anyNonCoolReadingRestartsTheMinute();
    aGapBetweenReadingsCountsAtMostTwoSeconds();
    aPhoneAtTheEdgeDoesNotFlap();
    theCapLiftsWhenThePhoneCools();
    theMeasuredFloorStaysOnTheLadder();
    adaptOffHandsTheTierBackToThePlatform();
    withoutAdaptOrAPlatformTheFloorStillStepsBackUp();
    aNewSurfaceStartsSettling();
    aNewStyleForgetsTheOldStylesLongWait();
    adaptStepsDownOneRungAfterAKeptUpRun();
    anExportsFrameTimesNeverRaiseTheFloor();
    anUnmatchedExportEndDoesNotUnpinTheNext();
    theThermalCapIsNotADeficit();
    aBurstOfStyleChangesUsedToRaiseTheFloor();
    aBurstOfStyleChangesIsNotADeficit();
    theShellsPacerArrayHoldsTheCap();
    aHighCapWalksDownTheLadder();
    theGovernorStepsTheOwnCapNotThePacedRate();
    followDeviceReadsThePacedRate();
    aHighCapThatCannotKeepUpStepsDownARung();
    theWarmNoteShowsOnceWhenHeatFirstDropsTheRate();
    everySurfaceInTheProcessReadsOneState();  // last: it uses the real process state
    if (failures == 0) std::printf("thermal_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
