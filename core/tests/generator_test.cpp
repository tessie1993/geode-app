// Host tests for core/scene/generator (#471, #472): which styles can be generators (the pool), the
// four slots, the assignment rule the director follows at sections and drops, and the generators'
// param rows; then the renderer's pure rules (viz/GeneratorTargets: target sizes, instance keys, pins)
// and the shell's text (AssignmentText). Built and run by CI ("Native host tests" in
// .github/workflows/android.yml) with the system compiler; no GL, no device.
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "scene/generator/Assignment.hpp"
#include "scene/generator/AssignmentText.hpp"
#include "scene/generator/GeneratorParams.hpp"
#include "scene/generator/Pool.hpp"
#include "scene/generator/Slot.hpp"
#include "viz/Catalogue.hpp"
#include "viz/GeneratorTargets.hpp"

using namespace geode::scene::generator;
namespace catalogue = geode::viz::catalogue;
using geode::signal::ParamKind;
using Reason = geode::viz::Director::Reason;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

const std::vector<std::string> kPool = {"plasma", "julia", "silk_web", "life_orbium", "acid_tv", "fluid_a", "cym_b"};

Assignment fourAssigned(Policy policy) {
    Assignment a;
    a.policy = policy;
    a.slots[0].id = "plasma";
    a.slots[1].id = "julia";
    a.slots[2].id = "silk_web";
    a.slots[3].id = "acid_tv";
    return a;
}

int changedSlots(const Assignment& before, const Assignment& after) {
    int n = 0;
    for (int i = 0; i < kSlotCount; ++i) n += before.slots[i].id != after.slots[i].id ? 1 : 0;
    return n;
}

bool sameAssignment(const Assignment& a, const Assignment& b) {
    for (int i = 0; i < kSlotCount; ++i) {
        if (a.slots[i].id != b.slots[i].id || a.slots[i].pinned != b.slots[i].pinned) return false;
    }
    return a.policy == b.policy;
}

void testSlots() {
    check(kSlotCount == 4, "four slots");
    check(std::string(slotName(Slot::Sky)) == "sky", "sky name");
    check(std::string(slotName(Slot::Wall)) == "wall", "wall name");
    check(std::string(slotName(Slot::Material)) == "material", "material name");
    check(std::string(slotName(Slot::CameraPlane)) == "plane", "plane name");
}

bool isSimKind(catalogue::Kind k) {
    using K = catalogue::Kind;
    return k == K::Silk || k == K::Fluid || k == K::Life || k == K::Acid || k == K::Cymatics;
}

void testPool() {
    const std::vector<std::string> pool = generatorPool();
    check(!pool.empty(), "pool is not empty");
    // Exactly the 2D shader styles (shader kind, no march) and the simulations, in catalogue order.
    std::vector<std::string> expected;
    for (const auto& id : catalogue::allIds()) {
        const catalogue::Kind k = catalogue::kindOf(id);
        if ((k == catalogue::Kind::Shader && !catalogue::isMarched(id)) || isSimKind(k)) expected.push_back(id);
    }
    check(pool == expected, "pool = 2D shader styles + simulations, in catalogue order");
    // The text path reads the same table the shell reads and gives the same list.
    check(generatorPool(catalogue::text().c_str()) == pool, "pool from catalogue text = pool from catalogue");
    check(generatorPool(nullptr).empty(), "no text, no pool");
    check(generatorPool("").empty(), "empty text, no pool");

    const std::set<std::string> in(pool.begin(), pool.end());
    check(in.count("plasma") == 1, "plasma (2D) is a generator");
    check(in.count("glow_vortex") == 1, "glow_vortex (2D neon) is a generator");
    check(in.count("silk_web") == 1, "silk is a generator");
    check(in.count("life_orbium") == 1, "life is a generator");
    check(in.count("acid_tv") == 1, "acid is a generator");
    check(in.count("neon_chrome_sea") == 0, "a marched style is not a generator");
    check(in.count("crystal_canyon") == 0, "a fly-through is not a generator");
    check(in.count("milkdrop") == 0, "milkdrop is not a generator");
    bool fluid = false, cym = false;
    for (const auto& id : pool) {
        fluid = fluid || catalogue::kindOf(id) == catalogue::Kind::Fluid;
        cym = cym || catalogue::kindOf(id) == catalogue::Kind::Cymatics;
    }
    check(fluid, "fluid styles are generators");
    check(cym, "cymatics styles are generators");
    check(in.size() == pool.size(), "pool ids unique");

    // A hand-made table: style rows only, kinds read by name, marched shader rows dropped.
    const char* text =
        "catalogue|1\n"
        "kind|SHADER|UNIVERSAL\n"
        "style|silk_web|SILK\n"
        "style|myco_rivals|MYCELIUM\n"
        "style|plasma|SHADER\n"
        "style|kifs|SHADER|MARCH_DETAIL\n"
        "style|milkdrop|MILKDROP\n"
        "style|fluid_x|FLUID\n"
        "style|cym_y|CYMATICS\n"
        "style|life_z|LIFE\n"
        "style|acid_w|ACID\n"
        "family|arbor|arbor_|lib_arbor|plasma\n";
    const std::vector<std::string> want = {"silk_web", "plasma", "fluid_x", "cym_y", "life_z", "acid_w"};
    check(generatorPool(text) == want, "pool from hand-made text");
}

