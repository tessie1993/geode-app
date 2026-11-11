#pragma once
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "viz/Catalogue.hpp"
#include "viz/Params.hpp"

// The Customize panel's map: eleven categories grouped by what their controls do, each split into a
// free Basics section and Pro sections below it, and which control lives where. Pure: no GL, no JNI.
//
// WHY native (ADR 0003): the map decides what a category's Randomize rolls and what its Reset puts
// back, which is parameter maths, so it sits beside the roll table (ParamRandomizer) it is checked
// against, and the shell only draws what it says. Every control the panel shows lands in exactly
// one section (core/tests/customize_test proves it against randomizer::rolledKeys()).
//
// A control is named by its ParamKeys string, the key the user locks by. Panel pieces that are not
// one SceneParams value (the macro bank's knobs, the moment rows, the route editor, shader code) are named
// "@<id>"; the shell draws each one with the composable it already has.
//
// The engine's roll groups (randomizer::Tab) keep their names: saved locks and tests use them. A
// category rolls its own controls whatever group the roll table files them under.
namespace geode::viz::customize {

// Bumped whenever a row type or field changes meaning; Kotlin refuses a table it cannot read.
constexpr int kTextVersion = 1;

// In panel order. Names match Kotlin's CustomizeCategory.
enum class Category { QuickKnobs, Look, Shape, Motion, Dive, Camera, Music, Effects, Liquid, Engine, Expert };
constexpr int kCategoryCount = 11;

// Which styles a category or section is offered on, beyond each control's own scope. A section is
// also hidden when no control in it reaches the style (a dive on a style that cannot dive).
enum class Show {
    Always,
    ThreeD,      // the styles on the camera rig (catalogue::fliesCamera)
    Particles,   // styles with a particle layer (Scope::ParticleSprite)
    Water,       // the Water style (Kind::Water)
    Engine,      // a style with an engine of its own to tune: Fluid, Cymatics, MilkDrop
    ShaderCode,  // a style with shader code the user can edit in Customize: Fluid's force and dye
};

struct Section {
    Category category;
    // Unique within its category; the panel saves which sections are open by it.
    const char* id;
    // The always-open, free section at the top. At most one per category.
    bool basics;
    Show show;
    std::vector<const char*> controls;
};

// A panel control no roll moves, with the SceneParams fields its Reset puts back (none for a "@"
// piece that is not a parameter) and, where its key has no catalogue scope of its own, the scope
// that says which styles it reaches.
struct HeldControl {
    const char* key;
    std::vector<const char*> fields;
    std::optional<catalogue::Scope> scope;
};

// Every section, category by category in panel order, Basics first in each.
const std::vector<Section>& sections();

// The panel controls outside the roll table.
const std::vector<HeldControl>& heldControls();

std::optional<Category> categoryNamed(std::string_view name);
const char* name(Category category);
const char* name(Show show);

// A kept category, after MilkDrop's mash-up bins, each of which can be locked against the next
// random load: "#MOTION" in a lock set holds every control Motion owns. Returns the set with each
// such marker replaced by its category's controls; a marker naming no category is dropped.
inline constexpr char kCategoryLockPrefix = '#';
// "#PRO" holds every control below a Basics: what a roll leaves alone without Pro (pro/FreeTier).
inline constexpr const char* kProLock = "#PRO";
std::set<std::string> expandLocks(const std::set<std::string>& locked);

// The category a control lives in, or nullopt for a key the panel does not show.
std::optional<Category> categoryOf(std::string_view key);

// Whether a control reaches styleId: its own catalogue scope, or the one heldControls() gives it.
bool reaches(std::string_view key, const std::string& styleId);
bool shows(const Section& section, const std::string& styleId);
bool shows(Category category, const std::string& styleId);

// Whether the panel offers a control on styleId: it reaches the style and its section shows there.
bool offered(std::string_view key, const std::string& styleId);

// Whether the picture reads a control in this look: false behind a switch that is off (a tunnel
// control with the endless zoom off, Liquid's with Liquid off).
bool live(std::string_view key, const SceneParams& params, const std::string& styleId);

// Whether a category's roll has anything to move on styleId. The shell greys the dice otherwise.
bool rolls(Category category, const std::string& styleId);

// The SceneParams fields a control writes: its roll's fields, or heldControls()'s.
std::vector<const char*> fieldsOf(std::string_view key);

// A new look for one category: every rolled control in it that the panel offers on styleId (any
// style when empty) and is not in locked. Controls in other categories hold. A category that sits
// behind a switch of its own (Dive, Liquid) never rolls that switch off, and turns it on when
// nothing the roll moved would show otherwise. Returns current when nothing can change.
SceneParams randomize(const SceneParams& current, uint64_t seed, Category category, const std::string& styleId,
                      const std::set<std::string>& locked);

// A new look across every category: each rolled control the panel offers on styleId, minus locked.
SceneParams randomizeAll(const SceneParams& current, uint64_t seed, const std::string& styleId,
                         const std::set<std::string>& locked);

// current with every field the category's controls write taken from another look: MilkDrop's
// mash-up, one bin loaded from another preset.
SceneParams borrow(const SceneParams& current, const SceneParams& from, Category category);

// current with every field the category's controls write put back to its default.
SceneParams reset(const SceneParams& current, Category category);

// The map as the shell reads it. Rows, '|'-separated, one per line, in this order:
//   customize|<kTextVersion>
//   category|<CATEGORY>|<SHOW>                        panel order
//   section|<CATEGORY>|<id>|BASICS or PRO|<SHOW>|<key>,<key>,...   sections() order
// Built once; the reference lives for the process.
const std::string& text();

// What the panel offers on styleId: one line per shown category ("<CATEGORY>"), one more when its
// roll has something to move there ("<CATEGORY>:roll"), and one per shown section
// ("<CATEGORY>/<id>"), in panel order.
std::string shownOn(const std::string& styleId);

}  // namespace geode::viz::customize
