// Host-side checks for core/viz/frame/LiveFps: the live frame rates Settings › Performance offers on a display, how a
// stored rate snaps to that display, and which display mode a rate asks for. Built and run by CI ("Native host tests"
// in .github/workflows/native-host-tests.yml) with the system compiler; no GL.
//
// What is pinned: the rates offered are the display's refresh rates divided by a whole number, 30 to 120 (a panel
// shows only those evenly: 90 Hz gives 90, 45 and 30), with 30 and 60 always and 60 the default; a stored rate
// snapped to the nearest offered, the lower on a tie; and the mode a rate asks for is the lowest refresh rate that
// shows it evenly, none for a rate no mode shows.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "viz/frame/LiveFps.hpp"
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
    int out[frame::kLiveFpsMaxRates + 1] = {};
    const int n = frame::liveFpsChoices(rates.empty() ? nullptr : rates.data(), static_cast<int>(rates.size()), out,
                                        frame::kLiveFpsMaxRates + 1);
    return std::vector<int>(out, out + n);
}

// Red against: a fixed ladder (no 45 on a 90 Hz panel), a divide that rounds (88 Hz read as 90), and a list that drops
// the tiers' rates on a display that says nothing.
void theRatesAreTheDisplaysOwnDivided() {
    check(choices({}) == std::vector<int>({30, 60, -1}), "an unknown display offers 30, 60 and Follow device");
    check(frame::liveFpsChoices(nullptr, 3, nullptr, 0) == 3, "a null rate list is an unknown display, and counting needs no array");
    check(choices({60.0f}) == std::vector<int>({30, 60, -1}), "a 60 Hz phone offers 30 and 60");
    check(choices({60.0f, 90.0f}) == std::vector<int>({30, 45, 60, 90, -1}), "a 60/90 Hz phone offers 90, 45 and 30 too");
    check(choices({60.0f, 120.0f}) == std::vector<int>({30, 40, 60, 120, -1}), "a 60/120 Hz panel offers 40, not 90");
    check(choices({60.0f, 90.0f, 120.0f}) == std::vector<int>({30, 40, 45, 60, 90, 120, -1}), "a 60/90/120 Hz panel offers each divide");
    check(choices({144.0f}) == std::vector<int>({30, 36, 48, 60, 72, -1}), "144 Hz offers its divides up to 120");
    check(choices({240.0f}) == std::vector<int>({30, 40, 48, 60, 80, 120, -1}), "240 Hz offers its divides up to 120");
    check(choices({59.94f, 119.88f}) == std::vector<int>({30, 40, 60, 120, -1}), "a 119.88 Hz mode is a 120 Hz mode");
    check(choices({89.5f}) == std::vector<int>({30, 45, 60, 90, -1}), "half a hertz off is still 90");
    check(choices({88.0f}) == std::vector<int>({30, 44, 60, 88, -1}), "88 Hz is its own rate, not 90");
    check(choices({NAN, INFINITY, -120.0f, 0.0f}) == std::vector<int>({30, 60, -1}), "nonsense rates offer nothing more");
    check(choices({1200.0f}) == std::vector<int>({30, 60, -1}), "a rate past eight divides of 120 offers nothing more");
    check(frame::vsyncSpan(90.0f, 45.0f) == 2 && frame::vsyncSpan(90.0f, 60.0f) == 0, "90 Hz shows 45 every second vsync, 60 never evenly");
    // The array is filled up to its capacity; the count is the whole list.
    const float rates[] = {120.0f, 90.0f, 60.0f};
    int two[2] = {0, 0};
    check(frame::liveFpsChoices(rates, 3, two, 2) == 7 && two[0] == 30 && two[1] == 40, "a short array gets the first, the count all");
    int all[frame::kLiveFpsMaxRates + 1] = {};
    check(frame::liveFpsChoices(rates, 3, all, frame::kLiveFpsMaxRates + 1) == 7 && all[5] == 120 && all[6] == frame::kLiveFpsFollow,
          "Follow device comes last, after the rates");
    check(!frame::liveFpsOffered(75, rates, 3), "a rate no mode shows evenly is never offered");
    check(frame::kLiveFpsDefault == 60, "the default stays 60");
    // Every Settings › Performance tier is a rate every display offers, so picking a tier never shows a value the tab
    // cannot.
    for (const frame::QualityTier& t : frame::kQualityTiers) {
        check(frame::liveFpsOffered(static_cast<int>(t.fpsCap), nullptr, 0), "every tier's rate is offered everywhere");
    }
}

