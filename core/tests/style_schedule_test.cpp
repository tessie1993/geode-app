// Host-side checks for an export segment's style schedule (core/viz/StyleSchedule). Built and run by CI
// ("Native host tests" in .github/workflows/android.yml) with the system compiler; no GL, no device.
//
// What is pinned (#357): the entitlement handed to set() gates every style a schedule would draw. A take
// recorded during the trial that opens on a free style and switches to a Pro one renders, for a free
// user, without the Pro switch; a beat cut steps only through the styles the user owns; and with no
// locked styles (Pro, the trial) the schedule plays exactly as before.
#include <cstdio>
#include <string>
#include <vector>

#include "viz/StyleSchedule.hpp"

using geode::viz::StyleCue;
using geode::viz::StyleSchedule;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

StyleCue cue(double atMs, const char* id) {
    StyleCue c;
    c.atMs = atMs;
    c.sceneId = id;
    return c;
}

// Every style the schedule switches to over `seconds` of render at 60 fps, a beat every 30 frames.
std::vector<std::string> drawn(StyleSchedule& schedule, double seconds) {
    std::vector<std::string> out;
    const int frames = static_cast<int>(seconds * 60.0);
    for (int f = 0; f < frames; ++f) {
        const auto beat = static_cast<float>(f / 30);
        if (const StyleCue* c = schedule.step(f * 1000.0 / 60.0, beat)) out.push_back(c->sceneId);
    }
    return out;
}

bool contains(const std::vector<std::string>& ids, const std::string& id) {
    for (const auto& s : ids) {
        if (s == id) return true;
    }
    return false;
}

// The issue's failure scenario: a free user renders a trial take that switches to bulb_07 after 10 s.
void takeSwitchToLockedStyleIsDropped() {
    StyleSchedule schedule;
    schedule.set({cue(10'000, "bulb_07"), cue(20'000, "plasma")}, {}, 4, {"bulb_07", "glow_vortex"});
    const auto ids = drawn(schedule, 30.0);
    check(!contains(ids, "bulb_07"), "a take's switch to a locked style is not drawn");
    check(contains(ids, "plasma"), "its later switch to a free style still is");
}

void beatCutSkipsLockedStyles() {
    StyleSchedule schedule;
    schedule.set({}, {"neon_chrome_sea", "bulb_07", "plasma", "glow_vortex"}, 1, {"bulb_07", "glow_vortex"});
    const auto ids = drawn(schedule, 10.0);
    check(!contains(ids, "bulb_07") && !contains(ids, "glow_vortex"), "a beat cut never lands on a locked style");
    check(contains(ids, "plasma") && contains(ids, "neon_chrome_sea"), "it steps through the free ones");
}

void allLockedLeavesNothingToDraw() {
    StyleSchedule schedule;
    schedule.set({cue(5'000, "bulb_07")}, {}, 4, {"bulb_07"});
    check(schedule.empty(), "a schedule of locked styles only is empty");
}

void proPlaysEveryStyle() {
    StyleSchedule schedule;
    schedule.set({cue(10'000, "bulb_07"), cue(20'000, "plasma")}, {}, 4);
    const auto ids = drawn(schedule, 30.0);
    check(ids.size() == 2 && ids[0] == "bulb_07" && ids[1] == "plasma", "with nothing locked every switch plays");
}

}  // namespace

int main() {
    takeSwitchToLockedStyleIsDropped();
    beatCutSkipsLockedStyles();
    allLockedLeavesNothingToDraw();
    proPlaysEveryStyle();
    if (failures == 0) std::printf("style_schedule_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
