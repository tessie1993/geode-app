// Host-side checks for Customize › Look's palette pages (core/viz/PalettePanel): Your colours' stop editor and its
// limits, the stack card's rows, the palette list's choice, and the C ABI over them. Built and run by CI
// (.github/workflows/native-host-tests.yml, "Customize rows host tests"); no GL.
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "api/geode_api.h"
#include "viz/Colour.hpp"
#include "viz/Palette.hpp"
#include "viz/PalettePanel.hpp"
#include "viz/Params.hpp"

using namespace geode::viz;
namespace pp = geode::viz::palettepanel;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

std::vector<std::string> linesOf(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) out.push_back(line);
    return out;
}

int count(const std::string& text, const std::string& prefix) {
    int n = 0;
    for (const auto& line : linesOf(text)) n += line.rfind(prefix, 0) == 0 ? 1 : 0;
    return n;
}

void theEditorKeepsOneToFiveStops() {
    const int red = 0xFF0000;
    pp::Maker m = pp::open(&red, 1, 0.0f, 0.0f);
    check(m.stops.size() == 1 && m.selected == 0, "a palette of one colour opens as one stop");
    m = pp::edit(m, pp::Edit::Remove, 0);
    check(m.stops.size() == 1, "the last stop is never removed");
    for (int i = 0; i < 9; ++i) m = pp::edit(m, pp::Edit::Add, 0);
    check(static_cast<int>(m.stops.size()) == colour::kMaxStops && m.selected == colour::kMaxStops - 1, "Add stops at five, the new one selected");
    m = pp::edit(m, pp::Edit::Select, 1);
    m = pp::edit(m, pp::Edit::Set, 0x00FF00);
    check(m.stops[1] == 0x00FF00 && m.stops[0] == red, "Set paints the selected stop alone");
    m = pp::edit(m, pp::Edit::Set, -5);
    check(m.stops[1] == 0x00FF00, "a colour that is none changes nothing");
    m = pp::edit(m, pp::Edit::Select, 4);
    m = pp::edit(m, pp::Edit::Remove, 0);
    check(m.stops.size() == 4 && m.selected == 3, "removing the last stop selects the one before");
    m = pp::edit(m, pp::Edit::Select, 99);
    check(m.selected == 3, "a select past the end holds to the last stop");
    const pp::Maker back = pp::decodeMaker(pp::encode(m));
    check(back.stops == m.stops && back.selected == m.selected, "the state round-trips as text");
    check(pp::decodeMaker("garbage").stops == std::vector<int>{colour::kWhite}, "unreadable text is one white stop");
}

void anOldGradientOpensAsFourStops() {
    const pp::Maker m = pp::open(nullptr, 0, 0.2f, 0.5f);
    check(static_cast<int>(m.stops.size()) == pp::kMakerHueStops, "a hue gradient opens as four stops");
    check(static_cast<int>(pp::stackColours(nullptr, 0, 0.2f, 0.5f).size()) == pp::kStackHueStops, "and goes on a stack as five");
    const int two[] = {0x112233, 0x445566};
    check(pp::stackColours(two, 2, 0.2f, 0.5f) == std::vector<int>({0x112233, 0x445566}), "a palette's own stops go on as they are");
}

void theEditorViewSaysWhatItOffers() {
    const int one = 0xFF8800;
    const std::string view = pp::makerView(pp::open(&one, 1, 0.0f, 0.0f));
    const auto lines = linesOf(view);
    check(lines.size() == 2, "a header and one stop");
    check(lines[0].rfind("maker|1|0|0|1|FF8800,", 0) == 0, "one stop: nothing to remove, room to add, a strip of it");
    check(lines[1] == "stop|0|FF8800|1|Colour 1 of 1, #FF8800", "the stop's row and what a screen reader hears");
}

void theStackCardShowsWhatTheStackHas() {
    const SceneParams p;
    const std::string none = pp::stackCard("", p, 0);
    check(count(none, "card|") == 1 && count(none, "layer|") == 0 && count(none, "blend|") == 0, "an unset stack: heading, no layers");
    check(count(none, "add|") == 1 && count(none, "pick|") > 0 && count(none, "yours|") == 0, "room to add, none of yours without one saved");

    std::string stack = geode_palette_stack_add_library(geode_palette_stack_unset(), 0);
    stack = geode_palette_stack_add_library(stack.c_str(), 1);
    const std::string two = pp::stackCard(stack, p, 2);
    check(count(two, "layer|") == 2 && count(two, "blend|") == 1 && count(two, "blendoption|") == 2, "two layers: their rows and the blend");
    check(count(two, "yours|") == 1, "Add one of yours with one saved");
    const auto lines = linesOf(two);
    check(lines[1].rfind("layer|0|", 0) == 0 && lines[1].find("|paletteAmount0|") != std::string::npos, "the first layer reads paletteAmount0");
    check(lines[1].find("|0|Move ") != std::string::npos && lines[2].find("|1|Move ") != std::string::npos,
          "only a layer under another moves up");
}

void theListPicksWhatTheLookHolds() {
    SceneParams p;
    p.palette = 3;
    auto lines = linesOf(pp::listView(p, "", "dusk\ndawn", "Dusk\nDawn"));
    check(lines[0].rfind("list|1|3|", 0) == 0, "a built-in palette is its own row");
    check(count(pp::listView(p, "", "dusk\ndawn", "Dusk\nDawn"), "option|") == SceneParams::kListedPalettes + 2, "the listed built-ins, then yours");
    p.palette = SceneParams::kListedPalettes + 1;
    check(linesOf(pp::listView(p, "", "", ""))[0].rfind("list|1|-1|", 0) == 0, "a retired built-in has no row");
    p.paletteStop0 = 0xFF0000;
    lines = linesOf(pp::listView(p, "dawn", "dusk\ndawn", "Dusk\nDawn"));
    check(lines[0].rfind("list|1|" + std::to_string(SceneParams::kListedPalettes + 1) + "|", 0) == 0, "a saved palette after the built-ins");
}

void theCAbiCarriesTheState() {
    std::string state = geode_palette_maker_open(nullptr, 0, 0.0f, 0.25f);
    state = geode_palette_maker_edit(state.c_str(), 2, 0);
    int out[8] = {};
    check(geode_palette_maker_stops(state.c_str(), out, 8) == pp::kMakerHueStops + 1, "the stops after an Add");
    check(geode_palette_maker_stops(state.c_str(), out, 2) == 2, "the stops held to the room given");
    int saved[8] = {};
    check(geode_palette_saved_colours(nullptr, 0, 0.1f, 0.3f, saved, 8) == pp::kStackHueStops, "a saved gradient's colours");
    check(static_cast<int>(std::string(geode_palette_saved_swatch(nullptr, 0, 0.1f, 0.3f)).size()) == pp::kSwatchSamples * 7 - 1, "a strip of 24");
}

}  // namespace

int main() {
    theEditorKeepsOneToFiveStops();
    anOldGradientOpensAsFourStops();
    theEditorViewSaysWhatItOffers();
    theStackCardShowsWhatTheStackHas();
    theListPicksWhatTheLookHolds();
    theCAbiCarriesTheState();
    if (failures == 0) std::printf("palette_panel_test: all checks passed\n");
    return failures;
}
