// Host-side checks for core/viz/frame/Quality (#437): Settings › Performance's tiers, its render resolutions in
// real pixels, the window buffer they size and the first-run pick for this phone. Built and run by CI ("Native
// host tests" in .github/workflows/native-host-tests.yml) with the system compiler; no GL. The C ABI too.
#include <algorithm>
#include <cstdio>
#include <iterator>
#include <limits>

#include "api/geode_api.h"
#include "viz/frame/Quality.hpp"
#include "viz/frame/ResolutionState.hpp"

using namespace geode::viz::frame;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

constexpr int64_t gb = 1024LL * 1024LL * 1024LL;

// Panels (natural orientation: the short side first, as Display.Mode reports a phone).
constexpr int k720[] = {720, 1600};
constexpr int k1080[] = {1080, 2400};
constexpr int k1440[] = {1440, 3200};
constexpr int k1200Tablet[] = {1200, 1920};

// The fps caps every display is offered (core/viz/frame/LiveFps: 90 and 120 only on fast panels). Every tier
// value must be a choice the tab offers, so picking a tier never shows a value the tab can't.
constexpr float kCaps[] = {30.0f, 60.0f};

bool offered(const float* first, const float* last, float v) { return std::find(first, last, v) != last; }

int choicesOn(const int* panel, int* out) { return resolutionChoices(panel[0], panel[1], out, kMaxResolutionChoices); }

void tiers() {
    const QualityTier& saver = qualityTier(QualityMode::Saver);
    const QualityTier& balanced = qualityTier(QualityMode::Balanced);
    const QualityTier& high = qualityTier(QualityMode::High);
    check(saver.resolution == 540 && saver.fpsCap == 30.0f && saver.marchScale == 0.5f && !saver.postFx, "Saver");
    check(balanced.resolution == 900 && balanced.fpsCap == 60.0f && balanced.marchScale == 0.75f && balanced.postFx, "Balanced");
    check(high.resolution == kNativeResolution && high.fpsCap == 60.0f && high.marchScale == 1.0f && high.postFx, "High");
    for (const QualityTier& t : kQualityTiers) {
        check(offered(std::begin(kCaps), std::end(kCaps), t.fpsCap), "fps cap is a choice the tab offers");
        check(offered(kScaleChoices.data(), kScaleChoices.data() + kScaleChoices.size(), t.marchScale),
              "march scale is a choice the tab offers");
        // A tier's resolution, as drawn, is one of the choices on every panel.
        for (const int* panel : {k720, k1080, k1440, k1200Tablet}) {
            int choices[kMaxResolutionChoices];
            const int n = choicesOn(panel, choices);
            const int drawn = drawnShortSide(t.resolution, panel[0]);
            check(std::any_of(choices, choices + n, [&](int c) { return drawnShortSide(c, panel[0]) == drawn; }),
                  "every tier's resolution is offered on every panel");
        }
    }
    for (const int* panel : {k720, k1080, k1440}) {
        check(drawnShortSide(saver.resolution, panel[0]) <= drawnShortSide(balanced.resolution, panel[0]) &&
                  drawnShortSide(balanced.resolution, panel[0]) <= drawnShortSide(high.resolution, panel[0]),
              "Saver draws no more pixels than Balanced, Balanced no more than High");
    }
    check(saver.fpsCap < high.fpsCap && saver.marchScale < high.marchScale, "Saver is cheaper than High");
}

