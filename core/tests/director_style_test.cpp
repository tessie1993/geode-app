// Host test of Auto's style at a director moment (viz/DirectorStyle): the owner's "make it morph and
// progress, but nothing hardcoded". A returning section brings back a style of its own family stepped on by
// its returns; a new section steps to the next style along the catalogue; a drop that opens a new section
// leaps to the next family; with Styles progress off a return brings back its own style and anything else
// keeps the style on screen. Never a random draw while an intent waits; a timer tick (no intent) still draws.
// Built by CI (.github/workflows/native-host-tests.yml) with DirectorStyle.cpp and the catalogue.
#include <cstdio>
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
    testTheSlot();
    if (failures) {
        std::printf("director_style_test: %d failed\n", failures);
        return 1;
    }
    std::printf("director_style_test: all passed\n");
    return 0;
}
