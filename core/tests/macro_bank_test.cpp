// Host-side checks for the macro bank (core/viz/MacroBank): the user's knobs as weighted offsets on
// scene params. Centre is the identity, a full turn each way moves every linked field by its weight
// and never past its range, a link the style cannot show is skipped, the offset rides on a moved
// base, every edit validates, the text survives a round trip and garbage, and the four built-in
// knobs move a flat style, a 3D style, Fluid and MilkDrop both ways, hard, within the safety clamp, and a bank
// saved with the first built-in knobs loads with today's. A knob goes off screen into the bank's library and back,
// a bank saved before the library keeps every knob on screen, and the dice rolls the turns of the knobs on screen. The
// process-wide bank (geode_macro_bank_set) starts as the default, is replaced whole, never in place. Every float
// param a control sets can be linked, by its modulation target's name or its schema name; New ranges re-rolls a
// knob's links in place and Add random links more, seeded, in reach and off the kept controls.
//   macro_bank_test --write core/tests/macro_editor.txt   rewrites the editor fixture after a deliberate change
// Built and run by CI (.github/workflows/native-host-tests.yml, "Macro bank host tests"); no GL.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "api/geode_api.h"
#include "viz/Catalogue.hpp"
#include "viz/CustomizeMap.hpp"
#include "viz/Lfo.hpp"
#include "viz/MacroBank.hpp"
#include "viz/ModTargets.hpp"
#include "viz/Params.hpp"
#include "viz/VisualSafety.hpp"

using namespace geode::viz;
using macrobank::Bank;
using macrobank::Knob;
using macrobank::Link;
using macrobank::Target;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

bool about(float a, float b) { return std::fabs(a - b) <= 1e-5f; }

const macrobank::Reach kEveryTarget = macrobank::Reach().set();
const float kNan = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();

const std::vector<Target>& linkableTargets() { return macrobank::linkableTargets(); }

float fieldOf(const SceneParams& p, Target target) { return p.getSlot(target.slot); }

bool reachesStyle(Target target, const std::string& styleId) {
    return target.slot >= 0 && macrobank::reachMask(styleId).test(static_cast<size_t>(target.slot));
}

bool sameParams(const SceneParams& a, const SceneParams& b) {
    std::array<float, SceneParams::kFieldCount> wa{};
    std::array<float, SceneParams::kFieldCount> wb{};
    a.toWire(wa.data());
    b.toWire(wb.data());
    return wa == wb;
}

// One knob, id 1, at `value`, with these links.
Bank oneKnob(std::vector<Link> links, float value) {
    Bank bank;
    Knob knob;
    knob.id = 1;
    knob.name = "Test";
    knob.value = value;
    knob.links = std::move(links);
    bank.knobs.push_back(std::move(knob));
    bank.nextId = 2;
    return bank;
}

Bank defaultsAt(float value) {
    Bank bank = macrobank::defaultBank();
    for (Knob& knob : bank.knobs) knob.value = value;
    return bank;
}

struct EngineStyles {
    std::string flat;
    std::string threeD;
    std::string fluid;
    std::string milkdrop;
};

EngineStyles engineStyles() {
    EngineStyles s;
    for (const std::string& id : catalogue::allIds()) {
        const catalogue::Kind kind = catalogue::kindOf(id);
        if (kind == catalogue::Kind::Shader && catalogue::isMarched(id) && s.threeD.empty()) s.threeD = id;
        if (kind == catalogue::Kind::Shader && !catalogue::isMarched(id) && s.flat.empty()) s.flat = id;
        if (kind == catalogue::Kind::Fluid && s.fluid.empty()) s.fluid = id;
        if (kind == catalogue::Kind::Milkdrop && s.milkdrop.empty()) s.milkdrop = id;
    }
    return s;
}

void linkableTargetsWriteLiveParams() {
    const auto targets = linkableTargets();
    check(targets.size() > 30u, "most modulation targets can be linked");
    for (const Target t : targets) {
        const std::string name = macrobank::name(t);
        const macrobank::Range range = macrobank::rangeOf(t);
        const ParamSpec& spec = SceneParams::schema()[static_cast<size_t>(t.slot)];
        check(range.max > range.min, name + " has a range");
        check(range.min >= spec.min && range.max <= spec.max, name + " stays inside its schema range");
        check(macrobank::targetNamed(name) == t, name + " is found by name");
    }
    for (LfoTarget t : {LfoTarget::None, LfoTarget::Retired5, LfoTarget::Retired25, LfoTarget::Lfo1Rate, LfoTarget::Lfo3Depth}) {
        check(!macrobank::linkable(t), std::string("not linkable: ") + modtargets::name(t));
        check(macrobank::rangeOf(t).max == 0.0f, "an unlinkable target has no range");
    }
    check(!macrobank::linkable(static_cast<LfoTarget>(-1)) && !macrobank::linkable(static_cast<LfoTarget>(999)),
          "outside the enum is not linkable");
    check(macrobank::targetNamed("PULSE") == LfoTarget::None && macrobank::targetNamed("RETIRED_5") == LfoTarget::None && macrobank::targetNamed("nope") == LfoTarget::None &&
              macrobank::targetNamed("") == LfoTarget::None,
          "an unlinkable or unknown name is None");
    // The top of a range is what safety::apply lets through, so no travel is clamped away later.
    check(about(macrobank::rangeOf(LfoTarget::Bloom).max, safety::kMaxBloom), "Bloom stops at its safety cap");
    check(about(macrobank::rangeOf(LfoTarget::Brightness).max, 1.0f + safety::kMaxFlashDepth), "Brightness stops at the clamp");
    check(about(macrobank::rangeOf(LfoTarget::Intensity).max, 1.0f + safety::kMaxFlashDepth), "Intensity stops at the clamp");
}

void centreIsTheIdentity() {
    SceneParams moved;
    moved.warp = 0.4f;
    moved.brightness = 2.0f;
    moved.speed = 3.0f;
    check(sameParams(macrobank::apply(moved, macrobank::defaultBank(), kEveryTarget), moved), "the default bank at centre changes nothing");
    std::vector<Link> all;
    for (const Target t : linkableTargets()) all.push_back(Link{t, 1.0f, -1.0f});
    check(sameParams(macrobank::apply(moved, oneKnob(all, 0.0f), kEveryTarget), moved), "a knob at centre changes nothing");
    check(sameParams(macrobank::apply(moved, Bank{}, kEveryTarget), moved), "an empty bank changes nothing");
    check(sameParams(macrobank::apply(moved, oneKnob({Link{LfoTarget::Warp, 0.0f, 0.0f}}, 1.0f), kEveryTarget), moved),
          "a link with no weight changes nothing");
}

void aFullTurnMovesEveryLinkByItsWeight() {
    const SceneParams base;
    for (const Target t : linkableTargets()) {
        const std::string name = macrobank::name(t);
        const macrobank::Range range = macrobank::rangeOf(t);
        const float span = range.max - range.min;
        const bool circular = SceneParams::schema()[static_cast<size_t>(t.slot)].kind == ParamKind::Circular;
        const Link link{t, 0.25f, -0.125f};
        const float was = fieldOf(base, t);
        const auto expected = [&](float fraction) {
            const float sum = was + fraction * span;
            return circular ? sum : std::clamp(sum, range.min, range.max);
        };
        const SceneParams up = macrobank::apply(base, oneKnob({link}, 1.0f), kEveryTarget);
        const SceneParams down = macrobank::apply(base, oneKnob({link}, -1.0f), kEveryTarget);
        const SceneParams half = macrobank::apply(base, oneKnob({link}, 0.5f), kEveryTarget);
        check(about(fieldOf(up, t), expected(0.25f)), name + " full up");
        check(about(fieldOf(down, t), expected(-0.125f)), name + " full down");
        check(about(fieldOf(half, t), expected(0.125f)), name + " half up");
        // Only the linked field moves.
        SceneParams undone = up;
        undone.setSlot(t.slot, was);
        check(sameParams(undone, base), name + " moves its own field only");
        // The down weight alone decides the way a turn down goes: here it rises.
        const SceneParams rising = macrobank::apply(base, oneKnob({Link{t, 0.0f, 0.25f}}, -1.0f), kEveryTarget);
        check(about(fieldOf(rising, t), expected(0.25f)), name + " turned down with a positive down weight");
    }
}

void neverPastTheRange() {
    for (const Target t : linkableTargets()) {
        if (SceneParams::schema()[static_cast<size_t>(t.slot)].kind == ParamKind::Circular) continue;
        const std::string name = macrobank::name(t);
        const macrobank::Range range = macrobank::rangeOf(t);
        const SceneParams base;
        // Two knobs on one target add up, then the sum is held.
        Bank bank = oneKnob({Link{t, 1.0f, -1.0f}}, 1.0f);
        Knob second = bank.knobs.front();
        second.id = 2;
        bank.knobs.push_back(second);
        check(fieldOf(macrobank::apply(base, bank, kEveryTarget), t) == range.max, name + " stops at its top");
        for (Knob& knob : bank.knobs) knob.value = -1.0f;
        check(fieldOf(macrobank::apply(base, bank, kEveryTarget), t) == range.min, name + " stops at its bottom");
    }
    // A wild knob or weight in a bank nobody normalized is held too.
    const SceneParams wild = macrobank::apply(SceneParams{}, oneKnob({Link{LfoTarget::Warp, 50.0f, 0.0f}}, 9.0f), kEveryTarget);
    check(wild.warp == 1.0f, "an out-of-range knob and weight are clamped");
}

void theHueTurnsUnclamped() {
    SceneParams base;
    base.colorShift = 0.9f;
    const SceneParams out = macrobank::apply(base, oneKnob({Link{LfoTarget::ColorShift, 0.5f, 0.0f}}, 1.0f), kEveryTarget);
    check(about(out.colorShift, 1.4f), "the hue adds past 1, as the LFO's does");
}

void theOffsetRidesOnTheBase() {
    const Bank bank = oneKnob({Link{LfoTarget::Warp, 0.5f, 0.0f}, Link{LfoTarget::Twist, 0.25f, -0.25f}}, 1.0f);
    SceneParams low;
    low.warp = 0.1f;
    low.twist = -0.3f;
    SceneParams high;
    high.warp = 0.3f;
    high.twist = 0.2f;
    const SceneParams a = macrobank::apply(low, bank, kEveryTarget);
    const SceneParams b = macrobank::apply(high, bank, kEveryTarget);
    check(about(a.warp, 0.6f) && about(b.warp, 0.8f), "the slider still moves Warp under a turned knob");
    check(about(a.twist, 0.2f) && about(b.twist, 0.7f), "the slider still moves Twist under a turned knob");
}

void aLinkOutOfReachIsSkipped() {
    const EngineStyles s = engineStyles();
    check(!s.flat.empty() && !s.threeD.empty() && !s.fluid.empty() && !s.milkdrop.empty(), "the catalogue has each engine");
    const Bank bank = oneKnob({Link{LfoTarget::Speed, 0.5f, 0.0f}, Link{LfoTarget::Morph, 0.5f, 0.0f},
                               Link{LfoTarget::FluidForce, 0.5f, 0.0f}, Link{LfoTarget::Warp, 0.5f, 0.0f}},
                              1.0f);
    const SceneParams base;
    const SceneParams milk = macrobank::apply(base, bank, s.milkdrop);
    check(milk.speed == base.speed && milk.morph == base.morph && milk.fluidSplatForce == base.fluidSplatForce,
          "MilkDrop skips the scene clock, the shader look and the emitters");
    check(milk.warp > base.warp, "MilkDrop still takes the composite's Warp");
    const SceneParams flat = macrobank::apply(base, bank, s.flat);
    check(flat.speed > base.speed && flat.morph > base.morph && flat.warp > base.warp, "a shader style takes its own");
    check(flat.fluidSplatForce == base.fluidSplatForce, "a shader style skips the emitters");
    const SceneParams fluid = macrobank::apply(base, bank, s.fluid);
    check(fluid.fluidSplatForce > base.fluidSplatForce && fluid.speed > base.speed && fluid.morph == base.morph,
          "Fluid takes the emitters and the clock, not the shader look");
    for (const std::string& id : {s.flat, s.threeD, s.fluid, s.milkdrop}) {
        const macrobank::Reach mask = macrobank::reachMask(id);
        for (int slot = 0; slot < SceneParams::kFieldCount; ++slot) {
            const Target t = Target::ofSlot(slot);
            const bool want = macrobank::linkable(t) && catalogue::reaches(SceneParams::meta(static_cast<ParamSlot>(slot)).scope, id);
            check(mask.test(static_cast<size_t>(slot)) == want, "reach bit of slot " + std::to_string(slot) + " on " + id);
        }
        for (int i = 0; i < modtargets::kTargetCount; ++i) {
            const auto t = static_cast<LfoTarget>(i);
            if (macrobank::linkable(t)) check(reachesStyle(t, id) == modtargets::reaches(t, id), "the bank reaches as the waves do: " + std::string(modtargets::name(t)));
        }
    }
    check(sameParams(macrobank::apply(base, bank, macrobank::Reach{}), base), "nothing in reach, nothing moved");
}

