// Host-side checks for core/viz/frame/Cadence (#604): what a live surface's frame owes the shell. Built and
// run by CI ("Native host tests" in .github/workflows/android.yml) with the system compiler; no GL.
//
// What is pinned: a new surface's first frame is a clear and nothing else (no thermal claim, no Liquid
// take), once; a repeated Settings › Performance push is applied once, a change again, a NaN once, and a
// new surface applies it again; the thermal bit goes to one caller a second across two surfaces sharing
// one ThermalState; the Liquid field carries the mode only when it changed; and the bits match the ABI.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

#include "api/geode_api.h"
#include "viz/frame/Cadence.hpp"
#include "viz/frame/Thermal.hpp"

using namespace geode::viz;
using namespace geode::viz::frame;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

static_assert(kFrameClearOnly == GEODE_FRAME_CLEAR_ONLY);
static_assert(kFrameThermalDue == GEODE_FRAME_THERMAL_DUE);
static_assert(kFrameLiquidChanged == GEODE_FRAME_LIQUID_CHANGED);

struct Asked {
    int thermal = 0;
    int liquid = 0;
};

// One frame with a thermal answer and a Liquid answer, counting what the cadence asked.
uint32_t frame(FrameCadence& c, Asked& asked, bool thermal, int liquid) {
    return c.step(
        [&] {
            asked.thermal++;
            return thermal;
        },
        [&] {
            asked.liquid++;
            return liquid;
        });
}

void firstFrameIsClearOnly() {
    FrameCadence c;
    Asked asked;
    // A cadence that never saw a surface draws (a handle's first surface always calls surfaceCreated).
    check(frame(c, asked, false, -1) == 0, "no surface yet: an ordinary frame");
    c.surfaceCreated();
    check(frame(c, asked, true, 3) == kFrameClearOnly, "the first frame on a new surface is a clear, nothing else");
    check(asked.thermal == 1 && asked.liquid == 1, "the clear frame asks neither the thermal slot nor the label");
    const uint32_t second = frame(c, asked, true, 3);
    check((second & kFrameClearOnly) == 0, "only the first frame is a clear");
    check((second & kFrameThermalDue) != 0, "the next frame takes the thermal slot");
    check((second & kFrameLiquidChanged) != 0 && FrameCadence::liquidModeOf(second) == 3, "and the label");
    c.surfaceCreated();
    check(frame(c, asked, false, -1) == kFrameClearOnly, "every new surface clears its first frame");
}

void qualityDedupes() {
    FrameCadence c;
    const UserQuality high{1.0f, 1.0f, true, true, 0};
    check(c.offerQuality(high), "the first push is applied");
    check(!c.offerQuality(high), "the same push again is skipped");
    check(!c.offerQuality(UserQuality{1.0f, 1.0f, true, true, 0}), "an equal copy is skipped too");
    UserQuality lower = high;
    lower.renderScale = 0.75f;
    check(c.offerQuality(lower), "a new render scale is applied");
    UserQuality hot = lower;
    hot.liquidHotPolicy = 2;
    check(c.offerQuality(hot), "a new Liquid hot policy is applied");
    UserQuality fx = hot;
    fx.postFx = false;
    check(c.offerQuality(fx), "post FX off is applied");
    UserQuality fixed = fx;
    fixed.adaptive = false;
    check(c.offerQuality(fixed), "adaptive off is applied");
    check(!c.offerQuality(fixed), "and then skipped");
    UserQuality corrupt = fixed;
    corrupt.marchScale = std::numeric_limits<float>::quiet_NaN();
    check(c.offerQuality(corrupt), "a NaN from a corrupt pref is applied once");
    check(!c.offerQuality(corrupt), "and not on every frame after");
    c.surfaceCreated();
    check(c.offerQuality(corrupt), "a new surface applies the quality again");
}

void thermalOncePerSecondAcrossSurfaces() {
    ThermalState state;
    FrameCadence live;
    FrameCadence wallpaper;
    live.surfaceCreated();
    wallpaper.surfaceCreated();
    double now = 100.0;
    const auto step = [&](FrameCadence& c) {
        return c.step([&] { return state.claimSample(now); }, [] { return -1; });
    };
    check(step(live) == kFrameClearOnly && step(wallpaper) == kFrameClearOnly, "both surfaces clear first");
    int due = 0;
    // Two surfaces at 60 fps for ten seconds: ten readings for the process, not twenty.
    for (int i = 0; i < 600; ++i) {
        now = 100.0 + i / 60.0;
        if (step(live) & kFrameThermalDue) due++;
        if (step(wallpaper) & kFrameThermalDue) due++;
    }
    check(due == 10, "one thermal reading a second for the whole process");
}

void liquidField() {
    FrameCadence c;
    Asked asked;
    for (int mode = 0; mode <= 4; ++mode) {
        const uint32_t due = frame(c, asked, false, mode);
        check((due & kFrameLiquidChanged) != 0, "a changed mode sets the bit");
        check(FrameCadence::liquidModeOf(due) == mode, "and carries the mode");
        check(GEODE_FRAME_LIQUID_MODE(due) == mode, "the ABI macro reads the same field");
    }
    check(frame(c, asked, false, -1) == 0, "an unchanged mode sets nothing");
    check((frame(c, asked, false, 0) & kFrameLiquidChanged) != 0, "mode 0 (off) is still a change");
}

}  // namespace

int main() {
    firstFrameIsClearOnly();
    qualityDedupes();
    thermalOncePerSecondAcrossSurfaces();
    liquidField();
    if (failures) {
        std::fprintf(stderr, "renderer_cadence_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("renderer_cadence_test: ok\n");
    return 0;
}
