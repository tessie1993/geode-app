// Host tests for core/scene/generator (#471, #472): which styles can be generators (the pool), the
// four slots, the assignment rule the director follows at sections and drops, and the generators'
// param rows. Built and run by CI ("Native host tests" in .github/workflows/android.yml) with the
// system compiler; no GL, no device.
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "scene/generator/Assignment.hpp"
#include "scene/generator/GeneratorParams.hpp"
#include "scene/generator/Pool.hpp"
#include "scene/generator/Slot.hpp"
#include "viz/Catalogue.hpp"

using namespace geode::scene::generator;
namespace catalogue = geode::viz::catalogue;
using geode::signal::ParamKind;

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
    if (failures == 0) std::printf("generator_test: OK\n");
    return failures == 0 ? 0 : 1;
}
