// Host-side checks for core/viz/SceneResidency (#378): which built scenes stay on the GPU. Built and
// run by CI ("Native host tests" in .github/workflows/android.yml) with the system compiler; no GL.
//
// What is pinned: an hour of Auto over every style keeps a bounded set resident (before the fix the
// list only grew), scenes in use are never released, fluid scenes weigh more, a hotter tier keeps
// fewer spares, and a tier change marks spares for a lazy resize instead of resizing them all.
#include <algorithm>
#include <cstdio>
#include <string>

#include "viz/SceneResidency.hpp"

using geode::viz::MemoryTrim;
using geode::viz::SceneResidency;
using geode::viz::ThermalTier;
using geode::viz::TrimShed;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool has(const SceneResidency& r, const std::string& id) {
    for (const auto& e : r.entries()) {
        if (e.id == id) return true;
    }
    return false;
}

// The issue's failure scenario: Auto steps the style at every section for an hour, through every
// style, some of them fluid. Residency must stay bounded by pins + budget, whatever the length.
void longAutoSessionStaysBounded() {
    SceneResidency r;
    const int budget = SceneResidency::spareBudget(ThermalTier::Full);
    std::string active;
    std::string outgoing;
    size_t most = 0;
    for (int step = 0; step < 600; step++) {
        const std::string next = "style_" + std::to_string(step % 237);
        outgoing = active;
        active = next;
        r.insert(next, step % 5 == 0, 0);
        const auto pinned = [&](const std::string& id) { return id == active || id == outgoing; };
        for (const auto& victim : r.evict(pinned, budget)) {
            check(victim != active && victim != outgoing, "a scene in use is never released");
        }
        most = std::max(most, r.size());
    }
    check(most <= 2 + static_cast<size_t>(budget), "an hour of Auto keeps at most the pins plus the spare budget");
    check(has(r, active) && has(r, outgoing), "the drawn and the fading scene stay resident");
}

void workedExampleFromTheHeader() {
    SceneResidency r;
    r.insert("neon", false, 0);
    r.insert("fluid", true, 0);
    r.insert("arbor", false, 0);
    r.insert("glass", false, 0);
    const auto pinned = [](const std::string& id) { return id == "glass"; };
    const auto victims = r.evict(pinned, SceneResidency::spareBudget(ThermalTier::Full));
    check(victims.size() == 1 && victims[0] == "fluid", "a fluid spare over budget goes, lighter spares stay");
    check(has(r, "glass") && has(r, "arbor") && has(r, "neon"), "spares that fit stay resident");
    const auto hot = r.evict(pinned, SceneResidency::spareBudget(ThermalTier::Minimal));
    check(hot.size() == 2 && r.size() == 1 && has(r, "glass"), "a hot phone keeps only what it draws");
}

void backAndForthStaysACacheHit() {
    SceneResidency r;
    std::string active;
    int builds = 0;
    for (int step = 0; step < 40; step++) {
        const std::string next = step % 2 == 0 ? "a" : "b";
        if (!has(r, next)) {
            r.insert(next, true, 0);
            ++builds;
        } else {
            r.touch(next);
        }
        active = next;
        r.evict([&](const std::string& id) { return id == active; }, SceneResidency::spareBudget(ThermalTier::Full));
    }
    check(builds == 2, "two looks cut on the beat build once each on a cool phone, even fluid ones");
}

void touchMakesMostRecent() {
    SceneResidency r;
    r.insert("a", false, 0);
    r.insert("b", false, 0);
    r.insert("c", false, 0);
    r.touch("a");
    check(r.entries().front().id == "a", "touch moves a scene to the front");
    const auto victims = r.evict([](const std::string&) { return false; }, 2);
    check(victims.size() == 1 && victims[0] == "b", "the least recently used spare goes first");
    r.insert("a", true, 0);
    check(r.size() == 2 && r.entries().front().weight == SceneResidency::kHeavyWeight, "re-inserting replaces");
    r.remove("a");
    check(!has(r, "a"), "remove forgets the scene");
    r.clear();
    check(r.size() == 0, "clear forgets everything");
}

void tiersShedSpares() {
    check(SceneResidency::spareBudget(ThermalTier::Full) >= SceneResidency::kHeavyWeight, "a cool phone can keep one fluid spare");
    check(SceneResidency::spareBudget(ThermalTier::Eased) <= SceneResidency::spareBudget(ThermalTier::Full), "eased keeps no more");
    check(SceneResidency::spareBudget(ThermalTier::Reduced) <= SceneResidency::spareBudget(ThermalTier::Eased), "reduced keeps no more");
    check(SceneResidency::spareBudget(ThermalTier::Minimal) == 0, "minimal keeps no spares");
}

void resizeIsLazy() {
    SceneResidency r;
    r.insert("drawn", true, 1);
    r.insert("spare", true, 1);
    check(!r.needsResize("drawn", 1) && !r.needsResize("spare", 1), "a fresh build is sized");
    // A tier change: the stamp moves; only the drawn scene is resized now.
    r.markSized("drawn", 2);
    check(!r.needsResize("drawn", 2), "the drawn scene is resized at once");
    check(r.needsResize("spare", 2), "a spare waits until it is drawn again");
    r.markSized("spare", 2);
    check(!r.needsResize("spare", 2), "and is then sized once");
    check(!r.needsResize("unknown", 2), "an unbuilt scene needs no resize");
}