void testOnlyMeNeverSwaps() {
    Assignment a = fourAssigned(Policy::OnlyMe);
    const Assignment before = a;
    bool any = false;
    for (std::uint32_t i = 0; i < 200; ++i) {
        any = onMoment(a, Moment::Drop, 42u, i, kPool) || any;
        any = onMoment(a, Moment::Section, 42u, i, kPool) || any;
    }
    check(!any, "OnlyMe: onMoment reports no change");
    check(sameAssignment(a, before), "OnlyMe: nothing changes");
    check(Assignment{}.policy == Policy::OnlyMe, "the default policy is OnlyMe");
}

void testPinsNeverSwapped() {
    Assignment a = fourAssigned(Policy::DirectorSwaps);
    a.slots[0].pinned = true;
    a.slots[2].pinned = true;
    for (std::uint32_t i = 0; i < 300; ++i) {
        onMoment(a, (i % 2) ? Moment::Drop : Moment::Section, 7u + i, i, kPool);
        check(a.slots[0].id == "plasma" && a.slots[0].pinned, "pinned sky never swaps");
        check(a.slots[2].id == "silk_web" && a.slots[2].pinned, "pinned material never swaps");
    }
    Assignment all = fourAssigned(Policy::DirectorSwaps);
    for (auto& s : all.slots) s.pinned = true;
    const Assignment before = all;
    check(!onMoment(all, Moment::Drop, 1u, 0u, kPool), "all pinned: a drop changes nothing");
    check(sameAssignment(all, before), "all pinned: unchanged");
}

void testOnlyUnpinnedAssigned() {
    Assignment a;
    a.policy = Policy::DirectorSwaps;
    a.slots[1].id = "julia";          // assigned, unpinned: may swap
    a.slots[3].id = "acid_tv";        // assigned, pinned: never
    a.slots[3].pinned = true;
    for (std::uint32_t i = 0; i < 200; ++i) {
        onMoment(a, Moment::Drop, 99u, i, kPool);
        check(a.slots[0].id.empty(), "an empty slot stays empty");
        check(a.slots[2].id.empty(), "an empty slot stays empty (material)");
        check(a.slots[3].id == "acid_tv", "a pinned slot keeps its id");
        check(!a.slots[1].id.empty(), "an assigned slot stays assigned");
    }
    Assignment empty;
    empty.policy = Policy::DirectorSwaps;
    check(!onMoment(empty, Moment::Drop, 3u, 0u, kPool), "nothing assigned: nothing changes");
}

