// Host-side checks for core/viz/frame/LiveFps (owner decision of 2026-11-12, "Offer 90 and 120 in
// Performance"): which live frame-rate caps Settings › Performance offers on a display, how a stored cap
// snaps to that display, and Follow device. Built and run by CI ("Native host tests" in .github/workflows/native-host-tests.yml) with the
// system compiler; no GL.
//
// What is pinned: 30 and 60 on every display and 60 the default; 90 and 120 only where a refresh rate the
// display can switch to shows them evenly (itself or a whole multiple, within a hertz); a stored cap
// snapped to the nearest offered, the lower on a tie; the pacer drawing each cap on 120 and 90 Hz panels;
// and Follow device: every vsync of the panel the system chose (144 included), its rate in the paced slot.
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

void thePacerDrawsTheCap() {
    check(drawnPerSecond(120.0f, 120.0f) == 120, "120 on a 120 Hz panel draws every vsync");
    check(drawnPerSecond(90.0f, 90.0f) == 90, "90 on a 90 Hz panel draws every vsync");
    // A 90 cap on a panel held at 120 Hz cannot divide evenly and draws the next whole divide, 60.
    check(drawnPerSecond(120.0f, 90.0f) == 60, "90 on a 120 Hz mode: 60");
    check(drawnPerSecond(120.0f, 30.0f) == 30, "30 on a 120 Hz mode: 30");
    check(drawnPerSecond(60.0f, 120.0f) == 60, "a 120 cap the panel cannot reach draws the panel's 60");
}

// Every cap other than Follow device paces as it is: the shell's cap is the pacer's cap.
void aCapIsPacedAsItIs() {
    frame::PacerState p;
    for (const float cap : {120.0f, 90.0f, 60.0f, 45.0f, 30.0f, 24.0f, 15.0f}) {
        check(frame::pacerCapFps(p, cap) == cap, "a cap is paced as it is");
    }
    check(frame::pacerCapFps(p, 0.0f) == 0.0f && frame::pacerCapFps(p, NAN) == 0.0f &&
              frame::pacerCapFps(p, INFINITY) == 0.0f && frame::pacerCapFps(p, -60.0f) == 0.0f,
          "no cap, NaN, infinity or a negative other than Follow draws every vsync");
}

// One Follow device surface: its pacer state and the clock its vsyncs arrive on.
struct Follow {
    frame::PacerState p;
    double tNs = 1e9;

    // `seconds` of a panel at `panelHz`; returns how many frames the last second drew. The cap is what
    // geode_pacer_on_vsync paces each vsync to.
    int run(double panelHz, int seconds) {
        const double period = 1e9 / panelHz;
        const int vsyncs = static_cast<int>(std::lround(panelHz * seconds));
        const int lastSecond = vsyncs - static_cast<int>(std::lround(panelHz));
        int drawn = 0;
        for (int i = 0; i < vsyncs; ++i) {
            tNs += period;
            const float cap = frame::pacerCapFps(p, static_cast<float>(frame::kLiveFpsFollow));
            if (frame::Pacer::onVsync(p, static_cast<int64_t>(std::llround(tNs)), cap) > 0.0 && i >= lastSecond) drawn++;
        }
        return drawn;
    }
};

// Follow device: every vsync of the panel the system chose, and its real rate in the paced slot.
void followDeviceDrawsThePanelsOwnRate() {
    Follow f;
    check(frame::pacerCapFps(f.p, -1.0f) == 0.0f, "before the panel is measured Follow draws every vsync");
    check(f.run(120.0, 2) == 120, "a 120 Hz panel: 120 frames a second");
    check(f.p.divisor == 1 && f.p.pacedMilliHz == 120'000, "every vsync, and the paced rate says 120 for MilkDrop and the target");
    check(f.p.followedMilliHz == 120'000, "the rate followed is kept");
    // The system changes the panel (battery saver, Smooth display off): Follow follows it.
    check(f.run(60.0, 3) == 60, "the panel drops to 60: 60 frames a second");
    check(f.p.pacedMilliHz == 60'000 && f.p.followedMilliHz == 60'000, "and the kept rate follows it");
    check(f.run(90.0, 3) == 90 && f.p.pacedMilliHz == 90'000, "a 90 Hz mode: 90");

    Follow odd;
    check(odd.run(144.0, 2) == 144, "an odd 144 Hz panel draws every vsync, never an uneven divide");
    check(odd.p.divisor == 1 && odd.p.pacedMilliHz == 144'000, "and reads as 144");

    check(Follow().run(60.0, 2) == 60, "a 60 Hz phone follows at 60");
}

}  // namespace

int main() {
    sixtyIsAlwaysOffered();
    highRatesOnlyWhereTheDisplayShowsThem();
    aStoredCapSnapsToTheDisplay();
    thePacerDrawsTheCap();
    aCapIsPacedAsItIs();
    followDeviceDrawsThePanelsOwnRate();
    if (failures == 0) std::printf("live_fps_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
