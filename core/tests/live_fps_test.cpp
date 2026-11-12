// Host-side checks for core/viz/frame/LiveFps (owner decision of 2026-11-12, "Offer 90 and 120 in
// Performance"): which live frame-rate caps Settings › Performance offers on a display, how a stored cap
// snaps to that display, and how a hot phone walks a cap down a rung per thermal tier (ADR 0008: fps
// first). Built and run by CI ("Native host tests" in .github/workflows/native-host-tests.yml) with the
// system compiler; no GL.
//
// What is pinned: 30 and 60 on every display and 60 the default; 90 and 120 only where a refresh rate the
// display can switch to shows them evenly (itself or a whole multiple, within a hertz); a stored cap
// snapped to the nearest offered, the lower on a tie; the thermal walk 120 -> 90 -> 60 -> 30, a 60 cap's
// old drop to 30 unchanged, nothing under the floor touched; and the pacer drawing what the walk asks on
// 120 and 90 Hz panels.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "viz/frame/LiveFps.hpp"
#include "viz/frame/Pacer.hpp"
#include "viz/frame/Quality.hpp"

namespace frame = geode::viz::frame;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

std::vector<int> choices(const std::vector<float>& rates) {
    int out[8] = {};
    const int n = frame::liveFpsChoices(rates.empty() ? nullptr : rates.data(), static_cast<int>(rates.size()), out, 8);
    return std::vector<int>(out, out + n);
}

void sixtyIsAlwaysOffered() {
    check(choices({}) == std::vector<int>({30, 60}), "an unknown display offers 30 and 60");
    check(frame::liveFpsChoices(nullptr, 3, nullptr, 0) == 2, "a null rate list is an unknown display, and counting needs no array");
    check(choices({60.0f}) == std::vector<int>({30, 60}), "a 60 Hz phone offers 30 and 60 only");
    check(choices({48.0f}) == std::vector<int>({30, 60}), "even a panel under 60 keeps 60: the pacer draws every vsync");
    check(frame::kLiveFpsDefault == 60, "the default stays 60");
    // Every Settings › Performance tier is a cap every display offers, so picking a tier never shows a value
    // the tab cannot.
    for (const frame::QualityTier& t : frame::kQualityTiers) {
        check(frame::liveFpsOffered(static_cast<int>(t.fpsCap), nullptr, 0), "every tier's cap is offered everywhere");
    }
}

void highRatesOnlyWhereTheDisplayShowsThem() {
    check(choices({60.0f, 90.0f, 120.0f}) == std::vector<int>({30, 60, 90, 120}), "a 60/90/120 Hz panel offers all four");
    check(choices({60.0f, 120.0f}) == std::vector<int>({30, 60, 120}), "a 60/120 Hz panel has no 90: 120 does not divide to it");
    check(choices({60.0f, 90.0f}) == std::vector<int>({30, 60, 90}), "a 90 Hz phone offers 90, not 120");
    check(choices({144.0f}) == std::vector<int>({30, 60}), "144 Hz alone divides to neither 90 nor 120 evenly");
    check(choices({144.0f, 120.0f, 60.0f}) == std::vector<int>({30, 60, 120}), "a 144 Hz panel with a 120 mode offers 120");
    check(choices({240.0f}) == std::vector<int>({30, 60, 120}), "240 Hz shows 120 every second vsync");
    check(choices({180.0f}) == std::vector<int>({30, 60, 90}), "180 Hz shows 90 every second vsync, not 120");
    check(choices({59.94f, 119.88f}) == std::vector<int>({30, 60, 120}), "a 119.88 Hz mode is a 120 Hz mode");
    check(choices({89.5f}) == std::vector<int>({30, 60, 90}), "half a hertz off is still 90");
    check(choices({88.0f}) == std::vector<int>({30, 60}), "two hertz off is not");
    check(choices({NAN, INFINITY, -120.0f, 0.0f}) == std::vector<int>({30, 60}), "nonsense rates offer nothing more");
    check(choices({1200.0f}) == std::vector<int>({30, 60}), "a rate past eight divides is not read as 120");
    // The array is filled up to its capacity; the count is the whole list.
    const float rates[] = {120.0f, 90.0f, 60.0f};
    int two[2] = {0, 0};
    check(frame::liveFpsChoices(rates, 3, two, 2) == 4 && two[0] == 30 && two[1] == 60, "a short array gets the first, the count all");
    check(!frame::liveFpsOffered(75, rates, 3), "a rate off the ladder is never offered");
}

void aStoredCapSnapsToTheDisplay() {
    const float fast[] = {60.0f, 90.0f, 120.0f};
    const float ninety[] = {60.0f, 90.0f};
    const float sixty[] = {60.0f};
    check(frame::liveFpsCap(120, fast, 3) == 120, "an offered cap stays");
    check(frame::liveFpsCap(90, fast, 3) == 90 && frame::liveFpsCap(30, fast, 3) == 30, "every offered cap stays");
    check(frame::liveFpsCap(120, ninety, 2) == 90, "120 on a 90 Hz panel is 90");
    check(frame::liveFpsCap(120, sixty, 1) == 60, "120 on a 60 Hz panel (a foldable's outer screen) is 60");
    check(frame::liveFpsCap(90, sixty, 1) == 60, "90 on a 60 Hz panel is 60");
    check(frame::liveFpsCap(120, nullptr, 0) == 60, "an unknown display runs a high cap at 60");
    check(frame::liveFpsCap(0, fast, 3) == 60 && frame::liveFpsCap(-5, fast, 3) == 60, "nothing stored is the default");
    check(frame::liveFpsCap(45, fast, 3) == 30, "halfway between two caps takes the lower");
    check(frame::liveFpsCap(75, fast, 3) == 60, "75 is as near 60 as 90: the lower");
    check(frame::liveFpsCap(100, fast, 3) == 90, "100 is nearest 90");
    check(frame::liveFpsCap(1000, fast, 3) == 120 && frame::liveFpsCap(1, fast, 3) == 30, "far out of range clamps to the ends");
}