void testDropSwapsAllUnpinnedAssigned() {
    for (std::uint32_t i = 0; i < 100; ++i) {
        Assignment a = fourAssigned(Policy::DirectorSwaps);
        a.slots[1].pinned = true;
        const Assignment before = a;
        check(onMoment(a, Moment::Drop, 1234u, i, kPool), "a drop reports a change");
        check(a.slots[0].id != before.slots[0].id, "a drop swaps sky");
        check(a.slots[1].id == before.slots[1].id, "a drop keeps the pinned wall");
        check(a.slots[2].id != before.slots[2].id, "a drop swaps material");
        check(a.slots[3].id != before.slots[3].id, "a drop swaps plane");
        for (const auto& s : a.slots) {
            bool inPool = false;
            for (const auto& id : kPool) inPool = inPool || id == s.id;
            check(inPool, "a swapped id comes from the pool");
        }
    }
}

void testSectionSwapsAtLeastOne() {
    int total = 0;
    int sectionMax = 0;
    for (std::uint32_t i = 0; i < 500; ++i) {
        Assignment a = fourAssigned(Policy::DirectorSwaps);
        const Assignment before = a;
        check(onMoment(a, Moment::Section, 555u, i, kPool), "a section reports a change");
        const int n = changedSlots(before, a);
        check(n >= 1, "a section swaps at least one slot");
        total += n;
        if (n > sectionMax) sectionMax = n;
        Assignment d = fourAssigned(Policy::DirectorSwaps);
        onMoment(d, Moment::Drop, 555u, i, kPool);
        check(changedSlots(before, d) >= n, "a drop swaps at least as many as a section");
    }
    // A seeded subset: not always every slot (else a section is a drop) and not always one.
    check(total < 500 * kSlotCount, "a section is a subset, not always every slot");
    check(sectionMax > 1, "a section sometimes swaps more than one slot");
}

void testDeterminism() {
    for (std::uint32_t i = 0; i < 50; ++i) {
        for (Moment m : {Moment::Section, Moment::Drop}) {
            Assignment a = fourAssigned(Policy::DirectorSwaps);
            Assignment b = fourAssigned(Policy::DirectorSwaps);
            const bool ra = onMoment(a, m, 0xDEADBEEFCAFEull, i, kPool);
            const bool rb = onMoment(b, m, 0xDEADBEEFCAFEull, i, kPool);
            check(ra == rb && sameAssignment(a, b), "same (seed, index) -> same result");
        }
    }
    // Different seeds or indices give different picks somewhere.
    int differ = 0;
    for (std::uint32_t i = 0; i < 50; ++i) {
        Assignment a = fourAssigned(Policy::DirectorSwaps);
        Assignment b = fourAssigned(Policy::DirectorSwaps);
        onMoment(a, Moment::Drop, 1u, i, kPool);
        onMoment(b, Moment::Drop, 2u, i, kPool);
        differ += sameAssignment(a, b) ? 0 : 1;
    }
    check(differ > 0, "a different seed picks differently");
    int differIdx = 0;
    for (std::uint32_t i = 0; i < 20; ++i) {
        Assignment p = fourAssigned(Policy::DirectorSwaps), q = fourAssigned(Policy::DirectorSwaps);
        onMoment(p, Moment::Drop, 9u, i, kPool);
        onMoment(q, Moment::Drop, 9u, i + 1, kPool);
        differIdx += sameAssignment(p, q) ? 0 : 1;
    }
    check(differIdx > 0, "a different moment index picks differently");
}

void testNeverPicksItsOwnId() {
    const std::vector<std::string> two = {"plasma", "julia"};
    for (std::uint32_t i = 0; i < 100; ++i) {
        Assignment a;
        a.policy = Policy::DirectorSwaps;
        a.slots[0].id = "plasma";
        check(onMoment(a, Moment::Section, 5u, i, two), "a two-id pool always swaps");
        check(a.slots[0].id == "julia", "never the id it already has");
    }
    // A pool with only the current id (or nothing) cannot swap.
    Assignment a;
    a.policy = Policy::DirectorSwaps;
    a.slots[0].id = "plasma";
    check(!onMoment(a, Moment::Drop, 5u, 0u, {"plasma"}), "a pool of itself: no change");
    check(!onMoment(a, Moment::Drop, 5u, 0u, {}), "an empty pool: no change");
    check(a.slots[0].id == "plasma", "kept");
    // An id the pool lacks (a style since removed) still swaps into the pool.
    Assignment b;
    b.policy = Policy::DirectorSwaps;
    b.slots[2].id = "gone_style";
    check(onMoment(b, Moment::Drop, 5u, 0u, two), "an unknown id swaps into the pool");
    check(b.slots[2].id == "plasma" || b.slots[2].id == "julia", "into the pool");
}

