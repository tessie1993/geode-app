#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

// What the stage's "next / previous style" steps: the sideways swipe, its ‹ › twins, a TalkBack action and the
// TV remote's left and right, whichever of them Settings › Gestures & controls gives the action (the gesture map
// is the shell's; this only decides what the action walks).
//
// Owner, 2026-11-12: "Step presets and types as well (they are separate different tabs)". The step walks what the
// open Visuals tab lists:
// - MilkDrop (and Textures, which opens from it): the user's .milk presets, in the page's order, while MilkDrop
//   can play (the engine is there and the entitlement keeps it) and there is a preset to step to. With the sheet
//   hidden this holds only while MilkDrop is still the style on screen: back in full screen after coming from
//   the MilkDrop tab the swipe goes on through the presets, but once Auto or a style pick has moved on, it steps
//   styles again.
// - Presets: the saved looks the user may apply, in the page's order, the same way: with the sheet hidden only
//   while the stage still shows one of them (style and params as saved).
// - Styles, Customize, Settings, or a tab with nothing to step: the styles, in the Styles page's order, as before.
// The MilkDrop style stays reachable from Styles as a style ("types"), the presets from their own tabs.
//
// The walk itself ([pickIndex]) wraps at the ends, never lands on the item already on screen, skips what the
// entitlement locks (a swipe that opened the paywall would stop the browse dead) and starts from an end when
// nothing in the list is on screen. The .milk walk keeps a history ([MilkSteps]), MilkDrop's own rule: previous
// returns to the preset each step came from, as long as no hand pick broke the chain, then falls back to list
// order. Pure apart from that history; host-tested in core/tests/stage_step_test.cpp.
namespace geode::viz::stage_step {

// The open Visuals page. Values are the ABI (GEODE_STAGE_STEP_PAGE_* in geode_api.h).
enum Page : int {
    kPageStyles = 0,
    kPageCustomize = 1,
    kPagePresets = 2,
    kPageMilkdrop = 3,
    kPageTextures = 4,
    kPageSettings = 5,
};

// Facts the shell observed (GEODE_STAGE_STEP_*).
enum Flag : uint32_t {
    kSheetShown = 1u << 0,  // the options sheet (or the side panel) is up
    kOnMilk = 1u << 1,      // the MilkDrop style is on screen
    kOnLook = 1u << 2,      // the stage shows a saved look the user may apply (style and params as saved)
    kMilkOpen = 1u << 3,    // MilkDrop can play: the engine is built in and the entitlement keeps the style
};

// What the step walks (GEODE_STAGE_STEP_STYLES / _LOOKS / _MILK).
enum Target : int {
    kStyles = 0,
    kLooks = 1,
    kMilk = 2,
};

// milkCount: the .milk presets listed; lookCount: the saved looks the user may apply.
inline Target target(int page, uint32_t flags, int milkCount, int lookCount) {
    const bool shown = (flags & kSheetShown) != 0;
    switch (page) {
        case kPageMilkdrop:
        case kPageTextures:
            if ((flags & kMilkOpen) != 0 && milkCount > 0 && (shown || (flags & kOnMilk) != 0)) return kMilk;
            return kStyles;
        case kPagePresets:
            if (lookCount > 0 && (shown || (flags & kOnLook) != 0)) return kLooks;
            return kStyles;
        default:
            return kStyles;
    }
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

inline int indexOf(const std::vector<std::string_view>& list, std::string_view item) {
    if (item.empty()) return -1;
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i] == item) return static_cast<int>(i);
    }
    return -1;
}

// The .milk walk with its history. Each step records where it came from and where it landed; previous retraces
// that chain while the preset on screen is the one the last step landed on. A hand pick (a tap on the list, a
// file opened, Auto's draw) leaves the screen on something no step landed on, so the chain no longer describes
// the way here: it is dropped and previous walks the list. A preset gone from the list (removed) ends the chain
// too. Bounded, so a long session's swiping cannot grow it without end.
class MilkSteps {
public:
    static constexpr size_t kMaxHistory = 64;

    // The index in [list] to load, or -1 for nothing. [current] is the .milk on screen, empty when MilkDrop is not.
    int step(const std::vector<std::string_view>& list, std::string_view current, int dir) {
        const int count = static_cast<int>(list.size());
        if (count == 0) return -1;
        const int here = indexOf(list, current);
        if (dir < 0 && !history_.empty()) {
            const Entry last = history_.back();
            history_.pop_back();
            const int back = indexOf(list, last.from);
            if (!current.empty() && last.to == current && back >= 0 && back != here) return back;
            history_.clear();
        }
        const int next = pickIndex(nullptr, count, here, dir);
        if (next < 0) return -1;
        if (dir >= 0) {
            history_.push_back(Entry{std::string(current), std::string(list[static_cast<size_t>(next)])});
            while (history_.size() > kMaxHistory) history_.pop_front();
        } else {
            history_.clear();
        }
        return next;
    }

    size_t depth() const { return history_.size(); }

    void clear() { history_.clear(); }

private:
    struct Entry {
        std::string from;
        std::string to;
    };
    std::deque<Entry> history_;
};

namespace detail {

struct Shared {
    std::mutex lock;
    MilkSteps milk;
};

inline Shared& shared() {
    static Shared s;
    return s;
}

}  // namespace detail

// The process's one .milk walk (geode_stage_step_milk): the stage is one, whichever gesture steps it.
inline int stepMilk(const std::vector<std::string_view>& list, std::string_view current, int dir) {
    auto& s = detail::shared();
    std::lock_guard<std::mutex> lock(s.lock);
    return s.milk.step(list, current, dir);
}

inline void resetMilk() {
    auto& s = detail::shared();
    std::lock_guard<std::mutex> lock(s.lock);
    s.milk.clear();
}

}  // namespace geode::viz::stage_step