void resolutionList() {
    int c[kMaxResolutionChoices + 1] = {};
    check(choicesOn(k1080, c) == 4 && c[0] == 540 && c[1] == 720 && c[2] == 900 && c[3] == kNativeResolution,
          "1080p: 540, 720, 900, Native");
    check(choicesOn(k1440, c) == 5 && c[3] == 1080 && c[4] == kNativeResolution, "1440p adds 1080 under Native");
    check(choicesOn(k720, c) == 2 && c[0] == 540 && c[1] == kNativeResolution, "720p: 540, Native");
    check(choicesOn(k1200Tablet, c) == 5 && c[3] == 1080, "a 1200p tablet offers 1080");
    const int landscape[] = {3200, 1440};
    check(choicesOn(landscape, c) == 5, "the short side decides, whatever the orientation");
    const int tiny[] = {480, 800};
    check(choicesOn(tiny, c) == 1 && c[0] == kNativeResolution, "a panel under every step offers Native alone");
    check(resolutionChoices(0, 0, c, kMaxResolutionChoices + 1) == kMaxResolutionChoices && c[6] == kNativeResolution,
          "an unknown panel offers every step");
    check(resolutionChoices(1440, 3200, c, 2) == 2 && resolutionChoices(1440, 3200, nullptr, 4) == 0, "fits the capacity");
}

void drawnSides() {
    check(drawnShortSide(kNativeResolution, 1440) == 1440, "Native is the screen's own");
    check(drawnShortSide(1080, 1440) == 1080, "a step under the screen is itself");
    check(drawnShortSide(1080, 720) == 720 && drawnShortSide(900, 900) == 900, "never more than the screen");
    check(drawnShortSide(7, 1440) == 540 && drawnShortSide(-3, 1440) == 1440, "a corrupt pref: the smallest step, or Native");
}

// The window buffer the shell fixes the surface to (SurfaceHolder.setFixedSize); the display scales it up.
void bufferSizes() {
    const BufferSize native = bufferSize(kNativeResolution, 1440, 3200, 1440, 3200);
    check(native.width == 1440 && native.height == 3200, "Native is the view's own size");
    const BufferSize fhd = bufferSize(1080, 1440, 3200, 1440, 3200);
    check(fhd.width == 1080 && fhd.height == 2400, "1080 on a 1440p panel full screen: 1080 x 2400");
    const BufferSize turned = bufferSize(1080, 1440, 3200, 3200, 1440);
    check(turned.width == 2400 && turned.height == 1080, "rotated, the same pixels");
    const BufferSize split = bufferSize(1080, 1440, 3200, 1440, 1560);
    check(split.width == 1080 && split.height == 1170, "a split-screen half keeps the same pixels per inch");
    const BufferSize lowMode = bufferSize(kNativeResolution, 1080, 2400, 1080, 2400);
    check(lowMode.width == 1080, "a 1440p panel running its 1080p mode: Native is the mode's own");
    check(bufferSize(1440, 1080, 2400, 1080, 2400).width == 1080, "and a step over the mode draws the mode");
    const BufferSize unknownMode = bufferSize(540, 0, 0, 1080, 2400);
    check(unknownMode.width == 540 && unknownMode.height == 1200, "an unknown mode reads the view as the screen");
    const BufferSize odd = bufferSize(900, 1080, 2400, 1081, 2401);
    check(odd.width == 901 && odd.height == 2001, "a side rounds to the nearest pixel");
    check(bufferSize(540, 1080, 2400, 0, 2400).width == 0, "no view, no buffer");
    check(bufferSize(540, 1080, 2400, 1, 1).width == 1, "at least one pixel");
    int out[GEODE_PERF_BUFFER_INTS] = {};
    check(geode_perf_buffer_size(720, 1080, 2400, 1080, 2400, out, GEODE_PERF_BUFFER_INTS) == GEODE_PERF_BUFFER_INTS &&
              out[0] == 720 && out[1] == 1600,
          "geode_perf_buffer_size");
    check(geode_perf_buffer_size(720, 1080, 2400, 0, 0, out, GEODE_PERF_BUFFER_INTS) == 0 &&
              geode_perf_buffer_size(720, 1080, 2400, 8, 8, out, 1) == 0,
          "no view or a short out writes nothing");
}

