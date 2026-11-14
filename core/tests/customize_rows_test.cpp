// Host-side checks for what Customize draws (core/viz/CustomizeRows, PanelText, StyleNames): every control the
// map places draws as one widget over a live param its meta row names, hides where the panel hid it, and carries
// the words, range, default, unit and steps the shell used to hold. Built and run by CI
// (.github/workflows/native-host-tests.yml, "Customize rows host tests"); no GL.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "api/geode_api.h"
#include "viz/Catalogue.hpp"
#include "viz/CustomizeMap.hpp"
#include "viz/CustomizeRows.hpp"
#include "viz/PanelText.hpp"
#include "viz/StyleNames.hpp"

using namespace geode::viz;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

std::vector<std::string> split(const std::string& line, char by) {
    std::vector<std::string> out;
    std::string cell;
    std::istringstream in(line);
    while (std::getline(in, cell, by)) out.push_back(cell);
    if (!line.empty() && line.back() == by) out.emplace_back();
    return out;
}

std::vector<std::vector<std::string>> rowsOf(const std::string& keys, const SceneParams& p, const customize::RowContext& ctx) {
    std::vector<std::vector<std::string>> out;
    std::istringstream lines(customize::rows(keys, p, ctx));
    for (std::string line; std::getline(lines, line);) out.push_back(split(line, '|'));
    return out;
}

const std::vector<std::string>* rowFor(const std::vector<std::vector<std::string>>& rows, const std::string& type, const std::string& key) {
    for (const auto& r : rows) {
        if (r.size() > 1 && r[0] == type && r[1] == key) return &r;
    }
    return nullptr;
}

customize::RowContext on(const std::string& styleId) {
    customize::RowContext ctx;
    ctx.styleId = styleId;
    return ctx;
}

// The map and this table name the same controls, and each control's field is a live row its meta labels.
void everyPlacedControlHasOneWidget() {
    std::set<std::string> placed;
    for (const auto& section : customize::sections()) placed.insert(section.controls.begin(), section.controls.end());
    std::set<std::string> drawn;
    for (const auto& c : customize::controls()) {
        check(drawn.insert(c.key).second, std::string("one widget per control: ") + c.key);
        check(placed.count(c.key) == 1, std::string("a drawn control is placed: ") + c.key);
        if (c.widget == customize::Widget::Piece) {
            check(c.label != nullptr && (std::strlen(c.label) == 0 || std::strlen(panel::text(c.label)) > 0),
                  std::string("a piece's words are in PanelText: ") + c.key);
            continue;
        }
        const int slot = SceneParams::slotOf(c.field);
        check(slot >= 0 && !SceneParams::retired(static_cast<ParamSlot>(slot)), std::string("a live field: ") + c.field);
        if (slot >= 0) {
            check(std::strcmp(SceneParams::meta(static_cast<ParamSlot>(slot)).label, c.key) == 0,
                  std::string(c.key) + " is the meta label of " + c.field);
        }
    }
    for (const auto& key : placed) check(drawn.count(key) == 1, "a placed control draws: " + key);
}

void slidersCarryTheirRangeDefaultUnitAndSteps() {
    const SceneParams p;
    const auto rows = rowsOf("Kaleidoscope\nGlide time\nFundamental (Hz)\nBloom\nSpawn points", p, on(""));
    check(rows.size() == 6 && rows[0][0] == "rows" && rows[0][1] == std::to_string(customize::kRowsVersion), "header and five rows");
    const auto* k = rowFor(rows, "SLIDER", "Kaleidoscope");
    check(k && (*k)[2] == "kaleidoFolds" && (*k)[4] == "1" && (*k)[5] == "1" && (*k)[6] == "16" && (*k)[7] == "1" && (*k)[8] == "-1" &&
              (*k)[9] == "14",
          "Kaleidoscope: 1..16 whole folds, 14 ticks between");
    const auto* g = rowFor(rows, "SLIDER", "Glide time");
    check(g && (*g)[8] == "2", "Glide time reads in seconds");
    const auto* f = rowFor(rows, "SLIDER", "Fundamental (Hz)");
    check(f && (*f)[3] == "Base note (Hz)" && (*f)[7].empty(), "Fundamental: its words, no default");
    const auto* b = rowFor(rows, "SLIDER", "Bloom");
    check(b && (*b)[3] == "Glow" && (*b)[6] == "0.6", "Bloom reads Glow, stops at the bloom cap");
    const auto* s = rowFor(rows, "INT", "Spawn points");
    check(s && s->size() == 7, "Spawn points is a whole-number slider");
}

