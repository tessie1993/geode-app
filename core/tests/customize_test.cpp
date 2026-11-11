// Host-side checks for the Customize panel's map (core/viz/CustomizeMap). Built and run by CI
// (.github/workflows/android.yml, "Customize map host tests") with the system compiler; no GL.
//
//   customize_test core/tests/customize.txt           checks, including text() against the fixture
//   customize_test --write core/tests/customize.txt   rewrites the fixture after a deliberate change
//
// The fixture is what the JVM tests parse (they cannot load libgeode): the free set (pro/FreeTier)
// and the panel's categories are tested against it, so this test is what keeps them honest.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "viz/CustomizeMap.hpp"
#include "viz/ParamRandomizer.hpp"

using namespace geode::viz;
using customize::Category;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

std::map<std::string, int> placements() {
    std::map<std::string, int> count;
    for (const auto& section : customize::sections()) {
        for (const char* control : section.controls) ++count[control];
    }
    return count;
}

// The owner's rule for #333: every control on today's pages lands in exactly one place.
void everyRolledControlIsPlacedOnce() {
    const auto count = placements();
    for (const auto& roll : randomizer::rolledKeys()) {
        const auto it = count.find(roll.key);
        check(it != count.end() && it->second == 1, std::string("rolled control placed exactly once: ") + roll.key);
    }
}

// The one batch the panel reads instead of a call per row: exactly the rolled keys, each once.
void lockableKeysAreTheRolledKeys() {
    std::set<std::string> rolled;
    for (const auto& roll : randomizer::rolledKeys()) rolled.insert(roll.key);
    std::set<std::string> listed;
    std::istringstream lines(randomizer::lockableKeysText());
    for (std::string key; std::getline(lines, key);) {
        check(listed.insert(key).second, "lockable key listed once: " + key);
    }
    check(!listed.empty(), "lockable keys are not empty");
    check(listed == rolled, "lockable keys equal the rolled keys");
    check(randomizer::lockableKeysText().back() == '\n', "lockable keys end with a newline");
}

void everyPlacedControlIsKnownAndPlacedOnce() {
    std::set<std::string> rolled;
    for (const auto& roll : randomizer::rolledKeys()) rolled.insert(roll.key);
    std::set<std::string> held;
    for (const auto& h : customize::heldControls()) {
        check(held.insert(h.key).second, std::string("held control listed once: ") + h.key);
        check(!rolled.count(h.key), std::string("held control is not also rolled: ") + h.key);
    }
    for (const auto& [key, n] : placements()) {
        check(n == 1, "control placed exactly once: " + key);
        check(rolled.count(key) || held.count(key), "placed control is rolled or held: " + key);
    }
    for (const auto& key : held) check(placements().count(key), "held control is placed: " + key);
}

void heldFieldsAreSceneParams() {
    SceneParams p;
    for (const auto& h : customize::heldControls()) {
        for (const char* field : h.fields) check(p.set(field, p.get(field)), std::string(h.key) + " writes a real field: " + field);
    }
}

void basicsAreFirstFewAndExpertHasNone() {
    std::map<Category, int> basics;
    std::map<Category, int> seen;
    for (const auto& section : customize::sections()) {
        const std::string where = std::string(customize::name(section.category)) + "/" + section.id;
        if (section.basics) {
            ++basics[section.category];
            check(seen[section.category] == 0, "Basics come first: " + where);
            check(section.controls.size() >= 1 && section.controls.size() <= 4, "Basics hold one to four controls: " + where);
        }
        ++seen[section.category];
    }
    for (int i = 0; i < customize::kCategoryCount; ++i) {
        const auto category = static_cast<Category>(i);
        const std::string name = customize::name(category);
        check(seen[category] > 0, "every category has a section: " + name);
        check(basics[category] == (category == Category::Expert ? 0 : 1), "one Basics, none on Expert: " + name);
        check(customize::categoryNamed(name) == category, "category name round-trips: " + name);
    }
    // Section ids are unique inside a category: the panel saves the open ones by them.
    std::set<std::string> ids;
    for (const auto& section : customize::sections()) {
        check(ids.insert(std::string(customize::name(section.category)) + "/" + section.id).second,
              std::string("section id unique: ") + section.id);
    }
}

std::set<std::string> fieldsOfCategory(Category category) {
    std::set<std::string> out;
    for (const auto& section : customize::sections()) {
        if (section.category != category) continue;
        for (const char* control : section.controls) {
            for (const char* field : customize::fieldsOf(control)) out.insert(field);
        }
    }
    return out;
}

