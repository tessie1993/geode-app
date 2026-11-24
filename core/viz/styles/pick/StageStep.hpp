#pragma once

#include <cstdint>

// What the stage's "next / previous style" steps: the sideways swipe, its ‹ › twins, a TalkBack action and the
// TV remote's left and right, whichever of them Settings › Gestures & controls gives the action (the gesture map
// is the shell's; this only decides what the action walks).
//
// Owner, 2026-11-12: "Step presets and types as well (they are separate different tabs)". The step walks what the
// open Visuals tab lists:
// - Presets: the saved looks the user may apply, in the page's order: with the sheet hidden only while the stage
//   still shows one of them (style and params as saved).
// - Styles, Customize, Settings, or a tab with nothing to step: the styles, in the Styles page's order, as before.
//
// The walk itself ([pickIndex]) wraps at the ends, never lands on the item already on screen, skips what the
// entitlement locks (a swipe that opened the paywall would stop the browse dead) and starts from an end when
// nothing in the list is on screen. Pure; host-tested in core/tests/stage_step_test.cpp.
namespace geode::viz::stage_step {

// The open Visuals page. Values are the ABI (GEODE_STAGE_STEP_PAGE_* in geode_api.h).
enum Page : int {
    kPageStyles = 0,
    kPageCustomize = 1,
    kPagePresets = 2,
    kPageTextures = 4,
    kPageSettings = 5,
};

// Facts the shell observed (GEODE_STAGE_STEP_*).
enum Flag : uint32_t {
    kSheetShown = 1u << 0,  // the options sheet (or the side panel) is up
    kOnLook = 1u << 2,      // the stage shows a saved look the user may apply (style and params as saved)
};

// What the step walks (GEODE_STAGE_STEP_STYLES / _LOOKS).
enum Target : int {
    kStyles = 0,
    kLooks = 1,
};

// lookCount: the saved looks the user may apply.
inline Target target(int page, uint32_t flags, int lookCount) {
    const bool shown = (flags & kSheetShown) != 0;
    if (page == kPagePresets && lookCount > 0 && (shown || (flags & kOnLook) != 0)) return kLooks;
    return kStyles;
}

// The index [step] (+1 next, -1 previous; 0 counts as next) places from [here] among [count] items, wrapping and
// skipping the closed ones (open[i] == 0) and [here] itself. [here] outside 0..count-1 (nothing in the list on
// screen) starts at the first for next and the last for previous. -1 when nothing else is open. [open] may be
// null: everything is open.
inline int pickIndex(const unsigned char* open, int count, int here, int step) {
    if (count <= 0) return -1;
    const int dir = step < 0 ? -1 : 1;
    const bool inList = here >= 0 && here < count;
    const int start = inList ? here : (dir > 0 ? -1 : 0);
    for (int i = 1; i <= count; ++i) {
        const int idx = ((start + dir * i) % count + count) % count;
        if (inList && idx == here) continue;
        if (open != nullptr && open[idx] == 0) continue;
        return idx;
    }
    return -1;
}

}  // namespace geode::viz::stage_step