void switchesHideWhatTheyGate() {
    SceneParams p;
    const std::string catches = "Catch points\nCatch pull\nCatch radius";
    p.fluidCatchPoints = 0;
    check(!rowFor(rowsOf(catches, p, on("fluid")), "SLIDER", "Catch pull"), "no Catch pull without catch points");
    p.fluidCatchPoints = 2;
    check(rowFor(rowsOf(catches, p, on("fluid")), "SLIDER", "Catch pull") != nullptr, "Catch pull with catch points");

    const std::string liquid = "Flow field on\nFlow strength\nFlow force\nFlow curl";
    p.flowEnabled = false;
    auto rows = rowsOf(liquid, p, on("julia"));
    check(rowFor(rows, "CHECK", "Flow field on") && (*rowFor(rows, "CHECK", "Flow field on"))[3] == "Liquid mode", "Liquid's switch, worded");
    check(!rowFor(rows, "SLIDER", "Flow strength"), "Liquid's sliders hide with Liquid off");
    p.flowEnabled = true;
    check(rowFor(rowsOf(liquid, p, on("julia")), "SLIDER", "Flow curl") != nullptr, "Liquid's sliders with Liquid on");

    p.endlessZoom = false;
    p.flowEnabled = false;
    check(!rowFor(rowsOf("Dive speed", p, on("julia")), "SLIDER", "Dive speed"), "no Dive speed with nothing diving");
    p.flowEnabled = true;
    check(rowFor(rowsOf("Dive speed", p, on("julia")), "SLIDER", "Dive speed") != nullptr, "Liquid dives on any style");

    p.fluidBloom = false;
    check(!rowFor(rowsOf("Fluid glow", p, on("fluid")), "SLIDER", "Fluid glow"), "no glow amount with Fluid's glow off");
    p.fluidSunrays = true;
    check(rowFor(rowsOf("Sunrays weight", p, on("fluid")), "SLIDER", "Sunrays weight") != nullptr, "Sunray strength with sunrays");
}

void theStyleDecidesWhatShows() {
    const SceneParams p;
    check(rowFor(rowsOf("Fluid curl", p, on("fluid")), "SLIDER", "Fluid curl") != nullptr, "Fluid curl on Fluid");
    check(!rowFor(rowsOf("Fluid curl", p, on("julia")), "SLIDER", "Fluid curl"), "no Fluid curl on a shader style");
    check(rowsOf("Geometry", p, on("julia")).size() == 1, "no plate on a shader style");
    for (const char* id : {"cymatics", "chladni_sand", "bessel_drum"}) {
        const auto rows = rowsOf("Geometry", p, on(id));
        const bool held = catalogue::overrideOf(id, "Geometry").has_value();
        check(held ? rowFor(rows, "HINT", "Geometry") != nullptr : rowFor(rows, "CHIPS", "Geometry") != nullptr,
              std::string(id) + ": a held plate is a tip, a free one a pick");
    }
    const auto plate = rowsOf("Geometry", p, on("chladni_sand"));
    const auto* hint = rowFor(plate, "HINT", "Geometry");
    check(hint && (*hint)[2] == "Shape set by this style: Chladni plate.", "the tip names the plate");
    // The stack draws on the shader styles; elsewhere the look's palette is one list.
    check(rowFor(rowsOf("Palette", p, on("julia")), "PIECE", "@palette_stack") != nullptr, "the stack on a shader style");
    check(rowFor(rowsOf("Palette", p, on("fluid")), "PIECE", "@palette_list") != nullptr, "the palette list on Fluid");
}

void searchHidesWholePagesAndMatchesWords() {
    const SceneParams p;
    customize::RowContext ctx = on("julia");
    const std::string keys = "Palette\n@waves\n@palette_maker\nReaction DNA";
    auto rows = rowsOf(keys, p, ctx);
    check(rowFor(rows, "PIECE", "@art_palette") && rowFor(rows, "PIECE", "@waves") && rowFor(rows, "PIECE", "@palette_maker") &&
              rowFor(rows, "PIECE", "@personality"),
          "every piece with no search");
    const auto* waves = rowFor(rows, "PIECE", "@waves");
    check(waves && (*waves)[3] == "Waves" && (*waves)[4] == "Randomize waves", "Waves and its button, worded");
    ctx.query = "pers";
    rows = rowsOf(keys, p, ctx);
    check(!rowFor(rows, "PIECE", "@art_palette") && !rowFor(rows, "PIECE", "@waves") && !rowFor(rows, "PIECE", "@palette_maker"),
          "a search hides the whole pages");
    check(rowFor(rows, "PIECE", "@personality") != nullptr, "Personality is found by its words");
    check(!rowFor(rows, "PIECE", "@palette_stack"), "Palette is not found by 'pers'");
    check(customize::matches(" PAL ", "Palette") && !customize::matches("xyz", "Palette") && customize::matches("", "x"), "case-blind match");
}