// A category's Randomize moves its own controls and nothing else, whatever roll group the roll
// table files them under (Camera's are in MOTION, Liquid's in FLUID, Look's Film in COLOR).
void randomizeStaysInsideItsCategory() {
    const SceneParams start;
    const auto& names = SceneParams::fieldNames();
    for (int i = 0; i < customize::kCategoryCount; ++i) {
        const auto category = static_cast<Category>(i);
        const auto own = fieldsOfCategory(category);
        bool moved = false;
        for (uint64_t seed = 1; seed <= 40; ++seed) {
            const SceneParams out = customize::randomize(start, seed, category, "", {});
            for (const char* field : names) {
                if (out.get(field) == start.get(field)) continue;
                moved = true;
                check(own.count(field), std::string(customize::name(category)) + " roll moved a field it does not own: " + field);
            }
        }
        const bool rollsSomething = [&] {
            for (const auto& roll : randomizer::rolledKeys()) {
                if (customize::categoryOf(roll.key) == category) return true;
            }
            return false;
        }();
        check(moved == rollsSomething, std::string(customize::name(category)) + " rolls exactly when it owns a rolled control");
    }
    // A lock holds inside a category too.
    for (uint64_t seed = 1; seed <= 20; ++seed) {
        const SceneParams out = customize::randomize(start, seed, Category::Motion, "", {"Speed"});
        check(out.speed == start.speed, "a locked Speed holds through Motion's roll");
    }
}

// A kept category (MilkDrop's locked mash-up bin) holds through a roll of everything.
void aKeptCategoryHoldsThroughARollOfEverything() {
    const auto kept = customize::expandLocks({"#MOTION", "Bloom", "#NO_SUCH"});
    check(kept.count("Speed") && kept.count("Drift X"), "#MOTION expands to Motion's controls");
    check(kept.count("Bloom"), "a control's own lock passes through");
    check(!kept.count("#MOTION") && !kept.count("#NO_SUCH"), "markers do not stay in the set");
    check(!kept.count("Palette") && !kept.count("Camera path"), "other categories stay free");
    check(customize::expandLocks({"#MUSIC"}).count("@waves"), "a kept Music keeps the waves, which its roll re-rolls");

    const SceneParams start;
    const auto motion = fieldsOfCategory(Category::Motion);
    bool movedElsewhere = false;
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        const SceneParams out = randomizer::randomize(start, seed, std::nullopt, "", kept);
        for (const char* field : SceneParams::fieldNames()) {
            if (out.get(field) == start.get(field)) continue;
            check(!motion.count(field), std::string("a kept Motion holds ") + field);
            movedElsewhere = true;
        }
    }
    check(movedElsewhere, "the roll still moves the other categories");
}

void resetPutsBackOnlyItsCategory() {
    SceneParams changed;
    changed.speed = 2.0f;           // Motion
    changed.camFly = 1.3f;          // Camera, which the roll table files under MOTION
    changed.grain = 0.4f;           // Effects
    changed.posterize = 0.5f;       // Effects, filed under COLOR
    changed.flowEnabled = true;     // Liquid, never rolled
    changed.noteColor = 1.7f;       // Music's Notes & key, never rolled
    const SceneParams defaults;

    const SceneParams motion = customize::reset(changed, Category::Motion);
    check(motion.speed == defaults.speed, "Motion reset puts Speed back");
    check(motion.camFly == changed.camFly, "Motion reset leaves the camera to Camera");

    const SceneParams effects = customize::reset(changed, Category::Effects);
    check(effects.grain == defaults.grain && effects.posterize == defaults.posterize, "Effects reset puts Film back");
    check(effects.speed == changed.speed, "Effects reset leaves Motion alone");

    check(customize::reset(changed, Category::Camera).camFly == defaults.camFly, "Camera reset puts Fly speed back");
    check(customize::reset(changed, Category::Liquid).flowEnabled == defaults.flowEnabled, "Liquid reset turns Liquid mode back");
    check(customize::reset(changed, Category::Music).noteColor == defaults.noteColor, "Music reset puts Notes & key back");
}

// MilkDrop's mash-up: one category of another look laid over this one.
void borrowTakesOnlyItsCategory() {
    SceneParams from;
    from.speed = 2.0f;   // Motion
    from.grain = 0.4f;   // Effects
    from.camFly = 1.3f;  // Camera
    const SceneParams current;
    const SceneParams out = customize::borrow(current, from, Category::Motion);
    check(out.speed == from.speed, "borrowing Motion takes its Speed");
    check(out.grain == current.grain && out.camFly == current.camFly, "borrowing Motion leaves Effects and Camera");
}

