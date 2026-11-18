// Host test of Auto's style at a director moment (viz/DirectorStyle): the owner's "make it morph and
// progress, but nothing hardcoded". A returning section brings back a style of its own family stepped on by
// its returns; a new section steps to the next style along the catalogue; a drop that opens a new section
// leaps to the next family; with Styles progress off a return brings back its own style and anything else
// keeps the style on screen. Never a random draw while an intent waits; a timer tick (no intent) still draws.
// Built by CI (.github/workflows/native-host-tests.yml) with DirectorStyle.cpp and the catalogue.
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "viz/Catalogue.hpp"
#include "viz/DirectorStyle.hpp"

namespace catalogue = geode::viz::catalogue;
namespace director = geode::viz::director;
using Intent = director::StyleIntent;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL %s\n", what.c_str());
        ++failures;
    }
}

std::string lines(const std::vector<std::string>& ids) {
    std::string out;
    for (size_t i = 0; i < ids.size(); ++i) {
        if (i > 0) out += '\n';
        out += ids[i];
    }
    return out;
}

// The first picker family with at least three styles, so a family can be stepped through.
const catalogue::PickerFamily& bigFamily() {
    for (const auto& f : catalogue::pickerFamilies()) {
        if (f.ids.size() >= 3) return f;
    }
    return catalogue::pickerFamilies().front();
}

Intent intent(Intent::Kind kind, const std::string& from, const std::string& remembered = {}, int step = 0, bool progress = true) {
    Intent i;
    i.kind = kind;
    i.from = from;
    i.remembered = remembered;
    i.step = step;
    i.progress = progress;
    return i;
}

void testProgressIsTheNextAlongTheCatalogue() {
    const auto& all = catalogue::allIds();
    check(all.size() > 4, "the catalogue lists styles");
    const std::string pool = lines(all);
    for (size_t i = 0; i + 1 < all.size(); i += 7) {
        const int pick = director::pickFor(intent(Intent::Kind::Progress, all[i]), pool, static_cast<int>(i));
        check(pick == static_cast<int>(i + 1), "a new section steps to the next style along the catalogue (" + all[i] + ")");
    }
    const int last = static_cast<int>(all.size()) - 1;
    check(director::pickFor(intent(Intent::Kind::Progress, all.back()), pool, last) == 0, "and wraps at the end");
    // The pool is the shell's: a style it leaves out (locked, or not in Auto's draw) is passed over.
    std::vector<std::string> some = {all[0], all[3], all[5]};
    check(director::pickFor(intent(Intent::Kind::Progress, all[0]), lines(some), 0) == 1, "past styles the pool does not hold");
    check(director::pickFor(intent(Intent::Kind::Progress, all[4]), lines(some), -1) == 2, "from a style the pool does not hold, the next it does");
}

void testReturnStepsThroughItsOwnFamily() {
    const auto& fam = bigFamily();
    check(fam.ids.size() >= 3, "a family of three or more");
    std::vector<std::string> pool = catalogue::allIds();
    const std::string text = lines(pool);
    const auto indexOf = [&](const std::string& id) {
        for (size_t i = 0; i < pool.size(); ++i) {
            if (pool[i] == id) return static_cast<int>(i);
        }
        return -1;
    };
    const std::string& first = fam.ids[0];
    // A style of another family on screen when the section comes back.
    std::string elsewhere;
    for (const auto& f : catalogue::pickerFamilies()) {
        if (f.name != fam.name && !f.ids.empty()) elsewhere = f.ids.front();
    }
    check(!elsewhere.empty(), "another family to come back from");
    for (int step = 1; step <= 3; ++step) {
        const std::string& want = fam.ids[static_cast<size_t>(step) % fam.ids.size()];
        const int pick = director::pickFor(intent(Intent::Kind::Return, elsewhere, first, step), text, indexOf(elsewhere));
        check(pick == indexOf(want), "return " + std::to_string(step) + " steps " + std::to_string(step) + " on in the remembered style's family");
    }
    // Only the remembered style left in its family: it comes back itself.
    const std::vector<std::string> alone = {elsewhere, first};
    check(director::pickFor(intent(Intent::Kind::Return, elsewhere, first, 1), lines(alone), 0) == 1, "a family of one in the pool brings its own style back");
}

void testLeapIsTheNextFamily() {
    const auto& families = catalogue::pickerFamilies();
    check(families.size() >= 2, "two families or more");
    const std::string pool = lines(catalogue::allIds());
    const std::string from = families[0].ids.front();
    const int pick = director::pickFor(intent(Intent::Kind::Leap, from), pool, 0);
    check(pick >= 0 && catalogue::allIds()[static_cast<size_t>(pick)] == families[1].ids.front(), "a drop into a new section leaps to the next family's first style");
}

