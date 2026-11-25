// Host-side checks for Auto visuals' decisions (core/viz/director/AutoPlan, geode_auto_*), as the Kotlin controller
// behaved before they moved here. No GL, no device.
//
// What is pinned:
// - the playlist steps on its timer only while it is on, has two entries or more, something plays and Customize is
//   shut; it wraps; a step by hand goes either way, wraps both ways and restarts the wait;
// - the timed draw is due only while on, playing and not held; closing Customize starts both waits over;
// - a director moment is skipped while held, and when the look changed since Auto's last change (and every moment
//   while the user keeps changing it); otherwise it brings the playlist's next entry or a draw;
// - the draw's pool: styles, then saved looks, none the entitlement refuses; a moment's draw takes
//   no saved look; the entry on screen is the first equal one;
// - the timer interval is held to 5..300 s.
#include <cmath>
#include <cstdio>
#include <limits>
#include <set>
#include <string>
#include <vector>

#include "api/geode_api.h"
#include "viz/director/AutoPlan.hpp"

namespace plan = geode::viz::auto_plan;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// Well past any spread: a wait is at most the interval plus 10 s.
constexpr std::int64_t kLong = 60'000;

plan::Look look(const char* style, float v, const char* palette = "") { return plan::Look{style, {v, 0.5f}, palette}; }