void shellSlidersAndNotes() {
    const SceneParams p;
    customize::RowContext ctx = on("");
    check(!rowFor(rowsOf("@transition_length", p, ctx), "SHELL", "Transition length"), "no length without a transition");
    ctx.transition = true;
    const auto length = rowsOf("@transition_length", p, ctx);
    const auto* t = rowFor(length, "SHELL", "Transition length");
    check(t && (*t)[2] == "TRANSITION" && (*t)[4] == "0.3" && (*t)[5] == "5" && (*t)[6] == "2", "the length: 0.3..5 s");
    const auto rise = rowsOf("@rise_fall", p, ctx);
    check(rowFor(rise, "SHELL", "Rise") && rowFor(rise, "SHELL", "Fall"), "rise and fall");
    ctx.liquidMode = 1;
    check(!rowFor(rowsOf("Flow field on", p, ctx), "NOTE", "Flow field on"), "no note at full Liquid");
}

void wordsAndNames() {
    check(panel::format("palette_stop", {"2", "5", "#FF8800"}) == "Colour 2 of 5, #FF8800", "a placeholder row fills");
    check(panel::format("palette_stack_remove", {}) == "Remove ", "a placeholder past the arguments drops");
    check(std::string(panel::text("no_such_id")).empty(), "an unknown id is empty");
    std::istringstream ids(panel::ids());
    std::set<std::string> seen;
    for (std::string id; std::getline(ids, id);) {
        check(seen.insert(id).second, "a word listed once: " + id);
        check(std::strlen(panel::text(id)) > 0 && !std::strchr(panel::text(id), '|'), "a word that crosses a row: " + id);
    }
    check(catalogue::styleLabel("silk_web") == "Halvorsen Web", "a named style");
    check(catalogue::styleLabel("neon_city") == "Neon City", "an id in words");
    check(catalogue::familyLabel("foam") == "Sphere Foam" && catalogue::familyLabel("arbor") == "Arbor", "family names");
    check(std::string(geode_style_label("fluid")) == "Original · Dye", "the C ABI's style name");
    check(std::strcmp(geode_panel_text("palette_save"), "Save palette") == 0, "the C ABI's words");
}

// The panel's tab titles and group headers, which the shell used to hold.
void titlesAreTheMapsOwn() {
    check(std::string(customize::title(customize::Category::QuickKnobs)) == "Quick knobs", "Quick knobs' title");
    check(customize::sectionTitle("colour_detail") == "Colour detail", "a section title");
    check(customize::sectionTitle("fluid_layers") == "Fluid: layers", "Fluid: layers");
    check(customize::sectionTitle("some_new_one") == "Some new one", "an untitled section reads as its words");
    // Every section has words of its own in the table: none falls back to its id.
    for (const auto& section : customize::sections()) {
        const std::string title = customize::sectionTitle(section.id);
        check(!title.empty() && title.find('_') == std::string::npos && title.find('|') == std::string::npos,
              std::string("a titled section: ") + section.id);
    }
    std::istringstream text(customize::text());
    std::string head;
    std::getline(text, head);
    check(head == "customize|" + std::to_string(customize::kTextVersion), "the map's header");
    std::string row;
    std::getline(text, row);
    check(row == "category|QUICK_KNOBS|ALWAYS|Quick knobs", "a category row carries its title");
}

// The owner's layout (was the shell's CustomizeMapTableTest): the Basics a listener reaches for first.
void theOwnersLayoutHolds() {
    const auto controlsOf = [](const char* category, const char* id) {
        std::vector<std::string> out;
        for (const auto& section : customize::sections()) {
            if (std::string(customize::name(section.category)) == category && std::string(section.id) == id) {
                out.assign(section.controls.begin(), section.controls.end());
            }
        }
        return out;
    };
    check(controlsOf("LOOK", "basics") == std::vector<std::string>{"Palette", "Brightness", "Saturation", "Bloom"}, "Look's Basics");
    check(controlsOf("MOTION", "basics") == std::vector<std::string>{"Speed", "Zoom", "Rotation"}, "Motion's Basics");
    check(controlsOf("CAMERA", "basics") == std::vector<std::string>{"Camera path", "Fly speed"}, "Camera's Basics");
    check(controlsOf("SHAPE", "stirrers") == std::vector<std::string>{"Stirrers", "Stirrer speed"}, "Shape's stirrers");
    const auto colour = controlsOf("LOOK", "colour_detail");
    check(std::find(colour.begin(), colour.end(), "Hue shift") != colour.end(), "Hue shift in Look's colour detail");
}

}  // namespace

int main() {
    theOwnersLayoutHolds();
    everyPlacedControlHasOneWidget();
    slidersCarryTheirRangeDefaultUnitAndSteps();
    switchesHideWhatTheyGate();
    theStyleDecidesWhatShows();
    searchHidesWholePagesAndMatchesWords();
    shellSlidersAndNotes();
    wordsAndNames();
    titlesAreTheMapsOwn();
    if (failures == 0) std::printf("customize_rows_test: all checks passed\n");
    return failures;
}
