// Host-side checks for core/viz/frame/Quality (#437): Settings › Performance's tiers and the
// first-run pick for this phone. Built and run by CI ("Native host tests" in
// .github/workflows/native-host-tests.yml) with the system compiler; no GL. Ported from the JVM
// PerformanceSettingsTest, with the same values as the Kotlin it replaced; the C ABI too.
#include <algorithm>
#include <cstdio>
#include <iterator>
#include <limits>

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
    check(saver.renderScale == 0.5f && saver.fpsCap == 30.0f && saver.marchScale == 0.5f && !saver.postFx, "Saver");
    check(balanced.renderScale == 0.75f && balanced.fpsCap == 60.0f && balanced.marchScale == 0.75f && balanced.postFx,
          "Balanced");
    check(high.renderScale == 1.0f && high.fpsCap == 60.0f && high.marchScale == 1.0f && high.postFx, "High");
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
    check(modeOf(0.5f, 30, 0.5f, false) == 0, "Saver's values are Saver");
    check(modeOf(0.75f, 60, 0.75f, true) == 1, "Balanced's values are Balanced");
    check(modeOf(1.0f, 60, 1.0f, true) == 2, "High's values are High");
    check(modeOf(1.0f, 30, 1.0f, true) == -1, "High at 30 fps is Custom");
    check(modeOf(1.0f, GEODE_LIVE_FPS_FOLLOW, 1.0f, true) == -1, "Follow device is no tier's cap");
    check(modeOf(0.5f, 30, 0.75f, false) == -1, "Saver with a medium march is Custom");
    check(modeOf(0.75f, 60, 0.75f, false) == -1, "Balanced without post FX is Custom");
}

void throughTheAbi() {
    check(geode_perf_mode_of(0.75f, 60, 0.75f, 1) == 1 && geode_perf_mode_of(0.6f, 60, 0.75f, 1) == -1, "geode_perf_mode_of");
    float scales[GEODE_PERF_SCALE_CHOICES + 1] = {};
    check(geode_perf_scale_choices(scales, GEODE_PERF_SCALE_CHOICES + 1) == 3 && scales[0] == 0.5f && scales[1] == 0.75f &&
              scales[2] == 1.0f,
          "the scale choices, ascending");
    check(geode_perf_scale_choices(scales, 1) == 1 && geode_perf_scale_choices(nullptr, 3) == 0, "scale choices fit the capacity");
    float tier[GEODE_PERF_TIER_FLOATS] = {};
    check(geode_perf_tier(1, tier, GEODE_PERF_TIER_FLOATS) == GEODE_PERF_TIER_FLOATS && tier[1] == 60.0f && tier[3] == 1.0f,
          "geode_perf_tier writes Balanced");
    check(geode_perf_tier(3, tier, GEODE_PERF_TIER_FLOATS) == 0, "no fourth tier");
}

// The window buffer the shell fixes the surface to (SurfaceHolder.setFixedSize); the display scales it up.
void bufferSizes() {
    const BufferSize full = bufferSize(1.0f, 1080, 2400);
    check(full.width == 1080 && full.height == 2400, "scale 1 is the view's own size");
    const BufferSize half = bufferSize(0.5f, 1440, 3200);
    check(half.width == 720 && half.height == 1600, "scale 0.5 halves each side");
    const BufferSize odd = bufferSize(0.75f, 1081, 2401);
    check(odd.width == 811 && odd.height == 1801, "a side rounds to the nearest pixel");
    check(bufferSize(std::numeric_limits<float>::quiet_NaN(), 1080, 2400).width == 1080, "a corrupt scale is full size");
    check(bufferSize(4.0f, 1080, 2400).width == 1080, "never bigger than the view");
    check(bufferSize(0.01f, 1080, 2400).width == 270, "never under a quarter");
    check(bufferSize(0.25f, 1, 1).width == 1 && bufferSize(0.25f, 1, 1).height == 1, "at least one pixel");
    check(bufferSize(1.0f, 0, 2400).width == 0, "no view, no buffer");
    int out[GEODE_PERF_BUFFER_INTS] = {};
    check(geode_perf_buffer_size(0.5f, 1080, 2400, out, GEODE_PERF_BUFFER_INTS) == GEODE_PERF_BUFFER_INTS && out[0] == 540 &&
              out[1] == 1200,
          "geode_perf_buffer_size");
    check(geode_perf_buffer_size(0.5f, 0, 0, out, GEODE_PERF_BUFFER_INTS) == 0 && geode_perf_buffer_size(0.5f, 8, 8, out, 1) == 0,
          "no view or a short out writes nothing");
}

}  // namespace

int main() {
    tiersMatchTheKotlinTheyReplace();
    tiersUseOfferedValues();
    saverIsCheaperThanHigh();
    devicePick();
    presetMatch();
    throughTheAbi();
    bufferSizes();
    if (failures == 0) std::printf("quality_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
