// Host-side checks for Randomize everything (core/viz/RandomizeAll), the two-circle gesture's "Randomize
// everything": the style and look it picks, and the roll of every Customize category on top. Built and
// run by CI (.github/workflows/native-host-tests.yml, "Randomize everything host tests") with the system compiler; no GL.
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "api/geode_api.h"
#include "viz/Catalogue.hpp"
#include "viz/CustomizeMap.hpp"
#include "viz/ParamRandomizer.hpp"
#include "viz/RandomizeAll.hpp"

using namespace geode::viz;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

bool isMilk(const std::string& id) { return catalogue::kindOf(id) == catalogue::Kind::Milkdrop; }

bool sameWire(const SceneParams& a, const SceneParams& b) {
    float x[SceneParams::kFieldCount];
    float y[SceneParams::kFieldCount];
    a.toWire(x);
    b.toWire(y);
    return std::memcmp(x, y, sizeof x) == 0;
}

// Every style but one family's opener and the loose shaders locked, roughly a free user's catalogue.
std::set<std::string> lockAllBut(const std::set<std::string>& open) {
    std::set<std::string> locked;
    for (const auto& id : catalogue::allIds()) {
        if (!open.count(id)) locked.insert(id);
    }
    return locked;
}

void samePickForTheSameSeed() {
    const std::vector<std::string> looks = {catalogue::allIds()[0], catalogue::allIds()[1], catalogue::allIds()[0]};
    for (uint64_t seed = 0; seed < 64; ++seed) {
        const auto a = randomize_all::pick(seed, "", {}, looks, 3);
        const auto b = randomize_all::pick(seed, "", {}, looks, 3);
        check(a.style == b.style && a.look == b.look && a.milk == b.milk, "pick is a function of its seed");
    }
    std::set<std::string> seen;
    for (uint64_t seed = 0; seed < 400; ++seed) seen.insert(randomize_all::pick(seed, "", {}, {}, 3).style);
    check(seen.size() > 50, "different seeds reach many styles");
}

void neverALockedStyle() {
    std::set<std::string> open;
    for (const auto& family : catalogue::families()) open.insert(family.opener());
    open.insert("neon_chrome_sea");
    const auto locked = lockAllBut(open);
    for (uint64_t seed = 0; seed < 2000; ++seed) {
        const auto picked = randomize_all::pick(seed, "", locked, {}, 10);
        check(open.count(picked.style) == 1, "picked an open style, not " + picked.style);
    }
    // Everything locked: nothing to pick, and the ABI says so.
    std::set<std::string> all(catalogue::allIds().begin(), catalogue::allIds().end());
    check(randomize_all::pick(7, "", all, {}, 10).style.empty(), "no style when every style is locked");
    std::string lockedText;
    for (const auto& id : all) lockedText += id + "\n";
    char style[64] = "x";
    int look = 5;
    int milk = 5;
    check(geode_randomize_all_pick(7, "", lockedText.c_str(), "", 10, style, sizeof style, &look, &milk) == 0,
          "ABI: 0 when every style is locked");
    check(style[0] == '\0' && look == -1 && milk == -1, "ABI: empty style, no look, no milk");
}

void notTheStyleOnScreen() {
    const std::string current = catalogue::families()[0].opener();
    const std::string other = catalogue::families()[1].opener();
    const auto locked = lockAllBut({current, other});
    for (uint64_t seed = 0; seed < 200; ++seed) {
        check(randomize_all::pick(seed, current, locked, {}, 0).style == other, "randomize everything leaves the style on screen");
    }
    // The only open style is the one on screen: it stays, the look and roll still move.
    check(randomize_all::pick(3, current, lockAllBut({current}), {}, 0).style == current, "the only open style is kept");
}

void milkDropOnlyWithAFileAndItsFileIsItsPreset() {
    for (uint64_t seed = 0; seed < 3000; ++seed) {
        check(!isMilk(randomize_all::pick(seed, "", {}, {}, 0).style), "no MilkDrop without a .milk file");
    }
    std::string milk;
    for (const auto& id : catalogue::allIds()) {
        if (isMilk(id)) milk = id;
    }
    const auto onlyMilk = lockAllBut({milk});
    for (uint64_t seed = 0; seed < 200; ++seed) {
        const auto picked = randomize_all::pick(seed, "", onlyMilk, {}, 5);
        check(picked.style == milk && picked.look == -1 && picked.milk >= 0 && picked.milk < 5, "MilkDrop takes a random .milk");
    }
    // A saved look for MilkDrop is a look like any other: it wins, and no second file is loaded over it.
    const auto withLook = randomize_all::pick(1, "", onlyMilk, {"x", milk}, 5);
    check(withLook.look == 1 && withLook.milk == -1, "a MilkDrop preset is used as the look");
}