void testSchema() {
    const auto schema = generatorSchema();
    check(schema.size() == kGeneratorParamCount, "one row per GeneratorParam");
    check(kGeneratorParamCount == 9, "policy + 4 pins + 4 mixes");
    check(schema.idsUnique(), "ids unique");
    const auto& policy = schema.def(kPolicy);
    check(policy.id == "scene.generators.policy", "policy id");
    check(policy.kind == ParamKind::Int && policy.min == 0.0f && policy.max == 1.0f && policy.defaultValue == 0.0f,
          "policy Int 0..1 default 0 (OnlyMe)");
    for (int i = 0; i < kSlotCount; ++i) {
        const Slot s = static_cast<Slot>(i);
        const auto& pin = schema.def(pinParam(s));
        const auto& mix = schema.def(mixParam(s));
        check(pin.id == std::string("scene.generators.pin.") + slotName(s), "pin id per slot");
        check(pin.kind == ParamKind::Bool && pin.defaultValue == 0.0f, "pin Bool default 0");
        check(mix.id == std::string("scene.generators.mix.") + slotName(s), "mix id per slot");
        check(mix.kind == ParamKind::Float && mix.min == 0.0f && mix.max == 1.0f && mix.defaultValue == 1.0f,
              "mix Float 0..1 default 1");
        check(schema.find(pin.id).value_or(geode::signal::kNoParam) == pinParam(s), "pin found by id");
        check(schema.find(mix.id).value_or(geode::signal::kNoParam) == mixParam(s), "mix found by id");
    }
    check(schema.clamp(kPolicy, 3.0f) == 1.0f, "policy clamps to 1");
    check(schema.clamp(mixParam(Slot::Sky), -1.0f) == 0.0f, "mix clamps to 0");
}

void testText() {
    Assignment a;
    check(assignmentText(a) == "0\t\t0\n1\t\t0\n2\t\t0\n3\t\t0\npolicy\t0\n", "empty assignment's text");
    a.slots[0] = {"plasma", true};
    a.slots[3] = {"silk_web", false};
    a.policy = Policy::DirectorSwaps;
    const std::string t = assignmentText(a);
    check(t == "0\tplasma\t1\n1\t\t0\n2\t\t0\n3\tsilk_web\t0\npolicy\t1\n", "slot lines in Slot order, then the policy");
    Assignment back;
    check(parseAssignmentText(t, back) && sameAssignment(a, back), "text round trip");
    const Assignment empty;
    check(parseAssignmentText(assignmentText(empty), back) && sameAssignment(empty, back), "empty round trip");
    // Line order and CRLF do not matter; each slot and the policy exactly once does.
    check(parseAssignmentText("policy\t1\r\n3\tsilk_web\t0\r\n2\t\t0\n1\t\t0\n0\tplasma\t1\n", back) && sameAssignment(a, back),
          "any line order, CRLF");
    const Assignment kept = back;
    const char* bad[] = {
        "",
        "0\t\t0\n1\t\t0\n2\t\t0\npolicy\t0\n",                  // slot 3 missing
        "0\t\t0\n1\t\t0\n2\t\t0\n3\t\t0\n",                     // policy missing
        "0\t\t0\n0\t\t0\n1\t\t0\n2\t\t0\n3\t\t0\npolicy\t0\n",  // a slot twice
        "0\t\t0\n1\t\t0\n2\t\t0\n3\t\t0\npolicy\t2\n",          // unknown policy
        "0\t\t2\n1\t\t0\n2\t\t0\n3\t\t0\npolicy\t0\n",          // pin not a bit
        "4\t\t0\n0\t\t0\n1\t\t0\n2\t\t0\n3\t\t0\npolicy\t0\n",  // no slot 4
        "0\tx\n1\t\t0\n2\t\t0\n3\t\t0\npolicy\t0\n",            // pin field missing
        "0\tx\t0\textra\n1\t\t0\n2\t\t0\n3\t\t0\npolicy\t0\n",  // a field too many
        "sky\tx\t0\n1\t\t0\n2\t\t0\n3\t\t0\npolicy\t0\n",       // names are not slot numbers
    };
    for (const char* b : bad) {
        Assignment out = kept;
        check(!parseAssignmentText(b, out), "malformed text rejected");
        check(sameAssignment(out, kept), "a rejected parse leaves the output alone");
    }
}