bool shownSection(const std::string& styleId, const char* where) {
    std::istringstream lines(customize::shownOn(styleId));
    for (std::string line; std::getline(lines, line);) {
        if (line == where) return true;
    }
    return false;
}

void categoriesAndSectionsFollowTheStyle() {
    check(customize::shows(Category::Camera, "crystal_canyon"), "Camera on a style on the rig");
    check(customize::shows(Category::Camera, "silk_web"), "Camera off the rig too: path and Fly speed pan it");
    check(shownSection("silk_web", "CAMERA/basics"), "Camera basics off the rig");
    check(shownSection("crystal_canyon", "CAMERA/framing") && shownSection("crystal_canyon", "CAMERA/feel"),
          "framing and feel on a style on the rig");
    for (const char* id : {"outrun_drive", "ringed_planet", "ribbon_storm"}) {
        check(shownSection(id, "CAMERA/framing") && shownSection(id, "CAMERA/feel"), std::string(id) + ": on the rig without a march");
    }
    for (const char* id : {"vanishing", "morphogen", "noneuclid", "curl_bloom", "nectar_flow", "silk_web", "milkdrop"}) {
        check(!shownSection(id, "CAMERA/framing") && !shownSection(id, "CAMERA/feel"), std::string(id) + ": no rig, no framing or feel");
    }
    check(shownSection("water", "LIQUID/water"), "Water section on the Water style");
    check(!shownSection("julia", "LIQUID/water"), "no Water section elsewhere");
    check(shownSection("fluid", "SHAPE/particles"), "Particles on a fluid style");
    check(!shownSection("julia", "SHAPE/particles"), "no Particles on a shader style");
    check(shownSection("fluid", "EXPERT/fluid_flow") && shownSection("fluid", "EXPERT/fluid_look"), "Fluid's engine in Expert");
    check(!shownSection("fluid", "EXPERT/plate") && !shownSection("fluid", "EXPERT/milkdrop"), "only its own engine on Fluid");
    check(shownSection("milkdrop", "EXPERT/milkdrop"), "MilkDrop's engine in Expert");
    check(!shownSection("julia", "EXPERT/fluid_flow") && !shownSection("julia", "EXPERT/plate") && !shownSection("julia", "EXPERT/milkdrop"),
          "no engine section on a shader style");
    check(shownSection("julia", "EXPERT/shader_code"), "shader code on a shader style");
    check(!shownSection("milkdrop", "EXPERT/shader_code"), "no shader code on MilkDrop");
    check(shownSection("julia", "LOOK/basics"), "Look's Basics everywhere");
    check(customize::shows(Category::Expert, "julia"), "Expert always has Layers");
    // Any style (the empty id) shows everything: Kotlin's previews and tests.
    for (const auto& section : customize::sections()) check(customize::shows(section, ""), std::string("any style shows ") + section.id);
}

std::string readFile(const char* path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream buf;
    buf << in.rdbuf();
    std::string out;
    for (char c : buf.str()) {
        if (c != '\r') out += c;
    }
    return out;
}

void textEqualsTheFixture(const char* path) {
    const std::string fixture = readFile(path);
    check(!fixture.empty(), std::string("fixture readable: ") + path);
    if (fixture == customize::text()) return;
    check(false, std::string("text() differs from ") + path + "; after a deliberate change run customize_test --write " + path);
    // The whole native text, so a fixture can be fixed from the CI log alone.
    std::printf("---- native customize text ----\n%s---- end ----\n", customize::text().c_str());
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "--write") == 0) {
        std::ofstream(argv[2], std::ios::binary) << customize::text();
        return 0;
    }
    everyRolledControlIsPlacedOnce();
    lockableKeysAreTheRolledKeys();
    everyPlacedControlIsKnownAndPlacedOnce();
    heldFieldsAreSceneParams();
    basicsAreFirstFewAndExpertHasNone();
    randomizeStaysInsideItsCategory();
    aKeptCategoryHoldsThroughARollOfEverything();
    resetPutsBackOnlyItsCategory();
    borrowTakesOnlyItsCategory();
    categoriesAndSectionsFollowTheStyle();
    textEqualsTheFixture(argc > 1 ? argv[1] : "core/tests/customize.txt");
    if (failures == 0) std::printf("customize_test: all checks passed\n");
    return failures;
}
