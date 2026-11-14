// Host-side checks for core/viz/PerformanceMonitor: the fluid scenes' frame-time monitor and a renderer's pace
// (SurfacePace). Built and run by CI ("Native host tests" in .github/workflows/native-host-tests.yml) with the
// system compiler; no GL.
//
// What is pinned: the paced rate a renderer reads is the lower of the pacer's rate and the surface's own cap, a
// Follow device surface reads the paced rate as it is (never -1), and nothing else steps it; the settling hold
// starts on a new surface and runs out within a couple of seconds; the monitor's target scales with a cap above
// 60 and sits under it at 60 and below; and the shell's pacer array holds the pacer's state.
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "api/geode_api.h"
#include "viz/PerformanceMonitor.hpp"
#include "viz/frame/Pacer.hpp"

using namespace geode::viz;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

void thePacedRateIsTheLowerOfPacedAndCap() {
    SurfacePace pace;
    check(pace.pacedFps() == 0.0f, "nothing said: free-running");
    pace.setPacedFps(60.0f);
    check(pace.pacedFps() == 60.0f, "one value is both, as Cast hands it");
    pace.setPacedFps(45.0f, 60.0f);
    check(pace.pacedFps() == 45.0f, "a 60 cap paced at 45 on a 90 Hz panel draws at 45");
    pace.setPacedFps(120.0f, 120.0f);
    check(pace.pacedFps() == 120.0f, "a 120 cap paced at 120 draws at 120: nothing steps it down");
    pace.setPacedFps(0.0f, 30.0f);
    check(pace.pacedFps() == 30.0f, "before the pacer says, the own cap is the rate");
    pace.setPacedFps(NAN, 0.0f);
    check(pace.pacedFps() == 0.0f, "no number and no cap is free-running");
}

// Follow device has no cap of its own (GEODE_LIVE_FPS_FOLLOW). Kills: the -1 read as a cap or as a rate
// (MilkDrop told -1 fps).
void followDeviceReadsThePacedRate() {
    SurfacePace pace;
    const float follow = static_cast<float>(GEODE_LIVE_FPS_FOLLOW);
    pace.setPacedFps(0.0f, follow);
    check(pace.pacedFps() == 0.0f, "before a vsync says, Follow device is free-running");
    pace.setPacedFps(144.0f, follow);
    check(pace.pacedFps() == 144.0f, "then the panel's rate as the pacer paced it");
}

// Kills: onSurfaceRecreated without the settling hold (a resume's first frames rebuild every program and texture,
// and read as a deficit by the fluid scenes' monitor), and a hold that never ends.
void aNewSurfaceStartsSettling() {
    SurfacePace pace;
    check(!pace.settling(), "a pace starts settled");
    pace.onSurfaceRecreated();
    check(pace.settling(), "a new surface settles");
    for (int i = 0; i < 20; ++i) pace.onFrame(1.0f / 60.0f);
    check(pace.settling(), "for more than 20 frames");
    for (int i = 0; i < 120; ++i) pace.onFrame(1.0f / 60.0f);
    check(!pace.settling(), "and the hold ends within a couple of seconds");
    pace.noteSettling();
    check(pace.settling(), "a style build settles again");
}

// Worked by hand: the monitor needs half its 30-frame window, then 2.5 s of deficit against its target, which is
// min(50, 0.85 x paced) at 60 and under, and 50 x paced / 60 above.
void theMonitorReadsAgainstThePacedRate() {
    PerformanceMonitor sixty;
    sixty.setPacedFps(60.0f);
    int relief = 0;
    for (int i = 0; i < 300; ++i) relief = sixty.onFrame(1.0f / 55.0f);
    check(relief == 0, "55 fps under a 60 cap keeps up (target 50)");
    PerformanceMonitor thirty;
    thirty.setPacedFps(30.0f);
    for (int i = 0; i < 300; ++i) relief = thirty.onFrame(1.0f / 30.0f);
    check(relief == 0 && thirty.keepingUp(), "30 fps under a 30 cap keeps up (target 25.5)");
    PerformanceMonitor fast;
    fast.setPacedFps(120.0f);
    for (int i = 0; i < 600; ++i) relief = fast.onFrame(1.0f / 90.0f);
    check(relief == PerformanceMonitor::kMildRelief, "90 fps under a 120 cap is a mild deficit (target 100)");
    PerformanceMonitor slow;
    slow.setPacedFps(60.0f);
    for (int i = 0; i < 300; ++i) relief = slow.onFrame(1.0f / 20.0f);
    check(relief == PerformanceMonitor::kSevereRelief, "20 fps under a 60 cap is a severe one");
}

// Kills: GEODE_PACER_STATE_LONGS left short of the pacer's state (the pacer writes past the shell's array) or
// GEODE_PACER_CAP_SLOT naming another slot (the shell would re-ask setFrameRate from the wrong number).
void theShellsPacerArrayHoldsTheCap() {
    frame::PacerState p;
    for (int i = 0; i < 120; ++i) frame::Pacer::onVsync(p, 1'000'000'000 + i * 8'333'333LL, 30.0f);
    constexpr int64_t kSentinel = -12345;
    int64_t wire[GEODE_PACER_STATE_LONGS + 1];
    for (auto& w : wire) w = kSentinel;
    frame::pack(p, wire);
    check(wire[GEODE_PACER_STATE_LONGS] == kSentinel, "the pacer state fits the array the shell allocates");
    check(wire[GEODE_PACER_CAP_SLOT] == 30'000, "the shell reads 30 fps from the cap slot");
    check(wire[GEODE_PACER_PACED_SLOT] == 30'000, "and the paced rate from the paced slot");
}

}  // namespace

int main() {
    thePacedRateIsTheLowerOfPacedAndCap();
    followDeviceReadsThePacedRate();
    aNewSurfaceStartsSettling();
    theMonitorReadsAgainstThePacedRate();
    theShellsPacerArrayHoldsTheCap();
    if (failures) {
        std::fprintf(stderr, "performance_monitor_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("performance_monitor_test: ok\n");
    return 0;
}
