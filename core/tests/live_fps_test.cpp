// Host-side checks for core/viz/frame/LiveFps (owner decision of 2026-11-12, "Offer 90 and 120 in
// Performance"): which live frame-rate caps Settings › Performance offers on a display, how a stored cap
// snaps to that display, and how a hot phone walks a cap down a rung per thermal tier (ADR 0008: fps
// first). Built and run by CI ("Native host tests" in .github/workflows/native-host-tests.yml) with the
// system compiler; no GL.
//
// What is pinned: 30 and 60 on every display and 60 the default; 90 and 120 only where a refresh rate the
// display can switch to shows them evenly (itself or a whole multiple, within a hertz); a stored cap
// snapped to the nearest offered, the lower on a tie; the thermal walk 120 -> 90 -> 60 -> 30, a 60 cap's
// old drop to 30 unchanged, nothing under the floor touched; the pacer drawing what the walk asks on 120
// and 90 Hz panels; and Follow device: every vsync of the panel the system chose (144 included), its rate
// in the paced slot, and a hot phone stepping down from the rate it followed while cool.
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
    check(choices({}) == std::vector<int>({30, 60, -1}), "an unknown display offers 30, 60 and Follow device");
    check(frame::liveFpsChoices(nullptr, 3, nullptr, 0) == 3, "a null rate list is an unknown display, and counting needs no array");
    check(choices({60.0f}) == std::vector<int>({30, 60, -1}), "a 60 Hz phone offers 30 and 60 only");
    check(choices({48.0f}) == std::vector<int>({30, 60, -1}), "even a panel under 60 keeps 60: the pacer draws every vsync");
    check(frame::kLiveFpsDefault == 60, "the default stays 60");
    // Every Settings › Performance tier is a cap every display offers, so picking a tier never shows a value
    // the tab cannot.
    for (const frame::QualityTier& t : frame::kQualityTiers) {
        check(frame::liveFpsOffered(static_cast<int>(t.fpsCap), nullptr, 0), "every tier's cap is offered everywhere");
    }
}