void nonFiniteInputIsIgnored() {
    SceneParams base;
    base.warp = 0.2f;
    check(sameParams(macrobank::apply(base, oneKnob({Link{LfoTarget::Warp, 1.0f, 1.0f}}, kNan), kEveryTarget), base), "a NaN knob moves nothing");
    check(sameParams(macrobank::apply(base, oneKnob({Link{LfoTarget::Warp, kNan, kNan}}, 1.0f), kEveryTarget), base), "a NaN weight moves nothing");
    SceneParams broken = base;
    broken.twist = kNan;
    const SceneParams out =
        macrobank::apply(broken, oneKnob({Link{LfoTarget::Twist, 0.5f, 0.0f}, Link{LfoTarget::Warp, 0.5f, 0.0f}}, 1.0f), kEveryTarget);
    check(std::isnan(out.twist) && about(out.warp, 0.7f), "a non-finite field is left alone, the others still move");
    const Bank bank = macrobank::defaultBank();
    const int id = bank.knobs.front().id;
    check(macrobank::setValue(bank, id, kNan) == bank && macrobank::setValue(bank, id, kInf) == bank, "a non-finite value is refused");
    check(macrobank::setLink(bank, id, LfoTarget::Zoom, kNan, 0.0f) == bank && macrobank::setLink(bank, id, LfoTarget::Zoom, 0.0f, kInf) == bank,
          "a non-finite weight is refused");
    const Bank dirty = oneKnob({Link{LfoTarget::Warp, kNan, kInf}}, kNan);
    const Bank clean = macrobank::normalized(dirty);
    check(clean.knobs.front().value == 0.0f && clean.knobs.front().links.front().up == 0.0f && clean.knobs.front().links.front().down == 0.0f,
          "normalized turns a non-finite number into 0");
}

void editsValidate() {
    const Bank start = macrobank::defaultBank();
    const int first = start.knobs.front().id;

    Bank b = macrobank::setValue(start, first, 0.5f);
    check(b.knobs.front().value == 0.5f, "set value");
    check(macrobank::setValue(start, first, 7.0f).knobs.front().value == 1.0f, "a value is held to 1");
    check(macrobank::setValue(start, first, -7.0f).knobs.front().value == -1.0f, "a value is held to -1");
    check(macrobank::setValue(start, 4242, 0.5f) == start, "an unknown knob is refused");

    b = macrobank::rename(start, first, "  My | knob\n ");
    check(b.knobs.front().name == "My  knob", "a name is trimmed and loses separators and control characters");
    check(b.knobs.front().hint.empty(), "a renamed knob drops the built-in hint");
    check(macrobank::rename(start, first, "   ") == start && macrobank::rename(start, first, "") == start, "an empty name is refused");
    check(macrobank::rename(start, first, start.knobs.front().name) == start, "the same name keeps the hint");
    const std::string longName(100, 'x');
    check(static_cast<int>(macrobank::rename(start, first, longName).knobs.front().name.size()) == macrobank::kMaxNameBytes, "a name is capped");
    // Two-byte characters: twelve fit the cap exactly, and one byte further in the cut backs off a whole character.
    std::string accented;
    for (int i = 0; i < 13; ++i) accented += "\xC3\xA9";
    check(macrobank::rename(start, first, accented).knobs.front().name.size() == 24u, "the cap keeps whole characters");
    accented = "a" + accented;
    check(macrobank::rename(start, first, accented).knobs.front().name.size() == 23u, "the cap never splits a character");

    b = macrobank::addKnob(start, "Mine");
    check(b.knobs.size() == start.knobs.size() + 1, "add a knob");
    const Knob& added = b.knobs.back();
    check(added.name == "Mine" && added.value == 0.0f && added.links.empty() && added.hint.empty(), "a new knob is last, centred and empty");
    std::set<int> ids;
    for (const Knob& knob : b.knobs) ids.insert(knob.id);
    check(ids.size() == b.knobs.size(), "ids are unique");
    const int mine = added.id;
    check(macrobank::addKnob(start, "").knobs.back().name == "Knob " + std::to_string(mine), "an unnamed knob is named by its id");

    // An id is never handed out twice, even after its knob is gone.
    const Bank removed = macrobank::removeKnob(b, mine);
    check(removed.knobs.size() == start.knobs.size(), "remove a knob");
    check(macrobank::addKnob(removed, "Again").knobs.back().id == mine + 1, "a removed knob's id is not reused");
    check(macrobank::removeKnob(start, 4242) == start, "removing an unknown knob changes nothing");

    Bank full = start;
    for (int i = 0; i < 20; ++i) full = macrobank::addKnob(full, "");
    check(macrobank::shownCount(full) == macrobank::kMaxShown && static_cast<int>(full.knobs.size()) == macrobank::kMaxShown,
          "the screen holds at most kMaxShown");
    Bank kept = start;
    for (const Knob& knob : start.knobs) kept = macrobank::removeKnob(kept, knob.id);
    check(kept == start, "a built-in knob cannot be removed");
    Bank none;
    none.nextId = 9;
    check(macrobank::addKnob(none, "New").knobs.size() == 1u, "a knob can be made in an empty bank");

    b = macrobank::setLink(b, mine, LfoTarget::Zoom, 0.25f, -0.5f);
    check(b.knobs.back().links.size() == 1u && (b.knobs.back().links.front() == Link{LfoTarget::Zoom, 0.25f, -0.5f}), "add a link");
    b = macrobank::setLink(b, mine, LfoTarget::Zoom, 5.0f, -5.0f);
    check(b.knobs.back().links.size() == 1u && (b.knobs.back().links.front() == Link{LfoTarget::Zoom, 1.0f, -1.0f}),
          "the same target replaces its link, weights held to -1..1");
    const Bank before = b;
    check(macrobank::setLink(b, mine, LfoTarget::Retired5, 0.5f, 0.5f) == before, "a retired target is refused");
    check(macrobank::setLink(b, mine, LfoTarget::Lfo1Rate, 0.5f, 0.5f) == before, "a chained target is refused");
    check(macrobank::setLink(b, mine, LfoTarget::None, 0.5f, 0.5f) == before, "None is refused");
    check(macrobank::setLink(b, 4242, LfoTarget::Warp, 0.5f, 0.5f) == before, "a link on an unknown knob is refused");
    check(macrobank::removeLink(b, mine, LfoTarget::Warp) == before, "removing a link that is not there changes nothing");
    check(macrobank::removeLink(b, mine, LfoTarget::Zoom).knobs.back().links.empty(), "remove a link");

    Bank many = macrobank::addKnob(none, "Many");
    const int manyId = many.knobs.front().id;
    for (const Target t : linkableTargets()) many = macrobank::setLink(many, manyId, t, 0.1f, 0.0f);
    check(static_cast<int>(many.knobs.front().links.size()) == macrobank::kMaxLinks, "a knob holds at most kMaxLinks");

    // Editing what a built-in knob moves drops its hint; turning it does not.
    check(!macrobank::setValue(start, first, 1.0f).knobs.front().hint.empty(), "a turned knob keeps its hint");
    check(macrobank::setLink(start, first, LfoTarget::Zoom, 0.1f, 0.0f).knobs.front().hint.empty(), "an edited knob drops its hint");
    const Link& existing = start.knobs.front().links.front();
    check(macrobank::setLink(start, first, existing.target, existing.up, existing.down) == start, "setting a link to itself changes nothing");
    check(macrobank::removeLink(start, first, existing.target).knobs.front().hint.empty(), "a knob that lost a link drops its hint");

    // normalized on a bank built by hand.
    Bank raw;
    Knob a;
    a.id = 3;
    a.name = "A";
    a.value = 4.0f;
    a.links = {Link{LfoTarget::Warp, 0.1f, 0.0f}, Link{LfoTarget::Retired5, 1.0f, 1.0f}, Link{LfoTarget::Warp, 0.3f, 0.0f}};
    Knob dup = a;
    dup.name = "Dup";
    Knob zero;
    zero.id = 0;
    Knob big;
    big.id = macrobank::kMaxId + 1;
    raw.knobs = {a, dup, zero, big};
    raw.nextId = -5;
    const Bank fixed = macrobank::normalized(raw);
    check(fixed.knobs.size() == 1u && fixed.knobs.front().name == "A", "a repeated or out-of-range id is dropped");
    check(fixed.knobs.front().value == 1.0f, "normalized clamps the value");
    check(fixed.knobs.front().links.size() == 1u && fixed.knobs.front().links.front().up == 0.3f, "one link per target, the later wins; unlinkable dropped");
    check(fixed.nextId == 4, "the next id is past every id in use");
    check(macrobank::normalized(fixed) == fixed, "normalized is idempotent");
}

