// Host-side checks for which style Customize › Layers draws (core/viz/LayerPick). Built and run by CI ("Native host
// tests" in .github/workflows/native-host-tests.yml) from the repository root, with the system compiler; no GL.
//
// The owner's report: "Layers does not work and is hardcoded with one preset." Asked what the layer should choose
// from: "User chooses. Or director or macro." What is pinned: the layer never draws the style on screen while there
// is another; with nothing picked it suits the style on screen rather than taking the catalogue's first style for
// everyone; the user's pick is kept; Auto Director picks a new one at each style moment and only then, the same
// way for the same play seed; a knob walks a fixed list with a dead band; and the page's rows list every style
// but the one on screen, once, by family, with the sources, the blends and the knobs.
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>
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

std::vector<std::vector<std::string>> rows(const std::string& text, const std::string& tag) {
    std::vector<std::vector<std::string>> out;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        std::vector<std::string> f;
        std::istringstream cells(line);
        for (std::string cell; std::getline(cells, cell, '|');) f.push_back(cell);
        if (line.size() > 0 && line.back() == '|') f.emplace_back();
        if (!f.empty() && f[0] == tag) out.push_back(f);
    }
    return out;
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

void theEditorListsWhatThePageShows() {
    const std::string active = "kifs";
    ls::Layer l = on(ls::Source::User);
    l.knob = 2;
    const std::vector<std::string> blends{"Normal", "Screen"};
    const std::vector<std::pair<int, std::string>> knobs{{1, "Glow"}, {2, "He|at\n"}};
    const std::string text = pick::editorText(l, active, "ignored", blends, knobs);
    check(text.rfind("layers|" + std::to_string(pick::kTextVersion) + "\n", 0) == 0, "the header first");
    const auto state = rows(text, "state");
    check(state.size() == 1 && state[0].size() == 7 && state[0][1] == "1" && state[0][2] == "USER" && state[0][3].empty() &&
              state[0][4] == "2" && state[0][5] == "1" && state[0][6] == "0.5",
          "the state row");
    const auto shown = rows(text, "shown");
    check(shown.size() == 1 && shown[0].size() == 2 && shown[0][1] == pick::suggest(active), "User shows the style it draws");
    const std::string directed = pick::editorText(on(ls::Source::Director), active, "plasma", blends, knobs);
    check(rows(directed, "shown")[0][1] == "plasma", "Auto Director and a knob show what the renderer drew");
    l.on = false;
    check(rows(pick::editorText(l, active, "plasma", blends, knobs), "shown")[0][1].empty(), "off shows none");
    const auto sources = rows(text, "source");
    check(sources.size() == 3 && sources[0][1] == "USER" && sources[1][1] == "DIRECTOR" && sources[2][1] == "MACRO" &&
              sources[0][2] == "You" && sources[1][2] == "Auto Director" && sources[2][2] == "Macro",
          "the three sources, as the page names them");
    const auto blendRows = rows(text, "blend");
    check(blendRows.size() == 2 && blendRows[1][1] == "1" && blendRows[1][2] == "Screen", "the blends, by ordinal");
    std::map<std::string, int> listed;
    for (const auto& family : rows(text, "family")) {
        std::istringstream ids(family[2]);
        for (std::string id; std::getline(ids, id, ',');) ++listed[id];
    }
    check(!listed.count(active), "the style on screen is not offered");
    for (const std::string& id : catalogue::allIds()) {
        if (id != active) check(listed[id] == 1, "every other style is offered once: " + id);
    }
    const auto knobRows = rows(text, "knob");
    check(knobRows.size() == 2 && knobRows[1][1] == "2" && knobRows[1][2] == "He at ", "the knobs, one line each");
}

}  // namespace

int main() {
    thePoolIsAutosDraw();
    aSuggestionSuitsAndIsNeverTheActiveStyle();
    theUsersPickIsKept();
    theKnobWalksWithADeadBand();
    thePickerFollowsItsSource();
    theEditorListsWhatThePageShows();
    if (failures) {
        std::printf("%d layer pick check(s) failed\n", failures);
        return 1;
    }
    std::printf("layer_pick_test: all checks passed\n");
    return 0;
}