// The first-run pick, a default the user may change (decision 44: nothing lowers it for load).
void devicePick() {
    check(modeForDevice(2 * gb, false, 0, 1080, 2400) == QualityMode::Saver, "2 GB starts on Saver");
    check(modeForDevice(8 * gb, true, 34, 1080, 2400) == QualityMode::Saver, "a low-RAM phone starts on Saver whatever else");
    check(modeForDevice(4 * gb, false, 0, 1080, 2400) == QualityMode::Balanced, "4 GB starts on Balanced");
    check(modeForDevice(8 * gb, false, 0, 720, 1600) == QualityMode::Balanced, "RAM alone never picks High");
    check(modeForDevice(16 * gb, false, 30, 1080, 2400) == QualityMode::Balanced, "nor does a performance class under 31");
    check(modeForDevice(0, false, 0, 0, 0) == QualityMode::Balanced, "unknown RAM is not read as small");
    check(modeForDevice(kSaverBelowRam, false, 0, 1080, 2400) == QualityMode::Balanced, "exactly 3000 MB is Balanced");
    check(modeForDevice(kSaverBelowRam - 1, false, 31, 1080, 2400) == QualityMode::Saver, "Saver's RAM rule beats the class");
    check(modeForDevice(4 * gb, false, 31, 1080, 2400) == QualityMode::High, "class 31 on a 1080p panel is High");
    check(modeForDevice(4 * gb, false, 31, 1080, 2640) == QualityMode::High, "a tall 1080p panel too");
    check(modeForDevice(4 * gb, false, 31, 720, 1600) == QualityMode::High, "and a 720p one");
    check(modeForDevice(4 * gb, false, 31, 0, 0) == QualityMode::High, "an unknown panel is not read as big");
    check(modeForDevice(8 * gb, false, 33, 1440, 3200) == QualityMode::Balanced, "a 1440p panel under class 34 starts on Balanced");
    check(modeForDevice(8 * gb, false, 31, 1344, 2992) == QualityMode::Balanced, "so does a 1344p one");
    check(modeForDevice(12 * gb, false, 34, 1440, 3200) == QualityMode::High, "class 34 is High on any panel");
    check(modeForDevice(12 * gb, false, 0, 1440, 3200) == QualityMode::Balanced, "no class on 1440p: Balanced");
}

// The tab's "Custom": a preset only while every compared value is the tier's, resolutions as drawn.
void presetMatch() {
    check(modeOf(540, 30, 0.5f, false, 1080) == 0, "Saver's values are Saver");
    check(modeOf(900, 60, 0.75f, true, 1080) == 1, "Balanced's values are Balanced");
    check(modeOf(kNativeResolution, 60, 1.0f, true, 1440) == 2, "High's values are High");
    check(modeOf(kNativeResolution, 60, 0.75f, true, 720) == 1, "Balanced on a 720p panel draws Native: still Balanced");
    check(modeOf(kNativeResolution, 30, 1.0f, true, 1080) == -1, "High at 30 fps is Custom");
    check(modeOf(kNativeResolution, GEODE_LIVE_FPS_FOLLOW, 1.0f, true, 1080) == -1, "Follow device is no tier's cap");
    check(modeOf(720, 60, 0.75f, true, 1080) == -1, "Balanced at 720 is Custom");
    check(modeOf(900, 60, 0.75f, false, 1080) == -1, "Balanced without post FX is Custom");
}