void aStoredRateSnapsToTheDisplay() {
    const float fast[] = {60.0f, 90.0f, 120.0f};
    const float ninety[] = {60.0f, 90.0f};
    const float sixty[] = {60.0f};
    check(frame::liveFpsCap(120, fast, 3) == 120, "an offered rate stays");
    check(frame::liveFpsCap(90, fast, 3) == 90 && frame::liveFpsCap(45, fast, 3) == 45, "every offered rate stays");
    check(frame::liveFpsCap(120, ninety, 2) == 90, "120 on a 90 Hz panel is 90");
    check(frame::liveFpsCap(120, sixty, 1) == 60, "120 on a 60 Hz panel (a foldable's outer screen) is 60");
    check(frame::liveFpsCap(45, sixty, 1) == 30, "45 on a 60 Hz panel is 30, the lower on a tie");
    check(frame::liveFpsCap(120, nullptr, 0) == 60, "an unknown display runs a high rate at 60");
    check(frame::liveFpsCap(0, fast, 3) == 60 && frame::liveFpsCap(-5, fast, 3) == 60, "nothing stored is the default");
    check(frame::liveFpsCap(75, fast, 3) == 60, "75 is as near 60 as 90: the lower");
    check(frame::liveFpsCap(50, fast, 3) == 45, "50 is nearest 45");
    check(frame::liveFpsCap(1000, fast, 3) == 120 && frame::liveFpsCap(1, fast, 3) == 30, "far out of range clamps to the ends");
    check(frame::liveFpsCap(frame::kLiveFpsFollow, fast, 3) == frame::kLiveFpsFollow &&
              frame::liveFpsCap(frame::kLiveFpsFollow, nullptr, 0) == frame::kLiveFpsFollow,
          "Follow device stays Follow device on any display");
    check(frame::liveFpsCap(-2, fast, 3) == 60, "any other negative is the default");
    check(frame::kLiveFpsFollow == -1, "Follow is -1: an older build reads a negative rate as none, every vsync");
}

// Red against: a mode chosen as the panel's top rate (60 would run on 90 Hz, unevenly), one that is not a mode the
// display has, and a mode asked for while following the device.
void aRateAsksForTheLowestModeThatShowsIt() {
    const float ninety[] = {60.0f, 90.0f};
    const float fast[] = {120.0f, 90.0f, 60.0f};
    const float ntsc[] = {59.94f, 119.88f};
    const float onlyNinety[] = {90.0f};
    check(frame::refreshRateFor(60, ninety, 2) == 60.0f, "60 on a 60/90 Hz panel runs the 60 Hz mode");
    check(frame::refreshRateFor(45, ninety, 2) == 90.0f, "45 runs the 90 Hz mode, every second vsync");
    check(frame::refreshRateFor(90, ninety, 2) == 90.0f, "90 runs the 90 Hz mode");
    check(frame::refreshRateFor(30, ninety, 2) == 60.0f, "30 runs the lowest mode that shows it");
    check(frame::refreshRateFor(40, fast, 3) == 120.0f, "40 runs the 120 Hz mode");
    check(frame::refreshRateFor(60, ntsc, 2) == 59.94f, "60 runs a 59.94 Hz mode");
    check(frame::refreshRateFor(60, onlyNinety, 1) == 0.0f, "a rate no mode shows evenly asks for none");
    check(frame::refreshRateFor(frame::kLiveFpsFollow, ninety, 2) == 0.0f, "Follow device asks for none");
    check(frame::refreshRateFor(60, nullptr, 0) == 0.0f, "an unknown display asks for none");
}

}  // namespace

int main() {
    theRatesAreTheDisplaysOwnDivided();
    aStoredRateSnapsToTheDisplay();
    aRateAsksForTheLowestModeThatShowsIt();
    if (failures == 0) std::printf("live_fps_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
