// Host-side checks for core/viz/frame/PushVersions (#751): which of the shell's pushed inputs (scene params,
// LFO configs, ADSR configs, sound tuning) a live frame still owes native. Built and run by CI ("Native host
// tests" in .github/workflows/android.yml) with the system compiler; no GL.
//
// What is pinned: a fresh surface wants every input; each version is wanted once, whichever inputs move;
// an equal or older version (out of order, or a counter that went back) is ignored and does not undo the
// newer one; a new surface wants every input again at the versions it already had; and the bits match the
// ABI and stay clear of FrameCadence's bits and Liquid field.
#include <cstdint>
#include <cstdio>

#include "api/geode_api.h"
#include "viz/frame/Cadence.hpp"
#include "viz/frame/PushVersions.hpp"

using namespace geode::viz::frame;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

static_assert(kFramePushParams == GEODE_FRAME_PUSH_PARAMS);
static_assert(kFramePushLfo == GEODE_FRAME_PUSH_LFO);
static_assert(kFramePushAdsr == GEODE_FRAME_PUSH_ADSR);
static_assert(kFramePushTuning == GEODE_FRAME_PUSH_TUNING);

constexpr uint32_t kAll = kFramePushParams | kFramePushLfo | kFramePushAdsr | kFramePushTuning;

void bitsAreTheirOwn() {
    const uint32_t cadence = kFrameClearOnly | kFrameThermalDue | kFrameLiquidChanged | (kFrameLiquidMask << kFrameLiquidShift);
    check((kAll & cadence) == 0, "the push bits share no bit with the cadence's bits or the Liquid field");
    check(GEODE_FRAME_LIQUID_MODE(kAll) == 0, "the push bits read as Liquid mode 0 through the ABI macro");
}

void appliedOncePerVersion() {
    PushVersions pushes;
    // A handle that never saw surfaceCreated still starts with nothing applied.
    check(pushes.offer({0, 0, 0, 0}) == kAll, "a fresh renderer wants every input at the shell's first versions");
    check(pushes.offer({0, 0, 0, 0}) == 0, "the same versions again want nothing");
    check(pushes.offer({0, 0, 0, 0}) == 0, "nor on every frame after");
    check(pushes.offer({5, 0, 0, 0}) == kFramePushParams, "new params alone want the params alone");
    check(pushes.offer({5, 0, 0, 0}) == 0, "and only once");
    check(pushes.offer({5, 6, 7, 0}) == (kFramePushLfo | kFramePushAdsr), "LFO and ADSR moved together want both");
    check(pushes.offer({5, 6, 7, 8}) == kFramePushTuning, "a new tuning wants the tuning");
    // The shell's counter is shared by every input, so versions jump; a jump is still one push.
    check(pushes.offer({100, 6, 7, 8}) == kFramePushParams, "a version that jumps ahead is wanted once");
    check(pushes.offer({100, 6, 7, 8}) == 0, "and then skipped");
}

void newSurfaceReapplies() {
    PushVersions pushes;
    pushes.surfaceCreated();
    check(pushes.offer({3, 4, 5, 6}) == kAll, "a new surface's first frame wants every input");
    check(pushes.offer({3, 4, 5, 6}) == 0, "then nothing at the same versions");
    pushes.surfaceCreated();
    check(pushes.offer({3, 4, 5, 6}) == kAll, "a second new surface (a lost EGL context) wants them all again");
    check(pushes.offer({3, 4, 5, 6}) == 0, "once");
    pushes.surfaceCreated();
    check(pushes.offer({3, 9, 5, 6}) == kAll, "a new surface with one new version still wants every input");
}

void staleIgnored() {
    PushVersions pushes;
    check(pushes.offer({10, 10, 10, 10}) == kAll, "first push");
    check(pushes.offer({9, 10, 10, 10}) == 0, "an older params version is ignored");
    check(pushes.offer({10, 3, 2, 1}) == 0, "older LFO, ADSR and tuning versions are ignored");
    check(pushes.offer({-1, -1, -1, -1}) == 0, "a counter that went below zero is ignored");
    check(pushes.offer({10, 10, 10, 10}) == 0, "a stale version did not lower what was applied");
    check(pushes.offer({11, 10, 10, 10}) == kFramePushParams, "the next newer version is wanted");
    // Out of order: 13 arrives before 12; 12 must not count as a change after 13.
    check(pushes.offer({13, 10, 10, 10}) == kFramePushParams, "13 is wanted");
    check(pushes.offer({12, 10, 10, 10}) == 0, "12 after 13 is ignored");
    check(pushes.offer({13, 10, 10, 10}) == 0, "and 13 is not wanted twice");
    // A new surface forgets what was applied, so even the old versions are wanted there once.
    pushes.surfaceCreated();
    check(pushes.offer({12, 10, 10, 10}) == kAll, "a new surface wants whatever version the shell holds");
    check(pushes.offer({11, 10, 10, 10}) == 0, "and ignores one older than that afterwards");
}

}  // namespace

int main() {
    bitsAreTheirOwn();
    appliedOncePerVersion();
    newSurfaceReapplies();
    staleIgnored();
    if (failures) {
        std::fprintf(stderr, "push_versions_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("push_versions_test: ok\n");
    return 0;
}