// #854: the system's trim levels. Only UI_HIDDEN (20) and BACKGROUND (40) act; the spares go whatever the
// thermal tier, and the pinned scenes (the one drawn, fading out, the layer) are never dropped.
void memoryTrimShedsSparesNotPinned() {
    check(SceneResidency::trimForAndroidLevel(20) == MemoryTrim::UiHidden, "UI_HIDDEN maps");
    check(SceneResidency::trimForAndroidLevel(40) == MemoryTrim::Background, "BACKGROUND maps");
    for (int level : {0, 5, 10, 15, 19, 21, 41, 60, 80}) {
        check(SceneResidency::trimForAndroidLevel(level) == MemoryTrim::None, "every other level is ignored");
    }
    for (ThermalTier tier : {ThermalTier::Full, ThermalTier::Eased, ThermalTier::Reduced, ThermalTier::Minimal}) {
        check(SceneResidency::spareBudget(tier, MemoryTrim::None) == SceneResidency::spareBudget(tier), "no trim keeps the tier budget");
        check(SceneResidency::spareBudget(tier, MemoryTrim::UiHidden) == 0, "UI_HIDDEN keeps no spares");
        check(SceneResidency::spareBudget(tier, MemoryTrim::Background) == 0, "BACKGROUND keeps no spares");
    }
    for (MemoryTrim trim : {MemoryTrim::UiHidden, MemoryTrim::Background}) {
        SceneResidency r;
        r.insert("old", false, 1);
        r.insert("fluid", true, 1);
        r.insert("fading", false, 1);
        r.insert("layer", false, 1);
        r.insert("drawn", false, 1);
        const auto pinned = [](const std::string& id) { return id == "drawn" || id == "fading" || id == "layer"; };
        const auto victims = r.evict(pinned, SceneResidency::spareBudget(ThermalTier::Full, trim));
        check(victims.size() == 2, "both spares go");
        check(has(r, "drawn") && has(r, "fading") && has(r, "layer"), "the pinned scenes stay");
        check(!has(r, "old") && !has(r, "fluid"), "the spares are gone");
    }
}

// The pause path (Renderer::trimSpares, geode_viz_trim_spares): a surface that stops drawing sheds as a
// UiHidden trim does, at once, whatever the tier. What it keeps is what it shows when it comes back: the
// scene drawn, the one fading out, the layer, the born style and the one requested (built or not). The
// signal that arrives later, read by the first frame after the return, then finds nothing left to drop,
// and a spare built after the return is under the tier's budget again.
void pauseTrimKeepsWhatIsShownAndIsNotRepeated() {
    for (ThermalTier tier : {ThermalTier::Full, ThermalTier::Eased, ThermalTier::Reduced, ThermalTier::Minimal}) {
        SceneResidency r;
        r.insert("old", false, 1);
        r.insert("fluid_spare", true, 1);
        r.insert("born", false, 1);
        r.insert("layer", false, 1);
        r.insert("fading", false, 1);
        r.insert("recent", false, 1);
        r.insert("drawn", true, 1);
        const auto pinned = [](const std::string& id) {
            return id == "drawn" || id == "fading" || id == "layer" || id == "born" || id == "requested";
        };
        const int budget = SceneResidency::spareBudget(tier, MemoryTrim::UiHidden);
        const auto victims = r.evict(pinned, budget);
        check(victims.size() == 3, "a pause sheds every spare, at any tier");
        check(!has(r, "old") && !has(r, "fluid_spare") && !has(r, "recent"), "the light, the heavy and the newest spare go");
        check(has(r, "drawn") && has(r, "fading") && has(r, "layer") && has(r, "born"), "what is shown stays");
        check(r.size() == 4, "nothing else is resident through the background");
        check(r.evict(pinned, budget).empty(), "the trim signal read after the return drops nothing more");
        // Back on screen: a new style is admitted under the tier's own budget, as before the pause, less the
        // pins past the heaviest (#999): the fading scene, the layer and the born style weigh 1 each.
        r.insert("next", false, 1);
        const auto later = r.evict(pinned, SceneResidency::spareBudget(tier));
        const bool room = SceneResidency::spareBudget(tier) - 3 >= 1;
        check(has(r, "next") == room, "after the return the tier decides again");
        check(later.size() == (room ? 0u : 1u), "and only what is over it goes");
        r.remove("fading");
        r.remove("layer");
        r.remove("born");
        r.insert("next", false, 1);
        const auto alone = r.evict(pinned, SceneResidency::spareBudget(tier));
        check(has(r, "next") == (SceneResidency::spareBudget(tier) >= 1), "with one scene drawn the whole tier budget is there");
        check(alone.size() == (SceneResidency::spareBudget(tier) >= 1 ? 0u : 1u), "and only what is over it goes");
    }
}