// What the knob editor adds to the edits: a copy, a move, a built-in knob put back, and the one text
// it draws from.
void theEditorCopiesMovesAndResets() {
    const Bank& def = macrobank::defaultBank();
    const int glow = def.knobs[0].id;
    for (const Knob& knob : def.knobs) check(macrobank::isBase(knob.id), "the default bank's knobs are built in");
    check(!macrobank::isBase(0) && !macrobank::isBase(def.nextId), "no other id is built in");

    Bank b = macrobank::setValue(def, glow, 0.5f);
    b = macrobank::duplicateKnob(b, glow);
    check(b.knobs.size() == def.knobs.size() + 1 && b.knobs[1].id == def.nextId, "a copy sits right after its knob with a new id");
    check(b.knobs[1].name == def.knobs[0].name + " 2" && b.knobs[1].hint.empty(), "a copy is named after its knob and has no hint");
    check(b.knobs[1].links == def.knobs[0].links && b.knobs[1].value == 0.5f, "a copy moves what its knob moves");
    check(!macrobank::isBase(b.knobs[1].id), "a copy is the user's knob");
    check(macrobank::duplicateKnob(def, 4242) == def, "copying an unknown knob changes nothing");
    const std::string longName(static_cast<size_t>(macrobank::kMaxNameBytes), 'x');
    const Bank named = macrobank::duplicateKnob(macrobank::rename(def, glow, longName), glow);
    check(static_cast<int>(named.knobs[1].name.size()) == macrobank::kMaxNameBytes, "a copy's name stays within the cap");
    check(named.knobs[1].name.ends_with(" 2"), "and still says it is a copy");
    Bank full = def;
    while (macrobank::shownCount(full) < macrobank::kMaxShown) full = macrobank::addKnob(full, "");
    check(macrobank::duplicateKnob(full, glow) == full, "a full bank takes no copy");

    const auto order = [](const Bank& bank) {
        std::vector<int> ids;
        for (const Knob& knob : bank.knobs) ids.push_back(knob.id);
        return ids;
    };
    check(order(macrobank::moveKnob(def, glow, 1)) == std::vector<int>({2, 1, 3, 4}), "a knob moves one place later");
    check(order(macrobank::moveKnob(def, 4, -1)) == std::vector<int>({1, 2, 4, 3}), "and one place earlier");
    check(order(macrobank::moveKnob(def, glow, 99)) == std::vector<int>({2, 3, 4, 1}), "a long move stops at the end");
    check(order(macrobank::moveKnob(def, 3, std::numeric_limits<int>::min())) == std::vector<int>({3, 1, 2, 4}),
          "and at the start");
    check(macrobank::moveKnob(def, glow, -1) == def && macrobank::moveKnob(def, glow, 0) == def, "a move with nowhere to go changes nothing");
    check(macrobank::moveKnob(def, 4242, 1) == def, "moving an unknown knob changes nothing");
    check(macrobank::moveKnob(def, glow, 2).knobs[2] == def.knobs[0], "a moved knob is unchanged");

    Bank edited = macrobank::rename(def, glow, "Mine");
    edited = macrobank::setLink(edited, glow, LfoTarget::Zoom, 0.5f, 0.0f);
    edited = macrobank::removeLink(edited, glow, LfoTarget::Bloom);
    edited = macrobank::setValue(edited, glow, -0.25f);
    const Bank back = macrobank::resetKnob(edited, glow);
    check(back.knobs[0].name == def.knobs[0].name && back.knobs[0].hint == def.knobs[0].hint, "reset puts a built-in knob's name back");
    check(back.knobs[0].links == def.knobs[0].links, "and its links");
    check(back.knobs[0].value == -0.25f, "and leaves its turn");
    check(macrobank::setValue(back, glow, 0.0f) == def, "a reset bank is the default bank");
    const Bank mine = macrobank::addKnob(def, "Mine");
    check(macrobank::resetKnob(mine, mine.knobs.back().id) == mine, "a user's knob has nothing to reset to");

    const int made = mine.knobs.back().id;
    const Bank linked = macrobank::addLink(mine, made, LfoTarget::Zoom);
    check(linked.knobs.back().links == std::vector<Link>({{LfoTarget::Zoom, macrobank::kNewLinkWeight, -macrobank::kNewLinkWeight}}),
          "a new link starts a quarter of the range each way");
    const Bank tuned = macrobank::setLink(linked, made, LfoTarget::Zoom, 0.9f, 0.0f);
    check(macrobank::addLink(tuned, made, LfoTarget::Zoom) == tuned, "adding a link the knob has keeps its weights");
    check(macrobank::addLink(mine, made, LfoTarget::None) == mine && macrobank::addLink(mine, 4242, LfoTarget::Zoom) == mine,
          "an unlinkable target or an unknown knob adds nothing");
    check(macrobank::decode(geode_macro_bank_add_link(macrobank::encode(mine).c_str(), made, "ZOOM")) == linked, "geode_macro_bank_add_link");
    check(macrobank::resetKnob(def, 4242) == def, "resetting an unknown knob changes nothing");
}

std::vector<std::vector<std::string>> rowsOf(const std::string& text, const std::string& tag) {
    std::vector<std::vector<std::string>> out;
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t end = text.find('\n', pos);
        const std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        std::vector<std::string> f;
        size_t at = 0;
        while (true) {
            const size_t bar = line.find('|', at);
            f.push_back(line.substr(at, bar == std::string::npos ? bar : bar - at));
            if (bar == std::string::npos) break;
            at = bar + 1;
        }
        if (f[0] == tag) out.push_back(f);
    }
    return out;
}

void theEditorTextSaysWhatToOffer() {
    const Bank& def = macrobank::defaultBank();
    const EngineStyles s = engineStyles();
    const std::string text = macrobank::editorText(def, s.flat);
    check(text.rfind("macroeditor|" + std::to_string(macrobank::kTextVersion) + "\n", 0) == 0, "the editor text opens with its version");
    const auto caps = rowsOf(text, "caps");
    check(caps.size() == 1u && caps[0][1] == std::to_string(macrobank::kMaxShown) && caps[0][2] == std::to_string(macrobank::kMaxLinks) &&
              caps[0][3] == std::to_string(macrobank::kMaxNameBytes),
          "it carries the caps");
    check(rowsOf(text, "bank") == std::vector<std::vector<std::string>>({{"bank", "1", "1"}}), "the default bank has room and is the default");
    check(rowsOf(text, "screen") == std::vector<std::vector<std::string>>({{"screen", "1"}}), "and room on screen");
    check(rowsOf(text, "library").empty(), "the default bank's library is empty");
    const auto knobs = rowsOf(text, "knob");
    check(knobs.size() == def.knobs.size(), "one knob row per knob");
    for (size_t i = 0; i < knobs.size(); ++i) {
        const auto& k = knobs[i];
        check(k.size() == 8u && k[1] == std::to_string(def.knobs[i].id) && k[6] == def.knobs[i].name && k[7] == def.knobs[i].hint,
              "a knob row carries its id, name and hint");
        check(k[2] == "1" && k[3] == "1" && k[4] == "1" && k[5] == "0", "a built-in knob is as built, has room for links and is centred");
    }
    size_t links = 0;
    for (const Knob& knob : def.knobs) links += knob.links.size();
    check(rowsOf(text, "link").size() == links, "one link row per link");
    const auto targets = rowsOf(text, "target");
    check(targets.size() == linkableTargets().size(), "one target row per linkable target");
    for (const auto& t : targets) {
        const Target target = macrobank::targetNamed(t[1]);
        check(macrobank::linkable(target) && t.size() == 4u, "a target row names a linkable target: " + t[1]);
        check((t[2] == "1") == reachesStyle(target, s.flat), "and whether it reaches the style: " + t[1]);
        check(t.size() == 4u && t[3] == macrobank::controlOf(target) && !t[3].empty(), "and the control that sets it: " + t[1]);
    }
    const auto adds = rowsOf(text, "addrandom");
    check(adds.size() == def.knobs.size(), "one Add random row per knob");
    for (size_t i = 0; i < adds.size() && i < def.knobs.size(); ++i) {
        const int room = macrobank::addRandomRoom(def, def.knobs[i].id, macrobank::reachMask(s.flat));
        check(adds[i] == std::vector<std::string>({"addrandom", std::to_string(std::min(macrobank::kAddRandomDefault, room)), std::to_string(room)}),
              "it offers the default count within the room: " + def.knobs[i].name);
    }
    // FluidForce is on Motion and only reaches the fluid engine.
    bool dimmed = false;
    for (const auto& l : rowsOf(text, "link")) dimmed = dimmed || (l[1] == "FLUID_FORCE" && l[4] == "0");
    check(dimmed, "a link the style cannot show says so");

    Bank b = macrobank::rename(def, def.knobs[0].id, "Mine");
    b = macrobank::setValue(b, def.knobs[1].id, 0.5f);
    b = macrobank::addKnob(b, "New");
    const int mine = b.knobs.back().id;
    for (const Target t : linkableTargets()) b = macrobank::setLink(b, mine, t, 0.1f, 0.1f);
    const auto edited = rowsOf(macrobank::editorText(b, s.flat), "knob");
    check(edited[0][2] == "1" && edited[0][3] == "0", "an edited built-in knob can be reset");
    check(edited[1][3] == "1" && edited[1][5] == "0.5", "a turned one is still as built");
    check(edited.back()[2] == "0" && edited.back()[3] == "0" && edited.back()[4] == "0", "a user's knob is not built in, and full of links");
    check(rowsOf(macrobank::editorText(b, s.flat), "bank")[0][2] == "0", "an edited bank is not the default");
    check(rowsOf(macrobank::editorText(macrobank::moveKnob(def, 1, 1), s.flat), "bank")[0][2] == "0", "nor is a reordered one");
    check(rowsOf(macrobank::editorText(macrobank::setValue(def, 1, 1.0f), s.flat), "bank")[0][2] == "1", "a turned one is");
    Bank full = def;
    while (macrobank::shownCount(full) < macrobank::kMaxShown) full = macrobank::addKnob(full, "");
    check(rowsOf(macrobank::editorText(full, s.flat), "bank")[0][1] == "0", "a full bank offers no new knob");
    check(std::string(geode_macro_bank_editor(nullptr, nullptr)) == macrobank::editorText(def, ""), "the ABI reads NULLs as the default bank");
}

// The editor text of a bank with every kind of row, the copy JVM tests parse (geode.macroEditor):
// an edited built-in knob, a turned one, a user's knob, a link out of reach.
std::string editorFixture() {
    const Bank& def = macrobank::defaultBank();
    Bank b = macrobank::rename(def, def.knobs[0].id, "Shine");
    b = macrobank::setValue(b, def.knobs[1].id, 0.5f);
    b = macrobank::addKnob(b, "Mine");
    b = macrobank::addLink(b, b.knobs.back().id, LfoTarget::Zoom);
    b = macrobank::setLink(b, b.knobs.back().id, LfoTarget::FluidGlow, 1.0f, 0.0f);
    b = macrobank::setCurve(b, b.knobs.back().id, LfoTarget::Zoom, macrobank::Curve::EaseIn);
    b = macrobank::addKnob(b, "Spare");
    b = macrobank::hideKnob(b, b.knobs.back().id);
    return macrobank::editorText(b, engineStyles().flat);
}

// A link's curve: how it follows the knob between centre and a full turn, the same each way.
void curvesShapeTheTurn() {
    using macrobank::Curve;
    for (int i = 0; i < macrobank::kCurveCount; ++i) {
        const auto curve = static_cast<Curve>(i);
        check(macrobank::shape(curve, 0.0f) == 0.0f && macrobank::shape(curve, 1.0f) == 1.0f,
              std::string("every curve runs 0 to 1: ") + macrobank::name(curve));
        float last = 0.0f;
        for (int k = 1; k <= 100; ++k) {
            const float y = macrobank::shape(curve, static_cast<float>(k) / 100.0f);
            check(y >= last, std::string("and never turns back: ") + macrobank::name(curve));
            last = y;
        }
        check(macrobank::curveNamed(macrobank::name(curve)) == curve, "a curve is found by its name");
    }
    check(!macrobank::curveNamed("WOBBLE").has_value(), "an unknown curve name is none");
    check(about(macrobank::shape(Curve::Linear, 0.5f), 0.5f), "linear is even");
    check(macrobank::shape(Curve::EaseIn, 0.5f) < 0.5f && macrobank::shape(Curve::EaseOut, 0.5f) > 0.5f, "ease in starts slow, ease out fast");
    check(about(macrobank::shape(Curve::SCurve, 0.5f), 0.5f) && macrobank::shape(Curve::SCurve, 0.2f) < 0.2f &&
              macrobank::shape(Curve::SCurve, 0.8f) > 0.8f,
          "the S-curve is slow at both ends");
    check(macrobank::shape(Curve::Linear, kNan) == 0.0f && macrobank::shape(Curve::EaseIn, 7.0f) == 1.0f, "shape holds its input to 0..1");

    const macrobank::Range r = macrobank::rangeOf(LfoTarget::Zoom);
    const SceneParams base;
    const float rest = fieldOf(base, LfoTarget::Zoom);
    for (const float turn : {0.5f, -0.5f, 1.0f, -1.0f}) {
        for (int i = 0; i < macrobank::kCurveCount; ++i) {
            const auto curve = static_cast<Curve>(i);
            const Bank b = oneKnob({{LfoTarget::Zoom, 0.4f, -0.3f, curve}}, turn);
            const float want = std::clamp(rest + macrobank::shape(curve, std::fabs(turn)) * (turn >= 0 ? 0.4f : -0.3f) * (r.max - r.min), r.min, r.max);
            check(about(fieldOf(macrobank::apply(base, b, kEveryTarget), LfoTarget::Zoom), want),
                  std::string("a link moves by its curve times its range: ") + macrobank::name(curve));
        }
    }
    check(sameParams(macrobank::apply(base, oneKnob({{LfoTarget::Zoom, 1.0f, 1.0f, Curve::EaseIn}}, 0.0f), kEveryTarget), base),
          "centre still moves nothing");
}

