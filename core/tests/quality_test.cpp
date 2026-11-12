// Host-side checks for core/viz/frame/Quality (#437): Settings › Performance's tiers and the
// first-run pick for this phone. Built and run by CI ("Native host tests" in
// .github/workflows/android.yml) with the system compiler; no GL. Ported from the JVM
// PerformanceSettingsTest, with the same values as the Kotlin it replaces.
#include <algorithm>
#include <cstdio>
#include <iterator>

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

// The choices Settings › Performance offers (render/PerformanceSettings.kt RENDER_SCALES and MARCH_SCALES,
// and the fps caps every display is offered, core/viz/frame/LiveFps: 90 and 120 only on fast panels). Every
// tier value must be one of them, so picking a tier never shows a value the tab can't.
constexpr float kScales[] = {0.5f, 0.75f, 1.0f};
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
        check(offered(std::begin(kScales), std::end(kScales), t.renderScale), "render scale is a choice the tab offers");
        check(offered(std::begin(kCaps), std::end(kCaps), t.fpsCap), "fps cap is a choice the tab offers");
        check(offered(std::begin(kScales), std::end(kScales), t.marchScale), "march scale is a choice the tab offers");
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

}  // namespace

int main() {
    tiersMatchTheKotlinTheyReplace();
    tiersUseOfferedValues();
    saverIsCheaperThanHigh();
    devicePick();
    if (failures == 0) std::printf("quality_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