// An old install's perf_render_scale, mapped once to the nearest resolution; never to a lower one on a tie.
void migration() {
    check(resolutionForScale(1.0f, 1080, 2400) == kNativeResolution, "100% is Native");
    check(resolutionForScale(0.75f, 1080, 2400) == 900, "75% on 1080p (810) is 900, the higher of 720 and 900");
    check(resolutionForScale(0.5f, 1080, 2400) == 540, "50% on 1080p is 540");
    check(resolutionForScale(0.75f, 1440, 3200) == 1080, "75% on 1440p is 1080");
    check(resolutionForScale(0.5f, 1440, 3200) == 720, "50% on 1440p is 720");
    check(resolutionForScale(1.0f, 1440, 3200) == kNativeResolution, "100% on 1440p is Native");
    check(resolutionForScale(0.5f, 720, 1600) == 540, "50% on 720p is 540, the smallest offered");
    check(resolutionForScale(0.75f, 720, 1600) == 540, "75% on 720p is 540");
    check(resolutionForScale(0.75f, 0, 0) == 900, "an unknown panel reads as 1080p");
    check(resolutionForScale(std::numeric_limits<float>::quiet_NaN(), 1080, 2400) == kNativeResolution, "a corrupt scale is Native");
    check(resolutionForScale(0.01f, 1080, 2400) == 540 && resolutionForScale(9.0f, 1080, 2400) == kNativeResolution,
          "an out-of-range scale holds to 0.25..1");
    // The old tiers land on the new ones on a 1080p phone, so an untouched first-run pick keeps its name.
    check(modeOf(resolutionForScale(0.5f, 1080, 2400), 30, 0.5f, false, 1080) == 0, "old Saver is Saver on 1080p");
    check(modeOf(resolutionForScale(0.75f, 1080, 2400), 60, 0.75f, true, 1080) == 1, "old Balanced is Balanced on 1080p");
    check(modeOf(resolutionForScale(1.0f, 1440, 3200), 60, 1.0f, true, 1440) == 2, "old High is High anywhere");
}

// The state the app holds: events in, one descriptor out, nothing for the reader to compute.
void resolutionState() {
    ResolutionState s;
    check(s.descriptor()[kFieldStepCount] == kMaxResolutionChoices && s.descriptor()[kFieldBufferWidth] == 0,
          "before any event: every step, no buffer");
    s.setPanel({1080, 2400, 1080, 2400});
    s.setView({1080, 2400});
    s.setSettings({900, 60, 0.75f, true});
    const Descriptor& d = s.descriptor();
    check(d[kFieldResolution] == 900 && d[kFieldPreset] == 1, "Balanced's values are the Balanced preset");
    check(d[kFieldStepCount] == 4 && d[kFieldSteps] == 540 && d[kFieldSteps + 3] == kNativeResolution,
          "a 1080p panel offers 540, 720, 900 and Native");
    check(d[kFieldSelected] == 2, "900 is the third step");
    check(d[kFieldBufferWidth] == 900 && d[kFieldBufferHeight] == 2000, "the buffer is the view at 900");
    check(d[kFieldDrawnWidth] == 900 && d[kFieldDrawnHeight] == 2000 && d[kFieldModeWidth] == 1080 && d[kFieldModeHeight] == 2400,
          "what it draws full screen, and the mode it is on");

    s.setView({1200, 540});
    check(s.descriptor()[kFieldBufferWidth] == 1000 && s.descriptor()[kFieldBufferHeight] == 450, "a new view, a new buffer");
    check(s.descriptor()[kFieldDrawnWidth] == 900, "the full-screen size does not follow the view");

    s.setSettings({900, 30, 0.75f, true});
    check(s.descriptor()[kFieldPreset] == -1 && s.descriptor()[kFieldSelected] == 2, "a changed fps cap reads Custom");

    s.setSettings({kNativeResolution, 60, 1.0f, true});
    check(s.descriptor()[kFieldPreset] == 2 && s.descriptor()[kFieldSelected] == 3, "Native is the last step, and High");
    check(s.descriptor()[kFieldBufferWidth] == 1200, "Native fills the view");

    s.setPanel({720, 1600, 720, 1600});
    check(s.descriptor()[kFieldStepCount] == 2 && s.descriptor()[kFieldSteps] == 540 && s.descriptor()[kFieldSteps + 1] == 0,
          "a 720p panel offers 540 and Native");
    s.setSettings({900, 60, 0.75f, true});
    check(s.descriptor()[kFieldSelected] == 1 && s.descriptor()[kFieldPreset] == 1,
          "900 on a 720p panel draws Native: the Native step, still Balanced");

    ResolutionState unknown;
    unknown.setSettings({720, 60, 1.0f, true});
    check(unknown.descriptor()[kFieldSelected] == 1 && unknown.descriptor()[kFieldDrawnWidth] == 0,
          "an unknown panel offers every step and has no drawn size");
    unknown.setSettings({1000, 60, 1.0f, true});
    check(unknown.descriptor()[kFieldSelected] == -1, "a stored value no step shows selects none");
}

