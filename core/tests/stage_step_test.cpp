// Host-side checks for what the stage's "next / previous style" steps (core/viz/StageStep,
// geode_stage_step_*). No GL, no device.
//
// What is pinned (owner, 2026-11-12: "Step presets and types as well (they are separate different tabs)"):
// - on the MilkDrop tab (and Textures, under it) the step walks the user's .milk presets while MilkDrop can play
//   and one is listed; with the sheet hidden only while MilkDrop is still on screen;
// - on Presets it walks the saved looks the same way, with the sheet hidden only while one is on screen;
// - everywhere else, and whenever that list is empty or MilkDrop is locked or missing, it walks the styles;
// - the walk wraps, skips what is on screen and what is locked, starts from an end when nothing listed is on
//   screen, and says -1 when nothing else is open (the old Kotlin nextStageStyle's cases, StageStyleTest);
// - the .milk walk's previous retraces the steps taken, a hand pick or a removed preset ends that chain, and the
//   history stays bounded.
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "api/geode_api.h"
#include "viz/StageStep.hpp"

namespace step = geode::viz::stage_step;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

void milkTabStepsMilk() {
    const uint32_t shown = step::kSheetShown | step::kMilkOpen;
    check(step::target(step::kPageMilkdrop, shown, 5, 3) == step::kMilk, "MilkDrop tab, sheet up: .milk presets");
    check(step::target(step::kPageMilkdrop, shown | step::kOnMilk, 5, 3) == step::kMilk, "MilkDrop on screen too: .milk");
    check(step::target(step::kPageTextures, shown, 5, 3) == step::kMilk, "Textures sits on MilkDrop: .milk");
    check(step::target(step::kPageMilkdrop, step::kMilkOpen | step::kOnMilk, 5, 3) == step::kMilk,
          "full screen after the MilkDrop tab, MilkDrop still on: .milk");
    check(step::target(step::kPageMilkdrop, step::kMilkOpen, 5, 3) == step::kStyles,
          "full screen after the MilkDrop tab, another style on (Auto moved on): styles");
    check(step::target(step::kPageMilkdrop, shown, 0, 3) == step::kStyles, "no .milk listed: styles");
    check(step::target(step::kPageMilkdrop, step::kSheetShown | step::kOnMilk, 5, 3) == step::kStyles,
          "MilkDrop locked or not built in: styles");
}

void presetsTabStepsLooks() {
    check(step::target(step::kPagePresets, step::kSheetShown, 0, 4) == step::kLooks, "Presets tab, sheet up: saved looks");
    check(step::target(step::kPagePresets, step::kOnLook, 0, 4) == step::kLooks, "full screen on a saved look: looks");
    check(step::target(step::kPagePresets, 0, 0, 4) == step::kStyles, "full screen, the look changed since: styles");
    check(step::target(step::kPagePresets, step::kSheetShown, 7, 0) == step::kStyles, "no look the user may apply: styles");
    check(step::target(step::kPagePresets, step::kSheetShown | step::kMilkOpen | step::kOnMilk, 7, 4) == step::kLooks,
          "MilkDrop on screen does not pull the Presets tab to .milk");
}

void otherTabsStepStyles() {
    const uint32_t all = step::kSheetShown | step::kOnMilk | step::kOnLook | step::kMilkOpen;
    check(step::target(step::kPageStyles, all, 5, 4) == step::kStyles, "Styles: styles, MilkDrop on screen or not");
    check(step::target(step::kPageStyles, step::kMilkOpen | step::kOnMilk, 5, 4) == step::kStyles,
          "full screen after Styles with MilkDrop on: styles (the types stay reachable)");
    check(step::target(step::kPageCustomize, all, 5, 4) == step::kStyles, "Customize: styles");
    check(step::target(step::kPageSettings, all, 5, 4) == step::kStyles, "Visuals › Settings: styles");
    check(step::target(42, all, 5, 4) == step::kStyles, "an unknown page: styles");
}