void playlistTimer() {
    plan::Plan p;
    std::int64_t t = 1'000'000;
    geode_auto_timer_restart(GEODE_AUTO_TIMER_PLAYLIST, t);
    check(p.playlistTick(3, true, true, 7, t + 1'000) == -1, "not before the interval");
    check(p.playlistTick(3, true, true, 7, t + kLong) == 1, "steps to the next entry once due");
    t += kLong;
    check(p.playlistTick(3, true, true, 7, t + kLong) == 2, "and the next");
    t += kLong;
    check(p.playlistTick(3, true, true, 7, t + kLong) == 0, "and wraps");
    t += kLong;
    check(p.playlistTick(1, true, true, 7, t + kLong) == -1, "one entry: nothing to step to");
    check(p.playlistTick(3, false, true, 7, t + 2 * kLong) == -1, "off: never");
    check(p.playlistTick(3, true, false, 7, t + 3 * kLong) == -1, "nothing plays: held");
    p.setEditing(true, t + 3 * kLong);
    check(p.playlistTick(3, true, true, 7, t + 4 * kLong) == -1, "Customize open: held");
    p.setEditing(false, t + 5 * kLong);
    check(p.playlistTick(3, true, true, 7, t + 5 * kLong + 1'000) == -1, "closing Customize starts the wait over");
    check(p.playlistTick(3, true, true, 7, t + 5 * kLong + kLong) == 1, "and it runs again");
}

void playlistSteps() {
    plan::Plan p;
    check(p.playlistStep(1, 1, 0) == -1, "fewer than two: no step");
    check(p.playlistStep(4, 1, 0) == 1, "forward");
    check(p.playlistStep(4, -1, 0) == 0, "back");
    check(p.playlistStep(4, -1, 0) == 3, "back wraps");
    check(p.playlistStep(4, 5, 0) == 0, "any positive step is one forward and wraps");
    std::int64_t t = 9'000'000;
    geode_auto_timer_restart(GEODE_AUTO_TIMER_PLAYLIST, t - kLong);
    p.playlistStep(4, 1, t);
    check(p.playlistTick(4, true, true, 7, t + 1'000) == -1, "a step restarts the wait");
}

void randomTimer() {
    plan::Plan p;
    const std::int64_t t = 20'000'000;
    p.handPick(t);
    check(!p.randomTick(true, true, 7, t + 1'000), "a hand pick starts the wait over");
    check(p.randomTick(true, true, 7, t + kLong), "due after the wait");
    check(!p.randomTick(false, true, 7, t + 3 * kLong), "off: never");
    check(!p.randomTick(true, false, 7, t + 4 * kLong), "nothing plays: held");
}

void moments() {
    plan::Plan p;
    check(p.moment(look("silk", 1), false, false) == plan::kMomentNone, "held while nothing plays");
    check(p.moment(look("silk", 1), true, false) == plan::kMomentRandom, "first moment: a draw");
    check(p.moment(look("silk", 1), true, true) == plan::kMomentPlaylist, "same look, playlist on: its next entry");
    check(p.moment(look("silk", 2), true, true) == plan::kMomentNone, "a control moved: the user's moment");
    check(p.moment(look("julia", 2), true, true) == plan::kMomentNone, "a style picked: still the user's");
    check(p.moment(look("julia", 2), true, true) == plan::kMomentPlaylist, "they stopped: Auto picks up");
    p.leftAs(look("crystal_canyon", 3));
    check(p.moment(look("crystal_canyon", 3), true, false) == plan::kMomentRandom, "Auto's own change is not the user's");
    check(p.moment(look("crystal_canyon", 3, "mine"), true, false) == plan::kMomentNone, "a saved palette chosen is a change");
    p.setEditing(true, 0);
    check(p.moment(look("x", 9), true, false) == plan::kMomentNone, "Customize open: held");
    p.setEditing(false, 0);
    check(p.moment(look("crystal_canyon", 3, "mine"), true, false) == plan::kMomentRandom, "a held moment leaves the look it saw");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    check(plan::Look{"a", {nan}, ""} == plan::Look{"a", {nan}, ""}, "NaN equals NaN, as the shell's frames compared");
    check(!(plan::Look{"a", {0.0f}, ""} == plan::Look{"a", {-0.0f}, ""}), "0 and -0 differ, as the shell's frames compared");
}

void pools() {
    const std::vector<std::string> styles = {"silk", "julia", "plasma", "fluid"};
    const std::set<std::string> locked = {"fluid"};
    const std::vector<std::string> looks = {"julia", "fluid", "silk"};
    const int all = plan::kDrawStyles | plan::kDrawPresets;
    auto p = plan::pool(styles, locked, "julia", looks, 0, all);
    check(p.ids == "silk\njulia\nplasma\njulia\nsilk", "styles, then looks; no refused style");
    check(p.onScreen == 1, "the style on screen comes first");
    check(p.entries.size() == 5 && p.entries[3] == std::make_pair(plan::kKindPreset, 0) &&
              p.entries[4] == std::make_pair(plan::kKindPreset, 2),
          "each entry names its own list");
    p = plan::pool(styles, locked, "julia", looks, 0, plan::kDrawPresets);
    check(p.onScreen == 0, "without styles the saved look on screen");
    p = plan::pool(styles, locked, "silk", looks, 2, all | plan::kDrawKeepLook);
    check(p.ids == "silk\njulia\nplasma", "a moment's draw keeps the look: no saved look");
    p = plan::pool(styles, locked, "silk", looks, -1, 0);
    check(p.ids.empty() && p.onScreen == -1, "no source: an empty pool");
}

void intervals() {
    check(geode_auto_interval_s(0) == 5 && geode_auto_interval_s(-77) == 5, "at least 5 s");
    check(geode_auto_interval_s(93) == 93, "inside the range as asked");
    check(geode_auto_interval_s(1'000) == 300, "at most 300 s");
}

void api() {
    const float a[2] = {0.25f, 0.5f};
    const float b[2] = {0.75f, 0.5f};
    geode_auto_left_as("silk", a, 2, nullptr);
    check(geode_auto_moment("silk", a, 2, "", 1, 0) == GEODE_AUTO_MOMENT_RANDOM, "a NULL palette id is \"\"");
    check(geode_auto_moment("silk", b, 2, "", 1, 1) == GEODE_AUTO_MOMENT_NONE, "the frame is compared");
    check(geode_auto_moment("silk", b, 2, "", 1, 1) == GEODE_AUTO_MOMENT_PLAYLIST, "then Auto's again");
    geode_auto_editing_set(1, 0);
    check(geode_auto_moment("silk", b, 2, "", 1, 1) == GEODE_AUTO_MOMENT_NONE, "held while Customize is open");
    geode_auto_editing_set(0, 0);
    check(geode_auto_playlist_step(2, 1, 0) == 1 && geode_auto_playlist_step(2, 1, 0) == 0, "the session's playlist index");
}

}  // namespace

int main() {
    playlistTimer();
    playlistSteps();
    randomTimer();
    moments();
    pools();
    intervals();
    api();
    if (failures == 0) std::printf("auto_plan_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
