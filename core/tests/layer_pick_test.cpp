// Host-side checks for which style Customize › Layers draws (core/viz/LayerPick). Built and run by CI ("Native host
// tests" in .github/workflows/native-host-tests.yml) from the repository root, with the system compiler; no GL.
//
// The owner's report: "Layers does not work and is hardcoded with one preset." Asked what the layer should choose
// from: "User chooses. Or director or macro." What is pinned: the layer never draws the style on screen while there
// is another; with nothing picked it suits the style on screen rather than taking the catalogue's first style for
// everyone; the user's pick is kept; Auto Director picks a new one at each style moment and only then, the same
// way for the same play seed; a knob walks a fixed list with a dead band; and the page's editor offers every
// style but the one on screen, once, by family, opens on the family of the style drawn now, and names the sources.
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "viz/Catalogue.hpp"
#include "viz/LayerPick.hpp"
#include "viz/LayerState.hpp"

using namespace geode::viz;
namespace pick = geode::viz::layer_pick;
namespace ls = geode::viz::layer_state;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

ls::Layer on(ls::Source source, const std::string& id = "") {
    ls::Layer l;
    l.on = true;
    l.source = source;
    l.sceneId = id;
    return l;
}

void thePoolIsAutosDraw() {
    const auto& pool = pick::pool();
    check(pool.size() > 20, "the pool holds Auto's styles");
    size_t at = 0;
    for (const std::string& id : catalogue::allIds()) {
        if (at < pool.size() && pool[at] == id) ++at;
    }
    check(at == pool.size(), "the pool is in the catalogue's order");
    for (const std::string& id : pool) {
        check(catalogue::has(catalogue::Trait::AutoDraw, id), id + " is one Auto draws");
        check(catalogue::kindOf(id) != catalogue::Kind::Milkdrop && catalogue::kindOf(id) != catalogue::Kind::Fluid,
              id + ": no MilkDrop preset or fluid solver starts as a layer on its own");
    }
}

void aSuggestionSuitsAndIsNeverTheActiveStyle() {
    std::set<std::string> distinct;
    for (const std::string& id : catalogue::allIds()) {
        const std::string s = pick::suggest(id);
        check(!s.empty() && s != id, "a layer over " + id + " is another style");
        check(s == pick::suggest(id), "the same style always gets the same layer: " + id);
        check(catalogue::has(catalogue::Trait::AutoDraw, s), "a suggestion comes from the pool: " + s);
        bool alike = false;
        for (const std::string& other : pick::pool()) alike = alike || (other != id && catalogue::lookOf(other) == catalogue::lookOf(id));
        if (alike) check(catalogue::lookOf(s) == catalogue::lookOf(id), "a layer over " + id + " looks like it: " + s);
        distinct.insert(s);
    }
    // The old page took the catalogue's first style for everyone.
    check(distinct.size() > 10, "the suggestion follows the style on screen, not the catalogue's order");
    check(pick::suggest(pick::pool().front()) != pick::pool().front(), "the catalogue's first style gets another");
}

void theUsersPickIsKept() {
    const auto& pool = pick::pool();
    const std::string a = pool[0];
    const std::string b = pool[5];
    check(pick::userPick(b, a) == b, "a picked style is drawn");
    check(pick::userPick(a, a) == pick::suggest(a), "the style on screen picked is swapped for one that suits it");
    check(pick::userPick("", a) == pick::suggest(a), "none picked suits the style on screen");
    check(pick::userPick("no_such_style", a) == pick::suggest(a), "an unknown id suits the style on screen");
    check(pick::userPick("water", a) == catalogue::resolveId("water"), "a retired style is the one that replaced it");
    check(pick::userPick("milkdrop", a) == "milkdrop", "the user may pick any style, not only the pool's");
}

void theKnobWalksWithADeadBand() {
    check(pick::macroIndex(-1.0f, 10, -1) == 0 && pick::macroIndex(1.0f, 10, -1) == 9, "the knob's ends are the list's");
    check(pick::macroIndex(0.0f, 10, -1) == 5, "the centre is the middle step");
    check(pick::macroIndex(0.0f, 0, -1) == -1, "an empty list picks none");
    check(pick::macroIndex(std::nanf(""), 10, -1) == 5, "a NaN turn is the centre");
    // Step 4 spans turns 0.4..0.5 (as 0..1); the dead band holds it a third of a step past either edge.
    check(pick::macroIndex(0.01f, 10, 4) == 4, "just past the edge holds the last step");
    check(pick::macroIndex(0.25f, 10, 4) == 6, "well past it moves on");
    check(pick::macroIndex(-0.25f, 10, 4) == 4, "just under the low edge holds too");
    check(pick::macroIndex(-0.35f, 10, 4) == 3, "well below it moves down");
}