void indexWrapsAndSkipsLocked() {
    check(step::pickIndex(nullptr, 4, 0, 1) == 1, "a -> b");
    check(step::pickIndex(nullptr, 4, 3, 1) == 0, "d -> a, wrapping");
    check(step::pickIndex(nullptr, 4, 0, -1) == 3, "a <- d, wrapping");
    check(step::pickIndex(nullptr, 4, 2, 0) == 3, "a step of 0 goes forward");
    const unsigned char cLocked[] = {1, 1, 0, 1};
    check(step::pickIndex(cLocked, 4, 1, 1) == 3, "b -> d past a locked c");
    check(step::pickIndex(cLocked, 4, 3, -1) == 1, "d <- b past a locked c");
}

void indexFromOutsideTheList() {
    check(step::pickIndex(nullptr, 4, -1, 1) == 0, "nothing listed on screen: next is the first");
    check(step::pickIndex(nullptr, 4, -1, -1) == 3, "nothing listed on screen: previous is the last");
    check(step::pickIndex(nullptr, 4, 9, 1) == 0, "an index past the end counts as outside");
    const unsigned char firstLocked[] = {0, 1, 1, 1};
    check(step::pickIndex(firstLocked, 4, -1, 1) == 1, "from outside, past a locked first");
}

void indexNothingElseOpen() {
    const unsigned char onlyA[] = {1, 0, 0, 0};
    check(step::pickIndex(onlyA, 4, 0, 1) == -1, "every other item locked: nothing");
    check(step::pickIndex(nullptr, 1, 0, 1) == -1, "the only item is on screen: nothing");
    check(step::pickIndex(nullptr, 1, -1, 1) == 0, "the only item, not on screen: it");
    check(step::pickIndex(nullptr, 0, -1, 1) == -1, "an empty list: nothing");
    const unsigned char none[] = {0, 0};
    check(step::pickIndex(none, 2, -1, -1) == -1, "everything locked: nothing");
}

std::vector<std::string_view> views(const std::vector<std::string>& v) { return {v.begin(), v.end()}; }

void milkWalksTheListWithHistory() {
    const std::vector<std::string> files = {"/m/a.milk", "/m/b.milk", "/m/c.milk", "/m/d.milk"};
    const auto list = views(files);
    step::MilkSteps walk;
    check(walk.step(list, "", 1) == 0, "MilkDrop not on screen: next loads the first .milk");
    check(walk.step(list, "", -1) == 3, "MilkDrop not on screen: previous loads the last .milk");
    walk.clear();
    check(walk.step(list, "/m/a.milk", 1) == 1, "a -> b");
    check(walk.step(list, "/m/b.milk", 1) == 2, "b -> c");
    check(walk.step(list, "/m/c.milk", 1) == 3, "c -> d");
    check(walk.step(list, "/m/d.milk", 1) == 0, "d -> a, wrapping");
    check(walk.step(list, "/m/a.milk", -1) == 3, "back: a -> d, the one it came from");
    check(walk.step(list, "/m/d.milk", -1) == 2, "back again: c");
    check(walk.step(list, "/m/c.milk", -1) == 1, "and again: b");
    check(walk.step(list, "/m/b.milk", -1) == 0, "and again: a");
    check(walk.depth() == 0, "the chain is spent");
    check(walk.step(list, "/m/a.milk", -1) == 3, "past the chain, previous follows the list: d");
}

void milkHistoryIsNotListOrder() {
    // The list changes under the walk (an import puts a new one first, the page lists newest first): back still
    // returns where it came from, where the list order would now give the new one.
    const std::vector<std::string> before = {"/m/a.milk", "/m/b.milk", "/m/c.milk"};
    const std::vector<std::string> after = {"/m/new.milk", "/m/a.milk", "/m/b.milk", "/m/c.milk"};
    step::MilkSteps walk;
    check(walk.step(views(before), "/m/c.milk", 1) == 0, "c -> a, wrapping");
    check(walk.step(views(after), "/m/a.milk", -1) == 3, "after an import, back from a is c (index 3 now), not new");
}

