// Host-side checks for the fixed-30 live frame-rate contract (core/viz/frame/LiveFps). Built and run by CI
// ("Native host tests" in .github/workflows/native-host-tests.yml) with the system compiler; no GL.
//
// What is pinned: every quality tier and every legacy stored value normalizes to 30 FPS; no Follow device or
// display-derived render-rate choice remains; the legacy manual refresh preference returns 0; 60/90/120 Hz are
// physical-panel timing cases (2/3/4 refresh periods per 30 FPS frame), never app FPS options.
#include <cmath>
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

void theOnlyLiveRateIsThirty() {
    check(frame::kLiveFpsDefault == 30 && frame::kLiveFpsMin == 30 && frame::kLiveFpsMax == 30,
          "the live rate is fixed at 30");
    check(choices({}) == std::vector<int>({30}), "an unknown display still has the one fixed rate");
    check(frame::liveFpsChoices(nullptr, 3, nullptr, 0) == 1, "counting needs no array");
    check(choices({60.0f}) == std::vector<int>({30}), "60 Hz does not offer a 60 FPS live mode");
    check(choices({60.0f, 90.0f}) == std::vector<int>({30}), "60/90 Hz does not offer 45 or 90 FPS live modes");
    check(choices({60.0f, 120.0f}) == std::vector<int>({30}), "60/120 Hz does not offer 40, 60 or 120 FPS live modes");
    check(choices({144.0f}) == std::vector<int>({30}), "144 Hz does not change the render target");
    check(choices({NAN, INFINITY, -120.0f, 0.0f}) == std::vector<int>({30}), "nonsense rates cannot change the target");
    check(frame::liveFpsOffered(30, nullptr, 0), "30 is offered");
    for (const int fps : {-1, 0, 24, 45, 60, 90, 120}) {
        check(!frame::liveFpsOffered(fps, nullptr, 0), "no other live rate is offered");
    }
    int shortOut[1] = {0};
    check(frame::liveFpsChoices(nullptr, 0, shortOut, 1) == 1 && shortOut[0] == 30, "the fixed choice fits one slot");
    check(frame::kLiveFpsFollow == -1, "the legacy Follow sentinel stays ABI-compatible");
}

void everyLegacyStoredRateNormalizesToThirty() {
    const float fast[] = {60.0f, 90.0f, 120.0f};
    const float onlyNinety[] = {90.0f};
    for (const int stored : {frame::kLiveFpsFollow, -5, 0, 1, 24, 30, 45, 60, 75, 90, 120, 1000}) {
        check(frame::liveFpsCap(stored, fast, 3) == 30, "every legacy cap becomes 30 on a fast display");
        check(frame::liveFpsCap(stored, onlyNinety, 1) == 30, "every legacy cap becomes 30 on a 90 Hz-only display");
        check(frame::liveFpsCap(stored, nullptr, 0) == 30, "every legacy cap becomes 30 on an unknown display");
    }
    for (const frame::QualityTier& tier : frame::kQualityTiers) {
        check(tier.fpsCap == static_cast<float>(frame::kLiveFpsDefault), "every quality tier keeps the fixed rate");
    }
}

// The old ABI slot stays callable, but it can no longer select a display mode or refresh-rate preference.
void theLegacyRefreshSlotStatesNoManualPreference() {
    const float rates[] = {60.0f, 90.0f, 120.0f};
    for (const int fps : {frame::kLiveFpsFollow, 0, 30, 45, 60, 90, 120}) {
        check(frame::refreshRateFor(fps, rates, 3) == 0.0f, "no legacy fps argument selects a display mode");
    }
    check(frame::refreshRateFor(30, nullptr, 0) == 0.0f, "an unknown display still states no manual preference");
    check(frame::refreshRateFor(30, rates, -1) == 0.0f, "a malformed legacy rate list cannot select a mode");
}

// Whole-refresh ratios are timing math for the panel the platform is already running, not a list of render rates.
void wholeRefreshRatiosDescribePlatformTimingOnly() {
    check(frame::vsyncSpan(60.0f, 30.0f) == 2, "30 FPS on 60 Hz: two refresh periods");
    check(frame::vsyncSpan(90.0f, 30.0f) == 3, "30 FPS on 90 Hz: three refresh periods");
    check(frame::vsyncSpan(120.0f, 30.0f) == 4, "30 FPS on 120 Hz: four refresh periods");
    check(frame::vsyncSpan(59.94f, 30.0f) == 2 && frame::vsyncSpan(119.88f, 30.0f) == 4,
          "nominal NTSC-rate panels keep the same whole-period ratios");
    check(frame::vsyncSpan(75.0f, 30.0f) == 0, "an uneven 2.5:1 ratio is rejected, not turned into another FPS option");
    check(frame::vsyncSpan(90.0f, 60.0f) == 0 && frame::vsyncSpan(120.0f, 45.0f) == 0,
          "uneven ratios never imply 60 or 45 FPS live options");
    check(frame::vsyncSpan(NAN, 30.0f) == 0 && frame::vsyncSpan(120.0f, NAN) == 0,
          "invalid timing inputs have no ratio");
}

}  // namespace

int main() {
    theOnlyLiveRateIsThirty();
    everyLegacyStoredRateNormalizesToThirty();
    theLegacyRefreshSlotStatesNoManualPreference();
    wholeRefreshRatiosDescribePlatformTimingOnly();
    if (failures == 0) std::printf("live_fps_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
