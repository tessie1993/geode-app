// Host-side checks for the live wallpaper's and the dream's run rule (core/viz/SurfaceRun) through the C ABI
// the shell calls. Built and run by CI ("Native host tests" in .github/workflows/android.yml); no GL, no device.
//
// What is pinned: the rule's priority order (stopped beats still beats moving), the preview's exemptions,
// listening ignoring the cover and the silence, and the trial's end (#376): a trial that runs out turns a
// moving surface still, and the caller is told to restate just after that moment, never before.
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

}  // namespace

int main() {
    priorityOrder();
    preview();
    listening();
    trialEnd();
    if (failures == 0) std::printf("surface_run_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
