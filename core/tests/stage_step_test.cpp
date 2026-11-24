// Host-side checks for what the stage's "next / previous style" steps (core/viz/styles/pick/StageStep,
// geode_stage_step_*). No GL, no device.
//
// What is pinned (owner, 2026-11-12: "Step presets and types as well (they are separate different tabs)"):
// - on Presets the step walks the saved looks, with the sheet hidden only while one is on screen;
// - everywhere else, and whenever no look may be applied, the styles;
// - the walk wraps, skips what is on screen and what is locked, starts from an end when nothing listed is on
//   screen, and says -1 when nothing else is open (the old Kotlin nextStageStyle's cases, StageStyleTest).
#include <cstdio>

#include "api/geode_api.h"
#include "viz/styles/pick/StageStep.hpp"

namespace step = geode::viz::stage_step;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

void presetsTabStepsLooks() {
    check(step::target(step::kPagePresets, step::kSheetShown, 4) == step::kLooks, "Presets tab, sheet up: saved looks");
    check(step::target(step::kPagePresets, step::kOnLook, 4) == step::kLooks, "full screen on a saved look: looks");
    check(step::target(step::kPagePresets, 0, 4) == step::kStyles, "full screen, the look changed since: styles");
    check(step::target(step::kPagePresets, step::kSheetShown, 0) == step::kStyles, "no look the user may apply: styles");
}

void otherTabsStepStyles() {
    const uint32_t shown = step::kSheetShown | step::kOnLook;
    check(step::target(step::kPageStyles, shown, 4) == step::kStyles, "Styles, another style on: styles");
    check(step::target(step::kPageStyles, 0, 4) == step::kStyles, "full screen after Styles: styles");
    check(step::target(step::kPageCustomize, shown, 4) == step::kStyles, "Customize: styles");
    check(step::target(step::kPageTextures, shown, 4) == step::kStyles, "Textures: styles");
    check(step::target(step::kPageSettings, shown, 4) == step::kStyles, "Visuals › Settings: styles");
    check(step::target(42, shown, 4) == step::kStyles, "an unknown page: styles");
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

void theAbi() {
    const unsigned char open[] = {1, 0, 1};
    check(geode_stage_step_index(open, 3, 0, 1) == 2, "C ABI: index skips the locked one");
    check(geode_stage_step_index(nullptr, 3, -1, -1) == 2, "C ABI: null open is all open");
    check(geode_stage_step_target(GEODE_STAGE_STEP_PAGE_PRESETS, GEODE_STAGE_STEP_ON_LOOK, 1) == GEODE_STAGE_STEP_LOOKS,
          "C ABI: looks");
    check(geode_stage_step_target(GEODE_STAGE_STEP_PAGE_STYLES, GEODE_STAGE_STEP_SHEET_SHOWN, 1) == GEODE_STAGE_STEP_STYLES,
          "C ABI: styles");
}

}  // namespace

int main() {
    presetsTabStepsLooks();
    otherTabsStepStyles();
    indexWrapsAndSkipsLocked();
    indexFromOutsideTheList();
    indexNothingElseOpen();
    theAbi();
    if (failures == 0) std::printf("stage_step_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