void thePickerFollowsItsSource() {
    const auto& pool = pick::pool();
    const std::string active = pool[3];
    pick::Picker p;
    ls::Layer off = on(ls::Source::User, pool[7]);
    off.on = false;
    check(p.pick(off, active, 0, 1, 0.0f).empty(), "off draws no layer");
    check(p.pick(on(ls::Source::User, pool[7]), active, 0, 1, 0.0f) == pool[7], "the user's pick");
    check(p.pick(on(ls::Source::User, active), active, 0, 1, 0.0f) == pick::suggest(active), "never the style on screen");

    // Auto Director: a suggestion first, then a new style at each style moment and only then.
    pick::Picker d;
    pick::Picker twin;
    const ls::Layer directed = on(ls::Source::Director);
    std::string last = d.pick(directed, active, 0, 42, 0.0f);
    check(last == pick::suggest(active), "Auto Director starts on the suggestion");
    twin.pick(directed, active, 0, 42, 0.0f);
    check(d.pick(directed, active, 0, 42, 0.0f) == last, "no moment, no change");
    std::vector<std::string> seen;
    for (int moment = 0; moment < 12; ++moment) {
        const std::string now = d.pick(directed, active, 1, 42, 0.0f);
        check(now != last && now != active && !now.empty(), "a moment brings another style, never the one on screen");
        check(twin.pick(directed, active, 1, 42, 0.0f) == now, "the same play seed picks the same film");
        seen.push_back(now);
        last = now;
    }
    pick::Picker other;
    other.pick(directed, active, 0, 7, 0.0f);
    bool differs = false;
    for (int moment = 0; moment < 12; ++moment) differs = differs || other.pick(directed, active, 1, 7, 0.0f) != seen[static_cast<size_t>(moment)];
    check(differs, "another play seed picks another film");
    // The style on screen became the layer's (Auto changed style to it): the layer moves on without a moment.
    check(d.pick(directed, last, 0, 42, 0.0f) != last, "never the style on screen");

    // A knob: the list's ends, never the style on screen, no knob is the suggestion.
    pick::Picker k;
    const ls::Layer knob = on(ls::Source::Macro);
    check(k.pick(knob, active, 0, 1, -1.0f) == pool.front(), "a knob turned down picks the list's first style");
    check(k.pick(knob, active, 0, 1, 1.0f) == pool.back(), "turned up, the last");
    check(k.pick(knob, pool.back(), 0, 1, 1.0f) == pool.front(), "the step on the style on screen draws the next one along");
    check(k.pick(knob, active, 0, 1, std::nanf("")) == pick::suggest(active), "no knob on screen picks as nothing picked does");
}

void theEditorHoldsWhatThePageShows() {
    const std::string active = "kifs";
    ls::Layer l = on(ls::Source::User);
    l.knob = 2;
    const std::vector<int> knobs{1, 2};
    const pick::Editor page = pick::editor(l, active, "ignored", -1, knobs);
    check(page.layer == ls::normalized(l), "the layer, as native keeps it");
    check(page.shown == pick::suggest(active), "User shows the style it draws");
    check(pick::editor(on(ls::Source::Director), active, "plasma", -1, knobs).shown == "plasma",
          "Auto Director and a knob show what the renderer drew");
    ls::Layer off = l;
    off.on = false;
    check(pick::editor(off, active, "plasma", -1, knobs).shown.empty(), "off shows none");
    check(page.knob == 1, "the knob, by its place among the knobs on screen");
    l.knob = 9;
    check(pick::editor(l, active, "", -1, knobs).knob == 0, "a knob not on screen is the first");
    check(std::string(pick::sourceLabel(ls::Source::User)) == "You" &&
              std::string(pick::sourceLabel(ls::Source::Director)) == "Auto Director" &&
              std::string(pick::sourceLabel(ls::Source::Macro)) == "Macro",
          "the three sources, as the page names them");

    // Every style but the one on screen, once, across the families the page offers.
    std::map<std::string, int> listed;
    for (int f = 0; f < static_cast<int>(page.families.size()); ++f) {
        const pick::Editor opened = pick::editor(l, active, "", f, knobs);
        check(opened.family == f, "the page's family is the one it asked for");
        check(!opened.styles.empty(), "a family offered has a style to pick");
        for (const std::string& id : opened.styles) ++listed[id];
    }
    check(!listed.count(active), "the style on screen is not offered");
    for (const std::string& id : catalogue::allIds()) {
        if (id != active) check(listed[id] == 1, "every other style is offered once: " + id);
    }

    // Unasked, the picker opens on the family of the style drawn now, with the user's pick found in it.
    const std::string picked = page.shown;
    const ls::Layer chose = on(ls::Source::User, picked);
    const pick::Editor mine = pick::editor(chose, active, "", -1, knobs);
    check(mine.style >= 0 && mine.styles[static_cast<size_t>(mine.style)] == picked, "the pick, by its place in its family");
    check(pick::editor(chose, active, "", static_cast<int>(page.families.size()), knobs).family == mine.family,
          "a family past the list opens on the drawn style's");
    check(pick::editor(on(ls::Source::User), active, "", -1, knobs).style == -1, "nothing picked is no place");
}

}  // namespace

int main() {
    thePoolIsAutosDraw();
    aSuggestionSuitsAndIsNeverTheActiveStyle();
    theUsersPickIsKept();
    theKnobWalksWithADeadBand();
    thePickerFollowsItsSource();
    theEditorHoldsWhatThePageShows();
    if (failures) {
        std::printf("%d layer pick check(s) failed\n", failures);
        return 1;
    }
    std::printf("layer_pick_test: all checks passed\n");
    return 0;
}
