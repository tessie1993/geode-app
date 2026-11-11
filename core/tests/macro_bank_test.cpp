// Host-side checks for the macro bank (core/viz/MacroBank): the user's knobs as weighted offsets on
// scene params. Centre is the identity, a full turn each way moves every linked field by its weight
// and never past its range, a link the style cannot show is skipped, the offset rides on a moved
// base, every edit validates, the text survives a round trip and garbage, and the four built-in
// knobs move a flat style, a 3D style, Fluid and MilkDrop both ways within the safety clamp. The
// process-wide bank (geode_macro_bank_set) starts as the default, is replaced whole, never in place.
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
#include "viz/Lfo.hpp"
#include "viz/MacroBank.hpp"
#include "viz/ModTargets.hpp"
#include "viz/Params.hpp"
#include "viz/VisualSafety.hpp"

using namespace geode::viz;
using macrobank::Bank;
using macrobank::Knob;
using macrobank::Link;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

bool about(float a, float b) { return std::fabs(a - b) <= 1e-5f; }

constexpr std::uint64_t kEveryTarget = ~std::uint64_t{0};
const float kNan = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();

std::vector<LfoTarget> linkableTargets() {
    std::vector<LfoTarget> out;
    for (int i = 0; i < modtargets::kTargetCount; ++i) {
        if (macrobank::linkable(static_cast<LfoTarget>(i))) out.push_back(static_cast<LfoTarget>(i));
    }
    return out;
}

float fieldOf(const SceneParams& p, LfoTarget target) { return p.getSlot(paramSlotOf(target)); }

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
    for (LfoTarget t : targets) {
        const std::string name = modtargets::name(t);
        const macrobank::Range range = macrobank::rangeOf(t);
        const ParamSpec& spec = SceneParams::schema()[static_cast<size_t>(paramSlotOf(t))];
        check(range.max > range.min, name + " has a range");
        check(range.min >= spec.min && range.max <= spec.max, name + " stays inside its schema range");
        check(macrobank::targetNamed(name) == t, name + " is found by name");
    }
    for (LfoTarget t : {LfoTarget::None, LfoTarget::Pulse, LfoTarget::Retired25, LfoTarget::Lfo1Rate, LfoTarget::Lfo3Depth}) {
        check(!macrobank::linkable(t), std::string("not linkable: ") + modtargets::name(t));
        check(macrobank::rangeOf(t).max == 0.0f, "an unlinkable target has no range");
    }
    check(!macrobank::linkable(static_cast<LfoTarget>(-1)) && !macrobank::linkable(static_cast<LfoTarget>(999)),
          "outside the enum is not linkable");
    check(macrobank::targetNamed("PULSE") == LfoTarget::None && macrobank::targetNamed("nope") == LfoTarget::None &&
              macrobank::targetNamed("") == LfoTarget::None,
          "an unlinkable or unknown name is None");
    // The top of a range is what safety::apply lets through, so no travel is clamped away later.
    check(about(macrobank::rangeOf(LfoTarget::Bloom).max, safety::kMaxFlashDepth), "Bloom stops at the safety depth");
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
    for (LfoTarget t : linkableTargets()) all.push_back(Link{t, 1.0f, -1.0f});
    check(sameParams(macrobank::apply(moved, oneKnob(all, 0.0f), kEveryTarget), moved), "a knob at centre changes nothing");
    check(sameParams(macrobank::apply(moved, Bank{}, kEveryTarget), moved), "an empty bank changes nothing");
    check(sameParams(macrobank::apply(moved, oneKnob({Link{LfoTarget::Warp, 0.0f, 0.0f}}, 1.0f), kEveryTarget), moved),
          "a link with no weight changes nothing");
}