// The signal reaches every renderer once, and a level that does nothing reaches none.
void trimSignalReachesEachRendererOnce() {
    SceneResidency::TrimSignal s;
    uint32_t a = 0;
    uint32_t b = 0;
    check(!s.poll(a), "nothing pending at start");
    s.request(MemoryTrim::None);
    check(!s.poll(a), "a level that does nothing is no request");
    s.request(MemoryTrim::UiHidden);
    check(s.poll(a) && s.poll(b), "each renderer sees the request");
    check(!s.poll(a) && !s.poll(b), "and only once");
    s.request(MemoryTrim::Background);
    check(s.poll(a), "a later request is seen again");
}

// #999: what a trim releases. A renderer still drawing (a visible wallpaper, the dream, Cast, an export
// reading the process signal in a frame) keeps its passes' history and its scene targets; only the spares
// go. A surface that stops drawing gives back the spares, the passes' targets and both scene targets.
void trimShedsByWhetherTheSurfaceDraws() {
    for (MemoryTrim trim : {MemoryTrim::UiHidden, MemoryTrim::Background}) {
        const TrimShed drawing = SceneResidency::shedFor(trim, true);
        check(drawing.spares, "a drawing renderer sheds its spares");
        check(!drawing.passTargets, "a drawing renderer keeps Liquid, Trails and the dive: no pop on screen or in an export");
        check(!drawing.sceneTargets, "a drawing renderer keeps its scene targets");
        const TrimShed stopped = SceneResidency::shedFor(trim, false);
        check(stopped.spares && stopped.passTargets && stopped.sceneTargets, "a surface that stops drawing gives back every target");
    }
    for (bool drawing : {true, false}) {
        const TrimShed none = SceneResidency::shedFor(MemoryTrim::None, drawing);
        check(!none.spares && !none.passTargets && !none.sceneTargets, "no trim sheds nothing");
    }
}

// #999: the budget counts what a scene weighs. A MilkDrop scene (a projectM instance with its own screen-size
// textures) is as heavy as a fluid, and the pinned scenes past the heaviest take their weight out of it.
void budgetCountsWeightAndPins() {
    check(SceneResidency::heavy(true, false) && SceneResidency::heavy(false, true), "fluid and MilkDrop scenes are heavy");
    check(!SceneResidency::heavy(false, false), "a shader style is light");
    const int full = SceneResidency::spareBudget(ThermalTier::Full);
    {
        SceneResidency r;
        r.insert("milk_spare_a", SceneResidency::heavy(false, true), 0);
        r.insert("milk_spare_b", SceneResidency::heavy(false, true), 0);
        r.insert("drawn", false, 0);
        const auto victims = r.evict([](const std::string& id) { return id == "drawn"; }, full);
        check(victims.size() == 1 && victims[0] == "milk_spare_a", "two MilkDrop spares no longer fit where three shader styles do");
    }
    {
        // One scene drawn: the budget is the tier's, as before.
        SceneResidency r;
        r.insert("a", false, 0);
        r.insert("b", false, 0);
        r.insert("c", false, 0);
        r.insert("drawn", true, 0);
        check(r.evict([](const std::string& id) { return id == "drawn"; }, full).empty(), "a single pin, however heavy, is free");
    }
    {
        // A fluid fading into a fluid: the fading one fills the budget, so the spares go.
        SceneResidency r;
        r.insert("spare_a", false, 0);
        r.insert("spare_b", false, 0);
        r.insert("fading", true, 0);
        r.insert("drawn", true, 0);
        const auto pinned = [](const std::string& id) { return id == "drawn" || id == "fading"; };
        const auto victims = r.evict(pinned, full);
        check(victims.size() == 2, "the second heavy pin takes the spares' room");
        check(has(r, "drawn") && has(r, "fading"), "and both pins stay");
    }
    {
        // A light style fading into a light one keeps room for two light spares at Full.
        SceneResidency r;
        r.insert("s1", false, 0);
        r.insert("s2", false, 0);
        r.insert("s3", false, 0);
        r.insert("fading", false, 0);
        r.insert("drawn", false, 0);
        const auto pinned = [](const std::string& id) { return id == "drawn" || id == "fading"; };
        const auto victims = r.evict(pinned, full);
        check(victims.size() == 1 && victims[0] == "s1", "a light second pin costs one spare");
    }
}

}  // namespace

int main() {
    trimShedsByWhetherTheSurfaceDraws();
    budgetCountsWeightAndPins();
    memoryTrimShedsSparesNotPinned();
    pauseTrimKeepsWhatIsShownAndIsNotRepeated();
    trimSignalReachesEachRendererOnce();
    longAutoSessionStaysBounded();
    workedExampleFromTheHeader();
    backAndForthStaysACacheHit();
    touchMakesMostRecent();
    tiersShedSpares();
    resizeIsLazy();
    if (failures > 0) {
        std::printf("%d scene residency check(s) failed\n", failures);
        return 1;
    }
    std::printf("scene residency: all checks passed\n");
    return 0;
}
