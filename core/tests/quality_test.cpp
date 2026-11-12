// Host-side checks for core/viz/frame/Quality (#437): Settings › Performance's tiers and the
// first-run pick for this phone. Built and run by CI ("Native host tests" in
// .github/workflows/native-host-tests.yml) with the system compiler; no GL. Ported from the JVM
// PerformanceSettingsTest, with the same values as the Kotlin it replaces; the C ABI too.
#include <algorithm>
#include <cstdio>
#include <iterator>

#include "api/geode_api.h"
#include "viz/frame/Quality.hpp"

using namespace geode::viz::frame;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

// The fps caps every display is offered (core/viz/frame/LiveFps: 90 and 120 only on fast panels). Every tier
// value must be a choice the tab offers, so picking a tier never shows a value the tab can't.
constexpr float kCaps[] = {30.0f, 60.0f};

bool offered(const float* first, const float* last, float v) { return std::find(first, last, v) != last; }

void tiersMatchTheKotlinTheyReplace() {
    const QualityTier& saver = qualityTier(QualityMode::Saver);
    const QualityTier& balanced = qualityTier(QualityMode::Balanced);
    const QualityTier& high = qualityTier(QualityMode::High);
    check(saver.renderScale == 0.5f && saver.fpsCap == 30.0f && saver.marchScale == 0.5f && !saver.postFx && saver.adaptive, "Saver");
    check(balanced.renderScale == 0.75f && balanced.fpsCap == 60.0f && balanced.marchScale == 0.75f && balanced.postFx && balanced.adaptive,
          "Balanced");
    check(high.renderScale == 1.0f && high.fpsCap == 60.0f && high.marchScale == 1.0f && high.postFx && !high.adaptive, "High");
}

void tiersUseOfferedValues() {
    for (const QualityTier& t : kQualityTiers) {
        check(offered(kScaleChoices.data(), kScaleChoices.data() + kScaleChoices.size(), t.renderScale),
              "render scale is a choice the tab offers");
        check(offered(std::begin(kCaps), std::end(kCaps), t.fpsCap), "fps cap is a choice the tab offers");
        check(offered(kScaleChoices.data(), kScaleChoices.data() + kScaleChoices.size(), t.marchScale),
              "march scale is a choice the tab offers");
    }
}

void saverIsCheaperThanHigh() {
    const QualityTier& saver = qualityTier(QualityMode::Saver);
    const QualityTier& high = qualityTier(QualityMode::High);
    check(saver.renderScale < high.renderScale && saver.fpsCap < high.fpsCap && saver.marchScale < high.marchScale,
          "Saver is cheaper than High on every axis");
}

void devicePick() {
    constexpr int64_t gb = 1024LL * 1024LL * 1024LL;
    check(modeForDevice(2 * gb, false, 0) == QualityMode::Saver, "2 GB starts on Saver");
    check(modeForDevice(8 * gb, true, 0) == QualityMode::Saver, "a low-RAM phone starts on Saver whatever its RAM");
    check(modeForDevice(4 * gb, false, 0) == QualityMode::Balanced, "4 GB starts on Balanced");
    check(modeForDevice(4 * gb, false, 33) == QualityMode::High, "performance class 33 promotes to High");
    check(modeForDevice(8 * gb, false, 0) == QualityMode::Balanced, "RAM alone never picks High: 8 GB starts on Balanced");
    check(modeForDevice(16 * gb, false, 30) == QualityMode::Balanced, "nor does a performance class under 31");
    check(modeForDevice(0, false, 0) == QualityMode::Balanced, "unknown RAM is not read as small");
    check(modeForDevice(kSaverBelowRam, false, 0) == QualityMode::Balanced, "exactly 3000 MB is Balanced");
    check(modeForDevice(kSaverBelowRam - 1, false, 31) == QualityMode::Saver, "Saver's RAM rule beats the performance class");
    check(modeForDevice(4 * gb, false, 31) == QualityMode::High, "exactly performance class 31 is High");
}

// The tab's "Custom": a preset only while every compared value is the tier's.
void presetMatch() {
    check(modeOf(0.5f, 30, 0.5f, false, true) == 0, "Saver's values are Saver");
    check(modeOf(0.75f, 60, 0.75f, true, true) == 1, "Balanced's values are Balanced");
    check(modeOf(1.0f, 60, 1.0f, true, false) == 2, "High's values are High");
    check(modeOf(1.0f, 30, 1.0f, true, false) == -1, "High at 30 fps is Custom");
    check(modeOf(1.0f, GEODE_LIVE_FPS_FOLLOW, 1.0f, true, false) == -1, "Follow device is no tier's cap");
    check(modeOf(0.5f, 30, 0.75f, false, true) == -1, "Saver with a medium march is Custom");
    check(modeOf(0.75f, 60, 0.75f, false, true) == -1, "Balanced without post FX is Custom");
    check(modeOf(1.0f, 60, 1.0f, true, true) == -1, "High with adaptive on is Custom");
}

void throughTheAbi() {
    check(geode_perf_mode_of(0.75f, 60, 0.75f, 1, 1) == 1 && geode_perf_mode_of(0.6f, 60, 0.75f, 1, 1) == -1, "geode_perf_mode_of");
    float scales[GEODE_PERF_SCALE_CHOICES + 1] = {};
    check(geode_perf_scale_choices(scales, GEODE_PERF_SCALE_CHOICES + 1) == 3 && scales[0] == 0.5f && scales[1] == 0.75f &&
              scales[2] == 1.0f,
          "the scale choices, ascending");
    check(geode_perf_scale_choices(scales, 1) == 1 && geode_perf_scale_choices(nullptr, 3) == 0, "scale choices fit the capacity");
    float tier[GEODE_PERF_TIER_FLOATS] = {};
    check(geode_perf_tier(1, tier, GEODE_PERF_TIER_FLOATS) == GEODE_PERF_TIER_FLOATS && tier[1] == 60.0f && tier[4] == 1.0f,
          "geode_perf_tier writes Balanced");
    check(geode_perf_tier(3, tier, GEODE_PERF_TIER_FLOATS) == 0, "no fourth tier");
    // The Liquid hot-phone policy as stored: its three values as they are, a newer build's falls back to Simplify.
    check(geode_perf_liquid_hot(0) == 0 && geode_perf_liquid_hot(1) == 1 && geode_perf_liquid_hot(2) == 2, "known policies");
    check(geode_perf_liquid_hot(99) == 0 && geode_perf_liquid_hot(-1) == 0, "an unknown policy is Simplify");
}

}  // namespace

int main() {
    tiersMatchTheKotlinTheyReplace();
    tiersUseOfferedValues();
    saverIsCheaperThanHigh();
    devicePick();
    presetMatch();
    throughTheAbi();
    if (failures == 0) std::printf("quality_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