void testStay() {
    const auto& all = catalogue::allIds();
    const std::string pool = lines(all);
    check(director::pickFor(intent(Intent::Kind::Progress, all[2], {}, 0, false), pool, 2) == director::kKeep, "Styles progress off: a new section keeps the style");
    check(director::pickFor(intent(Intent::Kind::Leap, all[2], {}, 0, false), pool, 2) == director::kKeep, "and a drop too");
    check(director::pickFor(intent(Intent::Kind::Return, all[2], all[5], 2, false), pool, 2) == 5, "a return brings back its own style");
}

Intent drawn(Intent i, uint64_t draw) {
    i.draw = draw;
    return i;
}

// MilkDrop is in Auto's pool once per .milk file, every entry under its one id. A moment that lands on MilkDrop
// draws one of them from the intent (the play's seed and the moment), never the one playing; the first entry
// used to stand for them all, so every moment put the same .milk up again, and with only .milk files in the
// pool a new section or a drop kept the one playing.
void testMilkFilesAreDrawn() {
    const auto& all = catalogue::allIds();
    std::string milk;
    int at = -1;
    for (size_t i = 0; i < all.size(); ++i) {
        if (catalogue::kindOf(all[i]) == catalogue::Kind::Milkdrop) {
            milk = all[i];
            at = static_cast<int>(i);
        }
    }
    check(!milk.empty() && all.size() > 2, "the catalogue lists MilkDrop among other styles");
    if (milk.empty()) return;

    const std::string only = lines(std::vector<std::string>(5, milk));
    std::set<int> seen;
    for (uint64_t draw = 0; draw < 16; ++draw) {
        const int pick = director::pickFor(drawn(intent(Intent::Kind::Progress, milk), draw), only, 2);
        check(pick >= 0 && pick < 5 && pick != 2, "a new section on .milk files alone puts up another .milk");
        seen.insert(pick);
        const int leapt = director::pickFor(drawn(intent(Intent::Kind::Leap, milk), draw), only, 2);
        check(leapt >= 0 && leapt < 5 && leapt != 2, "and a drop too");
        const int back = director::pickFor(drawn(intent(Intent::Kind::Return, milk, milk, 1), draw), only, 2);
        check(back >= 0 && back < 5 && back != 2, "and a return");
    }
    check(seen.size() > 1, "not the same .milk at every moment");
    check(director::pickFor(drawn(intent(Intent::Kind::Progress, milk), 9), only, 2) ==
              director::pickFor(drawn(intent(Intent::Kind::Progress, milk), 9), only, 2),
          "the same draw lands on the same .milk, so an export draws as the screen");

    // Auto's pool with styles and .milk files: a step onto MilkDrop from the style before it.
    std::vector<std::string> mixed;
    for (const auto& id : all) {
        if (id != milk) mixed.push_back(id);
    }
    const size_t styles = mixed.size();
    for (int i = 0; i < 4; ++i) mixed.push_back(milk);
    const int count = static_cast<int>(all.size());
    const std::string& before = all[static_cast<size_t>((at - 1 + count) % count)];
    int shown = -1;
    for (size_t i = 0; i < styles; ++i) {
        if (mixed[i] == before) shown = static_cast<int>(i);
    }
    std::set<int> landed;
    for (uint64_t draw = 0; draw < 16; ++draw) {
        const int pick = director::pickFor(drawn(intent(Intent::Kind::Progress, before), draw), lines(mixed), shown);
        check(pick >= static_cast<int>(styles) && pick < static_cast<int>(mixed.size()), "the step after " + before + " lands on a .milk");
        landed.insert(pick);
    }
    check(landed.size() > 1, "on one drawn from the moment, not always the first .milk");
}

void testTheSlot() {
    const auto& all = catalogue::allIds();
    const std::string pool = lines(all);
    check(director::pickStyle(pool, 0) == director::kNoIntent, "no intent waiting: Auto draws as it always has");
    director::publishStyleIntent(intent(Intent::Kind::Progress, all[0]));
    check(director::pickStyle(pool, 0) == 1, "a published intent is read");
    check(director::pickStyle(pool, 1) == director::kNoIntent, "once");
    director::publishStyleIntent(intent(Intent::Kind::None, all[0]));
    check(director::pickStyle(pool, 0) == director::kNoIntent, "an intent of no kind leaves Auto's draw");
    check(director::pickFor(intent(Intent::Kind::Progress, all[0]), "", -1) == director::kKeep, "an empty pool keeps the style");
}

}  // namespace

int main() {
    testProgressIsTheNextAlongTheCatalogue();
    testReturnStepsThroughItsOwnFamily();
    testLeapIsTheNextFamily();
    testStay();
    testMilkFilesAreDrawn();
    testTheSlot();
    if (failures) {
        std::printf("director_style_test: %d failed\n", failures);
        return 1;
    }
    std::printf("director_style_test: all passed\n");
    return 0;
}