void highRatesOnlyWhereTheDisplayShowsThem() {
    check(choices({60.0f, 90.0f, 120.0f}) == std::vector<int>({30, 60, 90, 120, -1}), "a 60/90/120 Hz panel offers all four");
    check(choices({60.0f, 120.0f}) == std::vector<int>({30, 60, 120, -1}), "a 60/120 Hz panel has no 90: 120 does not divide to it");
    check(choices({60.0f, 90.0f}) == std::vector<int>({30, 60, 90, -1}), "a 90 Hz phone offers 90, not 120");
    check(choices({144.0f}) == std::vector<int>({30, 60, -1}), "144 Hz alone divides to neither 90 nor 120 evenly");
    check(choices({144.0f, 120.0f, 60.0f}) == std::vector<int>({30, 60, 120, -1}), "a 144 Hz panel with a 120 mode offers 120");
    check(choices({240.0f}) == std::vector<int>({30, 60, 120, -1}), "240 Hz shows 120 every second vsync");
    check(choices({180.0f}) == std::vector<int>({30, 60, 90, -1}), "180 Hz shows 90 every second vsync, not 120");
    check(choices({59.94f, 119.88f}) == std::vector<int>({30, 60, 120, -1}), "a 119.88 Hz mode is a 120 Hz mode");
    check(choices({89.5f}) == std::vector<int>({30, 60, 90, -1}), "half a hertz off is still 90");
    check(choices({88.0f}) == std::vector<int>({30, 60, -1}), "two hertz off is not");
    check(choices({NAN, INFINITY, -120.0f, 0.0f}) == std::vector<int>({30, 60, -1}), "nonsense rates offer nothing more");
    check(choices({1200.0f}) == std::vector<int>({30, 60, -1}), "a rate past eight divides is not read as 120");
    // The array is filled up to its capacity; the count is the whole list.
    const float rates[] = {120.0f, 90.0f, 60.0f};
    int two[2] = {0, 0};
    check(frame::liveFpsChoices(rates, 3, two, 2) == 5 && two[0] == 30 && two[1] == 60, "a short array gets the first, the count all");
    int five[5] = {0, 0, 0, 0, 0};
    check(frame::liveFpsChoices(rates, 3, five, 5) == 5 && five[3] == 120 && five[4] == frame::kLiveFpsFollow,
          "Follow device comes last, after the rates");
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
    check(frame::liveFpsCap(frame::kLiveFpsFollow, fast, 3) == frame::kLiveFpsFollow &&
              frame::liveFpsCap(frame::kLiveFpsFollow, nullptr, 0) == frame::kLiveFpsFollow,
          "Follow device stays Follow device on any display");
    check(frame::liveFpsCap(-2, fast, 3) == 60, "any other negative is the default");
    check(frame::kLiveFpsFollow == -1, "Follow is -1: an older build reads a negative cap as none, every vsync");
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

// One Follow device surface: its pacer state and the clock its vsyncs arrive on.
struct Follow {
    frame::PacerState p;
    double tNs = 1e9;

    // `seconds` of a panel at `panelHz` under a thermal tier `rungsDown` steps hot; returns how many frames the
    // last second drew. The cap is what geode_pacer_on_vsync paces each vsync to.
    int run(double panelHz, int rungsDown, int seconds) {
        const double period = 1e9 / panelHz;
        const int vsyncs = static_cast<int>(std::lround(panelHz * seconds));
        const int lastSecond = vsyncs - static_cast<int>(std::lround(panelHz));
        int drawn = 0;
        for (int i = 0; i < vsyncs; ++i) {
            tNs += period;
            const float cap = frame::pacerCapFps(p, static_cast<float>(frame::kLiveFpsFollow), rungsDown);
            if (frame::Pacer::onVsync(p, static_cast<int64_t>(std::llround(tNs)), cap) > 0.0 && i >= lastSecond) drawn++;
        }
        return drawn;
    }
};

// Follow device: every vsync of the panel the system chose, its real rate in the paced slot, and the
// thermal walk from the rate it followed while cool.
void followDeviceDrawsThePanelsOwnRate() {
    Follow f;
    check(frame::pacerCapFps(f.p, -1.0f, 0) == 0.0f, "before the panel is measured Follow draws every vsync");
    check(f.run(120.0, 0, 2) == 120, "a 120 Hz panel: 120 frames a second");
    check(f.p.divisor == 1 && f.p.pacedMilliHz == 120'000, "every vsync, and the paced rate says 120 for MilkDrop and the target");
    check(f.p.followedMilliHz == 120'000, "the rate followed while cool is kept");
    // The system changes the panel (battery saver, Smooth display off): Follow follows it.
    check(f.run(60.0, 0, 3) == 60, "the panel drops to 60: 60 frames a second");
    check(f.p.pacedMilliHz == 60'000 && f.p.followedMilliHz == 60'000, "and the kept rate follows it while cool");
    check(f.run(90.0, 0, 3) == 90 && f.p.pacedMilliHz == 90'000, "a 90 Hz mode: 90");

    Follow odd;
    check(odd.run(144.0, 0, 2) == 144, "an odd 144 Hz panel draws every vsync, never an uneven divide");
    check(odd.p.divisor == 1 && odd.p.pacedMilliHz == 144'000, "and reads as 144");
    check(odd.run(144.0, 1, 2) == 72 && odd.p.divisor == 2, "warm: the 120 step is the whole divide 72");
    check(odd.run(144.0, 2, 2) == 72 && odd.p.divisor == 2, "Reduced: 90 asked, still the whole divide 72");
    check(odd.run(144.0, 3, 2) == 48 && odd.p.divisor == 3 && odd.p.pacedMilliHz == 48'000, "Minimal: 60 asked, 48 drawn, evenly");

    check(Follow().run(60.0, 0, 2) == 60, "a 60 Hz phone follows at 60");
    Follow cold;
    check(cold.run(120.0, 1, 2) == 60, "hot before any rate was followed: it steps from the panel it first measures");
    check(cold.p.followedMilliHz == 120'000, "and keeps that rate, so a panel the system lowers later cannot pull it down");
}

// Kills: Follow stepping from the panel as it is when hot. Drawing fewer frames can let the system lower the
// panel (adaptive refresh rate), and stepping from that walks a warm phone down to the floor.
void aHotFollowStepsFromTheRateItFollowedCool() {
    Follow f;
    f.run(120.0, 0, 2);
    check(f.run(120.0, 1, 2) == 60, "warm on a 120 Hz panel: 90 asked, the whole divide 60 drawn");
    check(f.run(90.0, 1, 3) == 90, "warm, and the system put the panel at 90: 90");
    check(f.run(60.0, 1, 3) == 60, "warm, and the system lowered the panel to 60: still 60, not 30");
    check(f.p.followedMilliHz == 120'000, "the kept rate does not move while hot");
    check(f.run(120.0, 2, 3) == 60, "Reduced: 60");
    check(f.run(120.0, 3, 2) == 30, "Minimal: 30");
    check(f.run(120.0, 0, 2) == 120, "cool again: back to the panel's 120");
    // fps first at every step: each hotter tier draws no more than the cooler one.
    for (const double hz : {60.0, 90.0, 120.0, 144.0}) {
        int cooler = 1000;
        for (int t = 0; t < 4; ++t) {
            Follow g;
            g.run(hz, 0, 2);
            const int drawn = g.run(hz, t, 2);
            check(drawn <= cooler, "a hotter tier never draws more under Follow");
            cooler = drawn;
        }
    }
}

}  // namespace

int main() {
    sixtyIsAlwaysOffered();
    highRatesOnlyWhereTheDisplayShowsThem();
    aStoredCapSnapsToTheDisplay();
    aHotPhoneWalksTheCapDownARungATier();
    thePacerDrawsTheWalk();
    followDeviceDrawsThePanelsOwnRate();
    aHotFollowStepsFromTheRateItFollowedCool();
    if (failures == 0) std::printf("live_fps_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
