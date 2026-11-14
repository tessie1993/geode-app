// Host-side checks for core/viz/frame/Cadence (#604): what a live surface's frame owes the shell. Built and
// run by CI ("Native host tests" in .github/workflows/android.yml) with the system compiler; no GL.
//
// What is pinned: a new surface's first frame is a clear and nothing else (no Liquid take), once; a repeated
// Settings › Performance push is applied once, a change again, a NaN once, and a new surface applies it again;
// the Liquid field carries the mode only when it changed; the error text is told once per change and once per
// handle; and the bits the shell sees match the ABI.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>

#include "api/geode_api.h"
#include "viz/frame/Cadence.hpp"

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
static_assert(kFrameLiquidChanged == GEODE_FRAME_LIQUID_CHANGED);
static_assert(kFrameErrorChanged == GEODE_FRAME_ERROR_CHANGED);

struct Asked {
    int liquid = 0;
};

// One frame with a Liquid answer, counting what the cadence asked.
uint32_t frame(FrameCadence& c, Asked& asked, int liquid) {
    return c.step([&] {
        asked.liquid++;
        return liquid;
    });
}

void firstFrameIsClearOnly() {
    FrameCadence c;
    Asked asked;
    // A cadence that never saw a surface draws (a handle's first surface always calls surfaceCreated).
    check(frame(c, asked, -1) == 0, "no surface yet: an ordinary frame");
    c.surfaceCreated();
    check(frame(c, asked, 1) == kFrameClearOnly, "the first frame on a new surface is a clear, nothing else");
    check(asked.liquid == 1, "the clear frame does not ask for the label");
    const uint32_t second = frame(c, asked, 1);
    check((second & kFrameClearOnly) == 0, "only the first frame is a clear");
    check((second & kFrameLiquidChanged) != 0 && FrameCadence::liquidModeOf(second) == 1, "the next frame takes the label");
    c.surfaceCreated();
    check(frame(c, asked, -1) == kFrameClearOnly, "every new surface clears its first frame");
}

void qualityDedupes() {
    FrameCadence c;
    const UserQuality high{1.0f, 1.0f, true};
    check(c.offerQuality(high), "the first push is applied");
    check(!c.offerQuality(high), "the same push again is skipped");
    check(!c.offerQuality(UserQuality{1.0f, 1.0f, true}), "an equal copy is skipped too");
    UserQuality lower = high;
    lower.renderScale = 0.75f;
    check(c.offerQuality(lower), "a new render scale is applied");
    UserQuality fx = lower;
    fx.postFx = false;
    check(c.offerQuality(fx), "post FX off is applied");
    check(!c.offerQuality(fx), "and then skipped");
    UserQuality corrupt = fx;
    corrupt.marchScale = std::numeric_limits<float>::quiet_NaN();
    check(c.offerQuality(corrupt), "a NaN from a corrupt pref is applied once");
    check(!c.offerQuality(corrupt), "and not on every frame after");
    c.surfaceCreated();
    check(c.offerQuality(corrupt), "a new surface applies the quality again");
}

void liquidField() {
    FrameCadence c;
    Asked asked;
    for (int mode = 0; mode <= 4; ++mode) {
        const uint32_t due = frame(c, asked, mode);
        check((due & kFrameLiquidChanged) != 0, "a changed mode sets the bit");
        check(FrameCadence::liquidModeOf(due) == mode, "and carries the mode");
    }
    check(frame(c, asked, -1) == 0, "an unchanged mode sets nothing");
    check((frame(c, asked, 0) & kFrameLiquidChanged) != 0, "mode 0 (off) is still a change");
}

// Kills: an offer that is always true (the shell reads the text every frame again), one that is never true
// for an empty text (a new handle leaves the shell showing the last handle's error), and one that does not
// keep what it reported.
void errorIsToldOncePerChange() {
    FrameCadence c;
    check(c.offerError(""), "a handle's first frame reports, even a clean one");
    check(!c.offerError(""), "a clean renderer then says nothing, frame after frame");
    check(c.offerError("shader failed"), "a new error is told");
    check(!c.offerError("shader failed"), "once");
    check(c.offerError("link failed"), "another error is a change");
    check(c.offerError(""), "and so is its clearing");
    c.surfaceCreated();
    check(!c.offerError(""), "a new surface on the same handle has nothing new to say");
    const uint32_t others = kFrameClearOnly | kFrameLiquidChanged | (kFrameLiquidMask << kFrameLiquidShift);
    check((kFrameErrorChanged & others) == 0 && (kFrameErrorChanged & 0xF0u) == 0, "the bit is its own");
}

}  // namespace

int main() {
    firstFrameIsClearOnly();
    qualityDedupes();
    liquidField();
    errorIsToldOncePerChange();
    if (failures) {
        std::fprintf(stderr, "renderer_cadence_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("renderer_cadence_test: ok\n");
    return 0;
}