void testTargets() {
    namespace gt = geode::viz::generator_targets;
    using geode::viz::ThermalTier;
    const ThermalTier tiers[] = {ThermalTier::Full, ThermalTier::Eased, ThermalTier::Reduced, ThermalTier::Minimal};
    // The camera plane fills the view at the render size, whatever the tier (that size is already scaled).
    for (ThermalTier t : tiers) {
        const auto s = gt::targetSize(Slot::CameraPlane, t, 1512, 3360);
        check(s.width == 1512 && s.height == 3360, "plane = render size");
    }
    // Surfaces: square powers of two, 512 at most, never growing as the phone heats.
    int last = 1 << 30;
    for (ThermalTier t : tiers) {
        for (Slot s : {Slot::Sky, Slot::Wall, Slot::Material}) {
            const auto z = gt::targetSize(s, t, 1512, 3360);
            check(z.width == z.height, "surface target is square");
            check(z.width > 0 && (z.width & (z.width - 1)) == 0, "surface target is a power of two");
            check(z.width <= 512, "surface target capped at 512");
            check(z.width <= last, "a hotter tier never grows a surface target");
        }
        last = gt::targetSize(Slot::Sky, t, 1512, 3360).width;
    }
    check(gt::targetSize(Slot::Sky, ThermalTier::Full, 1512, 3360).width == 512, "a cool phone's sky is 512");
    check(gt::targetSize(Slot::Sky, ThermalTier::Minimal, 1512, 3360).width < 512, "a hot phone's sky is smaller");
    check(gt::targetSize(Slot::Wall, ThermalTier::Full, 300, 200).width == 256, "a small render's surface fits under it");
    check(gt::targetSize(Slot::Material, ThermalTier::Full, 0, -5).width == 1, "a degenerate render size gives 1x1");
    check(gt::targetSize(Slot::CameraPlane, ThermalTier::Full, 0, 0).width == 1, "a degenerate plane is 1x1");

    check(gt::validSlot(0) && gt::validSlot(3) && !gt::validSlot(-1) && !gt::validSlot(4), "slot range");
    // Instance keys: "gen:<slot name>:<id>", none for an empty slot, the style read back from the tail.
    check(gt::instanceKey(Slot::Sky, "plasma") == "gen:sky:plasma", "sky key");
    check(gt::instanceKey(Slot::CameraPlane, "silk_web") == "gen:plane:silk_web", "plane key");
    check(gt::instanceKey(Slot::Wall, "").empty(), "no id, no key");
    check(gt::instanceKey(Slot::Wall, "plasma") != gt::instanceKey(Slot::Material, "plasma"), "one instance per slot");
    check(gt::isInstanceKey("gen:wall:plasma") && !gt::isInstanceKey("plasma") && !gt::isInstanceKey("ge"), "key test");
    check(gt::styleOf("gen:material:acid_tv") == "acid_tv", "style of an instance key");
    check(gt::styleOf("neon_chrome_sea") == "neon_chrome_sea", "style of a plain id is the id");
    check(gt::styleOf("gen:broken").empty(), "a malformed key builds nothing");
    for (const auto& id : generatorPool()) {
        check(gt::styleOf(gt::instanceKey(Slot::Sky, id)) == id, "every pool id survives its key");
    }

    // Pins: exactly the assigned slots' instances.
    Assignment a;
    a.slots[0].id = "plasma";
    a.slots[3].id = "silk_web";
    const gt::Keys keys = gt::wantedKeys(a);
    check(keys[0] == "gen:sky:plasma" && keys[1].empty() && keys[2].empty() && keys[3] == "gen:plane:silk_web", "wanted keys");
    check(gt::pins(keys, "gen:sky:plasma") && gt::pins(keys, "gen:plane:silk_web"), "assigned instances pinned");
    check(!gt::pins(keys, "plasma"), "the style's own scene is not pinned by its generator");
    check(!gt::pins(keys, "gen:wall:plasma"), "another slot's instance is not pinned");
    check(!gt::pins(keys, ""), "nothing pins the empty id");
}