constexpr float kCaps[] = {120.0f, 90.0f, 60.0f, 45.0f, 30.0f, 24.0f, 15.0f};

void aHotPhoneWalksTheCapDownARungATier() {
    const float walk120[] = {120.0f, 90.0f, 60.0f, 30.0f};
    const float walk90[] = {90.0f, 60.0f, 30.0f, 30.0f};
    const float walk60[] = {60.0f, 30.0f, 30.0f, 30.0f};
    for (int t = 0; t < 4; ++t) {
        check(frame::thermalStepCapFps(120.0f, t) == walk120[t], "120 walks to 90, 60, 30");
        check(frame::thermalStepCapFps(90.0f, t) == walk90[t], "90 walks to 60, 30");
        check(frame::thermalStepCapFps(60.0f, t) == walk60[t], "60 drops to 30 when warm, as before");
        check(frame::thermalStepCapFps(30.0f, t) == 30.0f, "30 is the floor");
        check(frame::thermalStepCapFps(24.0f, t) == 24.0f && frame::thermalStepCapFps(15.0f, t) == 15.0f,
              "a cap under the floor is already cheaper and stays");
    }
    check(frame::thermalStepCapFps(0.0f, 0) == 0.0f && frame::thermalStepCapFps(NAN, 0) == 0.0f, "a cool free-running surface stays free");
    check(frame::thermalStepCapFps(0.0f, 1) == 30.0f && frame::thermalStepCapFps(-60.0f, 2) == 30.0f &&
              frame::thermalStepCapFps(INFINITY, 3) == 30.0f,
          "a warm free-running surface takes the floor");
    check(frame::thermalStepCapFps(45.0f, 1) == 30.0f, "a cap between rungs steps to the rung under it");
    check(frame::thermalStepCapFps(100.0f, 1) == 90.0f && frame::thermalStepCapFps(100.0f, 2) == 60.0f, "and on down");
    check(frame::thermalStepCapFps(120.0f, 9) == 30.0f, "past the last tier it holds the floor");
    // fps first: above the floor every hotter tier costs frames until the floor, and nothing ever rises.
    for (const float cap : kCaps) {
        float cooler = frame::thermalStepCapFps(cap, 0);
        check(cooler == cap, "Full leaves the cap");
        for (int t = 1; t < 4; ++t) {
            const float hotter = frame::thermalStepCapFps(cap, t);
            check(hotter <= cooler, "a hotter tier never raises the rate");
            check(hotter < cooler || cooler <= frame::kThermalFloorFps, "a hotter tier drops the rate until the floor");
            check(hotter >= std::fmin(cap, frame::kThermalFloorFps), "never under the floor (or the cap, if lower)");
            cooler = hotter;
        }
    }
}

// Draws `seconds` of a panel at `panelHz` under `cap`; returns how many frames the second half drew per second.
int drawnPerSecond(float panelHz, float cap, int seconds = 2) {
    frame::PacerState p;
    const double period = 1e9 / panelHz;
    const int vsyncs = static_cast<int>(std::lround(panelHz * seconds));
    int drawn = 0;
    for (int i = 0; i < vsyncs; ++i) {
        const auto t = 1'000'000'000 + static_cast<int64_t>(std::llround(period * i));
        if (frame::Pacer::onVsync(p, t, cap) > 0.0 && i >= vsyncs / 2) drawn++;
    }
    return drawn * 2 / seconds;
}

void thePacerDrawsTheWalk() {
    check(drawnPerSecond(120.0f, 120.0f) == 120, "120 on a 120 Hz panel draws every vsync");
    check(drawnPerSecond(90.0f, 90.0f) == 90, "90 on a 90 Hz panel draws every vsync");
    // Eased: 90 asked for. A panel that switches to its 90 Hz mode draws 90; one held at 120 Hz cannot divide
    // to 90 evenly and draws the next whole divide, 60, which is still a step down.
    check(drawnPerSecond(90.0f, frame::thermalStepCapFps(120.0f, 1)) == 90, "warm on a 90 Hz mode: 90");
    check(drawnPerSecond(120.0f, frame::thermalStepCapFps(120.0f, 1)) == 60, "warm on a 120 Hz mode: 60");
    check(drawnPerSecond(120.0f, frame::thermalStepCapFps(120.0f, 2)) == 60, "Reduced: 60");
    check(drawnPerSecond(120.0f, frame::thermalStepCapFps(120.0f, 3)) == 30, "Minimal: 30");
    check(drawnPerSecond(60.0f, frame::thermalStepCapFps(120.0f, 0)) == 60, "a 120 cap the panel cannot reach draws the panel's 60");
}

}  // namespace

int main() {
    sixtyIsAlwaysOffered();
    highRatesOnlyWhereTheDisplayShowsThem();
    aStoredCapSnapsToTheDisplay();
    aHotPhoneWalksTheCapDownARungATier();
    thePacerDrawsTheWalk();
    if (failures == 0) std::printf("live_fps_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
