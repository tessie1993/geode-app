// Host-side checks for Visuals › Styles' Random and list order (core/viz/StylePick, geode_styles_*). No GL, no
// device.
//
// What is pinned (owner, 2026-11-12: "at style selection screen current playing must be on top listed. All style
// from all families are in random unless family only toggle is on"):
// - Random reaches every style of every family, never the one on screen;
// - with "This family only" it stays in the on-screen style's family, and with nothing on screen it has no
//   family to keep, so it draws from all;
// - a free user lands on a style the entitlement keeps while the pool has one; a pool of locked styles only is
//   offered whole (the page previews it under the paywall), and the toggle's family is never left for that;
// - an empty pool says -1;
// - every list leads with what is on screen and keeps the rest in order.
#include <cstdio>
#include <set>
#include <vector>

#include "api/geode_api.h"
#include "viz/StylePick.hpp"

namespace pick = geode::viz::style_pick;
namespace auto_draw = geode::viz::auto_draw;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// Three families: 0 = {0,1,2}, 1 = {3,4}, 2 = {5}.
const int kFamily[] = {0, 0, 0, 1, 1, 2};
constexpr int kCount = 6;

std::set<int> drawn(const unsigned char* open, int onScreen, bool familyOnly) {
    auto_draw::Draw draw(77);
    std::set<int> seen;
    for (int i = 0; i < 2000; ++i) seen.insert(pick::pick(draw, kFamily, open, kCount, onScreen, familyOnly));
    return seen;
}

void everyFamilyButTheOneOnScreen() {
    check(drawn(nullptr, 1, false) == std::set<int>{0, 2, 3, 4, 5}, "toggle off: every family, never on screen");
    check(drawn(nullptr, -1, false) == std::set<int>{0, 1, 2, 3, 4, 5}, "nothing on screen: every style");
}

void familyOnlyKeepsTheOnScreenFamily() {
    check(drawn(nullptr, 1, true) == std::set<int>{0, 2}, "family only: the on-screen family, not the one on screen");
    check(drawn(nullptr, 4, true) == std::set<int>{3}, "family only: a family of two gives the other");
    check(drawn(nullptr, 5, true) == std::set<int>{-1}, "family only: a family of one has nothing to draw");
    check(drawn(nullptr, -1, true) == std::set<int>{0, 1, 2, 3, 4, 5}, "family only, nothing on screen: every style");
    check(drawn(nullptr, 9, true) == std::set<int>{0, 1, 2, 3, 4, 5}, "family only, on screen past the end: every style");
}

void keptStylesFirst() {
    const unsigned char keeps[] = {1, 0, 0, 1, 0, 0};
    check(drawn(keeps, 0, false) == std::set<int>{3}, "free: the one kept style that is not on screen");
    check(drawn(keeps, 3, false) == std::set<int>{0}, "free: kept styles in other families");
    check(drawn(keeps, 0, true) == std::set<int>{1, 2}, "free, family only, nothing kept left: the locked ones of the family");
    const unsigned char none[] = {0, 0, 0, 0, 0, 0};
    check(drawn(none, 2, false) == std::set<int>{0, 1, 3, 4, 5}, "nothing kept: the whole pool, for a preview");
}

void emptyAndOne() {
    auto_draw::Draw draw(93);
    check(pick::pick(draw, nullptr, nullptr, 0, -1, false) == -1, "no styles: -1");
    check(pick::pick(draw, nullptr, nullptr, 1, 0, false) == -1, "one style on screen: -1");
    check(pick::pick(draw, nullptr, nullptr, 1, -1, false) == 0, "one style, not on screen: it");
    check(pick::pick(draw, nullptr, nullptr, 3, 1, true) != 1, "null families: one family, never on screen");
}

void abiDraws() {
    const unsigned char open[] = {1, 1, 1, 1, 1, 1};
    std::set<int> seen;
    for (int i = 0; i < 2000; ++i) seen.insert(geode_styles_random(kFamily, open, kCount, 3, 1));
    check(seen == std::set<int>{4}, "ABI: family only from style 3 gives 4");
    seen.clear();
    for (int i = 0; i < 2000; ++i) seen.insert(geode_styles_random(kFamily, open, kCount, 3, 0));
    check(seen == std::set<int>{0, 1, 2, 4, 5}, "ABI: every family but the one on screen");
}

std::vector<int> led(int count, int lead) {
    std::vector<int> out(static_cast<size_t>(count), -7);
    geode_styles_lead(count, lead, out.data());
    return out;
}

void listsLeadWithOnScreen() {
    check(led(5, 3) == std::vector<int>{3, 0, 1, 2, 4}, "the one on screen first, the rest in order");
    check(led(5, 0) == std::vector<int>{0, 1, 2, 3, 4}, "already first: unchanged");
    check(led(5, 4) == std::vector<int>{4, 0, 1, 2, 3}, "the last moves to the top");
    check(led(4, -1) == std::vector<int>{0, 1, 2, 3}, "nothing on screen: unchanged");
    check(led(4, 4) == std::vector<int>{0, 1, 2, 3}, "past the end: unchanged");
    check(led(1, 0) == std::vector<int>{0}, "one item");
    geode_styles_lead(0, 0, nullptr);
}

}  // namespace

int main() {
    everyFamilyButTheOneOnScreen();
    familyOnlyKeepsTheOnScreenFamily();
    keptStylesFirst();
    emptyAndOne();
    abiDraws();
    listsLeadWithOnScreen();
    if (failures == 0) std::printf("style_pick_test: ok\n");
    return failures == 0 ? 0 : 1;
}
