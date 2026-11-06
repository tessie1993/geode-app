// Host-side checks for core/viz/frame/Thermal (#440): the process's one thermal state. Built and run by
// CI ("Native host tests" in .github/workflows/android.yml) with the system compiler; no GL.
//
// What is pinned: the owner's fps-first ladder (ADR 0008: fps, then render scale, then march, then post
// FX), the thermal cap reaching the pacer as min(surface cap, thermal cap), two renderers' governors
// reading one state (#387: the second used to read NaN headroom and stay at Full), the once-a-second
// cadence across threads, the NaN rule (a missed window keeps the last real headroom a while), the dwell
// both ways, and the measured fallback raising the floor for every surface.
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

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
    t = pushFor(s, 0, NAN, t, 62);
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

}  // namespace

int main() {
    ladderIsFpsFirst();
    dwellBothWays();
    headroomAndNaN();
    cadenceOncePerSecond();
    capReachesThePacer();
    twoRenderersShareOneState();
    measuredFloorIsShared();
    if (failures == 0) std::printf("thermal_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
