// Host-side checks for core/viz/frame/LiveFps (owner decision of 2026-11-12, "Offer 90 and 120 in
// Performance"): which live frame-rate caps Settings › Performance offers on a display, how a stored cap
// snaps to that display. Built and run by CI ("Native host tests" in .github/workflows/native-host-tests.yml) with the
// system compiler; no GL.
//
// What is pinned: 30 and 60 on every display and 60 the default; 90 and 120 only where a refresh rate the
// display can switch to shows them evenly (itself or a whole multiple, within a hertz); and a stored cap
// snapped to the nearest offered, the lower on a tie.
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
    int out[8] = {};
    const int n = frame::liveFpsChoices(rates.empty() ? nullptr : rates.data(), static_cast<int>(rates.size()), out, 8);
    return std::vector<int>(out, out + n);
}

void sixtyIsAlwaysOffered() {
    check(choices({}) == std::vector<int>({30, 60, -1}), "an unknown display offers 30, 60 and Follow device");
    check(frame::liveFpsChoices(nullptr, 3, nullptr, 0) == 3, "a null rate list is an unknown display, and counting needs no array");
    check(choices({60.0f}) == std::vector<int>({30, 60, -1}), "a 60 Hz phone offers 30 and 60 only");
    check(choices({48.0f}) == std::vector<int>({30, 60, -1}), "even a panel under 60 keeps 60: the stage draws every vsync");
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

}  // namespace

int main() {
    sixtyIsAlwaysOffered();
    highRatesOnlyWhereTheDisplayShowsThem();
    aStoredCapSnapsToTheDisplay();
    if (failures == 0) std::printf("live_fps_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
