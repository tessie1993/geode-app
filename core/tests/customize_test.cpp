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

// The params schema's meta rows (#1005 PR 4a) say which control sets each field and its scope; a held
// control's own fields and scope must say the same until the held table is read from them too.
void heldControlsAgreeWithTheMeta() {
    for (const auto& h : customize::heldControls()) {
        std::set<std::string> labelled;
        for (int slot = 0; slot < SceneParams::kFieldCount; ++slot) {
            const ParamMeta& m = SceneParams::meta(static_cast<ParamSlot>(slot));
            if (m.live && std::strcmp(m.label, h.key) == 0) labelled.insert(SceneParams::fieldNames()[static_cast<size_t>(slot)]);
        }
        const std::set<std::string> fields(h.fields.begin(), h.fields.end());
        check(fields == labelled, std::string(h.key) + ": its fields are the rows its meta label names");
        if (h.scope) check(*h.scope == catalogue::scopeOf(h.key) || fields.empty(), std::string(h.key) + ": its scope is its rows'");
    }
    // Every field a placed control sets is labelled by that control.
    for (const auto& section : customize::sections()) {
        for (const char* control : section.controls) {
            for (const char* field : customize::fieldsOf(control)) {
                const int slot = SceneParams::slotOf(field);
                check(slot >= 0 && std::strcmp(SceneParams::meta(static_cast<ParamSlot>(slot)).label, control) == 0,
                      std::string(control) + " labels its field " + field);
            }
        }
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
        const bool below = category == Category::Expert || category == Category::Engine;
        check(basics[category] == (below ? 0 : 1), "one Basics, none on Engine or Expert: " + name);
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

// One style of each engine kind the panel treats differently, and the looks a roll starts from.
const char* const kRollStyles[] = {"julia", "crystal_canyon", "silk_web", "life_orbium", "milkdrop",
                                   "fluid", "curlflow",       "pool",     "cymatics"};

std::set<std::string> movedControls(const SceneParams& from, const SceneParams& to) {
    std::set<std::string> out;
    for (const auto& section : customize::sections()) {
        for (const char* control : section.controls) {
            for (const char* field : customize::fieldsOf(control)) {
                if (from.get(field) != to.get(field)) out.insert(control);
            }
        }
    }
    return out;
}

// The owner's report: "Randomize is not wired properly". On every engine kind, with Liquid off and
// on, each category's roll moves something the panel shows there and the picture reads, stays in
// its category, and the dice is offered exactly where it has something to move.
void everyRollMovesWhatThePanelShows() {
    for (const char* style : kRollStyles) {
        for (int liquid = 0; liquid < 2; ++liquid) {
            SceneParams start;
            start.flowEnabled = liquid == 1;
            for (int i = 0; i < customize::kCategoryCount; ++i) {
                const auto category = static_cast<Category>(i);
                const std::string where = std::string(customize::name(category)) + " on " + style + (liquid ? " in Liquid" : "");
                const bool offered = customize::shownOn(style).find(std::string(customize::name(category)) + ":roll\n") != std::string::npos;
                check(offered == customize::rolls(category, style), where + ": the dice line follows rolls()");
                const auto own = fieldsOfCategory(category);
                for (uint64_t seed = 1; seed <= 60; ++seed) {
                    const SceneParams out = customize::randomize(start, seed, category, style, {});
                    for (const char* field : SceneParams::fieldNames()) {
                        if (out.get(field) != start.get(field)) check(own.count(field), where + " moved outside itself: " + field);
                    }
                    const auto moved = movedControls(start, out);
                    check(!moved.empty() == offered, where + ": rolls exactly when the dice is offered");
                    bool read = moved.empty();
                    for (const auto& control : moved) {
                        check(customize::offered(control, style), where + " moved a control the panel hides: " + control);
                        read = read || customize::live(control, out, style);
                    }
                    check(read, where + ": the roll moved only controls behind a switch that is off");
                }
            }
            // The roll of everything keeps to what the panel shows too.
            for (uint64_t seed = 1; seed <= 60; ++seed) {
                const SceneParams out = customize::randomizeAll(start, seed, style, {});
                const auto moved = movedControls(start, out);
                check(!moved.empty(), std::string("a roll of everything moves ") + style);
                for (const auto& control : moved) {
                    check(customize::offered(control, style), std::string(style) + ": everything moved a hidden control: " + control);
                }
            }
        }
    }
    check(!customize::rolls(Category::QuickKnobs, "julia"), "the knobs are the user's: no roll");
    check(!customize::rolls(Category::Engine, "julia") && customize::rolls(Category::Engine, "fluid") &&
              customize::rolls(Category::Engine, "milkdrop") && !customize::rolls(Category::Expert, "fluid"),
          "Engine rolls where the style has one; Expert's layers and shader code have no roll");
}

// Dive and Liquid sit behind a switch: their roll never deals it off, opens it when the rest would
// not show, and leaves it to the user once it is kept.
void aSwitchedCategoryRollsWhatShows() {
    SceneParams diving;
    diving.endlessZoom = true;
    for (uint64_t seed = 1; seed <= 60; ++seed) {
        check(customize::randomize(diving, seed, Category::Dive, "julia", {}).endlessZoom, "Dive's roll keeps the dive on");
        check(customize::randomize(SceneParams{}, seed, Category::Dive, "julia", {}).endlessZoom, "Dive's roll starts the dive");
        const SceneParams kept = customize::randomize(SceneParams{}, seed, Category::Dive, "julia", {"Endless zoom"});
        check(!kept.endlessZoom, "a kept Endless zoom stays off");

        const SceneParams flat = customize::randomize(SceneParams{}, seed, Category::Liquid, "julia", {});
        check(flat.flowEnabled, "Liquid's roll opens Liquid mode when nothing of it shows");
        const SceneParams held = customize::randomize(SceneParams{}, seed, Category::Liquid, "julia", {"Flow field on"});
        check(!held.flowEnabled, "a kept Liquid switch stays off");
    }
    bool on = false;
    bool off = false;
    for (uint64_t seed = 1; seed <= 60; ++seed) {
        (customize::randomizeAll(SceneParams{}, seed, "julia", {}).endlessZoom ? on : off) = true;
    }
    check(on && off, "a roll of everything deals the dive on and off");
}

// Without Pro a roll moves the free Basics only (pro/FreeTier), in a category and across them.
void theFreeRollMovesBasicsOnly() {
    const auto pro = customize::expandLocks({customize::kProLock});
    std::set<std::string> basics;
    for (const auto& section : customize::sections()) {
        for (const char* control : section.controls) {
            check(pro.count(control) == (section.basics ? 0u : 1u), std::string("#PRO holds exactly what is below a Basics: ") + control);
            if (section.basics) basics.insert(control);
        }
    }
    const SceneParams start;
    for (const char* style : kRollStyles) {
        bool movedAny = false;
        for (uint64_t seed = 1; seed <= 40; ++seed) {
            for (const auto& control : movedControls(start, customize::randomizeAll(start, seed, style, pro))) {
                check(basics.count(control), std::string("the free roll moved a Pro control: ") + control);
                movedAny = true;
            }
            for (const auto& control : movedControls(start, customize::randomize(start, seed, Category::Look, style, pro))) {
                check(basics.count(control), std::string("Look's free roll moved a Pro control: ") + control);
            }
        }
        check(movedAny, std::string("the free roll moves the Basics on ") + style);
    }
    // #PRO narrows a roll and keeps nothing: what Keep puts back (Randomize everything) is the rest.
    check(customize::expandKeeps({customize::kProLock}).empty(), "#PRO is no Keep");
    check(customize::expandKeeps({customize::kProLock, "#LOOK", "Speed"}) == customize::expandLocks({"#LOOK", "Speed"}),
          "Keep is the lock set without #PRO");
    // A kept category still holds under it.
    auto both = customize::expandLocks({customize::kProLock, "#LOOK"});
    for (uint64_t seed = 1; seed <= 20; ++seed) {
        const SceneParams out = customize::randomizeAll(start, seed, "julia", both);
        check(out.palette == start.palette && out.brightness == start.brightness, "a kept Look holds through the free roll");
    }
}

// Reset and Take from a preset reach every control a category shows, the new Dive's included.
void resetAndBorrowCoverEveryControl() {
    for (int i = 0; i < customize::kCategoryCount; ++i) {
        const auto category = static_cast<Category>(i);
        SceneParams rolled;
        for (uint64_t seed = 1; seed <= 30; ++seed) rolled = customize::randomize(rolled, seed, category, "", {});
        const SceneParams back = customize::reset(rolled, category);
        check(movedControls(SceneParams{}, back).empty(), std::string(customize::name(category)) + ": Reset puts back all its roll moved");
    }
    SceneParams dive;
    dive.endlessZoom = true;
    dive.diveTwist = 0.5f;
    dive.diveAimX = 0.3f;
    dive.diveFollow = 1.6f;
    dive.speed = 2.0f;
    const SceneParams reset = customize::reset(dive, Category::Dive);
    check(!reset.endlessZoom && reset.diveTwist == 0.0f && reset.diveAimX == 0.0f && reset.diveFollow == 1.0f,
          "Dive reset puts the switch, the rolled and the held controls back");
    check(reset.speed == dive.speed, "Dive reset leaves Motion alone");
    const SceneParams taken = customize::borrow(SceneParams{}, dive, Category::Dive);
    check(taken.endlessZoom && taken.diveTwist == 0.5f && taken.diveFollow == 1.6f && taken.speed == SceneParams{}.speed,
          "Dive is taken from a preset whole and alone");
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
    // Pool spends Morph, Warp, Turbulence and Intensity on its water, so Shape and Look reach it.
    check(shownSection("pool", "SHAPE/basics") && shownSection("pool", "SHAPE/form") && shownSection("pool", "LOOK/light_detail"),
          "Pool's shape and light sections");
    check(!shownSection("pool", "CAMERA/framing") && !shownSection("pool", "ENGINE/fluid_flow") && !shownSection("pool", "EXPERT/shader_code"),
          "Pool is a shader style off the rig");
    check(shownSection("fluid", "SHAPE/particles"), "Particles on a fluid style");
    check(!shownSection("julia", "SHAPE/particles"), "no Particles on a shader style");
    check(shownSection("fluid", "ENGINE/fluid_flow") && shownSection("fluid", "ENGINE/fluid_look"), "Fluid's engine in Engine");
    check(!shownSection("fluid", "ENGINE/plate") && !shownSection("fluid", "ENGINE/milkdrop"), "only its own engine on Fluid");
    check(shownSection("milkdrop", "ENGINE/milkdrop"), "MilkDrop's engine in Engine");
    check(customize::shows(Category::Engine, "cymatics") && !customize::shows(Category::Engine, "julia") &&
              !customize::shows(Category::Engine, "curlflow"),
          "Engine only on a style with an engine to tune");
    check(!shownSection("julia", "ENGINE/fluid_flow") && !shownSection("julia", "ENGINE/plate") && !shownSection("julia", "ENGINE/milkdrop"),
          "no engine section on a shader style");
    // Shader code holds only Fluid's force and dye editors: the style's own shader box left Customize.
    check(shownSection("fluid", "EXPERT/shader_code"), "Fluid's shaders in Expert");
    check(!shownSection("julia", "EXPERT/shader_code"), "no shader code section on a shader style");
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
    heldControlsAgreeWithTheMeta();
    basicsAreFirstFewAndExpertHasNone();
    randomizeStaysInsideItsCategory();
    aKeptCategoryHoldsThroughARollOfEverything();
    everyRollMovesWhatThePanelShows();
    aSwitchedCategoryRollsWhatShows();
    theFreeRollMovesBasicsOnly();
    resetAndBorrowCoverEveryControl();
    resetPutsBackOnlyItsCategory();
    borrowTakesOnlyItsCategory();
    categoriesAndSectionsFollowTheStyle();
    textEqualsTheFixture(argc > 1 ? argv[1] : "core/tests/customize.txt");
    if (failures == 0) std::printf("customize_test: all checks passed\n");
    return failures;
}