void resolutionStateThroughTheAbi() {
    int d[GEODE_PERF_DESCRIPTOR_INTS] = {};
    check(geode_perf_resolution_read(d, GEODE_PERF_DESCRIPTOR_INTS - 1) == 0 && geode_perf_resolution_read(nullptr, 17) == 0,
          "a short out reads nothing");
    geode_perf_panel_set(1440, 3200, 1080, 2400);
    geode_perf_view_set(1080, 2400);
    geode_perf_settings_set(1080, 60, 0.75f, 1);
    check(geode_perf_resolution_read(d, GEODE_PERF_DESCRIPTOR_INTS) == GEODE_PERF_DESCRIPTOR_INTS, "geode_perf_resolution_read");
    check(d[GEODE_PERF_FIELD_RESOLUTION] == 1080 && d[GEODE_PERF_FIELD_STEP_COUNT] == 5 && d[GEODE_PERF_FIELD_SELECTED] == 3 &&
              d[GEODE_PERF_FIELD_STEPS + 4] == GEODE_PERF_NATIVE,
          "the steps and the selection through the ABI");
    check(d[GEODE_PERF_FIELD_BUFFER_WIDTH] == 1080 && d[GEODE_PERF_FIELD_DRAWN_HEIGHT] == 2400 && d[GEODE_PERF_FIELD_MODE_WIDTH] == 1080,
          "the buffer and the drawn size through the ABI");
}
void throughTheAbi() {
    check(geode_perf_mode_of(900, 60, 0.75f, 1, 1080, 2400) == 1 && geode_perf_mode_of(720, 60, 0.75f, 1, 1080, 2400) == -1,
          "geode_perf_mode_of");
    check(geode_perf_mode_for_device(8 * gb, 0, 33, 1440, 3200) == 1 && geode_perf_mode_for_device(8 * gb, 0, 33, 1080, 2400) == 2,
          "geode_perf_mode_for_device reads the panel");
    float scales[GEODE_PERF_SCALE_CHOICES + 1] = {};
    check(geode_perf_scale_choices(scales, GEODE_PERF_SCALE_CHOICES + 1) == 3 && scales[0] == 0.5f && scales[1] == 0.75f &&
              scales[2] == 1.0f,
          "the march choices, ascending");
    check(geode_perf_scale_choices(scales, 1) == 1 && geode_perf_scale_choices(nullptr, 3) == 0, "march choices fit the capacity");
    float tier[GEODE_PERF_TIER_FLOATS] = {};
    check(geode_perf_tier(1, tier, GEODE_PERF_TIER_FLOATS) == GEODE_PERF_TIER_FLOATS && tier[0] == 900.0f && tier[1] == 60.0f &&
              tier[3] == 1.0f,
          "geode_perf_tier writes Balanced");
    check(geode_perf_tier(2, tier, GEODE_PERF_TIER_FLOATS) == GEODE_PERF_TIER_FLOATS && tier[0] == GEODE_PERF_NATIVE, "High is Native");
    check(geode_perf_tier(3, tier, GEODE_PERF_TIER_FLOATS) == 0, "no fourth tier");
    int res[GEODE_PERF_RESOLUTION_CHOICES] = {};
    check(geode_perf_resolution_choices(1440, 3200, res, GEODE_PERF_RESOLUTION_CHOICES) == 5 && res[4] == GEODE_PERF_NATIVE,
          "geode_perf_resolution_choices");
    check(geode_perf_resolution_for_scale(0.75f, 1440, 3200) == 1080, "geode_perf_resolution_for_scale");
}

}  // namespace

int main() {
    tiers();
    resolutionList();
    drawnSides();
    bufferSizes();
    devicePick();
    presetMatch();
    migration();
    resolutionState();
    resolutionStateThroughTheAbi();
    throughTheAbi();
    if (failures == 0) std::printf("quality_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