void curvesAreEditedAndKept() {
    using macrobank::Curve;
    const Bank& def = macrobank::defaultBank();
    const int glow = def.knobs[0].id;
    // The first built-in knob's first link, and a target it does not link.
    const Target first = def.knobs[0].links[0].target;
    const Target other = LfoTarget::Ripple;
    Bank b = macrobank::setCurve(def, glow, first, Curve::EaseOut);
    check(b.knobs[0].links[0].curve == Curve::EaseOut, "setCurve sets the link's curve");
    check(b.knobs[0].hint.empty(), "and clears the built-in hint");
    check(macrobank::setCurve(b, glow, other, Curve::EaseIn) == b, "a curve on a target the knob does not link is refused");
    check(macrobank::setCurve(b, 999, first, Curve::EaseIn) == b, "and on an unknown knob");
    check(macrobank::setCurve(b, glow, first, static_cast<Curve>(9)) == b, "and an unknown curve");
    b = macrobank::setLink(b, glow, first, 0.5f, -0.5f);
    check(b.knobs[0].links[0].curve == Curve::EaseOut && b.knobs[0].links[0].up == 0.5f, "a range edit keeps the curve");
    const Bank copy = macrobank::duplicateKnob(b, glow);
    check(copy.knobs[1].links[0].curve == Curve::EaseOut, "a copy keeps its curves");
    check(macrobank::resetKnob(b, glow).knobs[0].links[0].curve == Curve::Linear, "Reset puts a built-in knob's curves back");
    check(rowsOf(macrobank::editorText(b, ""), "knob")[0][3] == "0", "a changed curve is not as built");
    Bank wild = b;
    wild.knobs[0].links[0].curve = static_cast<Curve>(42);
    check(macrobank::normalized(wild).knobs[0].links[0].curve == Curve::Linear, "normalized makes an unknown curve linear");
}

void curvesSurviveTheTextAndOldTextLoads() {
    using macrobank::Curve;
    const Bank& def = macrobank::defaultBank();
    const std::string plain = macrobank::encode(def);
    check(plain.find("LINEAR") == std::string::npos, "a linear link writes no curve, so the text reads as before curves");
    Bank b = macrobank::setCurve(def, def.knobs[0].id, LfoTarget::Warp, Curve::SCurve);
    b = macrobank::setCurve(b, def.knobs[1].id, LfoTarget::Speed, Curve::EaseIn);
    const std::string text = macrobank::encode(b);
    check(text.find("link|WARP|0.8|0|S_CURVE\n") != std::string::npos, "a curved link writes its curve last");
    check(macrobank::decode(text) == b, "curves round trip");
    // Text saved before curves: four fields per link.
    const std::string old = "macrobank|1|6\nknob|5|0.5|Old|\nlink|ZOOM|0.3|-0.2\nlink|BLOOM|0.1|0.1|WOBBLE\n";
    const Bank read = macrobank::decode(old);
    check(read.knobs.size() == 1u && read.knobs[0].links.size() == 2u, "old text loads every link");
    check(read.knobs[0].links[0].curve == Curve::Linear && about(read.knobs[0].links[0].up, 0.3f) && about(read.knobs[0].links[0].down, -0.2f),
          "an old link is linear with its range as saved");
    check(read.knobs[0].links[1].curve == Curve::Linear, "an unknown curve reads as linear");
    check(macrobank::decode("macrobank|1|6\nknob|5|0|K|\nlink|ZOOM|0.3|0.3|LINEAR|extra\n").knobs[0].links.empty(), "a link row with too many fields is dropped");
}

void theEditorTextShowsRangesInUnits() {
    const Bank& def = macrobank::defaultBank();
    const auto links = rowsOf(macrobank::editorText(def, ""), "link");
    for (const auto& l : links) {
        check(l.size() == 8u, "a link row carries its curve and both offsets: " + l[1]);
        const Target target = macrobank::targetNamed(l[1]);
        const macrobank::Range r = macrobank::rangeOf(target);
        check(l[5] == "LINEAR", "a built-in link is linear: " + l[1]);
        check(about(std::stof(l[6]), std::stof(l[2]) * (r.max - r.min)) && about(std::stof(l[7]), std::stof(l[3]) * (r.max - r.min)),
              "its offsets are its range in the param's units: " + l[1]);
    }
    bool bloom = false;
    for (const auto& l : links) bloom = bloom || (l[1] == "BLOOM" && l[6] == "0.6");
    check(bloom, "a full turn up of the knob that links Bloom adds 0.6, the safe top");
    const auto curves = rowsOf(macrobank::editorText(def, ""), "curve");
    check(curves.size() == static_cast<size_t>(macrobank::kCurveCount) && curves[0][1] == "LINEAR" && curves[3][1] == "S_CURVE",
          "the editor lists every curve");
}

void aSliderMakesOrJoinsAKnob() {
    const Bank& def = macrobank::defaultBank();
    check(macrobank::targetOfControl("Bloom") == LfoTarget::Bloom, "the Bloom slider is the Bloom target");
    check(macrobank::targetOfControl("Palette") == LfoTarget::None && macrobank::targetOfControl("No such") == LfoTarget::None &&
              macrobank::targetOfControl("@macros") == LfoTarget::None,
          "a control with no linkable param has no target");
    Bank b = macrobank::addKnobFor(def, "Bloom");
    check(b.knobs.size() == 5u && b.knobs.back().name == "Bloom", "a new knob is named after its first control");
    check(b.knobs.back().links.size() == 1u && b.knobs.back().links[0] == Link{LfoTarget::Bloom, macrobank::kNewLinkWeight, -macrobank::kNewLinkWeight},
          "and moves it at the starting weights");
    b = macrobank::addKnobFor(b, "Bloom");
    check(b.knobs.back().name == "Bloom 2", "a second one is told apart");
    check(macrobank::addKnobFor(b, "Palette") == b, "a control with no target makes no knob");
    Bank full = b;
    while (macrobank::shownCount(full) < macrobank::kMaxShown) full = macrobank::addKnob(full, "");
    check(macrobank::addKnobFor(full, "Zoom") == full, "a full bank makes no knob");

    const std::string text = macrobank::controlsText(def);
    check(text.rfind("macrocontrols|" + std::to_string(macrobank::kTextVersion) + "\n", 0) == 0, "the controls text opens with its version");
    const auto rows = rowsOf(text, "control");
    check(!rows.empty(), "some controls can be linked");
    std::set<std::string> keys;
    for (const auto& r : rows) {
        check(r.size() == 5u && customize::categoryNamed(r[3]).has_value() && macrobank::targetNamed(r[2]) == macrobank::targetOfControl(r[1]) && macrobank::targetNamed(r[2]) != LfoTarget::None,
              "a control row names its target: " + r[1]);
        check(keys.insert(r[1]).second, "each control once: " + r[1]);
        if (r[1].back() == ')') {
            const std::string named = macrobank::addKnobFor(def, r[1]).knobs.back().name;
            check(named.find('(') == std::string::npos && r[1].rfind(named, 0) == 0, "a knob's name drops the unit: " + named);
        }
    }
    int bloomKnob = 0;
    for (const Knob& knob : def.knobs) {
        for (const Link& link : knob.links) {
            if (link.target == LfoTarget::Bloom) bloomKnob = knob.id;
        }
    }
    for (const auto& r : rows) {
        if (r[1] == "Bloom") check(r[3] == "LOOK" && r[4] == std::to_string(bloomKnob), "Bloom is moved by the built-in knob that links it");
        if (r[1] == "Ripple") check(r[4].empty(), "Ripple by no knob");
    }
    const Bank both = macrobank::addLink(macrobank::addKnobFor(def, "Ripple"), def.knobs[0].id, LfoTarget::Ripple);
    for (const auto& r : rowsOf(macrobank::controlsText(both), "control")) {
        if (r[1] == "Ripple") check(r[4] == std::to_string(def.knobs[0].id) + "," + std::to_string(both.knobs.back().id), "ids in bank order");
    }
}

// An empty knob, then Random: native rolls its links, seeded.
void aRandomRollGivesAKnobLinks() {
    Bank b = macrobank::addKnob(macrobank::defaultBank(), "");
    const int id = b.knobs.back().id;
    check(b.knobs.back().links.empty() && b.knobs.back().name == "Knob " + std::to_string(id), "a new knob starts empty");
    check(macrobank::roll(b, id, 7) == macrobank::roll(b, id, 7), "the same seed rolls the same set");
    check(macrobank::roll(b, 999, 7) == macrobank::normalized(b), "a roll on an unknown knob is refused");
    const SceneParams base;
    int differ = 0;
    for (std::uint64_t seed = 0; seed < 300; ++seed) {
        const Bank r = macrobank::roll(b, id, seed);
        const Knob& k = r.knobs.back();
        check(k.links.size() >= 2u && k.links.size() <= 5u, "a roll is never empty and never a flood");
        std::set<Target> seen;
        for (const Link& l : k.links) {
            check(macrobank::linkable(l.target) && seen.insert(l.target).second, "a roll links distinct linkable targets");
            check(std::fabs(l.up) >= 0.15f - 1e-6f && std::fabs(l.up) <= 0.6f + 1e-6f, "a roll's range up is 15-60 %");
            check(l.down == 0.0f || (l.down * l.up < 0.0f && std::fabs(l.down) <= std::fabs(l.up) + 1e-6f), "and down none or back");
        }
        for (const float turn : {1.0f, -1.0f, 0.5f}) {
            Bank turned = macrobank::setValue(r, id, turn);
            for (Knob& other : turned.knobs) {
                if (other.id != id) other.value = 0.0f;
            }
            const SceneParams moved = macrobank::apply(base, turned, kEveryTarget);
            const SceneParams safe = safety::apply(moved);
            for (const Link& l : k.links) {
                const macrobank::Range range = macrobank::rangeOf(l.target);
                const float v = fieldOf(moved, l.target);
                if (SceneParams::schema()[static_cast<size_t>(l.target.slot)].kind != ParamKind::Circular) {
                    check(v >= range.min - 1e-5f && v <= range.max + 1e-5f, "a rolled knob stays inside the range");
                    check(about(fieldOf(safe, l.target), v), "and inside the safety clamp");
                }
            }
        }
        differ += r != macrobank::roll(b, id, seed + 1000) ? 1 : 0;
    }
    check(differ > 290, "different seeds roll different sets");
    const Bank rolled = macrobank::roll(macrobank::defaultBank(), macrobank::defaultBank().knobs[0].id, 3);
    check(rolled.knobs[0].hint.empty(), "a rolled built-in knob loses its hint");
    check(std::string(geode_macro_bank_roll(macrobank::encode(b).c_str(), id, 7)) == macrobank::encode(macrobank::roll(b, id, 7)),
          "geode_macro_bank_roll");
}