// The director hook (#472): only a Section or a Drop commit is a generator moment, numbered by the
// director's commit count, so a replay with the same seed swaps at the same commits.
void testDirectorMoment() {
    namespace gt = geode::viz::generator_targets;
    const auto section = gt::directorMoment(Reason::Section, 7);
    check(section && section->moment == Moment::Section && section->index == 7u, "a Section commit is a Section moment");
    const auto drop = gt::directorMoment(Reason::Drop, 9);
    check(drop && drop->moment == Moment::Drop && drop->index == 9u, "a Drop commit is a Drop moment");
    check(!gt::directorMoment(Reason::First, 1), "the first look is no moment");
    check(!gt::directorMoment(Reason::Modulation, 3), "a modulation is no moment");
    check(!gt::directorMoment(Reason::Long, 4), "a long hold is no moment");
    check(!gt::directorMoment(Reason::Section, -1), "a negative count is no moment");
}

// A play's run of director commits (7 of them Sections or Drops), fed through the hook as the
// renderer feeds it.
int playThrough(Assignment& a, std::uint64_t seed) {
    namespace gt = geode::viz::generator_targets;
    static const Reason kRun[] = {Reason::First, Reason::Modulation, Reason::Section, Reason::Long,
                                  Reason::Drop, Reason::Section, Reason::Modulation, Reason::Section,
                                  Reason::Drop, Reason::Drop, Reason::Section, Reason::Long};
    int changes = 0;
    int commits = 0;
    for (Reason why : kRun) {
        const auto m = gt::directorMoment(why, ++commits);
        if (m && onMoment(a, m->moment, seed, m->index, kPool)) ++changes;
    }
    return changes;
}

Assignment twoPinnedOneEmpty() {
    Assignment a = fourAssigned(Policy::DirectorSwaps);
    a.slots[1].pinned = true;
    a.slots[3].pinned = true;
    a.slots[2].id.clear();
    return a;
}

void testDirectorSequence() {
    Assignment only = fourAssigned(Policy::OnlyMe);
    only.slots[0].pinned = true;
    const Assignment before = only;
    check(playThrough(only, 0xC0FFEEu) == 0 && sameAssignment(only, before), "OnlyMe: a play's moments change nothing");

    for (std::uint64_t seed : {1ull, 42ull, 0xDEADBEEFull}) {
        Assignment a = twoPinnedOneEmpty();
        const int changes = playThrough(a, seed);
        check(changes == 7, "DirectorSwaps: every Section and Drop of the play swaps something");
        check(a.slots[1].id == "julia" && a.slots[1].pinned, "DirectorSwaps: a pinned wall survives the play");
        check(a.slots[3].id == "acid_tv" && a.slots[3].pinned, "DirectorSwaps: a pinned plane survives the play");
        check(a.slots[2].id.empty() && !a.slots[2].pinned, "DirectorSwaps: an empty slot stays empty");
        Assignment again = twoPinnedOneEmpty();
        playThrough(again, seed);
        check(sameAssignment(a, again), "DirectorSwaps: the same play swaps the same way");
    }
    Assignment allPinned = fourAssigned(Policy::DirectorSwaps);
    for (auto& s : allPinned.slots) s.pinned = true;
    const Assignment pinnedBefore = allPinned;
    check(playThrough(allPinned, 7u) == 0 && sameAssignment(allPinned, pinnedBefore), "DirectorSwaps: all pinned, nothing moves");
}

