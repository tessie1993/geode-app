// Host-side checks for the live wallpaper's and the dream's run rule (core/viz/SurfaceRun) through the C ABI
// the shell calls. Built and run by CI ("Native host tests" in .github/workflows/android.yml); no GL, no device.
//
// What is pinned: the rule's priority order (stopped beats still beats moving), the preview's exemptions,
// listening ignoring the cover and the silence, and the trial's end (#376): a trial that runs out turns a
// moving surface still, and the caller is told to restate just after that moment, never before. Then
// reduced motion (#441, ADR 0008): either switch holds a still frame on the wallpaper and on the dream, the
// preview too, and never wakes a stopped surface; the still frame's settle; and the per-surface fps clamp.
#include <cstdio>

#include "api/geode_api.h"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

constexpr int kStopped = 0;
constexpr int kStill = 1;
constexpr int kAnimated = 2;

constexpr unsigned kOnScreen = GEODE_SURFACE_HAS_SURFACE | GEODE_SURFACE_VISIBLE;
constexpr unsigned kDefaults = GEODE_SURFACE_PAUSE_IN_BATTERY_SAVER | GEODE_SURFACE_REST_WHEN_SILENT;
constexpr long long kPro = GEODE_SURFACE_FOREVER;
constexpr long long kFree = 0;
constexpr long long kNow = 1'000'000;

int mode(unsigned flags, long long until = kPro) { return geode_surface_run_mode(flags, until, kNow); }

void priorityOrder() {
    check(mode(kDefaults) == kStopped, "no surface, not visible: stopped");
    check(mode(GEODE_SURFACE_HAS_SURFACE | kDefaults) == kStopped, "not visible: stopped");
    check(mode(GEODE_SURFACE_VISIBLE | kDefaults) == kStopped, "no surface: stopped");
    check(mode(kOnScreen | kDefaults) == kAnimated, "on screen, Pro: moving");
    check(mode(kOnScreen | kDefaults | GEODE_SURFACE_COVERED) == kStopped, "covered: stopped");
    check(mode(kOnScreen | kDefaults, kFree) == kStill, "free: still");
    check(mode(kOnScreen | kDefaults | GEODE_SURFACE_COVERED, kFree) == kStopped, "covered beats free");
    check(mode(kOnScreen | kDefaults | GEODE_SURFACE_BATTERY_SAVER) == kStill, "battery saver: still");
    check(mode(kOnScreen | GEODE_SURFACE_BATTERY_SAVER) == kAnimated, "battery saver, option off: moving");
    check(mode(kOnScreen | kDefaults | GEODE_SURFACE_SILENT) == kStill, "silent: still");
    check(mode(kOnScreen | GEODE_SURFACE_SILENT) == kAnimated, "silent, rest option off: moving");
}

void preview() {
    const unsigned p = kOnScreen | kDefaults | GEODE_SURFACE_PREVIEW;
    check(mode(p | GEODE_SURFACE_COVERED) == kAnimated, "preview ignores the cover");
    check(mode(p | GEODE_SURFACE_BATTERY_SAVER | GEODE_SURFACE_SILENT) == kAnimated, "preview never rests");
    check(mode(p, kFree) == kStill, "preview without Pro: still");
}

void listening() {
    const unsigned f = kOnScreen | kDefaults;
    check(geode_surface_listening(f | GEODE_SURFACE_COVERED, kPro, kNow) == 1, "covered keeps listening");
    check(geode_surface_listening(f | GEODE_SURFACE_SILENT, kPro, kNow) == 1, "silence keeps listening");
    check(geode_surface_listening(f | GEODE_SURFACE_BATTERY_SAVER, kPro, kNow) == 0, "battery saver hears nothing");
    check(geode_surface_listening(f, kFree, kNow) == 0, "free hears nothing");
    check(geode_surface_listening(GEODE_SURFACE_HAS_SURFACE | kDefaults, kPro, kNow) == 0, "hidden hears nothing");
}