// Off screen and back: a knob in the library is centred and moves nothing, keeps its name and links, and Add
// macro offers it; a built-in knob is never deleted, only taken off screen.
void theLibraryHoldsWhatTheScreenDoesNot() {
    const Bank& def = macrobank::defaultBank();
    const EngineStyles s = engineStyles();
    const int shape = def.knobs[0].id;
    Bank turned = macrobank::setValue(def, shape, 1.0f);
    Bank b = macrobank::hideKnob(turned, shape);
    check(b.knobs.size() == def.knobs.size() && !b.knobs[0].shown && b.knobs[0].value == 0.0f, "a hidden knob stays in the bank, centred");
    check(b.knobs[0].links == def.knobs[0].links && b.knobs[0].name == def.knobs[0].name, "with its name and links");
    check(macrobank::shownCount(b) == 3, "and off screen");
    check(macrobank::setValue(b, shape, 0.5f) == b, "a hidden knob cannot be turned");
    Bank forced = b;
    forced.knobs[0].value = 1.0f;
    check(sameParams(macrobank::apply(SceneParams{}, forced, kEveryTarget), SceneParams{}), "a hidden knob moves nothing, whatever its value");
    check(macrobank::hideKnob(b, shape) == b && macrobank::hideKnob(b, 4242) == b, "hiding twice or an unknown knob changes nothing");

    const std::string text = macrobank::editorText(b, s.flat);
    const auto knobs = rowsOf(text, "knob");
    check(knobs.size() == 3u && knobs[0][1] == std::to_string(def.knobs[1].id), "the editor lists the knobs on screen only");
    const auto library = rowsOf(text, "library");
    check(library.size() == 1u && library[0].size() == 5u && library[0][1] == std::to_string(shape) && library[0][2] == "1" &&
              library[0][3] == def.knobs[0].name,
          "the library offers the hidden built-in knob");
    check(rowsOf(text, "bank")[0][2] == "0", "a bank with a knob off screen is not the default");
    check(rowsOf(macrobank::liveText(b, s.flat), "knob").size() == 3u, "the live text lists the knobs on screen");
    for (const auto& r : rowsOf(macrobank::controlsText(b), "control")) {
        if (r[1] == "Morph") check(r[4].empty(), "a hidden knob moves no slider");
    }

    Bank empty = def;
    for (const Knob& knob : def.knobs) empty = macrobank::hideKnob(empty, knob.id);
    check(macrobank::shownCount(empty) == 0 && empty.knobs.size() == 4u, "every built-in knob can leave the screen");
    check(rowsOf(macrobank::editorText(empty, s.flat), "library").size() == 4u, "and all four are offered back");
    check(rowsOf(macrobank::editorText(empty, s.flat), "bank")[0][1] == "1", "an empty screen takes a new knob");
    Bank mine = macrobank::addKnob(empty, "Mine");
    check(macrobank::shownCount(mine) == 1 && mine.knobs.back().shown, "a new knob goes on screen");
    mine = macrobank::hideKnob(mine, mine.knobs.back().id);
    const auto shelf = rowsOf(macrobank::editorText(mine, s.flat), "library");
    check(shelf.size() == 5u && shelf.back()[3] == "Mine" && shelf.back()[2] == "0", "the user's own come after the built-in ones");
    check(macrobank::removeKnob(mine, mine.knobs.back().id).knobs.size() == 4u, "a user's hidden knob can be deleted");
    check(macrobank::removeKnob(empty, shape) == empty, "a built-in one cannot");

    Bank back = macrobank::showKnob(b, shape);
    check(back.knobs.back().id == shape && back.knobs.back().shown && back.knobs.back().value == 0.0f, "a knob comes back last and centred");
    check(back.knobs.back().links == def.knobs[0].links, "as it left");
    check(macrobank::showKnob(back, shape) == back && macrobank::showKnob(back, 4242) == back, "showing one on screen or unknown changes nothing");
    Bank lacking = def;
    lacking.knobs.erase(lacking.knobs.begin() + 2);
    lacking = macrobank::normalized(lacking);
    const auto offered = rowsOf(macrobank::editorText(lacking, s.flat), "library");
    check(offered.size() == 1u && offered[0][1] == std::to_string(def.knobs[2].id), "a built-in knob missing from the bank is offered");
    const Bank restored = macrobank::showKnob(lacking, def.knobs[2].id);
    check(restored.knobs.size() == 4u && restored.knobs.back() == def.knobs[2], "and comes back as built");

    Bank crowded = empty;
    while (macrobank::shownCount(crowded) < macrobank::kMaxShown) crowded = macrobank::addKnob(crowded, "");
    check(macrobank::showKnob(crowded, shape) == crowded, "a full screen takes no knob from the library");
    check(rowsOf(macrobank::editorText(crowded, s.flat), "screen")[0][1] == "0" && rowsOf(macrobank::editorText(crowded, s.flat), "bank")[0][1] == "0",
          "and says so");
    check(macrobank::duplicateKnob(crowded, crowded.knobs.back().id) == crowded, "nor a copy");
    Bank packed = crowded;
    while (static_cast<int>(packed.knobs.size()) < macrobank::kMaxKnobs) {
        packed = macrobank::hideKnob(packed, packed.knobs.back().id);
        const Bank next = macrobank::addKnob(packed, "");
        if (next == packed) break;
        packed = next;
    }
    check(static_cast<int>(packed.knobs.size()) == macrobank::kMaxKnobs && !macrobank::roomForNew(packed), "the bank holds at most kMaxKnobs");

    // Moves count the screen only.
    Bank gap = macrobank::hideKnob(def, def.knobs[1].id);
    const Bank moved = macrobank::moveKnob(gap, shape, 1);
    std::vector<int> onScreen;
    for (const Knob& knob : moved.knobs) {
        if (knob.shown) onScreen.push_back(knob.id);
    }
    check(onScreen == std::vector<int>({def.knobs[2].id, shape, def.knobs[3].id}), "a move steps past the library's knobs");
    check(macrobank::moveKnob(gap, def.knobs[1].id, 1) == gap, "a hidden knob does not move");

    // The text: a hidden knob survives a round trip, and text from before the library keeps every knob on screen.
    check(macrobank::decode(macrobank::encode(mine)) == macrobank::normalized(mine), "the library survives the text");
    check(macrobank::encode(b).find("\nshelf|" + std::to_string(shape) + "\n") != std::string::npos, "a hidden knob is a shelf row");
    const std::string before = "macrobank|1|6\nknob|1|0.5|Mine|\nlink|ZOOM|0.25|-0.25\nknob|5|-0.25|Other|\n";
    const Bank old = macrobank::decode(before);
    check(old.knobs.size() == 2u && old.knobs[0].shown && old.knobs[1].shown && old.knobs[0].value == 0.5f, "an old bank keeps its knobs on screen");
    check(macrobank::encode(old) == before, "and writes back the same text");
    check(macrobank::decode("macrobank|1|6\nknob|1|0.5|Mine|\nshelf|x\nshelf|1|2\n").knobs[0].shown, "an unreadable shelf row is ignored");
    std::string flood = "macrobank|1|20\n";
    for (int i = 1; i <= 12; ++i) flood += "knob|" + std::to_string(i) + "|0.5|K|\n";
    const Bank many = macrobank::decode(flood);
    check(static_cast<int>(many.knobs.size()) == 12 && macrobank::shownCount(many) == macrobank::kMaxShown && many.knobs.back().value == 0.0f,
          "knobs past the screen's cap go to the library, centred");

    const std::string encoded = macrobank::encode(def);
    check(std::string(geode_macro_bank_hide_knob(encoded.c_str(), shape)) == macrobank::encode(macrobank::hideKnob(def, shape)),
          "geode_macro_bank_hide_knob");
    check(std::string(geode_macro_bank_show_knob(macrobank::encode(b).c_str(), shape)) == macrobank::encode(back), "geode_macro_bank_show_knob");
}

// The dice on Quick knobs: a new turn for each knob on screen, seeded, never past a full turn, the way the knob
// moves something on the style, and the links untouched.
void theDiceRollsTheTurns() {
    const Bank& def = macrobank::defaultBank();
    const EngineStyles s = engineStyles();
    check(macrobank::rollValues(def, s.flat, 7) == macrobank::rollValues(def, s.flat, 7), "the same seed rolls the same turns");
    int differ = 0;
    int ups = 0;
    int downs = 0;
    for (std::uint64_t seed = 0; seed < 200; ++seed) {
        const Bank r = macrobank::rollValues(def, kEveryTarget, seed);
        check(r.knobs.size() == def.knobs.size(), "a roll keeps the knobs");
        for (size_t i = 0; i < r.knobs.size(); ++i) {
            const Knob& k = r.knobs[i];
            check(k.links == def.knobs[i].links && k.name == def.knobs[i].name && k.hint == def.knobs[i].hint, "a roll keeps what a knob moves");
            check(std::fabs(k.value) >= macrobank::kRollMin - 1e-6f && std::fabs(k.value) <= 1.0f, "a rolled turn shows and stays in -1..1");
            (k.value > 0.0f ? ups : downs) += 1;
        }
        differ += r != macrobank::rollValues(def, kEveryTarget, seed + 1000) ? 1 : 0;
    }
    check(differ > 195, "different seeds roll different turns");
    check(ups > 200 && downs > 200, "both ways come up");

    Bank one = oneKnob({Link{LfoTarget::Zoom, 0.5f, 0.0f}}, 0.0f);
    for (std::uint64_t seed = 0; seed < 50; ++seed) check(macrobank::rollValues(one, kEveryTarget, seed).knobs[0].value > 0.0f, "a knob that only moves up turns up");
    one = oneKnob({Link{LfoTarget::Zoom, 0.0f, -0.5f}}, 0.0f);
    for (std::uint64_t seed = 0; seed < 50; ++seed) check(macrobank::rollValues(one, kEveryTarget, seed).knobs[0].value < 0.0f, "one that only moves down, down");
    one = oneKnob({Link{LfoTarget::FluidGlow, 0.5f, 0.5f}}, 0.25f);
    check(!modtargets::reaches(LfoTarget::FluidGlow, s.flat), "FluidGlow is not on a flat style");
    check(macrobank::rollValues(one, s.flat, 3).knobs[0].value == 0.25f, "a knob that moves nothing on the style keeps its turn");
    check(macrobank::rollValues(oneKnob({}, -0.5f), s.flat, 3).knobs[0].value == -0.5f, "and so does an empty one");

    Bank hidden = macrobank::hideKnob(def, def.knobs[0].id);
    const Bank rolled = macrobank::rollValues(hidden, kEveryTarget, 11);
    check(rolled.knobs[0].value == 0.0f && !rolled.knobs[0].shown, "the library's knobs stay centred");
    check(rolled.knobs[1].value == macrobank::rollValues(def, kEveryTarget, 11).knobs[1].value, "and do not shift the others' turns");
    check(std::string(geode_macro_bank_roll_values(macrobank::encode(def).c_str(), s.flat.c_str(), 7)) ==
              macrobank::encode(macrobank::rollValues(def, s.flat, 7)),
          "geode_macro_bank_roll_values");
}

void theSaveNameIsNatives() {
    Bank b = macrobank::addKnob(macrobank::defaultBank(), "");
    const int id = b.knobs.back().id;
    check(macrobank::autoName(b, id) == "Knob " + std::to_string(id), "an empty knob is offered Knob <id>");
    b = macrobank::addLink(b, id, LfoTarget::Zoom);
    check(macrobank::autoName(b, id) == "Zoom", "a linked one is named after its first control");
    b = macrobank::addKnobFor(b, "Zoom");
    check(macrobank::autoName(b, id) == "Zoom 2", "told apart from another knob");
    check(macrobank::autoName(b, b.knobs.back().id) == "Zoom", "but not from itself");
    check(macrobank::autoName(b, 999).empty(), "an unknown knob has no name");
    check(std::string(geode_macro_bank_auto_name(macrobank::encode(b).c_str(), id)) == "Zoom 2", "geode_macro_bank_auto_name");
}