// Which slots the renderer steps: only those the scenes on screen sample, and at the Minimal tier only
// the camera plane (it is the picture; a sky, wall or material is decoration a hot phone sheds).
void testDrawsSlot() {
    namespace gt = geode::viz::generator_targets;
    using geode::viz::ThermalTier;
    const ThermalTier tiers[] = {ThermalTier::Full, ThermalTier::Eased, ThermalTier::Reduced, ThermalTier::Minimal};
    for (ThermalTier t : tiers) {
        for (int i = 0; i < kSlotCount; ++i) {
            check(!gt::drawsSlot(static_cast<Slot>(i), t, false), "an unsampled slot is never drawn");
        }
        check(gt::drawsSlot(Slot::CameraPlane, t, true), "a sampled plane is drawn on every tier");
    }
    for (ThermalTier t : {ThermalTier::Full, ThermalTier::Eased, ThermalTier::Reduced}) {
        for (Slot s : {Slot::Sky, Slot::Wall, Slot::Material}) check(gt::drawsSlot(s, t, true), "a sampled surface is drawn below Minimal");
    }
    for (Slot s : {Slot::Sky, Slot::Wall, Slot::Material}) {
        check(!gt::drawsSlot(s, ThermalTier::Minimal, true), "Minimal sheds the sky, wall and material");
    }
}

// The live assignment, published process-wide for an export to adopt (like director::setGlobal).
void testPublished() {
    check(sameAssignment(published(), Assignment{}), "nothing published: the default assignment");
    Assignment a = fourAssigned(Policy::DirectorSwaps);
    a.slots[2].pinned = true;
    publish(a);
    check(sameAssignment(published(), a), "an export adopts what the live renderer published");
    Assignment b;
    b.slots[0].id = "julia";
    publish(b);
    check(sameAssignment(published(), b), "the latest publish wins");

    // Who publishes: the live renderer the shell set generators on; never an export's, nor a renderer
    // nobody set anything on (the wallpaper, the dream).
    namespace gt = geode::viz::generator_targets;
    check(gt::publishesAssignment(false, true), "the live renderer the shell sets publishes");
    check(!gt::publishesAssignment(false, false), "a live renderer nobody set anything on never publishes");
    check(!gt::publishesAssignment(true, true) && !gt::publishesAssignment(true, false), "an export never publishes");

    // A Visuals view detached and attached again: the old renderer published its choices and a swap, the
    // new one starts from them (Renderer's constructor adopts published()), and so does an export.
    Assignment live = twoPinnedOneEmpty();
    publish(live);
    playThrough(live, 99u);
    publish(live);  // the renderer publishes after each swap
    const Assignment adopted = published();
    check(sameAssignment(adopted, live), "a new live renderer adopts the choices and the director's swaps");
    check(adopted.slots[1].pinned && adopted.slots[3].pinned && adopted.policy == Policy::DirectorSwaps,
          "the pins and the policy survive the renderer's rebuild");
    publish(Assignment{});
}

}  // namespace

int main() {
    testSlots();
    testPool();
    testOnlyMeNeverSwaps();
    testPinsNeverSwapped();
    testOnlyUnpinnedAssigned();
    testDropSwapsAllUnpinnedAssigned();
    testSectionSwapsAtLeastOne();
    testDeterminism();
    testNeverPicksItsOwnId();
    testSchema();
    testText();
    testTargets();
    testDirectorMoment();
    testDirectorSequence();
    testDrawsSlot();
    testPublished();
    if (failures == 0) std::printf("generator_test: OK\n");
    return failures == 0 ? 0 : 1;
}
