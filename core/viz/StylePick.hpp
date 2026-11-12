#pragma once

#include <vector>

#include "viz/AutoDraw.hpp"

// Visuals › Styles: what its Random draws and the order its lists show (owner, 2026-11-12: "at style selection
// screen current playing must be on top listed. All style from all families are in random unless family only
// toggle is on"). The shell hands over the page's styles by index, each with its family index and whether the
// entitlement keeps it; the rules are here. Host-tested in core/tests/style_pick_test.cpp.
namespace geode::viz::style_pick {

// The styles Random may land on: every style of every family but the one on screen, or with [familyOnly] only
// the on-screen style's family (the family the stage shows, not the one the dropdown has open, so the toggle
// means the same from Your styles and repeated taps stay in one family). Nothing on screen in the list: every
// family. A free user lands on a style the entitlement keeps while the pool has one (open[i] != 0); only a pool
// of locked styles is offered whole, which the page plays as a preview under the paywall. [family] or [open]
// may be null: one family, everything open.
inline std::vector<int> pool(const int* family, const unsigned char* open, int count, int onScreen, bool familyOnly) {
    std::vector<int> all;
    std::vector<int> kept;
    if (count <= 0) return all;
    const bool here = onScreen >= 0 && onScreen < count;
    const bool keepFamily = familyOnly && here && family != nullptr;
    for (int i = 0; i < count; ++i) {
        if (i == onScreen) continue;
        if (keepFamily && family[i] != family[onScreen]) continue;
        all.push_back(i);
        if (open == nullptr || open[i] != 0) kept.push_back(i);
    }
    return kept.empty() ? all : kept;
}

// One draw from [pool]: an index into the page's styles, -1 when the pool is empty (a family of one style with
// the toggle on, or a page of one style).
inline int pick(auto_draw::Draw& draw, const int* family, const unsigned char* open, int count, int onScreen,
                bool familyOnly) {
    const std::vector<int> candidates = pool(family, open, count, onScreen, familyOnly);
    const int k = draw.pick(static_cast<int>(candidates.size()), -1);
    return k < 0 ? -1 : candidates[static_cast<size_t>(k)];
}

// The order a list on the page shows its [count] items: [lead] (the one on screen, or the family holding it)
// first, the rest in their own order. The same rule at every level, so the dropdown leads with the playing
// family and that family's list with the playing style. [lead] outside 0..count-1 leaves the order as it is.
inline void lead(int count, int lead, int* out) {
    if (out == nullptr || count <= 0) return;
    const bool here = lead >= 0 && lead < count;
    int at = 0;
    if (here) out[at++] = lead;
    for (int i = 0; i < count; ++i) {
        if (here && i == lead) continue;
        out[at++] = i;
    }
}

}  // namespace geode::viz::style_pick
