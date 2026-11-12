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

// #999 F3, "freeze, same as live": the renderer advances the schedule on the picture's clock. 60 fps, a
// beat every 30 frames that the tracker's flywheel keeps ticking through the silence, music for 5 s,
// silence (motionRate 0) for 10 s, music again.
constexpr int kSilentFrom = 300;
constexpr int kSilentTo = 900;
float rateAt(int frame) { return frame >= kSilentFrom && frame < kSilentTo ? 0.0f : 1.0f; }

void aSilentStretchHoldsTheBeatCut() {
    StyleSchedule schedule;
    schedule.set({}, {"a", "b", "c"}, 1);
    int inSilence = 0;
    int before = 0;
    int after = 0;
    for (int f = 0; f < 1200; ++f) {
        const auto beat = static_cast<float>(f / 30);
        if (!schedule.advance(1.0f / 60.0f, rateAt(f), beat)) continue;
        if (f < kSilentFrom) ++before;
        else if (f < kSilentTo) ++inSilence;
        else ++after;
    }
    check(before > 0, "the beat cut cuts while the music plays");
    check(inSilence == 0, "no cut in a silent stretch: the picture is still, so is the schedule");
    // Ten beats are heard after the silence (frames 900, 930, ... 1170), and every one cuts: none of the
    // twenty the flywheel ticked through the silence is counted or bursts out at its end.
    check(after == 10, "after the silence the cut follows the beats heard, one each");
}

void aSilentStretchHoldsTheTimedCues() {
    StyleSchedule schedule;
    // At 6 s of the picture's clock: 5 s of music, 10 s still, then 1 s more, so render frame 960.
    schedule.set({cue(6'000, "plasma")}, {}, 4);
    int at = -1;
    for (int f = 0; f < 1200 && at < 0; ++f) {
        if (schedule.advance(1.0f / 60.0f, rateAt(f), static_cast<float>(f / 30))) at = f;
    }
    check(at >= 959 && at <= 961, "a timed cue keeps the picture's time: it lands 1 s after the silence, not in it");
    check(schedule.clockMs() > 6'000.0 && schedule.clockMs() < 6'100.0, "the schedule's clock did not run while still");
}

}  // namespace

int main() {
    takeSwitchToLockedStyleIsDropped();
    beatCutSkipsLockedStyles();
    allLockedLeavesNothingToDraw();
    proPlaysEveryStyle();
    aSilentStretchHoldsTheBeatCut();
    aSilentStretchHoldsTheTimedCues();
    if (failures == 0) std::printf("style_schedule_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