void trialEnd() {
    const unsigned f = kOnScreen | kDefaults;
    const long long ends = kNow + 60'000;
    check(geode_surface_run_mode(f, ends, kNow) == kAnimated, "running trial: moving");
    check(geode_surface_run_mode(f, ends, ends - 1) == kAnimated, "trial's last millisecond: moving");
    check(geode_surface_run_mode(f, ends, ends) == kStill, "trial over: still");
    check(geode_surface_entitled(ends, kNow) == 1 && geode_surface_entitled(ends, ends) == 0, "dream ends with the trial");

    const long long wait = geode_surface_recheck_in_ms(ends, kNow);
    check(wait > ends - kNow, "restate lands after the end, never before");
    check(wait <= ends - kNow + 2'000, "restate lands soon after the end");
    check(geode_surface_run_mode(f, ends, kNow + wait) == kStill, "the restate finds it still");

    check(geode_surface_recheck_in_ms(kPro, kNow) == -1, "Pro never rechecks");
    check(geode_surface_recheck_in_ms(kFree, kNow) == -1, "free never rechecks");
    check(geode_surface_recheck_in_ms(ends, ends + 5) == -1, "a trial already over never rechecks");
}

// The dream restates with the surface fixed and never as a preview (it has a window, not a picker); the
// wallpaper with whatever its engine sees. Both must come out still under reduced motion.
constexpr unsigned kDream = kOnScreen | kDefaults;
constexpr unsigned kWallpaper = kOnScreen | kDefaults;
constexpr unsigned kReduced = GEODE_SURFACE_REDUCED_MOTION;

void reducedMotion() {
    check(mode(kWallpaper | kReduced) == kStill, "wallpaper, reduced motion: still");
    check(mode(kDream | kReduced) == kStill, "dream, reduced motion: still");
    check(mode(kOnScreen | kReduced) == kStill, "reduced motion with every rest option off: still");
    check(mode(kWallpaper | kReduced | GEODE_SURFACE_PREVIEW) == kStill, "preview under reduced motion: still");
    check(mode(kWallpaper | kReduced | GEODE_SURFACE_PREVIEW | GEODE_SURFACE_COVERED) == kStill,
          "a covered preview stays the picker's: still, not stopped");

    // Stopped beats still: reduced motion never draws on a surface nobody can see.
    check(mode(GEODE_SURFACE_HAS_SURFACE | kDefaults | kReduced) == kStopped, "reduced motion, not visible: stopped");
    check(mode(GEODE_SURFACE_VISIBLE | kDefaults | kReduced) == kStopped, "reduced motion, no surface: stopped");
    check(mode(kWallpaper | kReduced | GEODE_SURFACE_COVERED) == kStopped, "reduced motion, covered: stopped");
    check(mode(kDream | kReduced | GEODE_SURFACE_COVERED, kFree) == kStopped, "covered beats free and reduced motion");

    // Still with or without the other reasons; none of them turns it back to moving.
    check(mode(kWallpaper | kReduced, kFree) == kStill, "reduced motion and free: still");
    check(mode(kWallpaper | kReduced | GEODE_SURFACE_SILENT | GEODE_SURFACE_BATTERY_SAVER) == kStill,
          "reduced motion, silent, battery saver: still");

    // A surface that cannot move hears nothing, so the capture is released: the still frame no longer
    // reacts to the music (the owner's call for vestibular safety).
    check(geode_surface_listening(kWallpaper | kReduced, kPro, kNow) == 0, "reduced motion hears nothing");
    check(geode_surface_listening(kDream | kReduced | GEODE_SURFACE_SILENT, kPro, kNow) == 0,
          "reduced motion in silence hears nothing");

    // The trial's restate is about the entitlement alone, reduced motion or not.
    const long long ends = kNow + 60'000;
    check(geode_surface_run_mode(kDream | kReduced, ends, kNow) == kStill, "running trial, reduced motion: still");
    check(geode_surface_entitled(ends, kNow) == 1, "reduced motion does not end the dream");
}

void settle() {
    check(geode_surface_settle_ms(kAnimated, kStill) == 2'000, "moving to still: settle 2 s");
    check(geode_surface_settle_ms(kStopped, kStill) == 2'000, "stopped to still: settle 2 s (a fresh scene builds)");
    check(geode_surface_settle_ms(kStill, kStill) == 0, "already still: one frame");
    check(geode_surface_settle_ms(kStill, kAnimated) == 0, "to moving: no settle");
    check(geode_surface_settle_ms(kStill, kStopped) == 0, "to stopped: no settle");
    check(geode_surface_settle_ms(kAnimated, kAnimated) == 0, "moving stays moving: no settle");
}

void fps() {
    check(GEODE_SURFACE_DEFAULT_FPS == 30, "default 30 (owner, ADR 0008)");
    check(geode_surface_fps_choice(0) == 30, "unset: the default");
    check(geode_surface_fps_choice(-5) == 30, "negative: the default");
    check(geode_surface_fps_choice(15) == 15, "15 is listed");
    check(geode_surface_fps_choice(30) == 30, "30 is listed");
    check(geode_surface_fps_choice(60) == 60, "60 is listed");
    check(geode_surface_fps_choice(1) == 15, "below the list: the lowest");
    check(geode_surface_fps_choice(240) == 60, "above the list: the highest");
    check(geode_surface_fps_choice(24) == 30, "24 snaps to the nearest, 30");
    check(geode_surface_fps_choice(45) == 30, "a tie takes the lower rate");
    check(geode_surface_fps_choice(50) == 60, "50 snaps to 60");

    int out[8] = {};
    const int n = geode_surface_fps_choices(out, 8);
    check(n == 3 && out[0] == 15 && out[1] == 30 && out[2] == 60, "the list: 15, 30, 60");
    bool listed = true;
    for (int i = 0; i < n; ++i) listed = listed && geode_surface_fps_choice(out[i]) == out[i];
    check(listed, "every listed rate is its own choice");
    int one[1] = {};
    check(geode_surface_fps_choices(one, 1) == 3 && one[0] == 15, "a short buffer gets the head, the count is whole");
    check(geode_surface_fps_choices(nullptr, 0) == 3, "a null buffer only counts");
}

}  // namespace

int main() {
    priorityOrder();
    preview();
    listening();
    trialEnd();
    reducedMotion();
    settle();
    fps();
    if (failures == 0) std::printf("surface_run_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