void theEditorFixtureIsCurrent(const char* path) {
    std::ifstream in(path, std::ios::binary);
    const std::string held((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    check(held == editorFixture(), std::string("editorText differs from ") + path + "; after a deliberate change run macro_bank_test --write " + path);
}

void textRoundTrips() {
    const Bank& def = macrobank::defaultBank();
    check(macrobank::decode(macrobank::encode(def)) == def, "the default bank round trips");
    Bank edited = macrobank::setValue(def, def.knobs[1].id, -0.37f);
    edited = macrobank::rename(edited, def.knobs[2].id, "Sm\xC3\xB6rg\xC3\xA5s");
    edited = macrobank::moveKnob(edited, def.knobs[0].id, 2);
    edited = macrobank::addKnob(edited, "Mine");
    edited = macrobank::setLink(edited, edited.knobs.back().id, LfoTarget::CamFov, 0.123456789f, -1.0f);
    edited = macrobank::setLink(edited, edited.knobs.back().id, LfoTarget::Zoom, 1.0f / 3.0f, 0.0f);
    const std::string text = macrobank::encode(edited);
    check(macrobank::decode(text) == edited, "an edited bank round trips bit for bit");
    check(macrobank::encode(macrobank::decode(text)) == text, "and its text is stable");
    check(text.rfind("macrobank|" + std::to_string(macrobank::kTextVersion) + "|", 0) == 0, "the text opens with its version");
    check(macrobank::decode(macrobank::encode(Bank{})).knobs.empty(), "an empty bank stays empty");
    // Windows line ends and blank lines read the same.
    std::string crlf;
    for (const char c : text) crlf += c == '\n' ? std::string("\r\n\r\n") : std::string(1, c);
    check(macrobank::decode(crlf) == edited, "CRLF and blank lines are tolerated");
}

void garbageDecodesToSomethingValid() {
    const Bank& def = macrobank::defaultBank();
    for (const char* bad : {"", "\n\n", "hello", "macrobank", "macrobank|x|1\n", "macrobank|2|9\nknob|1|0|A|\n", "knob|1|0|A|\n",
                            "macrobank|1\n", "macrobank|1|1|extra\n", "\xff\xfe|||\n|", "macrobank|1|notanumber\n"}) {
        check(macrobank::decode(bad) == def, std::string("unreadable text is the default bank: '") + bad + "'");
    }
    const std::string header = "macrobank|" + std::to_string(macrobank::kTextVersion) + "|1\n";
    const Bank mixed = macrobank::decode(header +
                                         "knob|7|0.5|Keep|\n"
                                         "link|WARP|0.5|-0.25\n"
                                         "link|NOT_A_TARGET|1|1\n"
                                         "link|PULSE|1|1\n"
                                         "link|LFO1_RATE|1|1\n"
                                         "link|TWIST|abc|0\n"
                                         "link|TWIST|0.5\n"
                                         "link|ZOOM|nan|0\n"
                                         "link|ZOOM|9|-9\n"
                                         "what|is|this\n"
                                         "knob|nope|0|Bad|\n"
                                         "link|BLOOM|1|0\n"
                                         "knob|7|0|Twice|\n"
                                         "link|BLOOM|1|0\n"
                                         "knob|8|inf|Inf|\n"
                                         "knob|9|-3||\n"
                                         "knob|10|0|Too|many|fields\n");
    check(mixed.knobs.size() == 2u, "rows that cannot be read are dropped, the rest kept");
    if (mixed.knobs.size() == 2u) {
        const Knob& keep = mixed.knobs[0];
        check(keep.id == 7 && keep.name == "Keep" && keep.value == 0.5f, "the good knob is read");
        check(keep.links.size() == 2u, "unknown, unlinkable and unreadable links are dropped, and a dropped knob's links with it");
        check(keep.links.size() == 2u && (keep.links[0] == Link{LfoTarget::Warp, 0.5f, -0.25f}) &&
                  (keep.links[1] == Link{LfoTarget::Zoom, 1.0f, -1.0f}),
              "weights are read and held to -1..1");
        check(mixed.knobs[1].id == 9 && mixed.knobs[1].value == -1.0f && mixed.knobs[1].name == "Knob 9", "a wild value is held, a blank name filled");
    }
    check(mixed.nextId == 10, "the next id is past every id read");
    std::string flood = header;
    for (int i = 1; i <= 500; ++i) flood += "knob|" + std::to_string(i) + "|0|K|\nlink|WARP|1|1\n";
    check(static_cast<int>(macrobank::decode(flood).knobs.size()) == macrobank::kMaxKnobs, "a flood of knobs stops at the cap");
    std::string links = header + "knob|1|0|K|\n";
    for (int i = 0; i < 5000; ++i) links += "link|WARP|0.5|0\n";
    check(macrobank::decode(links).knobs.front().links.size() == 1u, "a flood of links on one target is one link");
}

void theDefaultBankIsFourLiveKnobs() {
    const Bank& def = macrobank::defaultBank();
    check(def.knobs.size() == 4u, "four built-in knobs");
    check(macrobank::normalized(def) == def, "the default bank is already valid");
    std::set<std::string> names;
    std::set<Target> targets;
    size_t links = 0;
    for (const Knob& knob : def.knobs) {
        names.insert(knob.name);
        check(knob.value == 0.0f, knob.name + " starts at centre");
        check(!knob.hint.empty(), knob.name + " has a hint");
        check(!knob.links.empty(), knob.name + " has links");
        for (const char* old : {"Energy", "Speed", "Complexity", "Mood", "Space", "Chaos"}) check(knob.name != old, "no old knob name");
        for (const Link& link : knob.links) {
            targets.insert(link.target);
            ++links;
            check(link.up != 0.0f || link.down != 0.0f, knob.name + ": every link moves something");
        }
    }
    check(names.size() == 4u, "four different names");
    check(targets.size() == links, "no target is on two knobs: the four do not overlap");

    const EngineStyles s = engineStyles();
    const SceneParams base;
    for (const std::string& id : {s.flat, s.threeD, s.fluid, s.milkdrop}) {
        for (const Knob& knob : def.knobs) {
            Bank up = def;
            Bank down = def;
            for (Knob& k : up.knobs) k.value = k.id == knob.id ? 1.0f : 0.0f;
            for (Knob& k : down.knobs) k.value = k.id == knob.id ? -1.0f : 0.0f;
            check(!sameParams(macrobank::apply(base, up, id), base), knob.name + " turned up moves " + id);
            check(!sameParams(macrobank::apply(base, down, id), base), knob.name + " turned down moves " + id);
        }
        const std::string live = macrobank::liveText(def, id);
        std::string expected;
        for (const Knob& knob : def.knobs) expected += "knob|" + std::to_string(knob.id) + "|1|1\n";
        check(live == expected, "liveText says every built-in knob moves " + id + " both ways");
        // Safety has nothing left to clamp: no part of a built-in knob's travel is dead.
        for (float value : {1.0f, -1.0f}) {
            const SceneParams out = macrobank::apply(base, defaultsAt(value), id);
            check(sameParams(safety::apply(out), out), "the built-in knobs stay inside the safety clamp on " + id);
        }
    }
    // Every link of a built-in knob does something from the default look, each way it has a weight.
    const SceneParams up = macrobank::apply(base, defaultsAt(1.0f), kEveryTarget);
    const SceneParams down = macrobank::apply(base, defaultsAt(-1.0f), kEveryTarget);
    for (const Knob& knob : def.knobs) {
        for (const Link& link : knob.links) {
            const std::string name = knob.name + " > " + macrobank::name(link.target);
            if (link.up != 0.0f) check(fieldOf(up, link.target) != fieldOf(base, link.target), name + " moves when turned up");
            if (link.down != 0.0f) check(fieldOf(down, link.target) != fieldOf(base, link.target), name + " moves when turned down");
        }
    }
    // A knob with nothing in reach says so.
    const Bank fluidOnly = oneKnob({Link{LfoTarget::FluidCurl, 0.5f, 0.0f}}, 0.0f);
    check(macrobank::liveText(fluidOnly, s.milkdrop) == "knob|1|0|0\n", "a knob MilkDrop cannot show is not live there");
    check(macrobank::liveText(fluidOnly, s.fluid) == "knob|1|1|0\n", "and is live upward on Fluid");
}

// How hard a knob turned all the way one way (up = true) moves a style: the links in reach whose param is
// not one the safety clamp keeps small (Bloom, Brightness, Intensity stop 0.25 over neutral, so a whole range
// of them is a small change in the picture), counted when they move at least kStrongShare of the range.
constexpr float kStrongShare = 0.2f;
constexpr int kStrongLinks = 2;
constexpr float kStrongSum = 0.6f;

struct Strength {
    int strong = 0;
    float sum = 0.0f;
    bool enough() const { return strong >= kStrongLinks && sum >= kStrongSum; }
};

bool heldSmallBySafety(Target target) {
    const macrobank::Range r = macrobank::rangeOf(target);
    const ParamSpec& spec = SceneParams::schema()[static_cast<size_t>(target.slot)];
    return r.max < spec.max || r.min > spec.min;
}

Strength strengthOf(const Knob& knob, bool up, const std::string& styleId) {
    Strength s;
    for (const Link& link : knob.links) {
        if (!reachesStyle(link.target, styleId) || heldSmallBySafety(link.target)) continue;
        const float share = std::fabs(up ? link.up : link.down);
        s.sum += share;
        if (share >= kStrongShare) ++s.strong;
    }
    return s;
}

// The owner's report (2026-11-11): the four built-in knobs "dont change visuals much". Each built-in knob, each
// way, must move at least kStrongLinks params every engine reads (a flat shader style, a 3D one, a simulation
// and MilkDrop) by kStrongShare of their range or more, kStrongSum in all; the knobs that shipped first (Glow,
// Heat, Bend, Flow) did not, which is what this pins against.
void theBuiltInKnobsMoveThePictureHard() {
    const EngineStyles s = engineStyles();
    const Bank& def = macrobank::defaultBank();
    for (const Knob& knob : def.knobs) {
        for (const std::string& id : {s.flat, s.threeD, s.fluid, s.milkdrop}) {
            for (const bool up : {true, false}) {
                const Strength st = strengthOf(knob, up, id);
                // MilkDrop runs on projectM's own clock (Speed does not reach it), so a knob that slows the picture
                // down can only still the camera there: one param, hard, is what that engine has to give.
                bool slows = false;
                for (const Link& link : knob.links) slows = slows || (link.target == LfoTarget::Speed && (up ? link.up : link.down) < 0.0f);
                const bool clockless = slows && !modtargets::reaches(LfoTarget::Speed, id);
                check(clockless ? st.strong >= 1 : st.enough(), knob.name + (up ? " up" : " down") + " moves " + std::to_string(st.strong) + " params hard on " + id +
                                       " (sum " + std::to_string(st.sum) + ")");
            }
        }
    }
    // The rule would have caught the first four: each is weak one way on some engine.
    const std::vector<std::vector<Link>> first = {
        {{LfoTarget::Bloom, 1.0f, 0.0f}, {LfoTarget::Intensity, 0.2f, -0.5f}, {LfoTarget::Vignette, 0.0f, 0.5f}},
        {{LfoTarget::Temperature, 0.3f, -0.3f}, {LfoTarget::Saturation, 0.2f, -0.35f}, {LfoTarget::ColorShift, 0.06f, -0.06f}},
        {{LfoTarget::Warp, 0.6f, 0.0f}, {LfoTarget::Twist, 0.2f, -0.2f}, {LfoTarget::Morph, 0.5f, 0.0f}, {LfoTarget::Fisheye, 0.15f, -0.25f}},
        {{LfoTarget::Speed, 0.25f, -0.15f},
         {LfoTarget::Rotation, 0.03f, -0.03f},
         {LfoTarget::Sway, 0.4f, 0.0f},
         {LfoTarget::Turbulence, 0.4f, 0.0f},
         {LfoTarget::CamFly, 0.2f, -0.1f},
         {LfoTarget::FluidForce, 0.3f, -0.2f}},
    };
    for (const auto& links : first) {
        const Knob old = oneKnob(links, 0.0f).knobs[0];
        bool weak = false;
        for (const std::string& id : {s.flat, s.threeD, s.fluid, s.milkdrop}) {
            weak = weak || !strengthOf(old, true, id).enough() || !strengthOf(old, false, id).enough();
        }
        check(weak, "the rule catches a first-shipped knob: " + macrobank::name(links[0].target));
    }
    // And the motion the knobs add is still the safety clamp's: reduced motion scales it like any slider's.
    const SceneParams base;
    const SceneParams full = macrobank::apply(base, defaultsAt(1.0f), kEveryTarget);
    const SceneParams calm = safety::apply(full, true);
    check(calm.speed < full.speed && calm.sway < full.sway && calm.camFly < full.camFly, "reduced motion still scales what the knobs add");
}

// A bank saved before the redo: a built-in knob saved as it first shipped comes back as today's knob with its
// id, centred; an edited one and the user's own knobs come back as saved.
void theFirstBuiltInKnobsLoadAsTheNewOnes() {
    const std::string saved =
        "macrobank|1|6\n"
        "knob|1|0.5|Glow|Down dims the picture into a dark frame, up makes it bright and blooming\n"
        "link|BLOOM|1|0\nlink|INTENSITY|0.2|-0.5\nlink|VIGNETTE|0|0.5\n"
        "knob|2|-0.25|Heat|Down turns the colours cool and pale, up warm and vivid\n"
        "link|TEMPERATURE|0.3|-0.3\nlink|SATURATION|0.2|-0.35\nlink|COLOR_SHIFT|0.06|-0.06\n"
        "knob|3|0.4|My bend|\n"
        "link|WARP|0.6|0\nlink|TWIST|0.2|-0.2\nlink|MORPH|0.5|0\nlink|FISHEYE|0.15|-0.25\n"
        "knob|4|0|Flow|Down slows the picture and turns it back, up speeds it up and sends it spinning\n"
        "link|SPEED|0.25|-0.15\nlink|ROTATION|0.03|-0.03\nlink|SWAY|0.4|0\nlink|TURBULENCE|0.4|0\nlink|CAM_FLY|0.2|-0.1|EASE_IN\n"
        "link|FLUID_FORCE|0.3|-0.2\n"
        "knob|5|0.75|Mine|\nlink|ZOOM|0.3|-0.2\n";
    const Bank read = macrobank::decode(saved);
    const Bank& def = macrobank::defaultBank();
    check(read.knobs.size() == 5u, "every saved knob loads");
    check(read.knobs[0] == def.knobs[0] && read.knobs[1] == def.knobs[1], "an unedited first knob is today's built-in knob, centred");
    check(read.knobs[2].name == "My bend" && read.knobs[2].value == 0.4f && read.knobs[2].links.size() == 4u &&
              read.knobs[2].links[0] == Link{LfoTarget::Warp, 0.6f, 0.0f},
          "a renamed one stays as the user left it");
    check(read.knobs[3].name == "Flow" && read.knobs[3].links[4].curve == macrobank::Curve::EaseIn, "so does one with a changed curve");
    check(read.knobs[4].name == "Mine" && read.knobs[4].value == 0.75f && read.knobs[4].links.size() == 1u, "the user's own knob loads as saved");
    check(macrobank::decode(macrobank::encode(read)) == read, "and the loaded bank round trips");
    check(macrobank::decode(macrobank::encode(def)) == def, "today's built-in knobs load as themselves");
}

void applyIsDeterministic() {
    SceneParams base;
    base.warp = 0.3f;
    base.speed = 2.0f;
    const Bank bank = defaultsAt(0.6f);
    const EngineStyles s = engineStyles();
    const SceneParams once = macrobank::apply(base, bank, s.flat);
    for (int i = 0; i < 5; ++i) check(sameParams(macrobank::apply(base, bank, s.flat), once), "the same bank and look give the same params");
    // Knob order does not change the result: offsets are summed per target before they land.
    Bank reversed = bank;
    std::reverse(reversed.knobs.begin(), reversed.knobs.end());
    check(sameParams(macrobank::apply(base, reversed, s.flat), once), "knob order does not matter");
}

void targetsTextListsWhatALinkMayName() {
    const std::string& text = macrobank::targetsText();
    check(text.rfind("macrotargets|" + std::to_string(macrobank::kTextVersion) + "\n", 0) == 0, "targets text version row");
    const std::string caps = "caps|" + std::to_string(macrobank::kMaxKnobs) + "|" + std::to_string(macrobank::kMaxLinks) + "|" +
                             std::to_string(macrobank::kMaxNameBytes) + "\n";
    check(text.find(caps) != std::string::npos, "targets text caps row");
    size_t rows = 0;
    for (size_t pos = text.find("\ntarget|"); pos != std::string::npos; pos = text.find("\ntarget|", pos + 1)) ++rows;
    check(rows == linkableTargets().size(), "one target row per linkable target");
    check(text.find("\ntarget|BLOOM|bloom|0|0.6|0|UNIVERSAL\n") != std::string::npos, "the Bloom row carries its safe range");
    check(text.find("|PULSE|") == std::string::npos && text.find("RETIRED") == std::string::npos && text.find("LFO1_RATE") == std::string::npos, "no unlinkable target is listed");
}

void theAbiEditsTextToText() {
    const std::string def = geode_macro_bank_default();
    check(def == macrobank::encode(macrobank::defaultBank()), "geode_macro_bank_default is the default bank's text");
    check(std::string(geode_macro_bank_normalize(nullptr)) == def && std::string(geode_macro_bank_normalize("junk")) == def,
          "NULL and junk normalize to the default bank");
    check(std::string(geode_macro_bank_targets_text()) == macrobank::targetsText(), "geode_macro_bank_targets_text");
    const int first = macrobank::defaultBank().knobs.front().id;

    std::string text = geode_macro_bank_set_value(def.c_str(), first, 0.75f);
    check(macrobank::decode(text).knobs.front().value == 0.75f, "geode_macro_bank_set_value");
    text = geode_macro_bank_add_knob(text.c_str(), "Mine");
    const int mine = macrobank::decode(text).knobs.back().id;
    check(macrobank::decode(text).knobs.size() == 5u, "geode_macro_bank_add_knob");
    text = geode_macro_bank_rename(text.c_str(), mine, "Yours");
    check(macrobank::decode(text).knobs.back().name == "Yours", "geode_macro_bank_rename");
    text = geode_macro_bank_set_link(text.c_str(), mine, "CAM_FOV", 0.5f, -0.5f);
    check(macrobank::decode(text).knobs.back().links.size() == 1u, "geode_macro_bank_set_link");
    const std::string held = text;
    check(std::string(geode_macro_bank_set_link(text.c_str(), mine, "NOT_A_TARGET", 0.5f, 0.5f)) == held, "an unknown target changes nothing");
    check(std::string(geode_macro_bank_set_link(text.c_str(), mine, nullptr, 0.5f, 0.5f)) == held, "a NULL target changes nothing");
    check(std::string(geode_macro_bank_rename(text.c_str(), mine, nullptr)) == held, "a NULL name changes nothing");
    const EngineStyles s = engineStyles();
    const std::string live = geode_macro_bank_live(text.c_str(), s.flat.c_str());
    check(live.find("knob|" + std::to_string(mine) + "|1|1\n") != std::string::npos, "geode_macro_bank_live");
    check(std::string(geode_macro_bank_live(nullptr, nullptr)).rfind("knob|", 0) == 0, "live on NULLs is the default bank on an unknown style");
    text = geode_macro_bank_remove_link(text.c_str(), mine, "CAM_FOV");
    check(macrobank::decode(text).knobs.back().links.empty(), "geode_macro_bank_remove_link");
    text = geode_macro_bank_duplicate_knob(text.c_str(), mine);
    check(macrobank::decode(text).knobs.size() == 6u, "geode_macro_bank_duplicate_knob");
    text = geode_macro_bank_move_knob(text.c_str(), mine, -4);
    check(macrobank::decode(text).knobs.front().id == mine, "geode_macro_bank_move_knob");
    text = geode_macro_bank_remove_knob(text.c_str(), mine);
    text = geode_macro_bank_remove_knob(text.c_str(), macrobank::decode(text).knobs.back().id);
    check(macrobank::decode(text).knobs.size() == 4u, "geode_macro_bank_remove_knob");
    text = geode_macro_bank_reset_knob(geode_macro_bank_rename(text.c_str(), first, "Renamed"), first);
    check(macrobank::decode(text).knobs.front().name == macrobank::defaultBank().knobs.front().name, "geode_macro_bank_reset_knob");
    const std::string firstTarget = macrobank::name(macrobank::defaultBank().knobs.front().links.front().target);
    text = geode_macro_bank_set_curve(def.c_str(), first, firstTarget.c_str(), "S_CURVE");
    check(macrobank::decode(text).knobs.front().links.front().curve == macrobank::Curve::SCurve, "geode_macro_bank_set_curve");
    check(std::string(geode_macro_bank_set_curve(text.c_str(), first, firstTarget.c_str(), "WOBBLE")) == text &&
              std::string(geode_macro_bank_set_curve(text.c_str(), first, firstTarget.c_str(), nullptr)) == text,
          "an unknown or NULL curve changes nothing");
    text = geode_macro_bank_add_knob_for(def.c_str(), "Zoom");
    check(macrobank::decode(text).knobs.back().name == "Zoom", "geode_macro_bank_add_knob_for");
    check(std::string(geode_macro_bank_add_knob_for(def.c_str(), nullptr)) == def, "a NULL control makes no knob");
    check(std::string(geode_macro_bank_controls(text.c_str())) == macrobank::controlsText(macrobank::decode(text)), "geode_macro_bank_controls");
    // An edit may be fed its own result: the argument is read before the held string is replaced.
    const char* chained = geode_macro_bank_set_value(geode_macro_bank_set_value(def.c_str(), first, 0.25f), first, -0.25f);
    check(macrobank::decode(chained).knobs.front().value == -0.25f, "a result can be passed straight back in");
}

// The process-wide bank every renderer reads: the default until set, then the normalized bank the shell
// pushed. A set swaps the shared copy, so one a frame already holds keeps its knobs.
void theGlobalBankIsSwappedWhole() {
    check(*macrobank::global() == macrobank::defaultBank(), "the process bank starts as the default bank");
    const Bank turned = macrobank::setValue(macrobank::defaultBank(), macrobank::defaultBank().knobs[0].id, 0.75f);
    const auto held = macrobank::global();
    geode_macro_bank_set(macrobank::encode(turned).c_str());
    check(*macrobank::global() == turned, "geode_macro_bank_set makes the pushed bank the process bank");
    check(*held == macrobank::defaultBank(), "a copy taken before the set keeps its knobs");
    geode_macro_bank_set("not a bank");
    check(*macrobank::global() == macrobank::defaultBank(), "unreadable text sets the default bank");
    Bank wild = turned;
    wild.knobs[0].value = 9.0f;
    macrobank::setGlobal(wild);
    check(macrobank::global()->knobs[0].value == 1.0f, "the process bank is held normalized");
    macrobank::setGlobal(macrobank::defaultBank());
}

// The owner's ask (2026-11-12): every parameter is an option. Every float the look glides that a Customize control
// sets can be linked; what cannot is left out on purpose (see macrobank::Target).
void everyGlidingParamIsAnOption() {
    std::set<int> slots;
    for (const Target t : linkableTargets()) check(slots.insert(t.slot).second, "each param is listed once: " + macrobank::name(t));
    for (int i = 0; i < modtargets::kTargetCount; ++i) {
        const auto t = static_cast<LfoTarget>(i);
        if (!modtargets::offered(t) || modtargets::chainSlot(t) >= 0 || paramSlotOf(t) < 0) continue;
        check(macrobank::linkable(t), std::string("every modulation target's param is an option: ") + modtargets::name(t));
        check(macrobank::name(t) == modtargets::name(t), std::string("and keeps its target's name: ") + modtargets::name(t));
    }
    check(linkableTargets().size() > static_cast<size_t>(modtargets::kTargetCount), "more params than the modulation targets");
    for (int slot = 0; slot < SceneParams::kFieldCount; ++slot) {
        const auto at = static_cast<ParamSlot>(slot);
        const ParamSpec& spec = SceneParams::spec(at);
        const ParamMeta& meta = SceneParams::meta(at);
        const bool glides = (spec.kind == ParamKind::Float || spec.kind == ParamKind::Circular) && spec.fade == ParamFade::Fade;
        const bool labelled = meta.label != nullptr && meta.label[0] != '\0' && meta.label[0] != '@';
        const bool want = meta.live && glides && labelled && macrobank::rangeOf(Target::ofSlot(slot)).max > macrobank::rangeOf(Target::ofSlot(slot)).min;
        check(macrobank::linkable(Target::ofSlot(slot)) == want, std::string("linkable is the rule: ") + spec.name);
    }
    for (const char* in : {"cymaticsRing", "fluidPressure", "grain", "contrast", "diveFog", "reactAmount"}) {
        const Target t = macrobank::targetNamed(in);
        check(macrobank::linkable(t) && macrobank::name(t) == in, std::string("a param no target writes is linked by its schema name: ") + in);
    }
    for (const char* out : {"symmetry", "palette", "trails", "paletteBaseOverride", "paramFadeSec", "noteColor", "noteDrums", "beatResponse",
                            "reactSeed", "fluidIterations", "pulse",
                            // retired with Water (Pool replaced it): kept slots, no longer params
                            "waterSpecular", "waterRippleStrength", "rippleOverlayStrength", "WATER_RIPPLE", "RETIRED_38"}) {
        check(!macrobank::linkable(macrobank::targetNamed(out)), std::string("left out: ") + out);
    }
    check(macrobank::targetNamed("warp") == LfoTarget::Warp && macrobank::targetNamed("WARP") == LfoTarget::Warp, "a target's param answers to both names");
    Bank b = macrobank::addKnob(macrobank::defaultBank(), "Mine");
    const int mine = b.knobs.back().id;
    b = macrobank::addLink(b, mine, macrobank::targetNamed("cymaticsRing"));
    const std::string text = macrobank::encode(b);
    check(text.find("\nlink|cymaticsRing|0.25|-0.25\n") != std::string::npos, "a new param saves by its schema name");
    check(macrobank::decode(text) == b, "and loads back");
    check(macrobank::targetOfControl("Plate ring") == macrobank::targetNamed("cymaticsRing"), "its slider can make a knob");
    SceneParams base;
    const SceneParams up = macrobank::apply(base, macrobank::setValue(b, mine, 1.0f), kEveryTarget);
    check(up.cymaticsRing > base.cymaticsRing, "and the knob moves it");
}

// The knob editor's second Random: the links stay, their ranges and curves are rolled again.
void newRangesKeepTheParams() {
    const Bank& def = macrobank::defaultBank();
    const int id = def.knobs[0].id;
    const Bank r = macrobank::rollRanges(def, id, 5);
    check(r == macrobank::rollRanges(def, id, 5), "the same seed rolls the same ranges");
    check(r.knobs[0].links.size() == def.knobs[0].links.size(), "no link is added or dropped");
    int differ = 0;
    for (size_t i = 0; i < r.knobs[0].links.size(); ++i) {
        const Link& l = r.knobs[0].links[i];
        check(l.target == def.knobs[0].links[i].target, "each link keeps its param, in order");
        check(std::fabs(l.up) >= 0.15f - 1e-6f && std::fabs(l.up) <= 0.6f + 1e-6f, "a new range up is 15-60 %");
        check(l.down == 0.0f || (l.down * l.up < 0.0f && std::fabs(l.down) <= std::fabs(l.up) + 1e-6f), "and down none or back");
        differ += l == def.knobs[0].links[i] ? 0 : 1;
    }
    check(differ > 0 && r.knobs[0].hint.empty(), "the ranges change and the built-in hint goes");
    check(r.knobs[1] == def.knobs[1], "the other knobs are untouched");
    const Bank empty = macrobank::addKnob(def, "Empty");
    check(macrobank::rollRanges(empty, empty.knobs.back().id, 5) == empty, "a knob with no links is unchanged");
    check(macrobank::rollRanges(def, 4242, 5) == def, "and so is an unknown knob");
    check(std::string(geode_macro_bank_roll_ranges(macrobank::encode(def).c_str(), id, 5)) == macrobank::encode(r), "geode_macro_bank_roll_ranges");
}

// Add random: a count, then that many more params, picked at random, never one the knob moves, never out of the
// style's reach, never a kept control, each at a random range; the links the knob had stay.
void addRandomLinksMoreParams() {
    const EngineStyles s = engineStyles();
    const macrobank::Reach reach = macrobank::reachMask(s.flat);
    const std::set<std::string> none;
    const Bank& def = macrobank::defaultBank();
    const int id = def.knobs[0].id;
    const Bank r = macrobank::addRandom(def, id, 3, reach, none, 9);
    check(r == macrobank::addRandom(def, id, 3, reach, none, 9), "the same seed adds the same params");
    const auto& was = def.knobs[0].links;
    const auto& now = r.knobs[0].links;
    check(now.size() == was.size() + 3, "it adds the count asked for");
    check(std::equal(was.begin(), was.end(), now.begin()), "and keeps the links the knob had");
    std::set<Target> seen;
    for (const Link& l : now) check(seen.insert(l.target).second, "no param twice");
    for (size_t i = was.size(); i < now.size(); ++i) {
        check(reachesStyle(now[i].target, s.flat), "an added param is on the style: " + macrobank::name(now[i].target));
        check(std::fabs(now[i].up) >= 0.15f - 1e-6f && std::fabs(now[i].up) <= 0.6f + 1e-6f, "at a random range");
    }
    int differ = 0;
    for (std::uint64_t seed = 0; seed < 50; ++seed) differ += macrobank::addRandom(def, id, 3, reach, none, seed) != r ? 1 : 0;
    check(differ > 40, "different seeds pick different params");
    // Kept controls are never picked.
    std::set<std::string> kept;
    for (const Target t : linkableTargets()) {
        if (reachesStyle(t, s.flat) && kept.size() < 20) kept.insert(macrobank::controlOf(t));
    }
    for (std::uint64_t seed = 0; seed < 50; ++seed) {
        const Bank k = macrobank::addRandom(def, id, 5, reach, kept, seed);
        for (size_t i = was.size(); i < k.knobs[0].links.size(); ++i) {
            check(kept.count(macrobank::controlOf(k.knobs[0].links[i].target)) == 0, "a kept control is never added");
        }
    }
    // The cap, the room and refusals.
    const Bank full = macrobank::addRandom(def, id, 99, reach, none, 1);
    check(static_cast<int>(full.knobs[0].links.size()) == macrobank::kMaxLinks, "it stops at kMaxLinks");
    check(macrobank::addRandomRoom(full, id, reach) == 0 && macrobank::addRandom(full, id, 2, reach, none, 1) == full, "a full knob takes none");
    check(macrobank::addRandomRoom(def, id, reach) == macrobank::kMaxLinks - static_cast<int>(was.size()), "the room is what fits");
    check(macrobank::addRandomRoom(def, id, macrobank::Reach{}) == 0 && macrobank::addRandom(def, id, 3, macrobank::Reach{}, none, 1) == def,
          "nothing in reach, nothing to add");
    check(macrobank::addRandom(def, id, 0, reach, none, 1) == def && macrobank::addRandom(def, id, -2, reach, none, 1) == def, "a count of none adds none");
    check(macrobank::addRandom(def, 4242, 3, reach, none, 1) == def, "an unknown knob is refused");
    // The ABI reads Customize's process-wide keep set.
    const std::string keepAll = [&] {
        std::string lines;
        for (const Target t : linkableTargets()) lines += std::string(macrobank::controlOf(t)) + "\n";
        return lines;
    }();
    customize::setKept(keepAll);
    const std::string encoded = macrobank::encode(def);
    check(std::string(geode_macro_bank_add_random(encoded.c_str(), id, s.flat.c_str(), 3, 9)) == encoded, "every control kept: nothing added");
    customize::setKept("");
    check(std::string(geode_macro_bank_add_random(encoded.c_str(), id, s.flat.c_str(), 3, 9)) == macrobank::encode(r), "geode_macro_bank_add_random");
}

// A wave or an envelope on a knob (LfoTarget::Macro): base plus modulation. The knob moves its links by its turn
// plus what they add, held to -1..1, exactly as if turned there; the user's turn stays; a knob in the library takes
// none; the live text says who turns which knob.
void modulationTurnsAKnobWithoutRewritingIt() {
    using macrobank::KnobMods;
    const Bank& def = macrobank::defaultBank();
    const int colour = def.knobs[2].id;
    const Bank bank = macrobank::setValue(def, colour, 0.25f);
    KnobMods mods;
    mods.add(colour, 0.5f, KnobMods::kWave, -0.5f, 0.5f);
    mods.add(colour, 0.125f, KnobMods::kEnvelope, 0.0f, 0.75f);
    mods.add(4242, 1.0f, KnobMods::kWave);
    check(mods.count == 2 && mods.of(colour) != nullptr && mods.of(colour)->add == 0.625f, "a knob's modulation sums its sources");
    check(mods.of(colour)->sources == (KnobMods::kWave | KnobMods::kEnvelope), "and says which they are");
    check(mods.of(1) == nullptr, "a knob nothing turns has none");
    check(macrobank::effective(bank.knobs[2], &mods) == 0.875f && macrobank::effective(bank.knobs[2], nullptr) == 0.25f,
          "the effective turn is the turn plus the modulation");
    KnobMods big;
    big.add(colour, 5.0f, KnobMods::kWave);
    check(macrobank::effective(bank.knobs[2], &big) == 1.0f, "held to a full turn");
    KnobMods bad;
    bad.add(colour, std::numeric_limits<float>::quiet_NaN(), KnobMods::kWave);
    check(macrobank::effective(bank.knobs[2], &bad) == 0.25f, "a non-finite modulation adds nothing");
    const SceneParams base;
    const SceneParams modded = macrobank::apply(base, bank, kEveryTarget, &mods);
    check(sameParams(modded, macrobank::apply(base, macrobank::setValue(def, colour, 0.875f), kEveryTarget)), "it moves the links as that turn would");
    check(bank.knobs[2].value == 0.25f, "and the user's turn stays");
    check(sameParams(macrobank::apply(base, def, kEveryTarget, nullptr), base), "no modulation at centre is the identity");
    KnobMods onCentre;
    onCentre.add(colour, -0.4f, KnobMods::kWave);
    check(!sameParams(macrobank::apply(base, def, kEveryTarget, &onCentre), base), "a wave turns a centred knob");
    const Bank hidden = macrobank::hideKnob(def, colour);
    check(sameParams(macrobank::apply(base, hidden, kEveryTarget, &onCentre), base), "a knob in the library takes no modulation");
    KnobMods many;
    for (int id = 1; id <= macrobank::kMaxKnobs + 4; ++id) many.add(id, 0.1f, KnobMods::kWave);
    check(many.count == macrobank::kMaxKnobs, "modulation holds at most a bank's knobs");
    macrobank::publishLive(mods);
    check(macrobank::liveModsText() == "mod|" + std::to_string(colour) + "|0.625|3|-0.5|1.25\nmod|4242|1|1|0|0\n", "the live text lists who turns which knob");
    check(std::string(geode_macro_bank_live_mods()) == macrobank::liveModsText(), "geode_macro_bank_live_mods");
    macrobank::publishLive(KnobMods{});
    check(macrobank::liveModsText().empty(), "and nothing when nothing does");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "--write") == 0) {
        std::ofstream(argv[2], std::ios::binary) << editorFixture();
        return 0;
    }
    linkableTargetsWriteLiveParams();
    centreIsTheIdentity();
    aFullTurnMovesEveryLinkByItsWeight();
    neverPastTheRange();
    theHueTurnsUnclamped();
    theOffsetRidesOnTheBase();
    aLinkOutOfReachIsSkipped();
    nonFiniteInputIsIgnored();
    editsValidate();
    theEditorCopiesMovesAndResets();
    theEditorTextSaysWhatToOffer();
    curvesShapeTheTurn();
    curvesAreEditedAndKept();
    curvesSurviveTheTextAndOldTextLoads();
    theEditorTextShowsRangesInUnits();
    aSliderMakesOrJoinsAKnob();
    aRandomRollGivesAKnobLinks();
    theLibraryHoldsWhatTheScreenDoesNot();
    theDiceRollsTheTurns();
    theSaveNameIsNatives();
    theEditorFixtureIsCurrent(argc > 1 ? argv[1] : "core/tests/macro_editor.txt");
    textRoundTrips();
    garbageDecodesToSomethingValid();
    theDefaultBankIsFourLiveKnobs();
    theBuiltInKnobsMoveThePictureHard();
    theFirstBuiltInKnobsLoadAsTheNewOnes();
    applyIsDeterministic();
    targetsTextListsWhatALinkMayName();
    theAbiEditsTextToText();
    theGlobalBankIsSwappedWhole();
    everyGlidingParamIsAnOption();
    newRangesKeepTheParams();
    addRandomLinksMoreParams();
    modulationTurnsAKnobWithoutRewritingIt();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("macro_bank_test: OK\n");
    return 0;
}
