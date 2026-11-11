// Host-side checks for which transition a style change plays and for how long (core/viz/TransitionChoice).
// No GL, no device.
//
// What is pinned:
// - "random" never draws "cut", "random" or the transition it drew last, reaches every other id, and falls
//   back sanely on a catalogue with nothing else in it;
// - a change by hand fades for the user's length; Auto's for MilkDrop's 2.7/1.7 of it, never shorter,
//   never past 5 s;
// - Auto's mark is read once per reader, only for its own key, only inside the window; a new mark of the
//   same key is read again.
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "viz/TransitionChoice.hpp"

namespace tc = geode::viz::transition_choice;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

}  // namespace

int main() {
    const std::vector<std::string> ids = {"fade", "cut", "wipe", "dissolve", "zoom", "random", "cube", "doorway"};
    uint64_t rng = 7;
    std::string last;
    std::set<std::string> seen;
    bool repeated = false;
    for (int i = 0; i < 2000; ++i) {
        const std::string id = tc::pick(ids, last, rng);
        if (id == "cut" || id == "random") check(false, "random never draws cut or random");
        if (id == last) repeated = true;
        seen.insert(id);
        last = id;
    }
    check(!repeated, "random never plays the same transition twice running");
    check(seen.size() == 6, "random reaches every other transition");

    uint64_t a = 42;
    uint64_t b = 42;
    check(tc::pick(ids, "", a) == tc::pick(ids, "", b), "the same seed draws the same transition (an export)");
    check(tc::pick({"fade", "cut"}, "fade", rng) == "fade", "with nothing else left the last one plays again");
    check(tc::pick({"cut", "random"}, "", rng) == "fade", "nothing to draw from plays a fade");
    check(tc::pick({}, "", rng) == "fade", "an empty catalogue plays a fade");

    check(tc::durationMs(1200, false) == 1200, "by hand: the user's length");
    check(tc::durationMs(1700, true) == 2700, "Auto: MilkDrop's 2.7 s for its 1.7 s");
    check(tc::durationMs(1200, true) > 1200, "Auto fades longer than a change by hand");
    check(tc::durationMs(4000, true) == tc::kMaxMs, "Auto's fade stops at 5 s");
    check(tc::durationMs(5000, true) == 5000, "Auto never fades shorter than by hand");
    check(tc::durationMs(0, false) == 1 && tc::durationMs(-5, true) >= 1, "a zero length never divides by zero");

    tc::Marks marks;
    uint64_t renderer = 0;
    uint64_t wallpaper = 0;
    marks.mark("neon_chrome_sea", 10.0);
    check(!marks.take("hex_hive", 10.5, renderer), "a mark is for its own style only");
    check(marks.take("neon_chrome_sea", 10.5, renderer), "the switch Auto marked is Auto's");
    check(!marks.take("neon_chrome_sea", 11.0, renderer), "a mark is read once per reader");
    check(marks.take("neon_chrome_sea", 11.0, wallpaper), "another reader reads it too");
    marks.mark("hex_hive", 20.0);
    check(!marks.take("hex_hive", 20.0 + tc::kMarkWindowS + 0.1, renderer), "a mark past its window is a change by hand");
    marks.mark("neon_chrome_sea", 30.0);
    check(marks.take("neon_chrome_sea", 30.2, renderer), "a new mark of the same style is read again");
    for (int i = 0; i < 20; ++i) marks.mark("style_" + std::to_string(i), 40.0);
    uint64_t fresh = 0;
    check(marks.take("style_19", 40.0, fresh), "the newest marks are kept");

    uint64_t live = 0;
    tc::markAuto("/milk/a.milk");
    check(tc::takeAuto("/milk/a.milk", live), "the process-wide mark reaches a reader");
    check(!tc::takeAuto("/milk/a.milk", live), "and only once");

    if (failures == 0) std::printf("transition_choice_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