void aLookMadeForTheStyle() {
    const auto& ids = catalogue::allIds();
    const std::vector<std::string> looks = {ids[0], ids[1], ids[0], ids[2]};
    const auto locked = lockAllBut({ids[0], ids[1], ids[2], ids[3]});
    std::set<int> used;
    for (uint64_t seed = 0; seed < 400; ++seed) {
        const auto picked = randomize_all::pick(seed, "", locked, looks, 0);
        if (picked.style == ids[3]) {
            check(picked.look == -1, "no look for a style none was made for");
        } else {
            check(picked.look >= 0 && looks[static_cast<size_t>(picked.look)] == picked.style, "the look fits the style");
            used.insert(picked.look);
        }
        check(picked.milk == -1, "no .milk off MilkDrop");
    }
    check(used.count(0) && used.count(2), "every look made for a style can be drawn");
}

// The fields each category's rolled controls write that reach the style.
std::vector<const char*> rolledFields(customize::Category category, const std::string& style, const std::set<std::string>& kept) {
    std::vector<const char*> out;
    for (const auto& roll : randomizer::rolledKeys()) {
        if (customize::categoryOf(roll.key) != category || kept.count(roll.key)) continue;
        if (!catalogue::reaches(catalogue::scopeOf(roll.key), style)) continue;
        for (const char* field : roll.fields) {
            if (field) out.push_back(field);
        }
    }
    return out;
}

void rollIsAFunctionOfItsSeed() {
    const SceneParams base{};
    for (const auto& style : {std::string("neon_chrome_sea"), catalogue::allIds()[0]}) {
        for (uint64_t seed = 0; seed < 16; ++seed) {
            const auto a = randomize_all::roll(base, base, seed, style, {});
            check(sameWire(a, randomize_all::roll(base, base, seed, style, {})), "roll is a function of its seed");
        }
        check(!sameWire(randomize_all::roll(base, base, 1, style, {}), randomize_all::roll(base, base, 2, style, {})), "seeds differ");
    }
}

// The owner's "all parameters": every category with a control reaching the style moves, on every style.
void everyCategoryMoves() {
    const SceneParams base{};
    int moved = 0;
    for (const auto& style : catalogue::allIds()) {
        const SceneParams out = randomize_all::roll(base, base, 42, style, {});
        for (int i = 0; i < customize::kCategoryCount; ++i) {
            const auto category = static_cast<customize::Category>(i);
            const auto fields = rolledFields(category, style, {});
            if (fields.empty()) continue;
            bool changed = false;
            for (const char* field : fields) changed = changed || out.get(field) != base.get(field);
            check(changed, std::string("category ") + customize::name(category) + " moves on " + style);
            ++moved;
        }
        // Nothing the style cannot show is spent: a control that does not reach it keeps base's value.
        for (const auto& roll : randomizer::rolledKeys()) {
            if (catalogue::reaches(catalogue::scopeOf(roll.key), style)) continue;
            for (const char* field : roll.fields) {
                bool sharedWithReaching = false;
                for (const auto& other : randomizer::rolledKeys()) {
                    if (!catalogue::reaches(catalogue::scopeOf(other.key), style)) continue;
                    for (const char* f : other.fields) sharedWithReaching = sharedWithReaching || (f && field && std::strcmp(f, field) == 0);
                }
                if (field && !sharedWithReaching) check(out.get(field) == base.get(field), std::string("unreached field held: ") + field);
            }
        }
    }
    check(moved > 0, "some category had controls to roll");
}