void milkHandPickBreaksTheChain() {
    const std::vector<std::string> files = {"/m/a.milk", "/m/b.milk", "/m/c.milk", "/m/d.milk"};
    const auto list = views(files);
    step::MilkSteps walk;
    check(walk.step(list, "/m/a.milk", 1) == 1, "a -> b");
    // The user tapped d on the MilkDrop page: no step landed there.
    check(walk.step(list, "/m/d.milk", -1) == 2, "back from a hand-picked d follows the list: c, not a");
    check(walk.depth() == 0, "and the stale chain is gone");
}

void milkRemovedPresetEndsTheChain() {
    const std::vector<std::string> files = {"/m/a.milk", "/m/b.milk", "/m/c.milk"};
    step::MilkSteps walk;
    check(walk.step(views(files), "/m/a.milk", 1) == 1, "a -> b");
    const std::vector<std::string> without = {"/m/b.milk", "/m/c.milk"};
    check(walk.step(views(without), "/m/b.milk", -1) == 1, "a was removed: back from b follows the list (wraps to c)");
}

void milkSinglePreset() {
    const std::vector<std::string> one = {"/m/a.milk"};
    step::MilkSteps walk;
    check(walk.step(views(one), "/m/a.milk", 1) == -1, "the only .milk already on: nothing to step to");
    check(walk.step(views(one), "", 1) == 0, "the only .milk, MilkDrop not on: load it");
    check(walk.step({}, "", 1) == -1, "no .milk: nothing");
}

void milkHistoryIsBounded() {
    const std::vector<std::string> files = {"/m/a.milk", "/m/b.milk"};
    const auto list = views(files);
    step::MilkSteps walk;
    std::string_view on = list[0];
    for (int i = 0; i < 500; ++i) on = list[static_cast<size_t>(walk.step(list, on, 1))];
    check(walk.depth() == step::MilkSteps::kMaxHistory, "500 steps keep the last 64");
}

void theAbi() {
    geode_stage_step_milk_reset();
    const char* paths[] = {"/m/a.milk", "/m/b.milk", "/m/c.milk"};
    check(geode_stage_step_milk(paths, 3, "/m/a.milk", 1) == 1, "C ABI: a -> b");
    check(geode_stage_step_milk(paths, 3, "/m/b.milk", -1) == 0, "C ABI: back to a");
    check(geode_stage_step_milk(paths, 3, nullptr, 1) == 0, "C ABI: null current is MilkDrop not on screen");
    check(geode_stage_step_milk(nullptr, 3, "", 1) == -1 && geode_stage_step_milk(paths, 0, "", 1) == -1, "C ABI: no list");
    geode_stage_step_milk_reset();
    const unsigned char open[] = {1, 0, 1};
    check(geode_stage_step_index(open, 3, 0, 1) == 2, "C ABI: index skips the locked one");
    check(geode_stage_step_index(nullptr, 3, -1, -1) == 2, "C ABI: null open is all open");
    check(geode_stage_step_target(GEODE_STAGE_STEP_PAGE_MILKDROP, GEODE_STAGE_STEP_SHEET_SHOWN | GEODE_STAGE_STEP_MILK_OPEN, 2, 0) ==
              GEODE_STAGE_STEP_MILK,
          "C ABI: target");
    check(geode_stage_step_target(GEODE_STAGE_STEP_PAGE_PRESETS, GEODE_STAGE_STEP_ON_LOOK, 0, 1) == GEODE_STAGE_STEP_LOOKS,
          "C ABI: looks");
}

}  // namespace

int main() {
    milkTabStepsMilk();
    presetsTabStepsLooks();
    otherTabsStepStyles();
    indexWrapsAndSkipsLocked();
    indexFromOutsideTheList();
    indexNothingElseOpen();
    milkWalksTheListWithHistory();
    milkHistoryIsNotListOrder();
    milkHandPickBreaksTheChain();
    milkRemovedPresetEndsTheChain();
    milkSinglePreset();
    milkHistoryIsBounded();
    theAbi();
    if (failures == 0) std::printf("stage_step_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