void aFullTurnMovesEveryLinkByItsWeight() {
    const SceneParams base;
    for (LfoTarget t : linkableTargets()) {
        const std::string name = modtargets::name(t);
        const macrobank::Range range = macrobank::rangeOf(t);
        const float span = range.max - range.min;
        const bool circular = SceneParams::schema()[static_cast<size_t>(paramSlotOf(t))].kind == ParamKind::Circular;
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
        undone.setSlot(paramSlotOf(t), was);
        check(sameParams(undone, base), name + " moves its own field only");
        // The down weight alone decides the way a turn down goes: here it rises.
        const SceneParams rising = macrobank::apply(base, oneKnob({Link{t, 0.0f, 0.25f}}, -1.0f), kEveryTarget);
        check(about(fieldOf(rising, t), expected(0.25f)), name + " turned down with a positive down weight");
    }
}

void neverPastTheRange() {
    for (LfoTarget t : linkableTargets()) {
        if (SceneParams::schema()[static_cast<size_t>(paramSlotOf(t))].kind == ParamKind::Circular) continue;
        const std::string name = modtargets::name(t);
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
        const std::uint64_t mask = macrobank::reachMask(id);
        for (int i = 0; i < modtargets::kTargetCount; ++i) {
            const auto t = static_cast<LfoTarget>(i);
            const bool bit = ((mask >> i) & std::uint64_t{1}) != 0;
            check(bit == (macrobank::linkable(t) && modtargets::reaches(t, id)), "reach bit of " + std::string(modtargets::name(t)) + " on " + id);
        }
    }
    check(sameParams(macrobank::apply(base, bank, std::uint64_t{0}), base), "nothing in reach, nothing moved");
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
    check(static_cast<int>(full.knobs.size()) == macrobank::kMaxKnobs, "the bank holds at most kMaxKnobs");
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
    check(macrobank::setLink(b, mine, LfoTarget::Pulse, 0.5f, 0.5f) == before, "an inert target is refused");
    check(macrobank::setLink(b, mine, LfoTarget::Lfo1Rate, 0.5f, 0.5f) == before, "a chained target is refused");
    check(macrobank::setLink(b, mine, LfoTarget::None, 0.5f, 0.5f) == before, "None is refused");
    check(macrobank::setLink(b, 4242, LfoTarget::Warp, 0.5f, 0.5f) == before, "a link on an unknown knob is refused");
    check(macrobank::removeLink(b, mine, LfoTarget::Warp) == before, "removing a link that is not there changes nothing");
    check(macrobank::removeLink(b, mine, LfoTarget::Zoom).knobs.back().links.empty(), "remove a link");

    Bank many = macrobank::addKnob(none, "Many");
    const int manyId = many.knobs.front().id;
    for (LfoTarget t : linkableTargets()) many = macrobank::setLink(many, manyId, t, 0.1f, 0.0f);
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
    a.links = {Link{LfoTarget::Warp, 0.1f, 0.0f}, Link{LfoTarget::Pulse, 1.0f, 1.0f}, Link{LfoTarget::Warp, 0.3f, 0.0f}};
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
    check(b.knobs[1].name == "Glow 2" && b.knobs[1].hint.empty(), "a copy is named after its knob and has no hint");
    check(b.knobs[1].links == def.knobs[0].links && b.knobs[1].value == 0.5f, "a copy moves what its knob moves");
    check(!macrobank::isBase(b.knobs[1].id), "a copy is the user's knob");
    check(macrobank::duplicateKnob(def, 4242) == def, "copying an unknown knob changes nothing");
    const std::string longName(static_cast<size_t>(macrobank::kMaxNameBytes), 'x');
    const Bank named = macrobank::duplicateKnob(macrobank::rename(def, glow, longName), glow);
    check(static_cast<int>(named.knobs[1].name.size()) == macrobank::kMaxNameBytes, "a copy's name stays within the cap");
    check(named.knobs[1].name.ends_with(" 2"), "and still says it is a copy");
    Bank full = def;
    while (static_cast<int>(full.knobs.size()) < macrobank::kMaxKnobs) full = macrobank::addKnob(full, "");
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
    check(caps.size() == 1u && caps[0][1] == std::to_string(macrobank::kMaxKnobs) && caps[0][2] == std::to_string(macrobank::kMaxLinks) &&
              caps[0][3] == std::to_string(macrobank::kMaxNameBytes),
          "it carries the caps");
    check(rowsOf(text, "bank") == std::vector<std::vector<std::string>>({{"bank", "1", "1"}}), "the default bank has room and is the default");
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
        const LfoTarget target = macrobank::targetNamed(t[1]);
        check(target != LfoTarget::None && t.size() == 3u, "a target row names a linkable target: " + t[1]);
        check((t[2] == "1") == modtargets::reaches(target, s.flat), "and whether it reaches the style: " + t[1]);
    }
    // FluidForce is on Flow and only reaches the fluid engine.
    bool dimmed = false;
    for (const auto& l : rowsOf(text, "link")) dimmed = dimmed || (l[1] == "FLUID_FORCE" && l[4] == "0");
    check(dimmed, "a link the style cannot show says so");

    Bank b = macrobank::rename(def, def.knobs[0].id, "Mine");
    b = macrobank::setValue(b, def.knobs[1].id, 0.5f);
    b = macrobank::addKnob(b, "New");
    const int mine = b.knobs.back().id;
    for (const LfoTarget t : linkableTargets()) b = macrobank::setLink(b, mine, t, 0.1f, 0.1f);
    const auto edited = rowsOf(macrobank::editorText(b, s.flat), "knob");
    check(edited[0][2] == "1" && edited[0][3] == "0", "an edited built-in knob can be reset");
    check(edited[1][3] == "1" && edited[1][5] == "0.5", "a turned one is still as built");
    check(edited.back()[2] == "0" && edited.back()[3] == "0" && edited.back()[4] == "0", "a user's knob is not built in, and full of links");
    check(rowsOf(macrobank::editorText(b, s.flat), "bank")[0][2] == "0", "an edited bank is not the default");
    check(rowsOf(macrobank::editorText(macrobank::moveKnob(def, 1, 1), s.flat), "bank")[0][2] == "0", "nor is a reordered one");
    check(rowsOf(macrobank::editorText(macrobank::setValue(def, 1, 1.0f), s.flat), "bank")[0][2] == "1", "a turned one is");
    Bank full = def;
    while (static_cast<int>(full.knobs.size()) < macrobank::kMaxKnobs) full = macrobank::addKnob(full, "");
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
    return macrobank::editorText(b, engineStyles().flat);
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
    std::set<LfoTarget> targets;
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
            const std::string name = knob.name + " > " + modtargets::name(link.target);
            if (link.up != 0.0f) check(fieldOf(up, link.target) != fieldOf(base, link.target), name + " moves when turned up");
            if (link.down != 0.0f) check(fieldOf(down, link.target) != fieldOf(base, link.target), name + " moves when turned down");
        }
    }
    // A knob with nothing in reach says so.
    const Bank fluidOnly = oneKnob({Link{LfoTarget::FluidCurl, 0.5f, 0.0f}}, 0.0f);
    check(macrobank::liveText(fluidOnly, s.milkdrop) == "knob|1|0|0\n", "a knob MilkDrop cannot show is not live there");
    check(macrobank::liveText(fluidOnly, s.fluid) == "knob|1|1|0\n", "and is live upward on Fluid");
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
    check(text.find("\ntarget|BLOOM|bloom|0|0.25|0|UNIVERSAL\n") != std::string::npos, "the Bloom row carries its safe range");
    check(text.find("|PULSE|") == std::string::npos && text.find("LFO1_RATE") == std::string::npos, "no unlinkable target is listed");
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
    theEditorFixtureIsCurrent(argc > 1 ? argv[1] : "core/tests/macro_editor.txt");
    textRoundTrips();
    garbageDecodesToSomethingValid();
    theDefaultBankIsFourLiveKnobs();
    applyIsDeterministic();
    targetsTextListsWhatALinkMayName();
    theAbiEditsTextToText();
    theGlobalBankIsSwappedWhole();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("macro_bank_test: OK\n");
    return 0;
}