// Keep means keep: a kept category and a kept control hold the value they had before the gesture,
// through the preset (base) as well as the roll; the rest still moves.
void keptControlsHoldThroughThePresetAndTheRoll() {
    const std::string style = "neon_chrome_sea";
    const SceneParams before = randomizer::randomize(SceneParams{}, 99, std::nullopt, style, {});
    const SceneParams base = randomizer::randomize(SceneParams{}, 1234, std::nullopt, style, {});  // the preset
    std::string motionKey;
    for (const auto& roll : randomizer::rolledKeys()) {
        if (customize::categoryOf(roll.key) == customize::Category::Motion && catalogue::reaches(catalogue::scopeOf(roll.key), style)) {
            motionKey = roll.key;
            break;
        }
    }
    check(!motionKey.empty(), "a Motion control reaches the style");
    const std::set<std::string> locked = {"#LOOK", motionKey};
    const SceneParams out = randomize_all::roll(before, base, 5, style, locked);
    const auto kept = customize::expandLocks(locked);
    for (const auto& key : kept) {
        for (const char* field : customize::fieldsOf(key)) {
            check(out.get(field) == before.get(field), std::string("kept field holds its value from before: ") + field);
        }
    }
    for (const auto category : {customize::Category::Shape, customize::Category::Effects}) {
        const auto fields = rolledFields(category, style, kept);
        bool changed = fields.empty();
        for (const char* field : fields) changed = changed || out.get(field) != base.get(field);
        check(changed, std::string("an unkept category still moves: ") + customize::name(category));
    }

    // The ABI carries the same: wire in, wire out.
    float b[SceneParams::kFieldCount];
    float s[SceneParams::kFieldCount];
    float o[SceneParams::kFieldCount];
    before.toWire(b);
    base.toWire(s);
    const std::string lockedText = "#LOOK\n" + motionKey;
    check(geode_randomize_all_roll(style.c_str(), lockedText.c_str(), 5, 1, b, s, o) == 1, "ABI roll changes the look");
    check(sameWire(SceneParams::fromWire(o), out), "ABI roll equals the pure roll");

    // No category roll (free): the preset stands, only the kept controls go back to their values from before.
    const SceneParams keepOnly = randomize_all::roll(before, base, 5, style, locked, false);
    SceneParams expected = base;
    for (const auto& key : kept) {
        for (const char* field : customize::fieldsOf(key)) expected.set(field, before.get(field));
    }
    check(sameWire(keepOnly, expected), "without the roll only the kept controls change");
    geode_randomize_all_roll(style.c_str(), lockedText.c_str(), 5, 0, b, s, o);
    check(sameWire(SceneParams::fromWire(o), expected), "ABI: roll_categories 0 is keep-only");
}

// Without Pro the gesture follows the dice's free rule (owner, 2026-11-12): the shell adds "#PRO" to the
// lock set, as Customize's Randomize does, so the roll moves the free Basics only. #PRO is not Keep: the
// controls below a Basics stay as the new preset made them, never put back to the look before.
void theFreeRollIsTheDicesFreeRule() {
    std::set<std::string> basics;
    std::set<std::string> below;
    for (const auto& section : customize::sections()) {
        for (const char* control : section.controls) (section.basics ? basics : below).insert(control);
    }
    check(customize::expandKeeps({customize::kProLock}).empty(), "#PRO keeps nothing");
    check(customize::expandLocks({customize::kProLock}) == below, "#PRO holds exactly what is below a Basics");
    for (const auto& style : {std::string("neon_chrome_sea"), std::string("julia"), catalogue::allIds()[0]}) {
        const SceneParams before = randomizer::randomize(SceneParams{}, 99, std::nullopt, style, {});
        const SceneParams base = randomizer::randomize(SceneParams{}, 1234, std::nullopt, style, {});  // the preset
        bool movedAny = false;
        for (uint64_t seed = 1; seed <= 24; ++seed) {
            const SceneParams out = randomize_all::roll(before, base, seed, style, {customize::kProLock});
            for (const auto& roll : randomizer::rolledKeys()) {
                if (basics.count(roll.key)) continue;
                for (const char* field : roll.fields) {
                    if (field) check(out.get(field) == base.get(field), std::string("the free roll leaves the preset's ") + roll.key);
                }
            }
            for (const auto& key : basics) {
                for (const char* field : customize::fieldsOf(key)) movedAny = movedAny || out.get(field) != base.get(field);
            }
        }
        check(movedAny, "the free roll moves the Basics on " + style);

        // A kept category still goes back to its value from before; #PRO beside it changes nothing there.
        const std::set<std::string> lockedLook = {customize::kProLock, "#LOOK"};
        const SceneParams kept = randomize_all::roll(before, base, 7, style, lockedLook);
        for (const auto& key : customize::expandKeeps({"#LOOK"})) {
            for (const char* field : customize::fieldsOf(key)) {
                check(kept.get(field) == before.get(field), std::string("a kept Look holds through the free roll: ") + field);
            }
        }

        // The ABI carries the same "#PRO" line the dice's geode_params_randomize reads.
        float b[SceneParams::kFieldCount];
        float s[SceneParams::kFieldCount];
        float o[SceneParams::kFieldCount];
        before.toWire(b);
        base.toWire(s);
        geode_randomize_all_roll(style.c_str(), "#PRO\n#LOOK", 7, 1, b, s, o);
        check(sameWire(SceneParams::fromWire(o), kept), "ABI: the free roll equals the pure one");
    }
}

}  // namespace

int main() {
    samePickForTheSameSeed();
    neverALockedStyle();
    notTheStyleOnScreen();
    milkDropOnlyWithAFileAndItsFileIsItsPreset();
    aLookMadeForTheStyle();
    rollIsAFunctionOfItsSeed();
    everyCategoryMoves();
    keptControlsHoldThroughThePresetAndTheRoll();
    theFreeRollIsTheDicesFreeRule();
    if (failures == 0) std::printf("randomize_all_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
