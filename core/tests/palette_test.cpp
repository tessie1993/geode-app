// The palette stack (viz/Palette, Customize › Look): each gradient option, the 16-colour and 8-palette caps,
// Share and Mix, the mirror of one palette, one colour's three drawings, the text codec and the edits, an old
// look's colours drawn as they were, the recent colours, and the library text against the fixture.
//
//   palette_test core/tests/palettes.txt           checks, including libraryText() against the fixture
//   palette_test --write core/tests/palettes.txt   rewrites the fixture after a deliberate change
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "api/geode_api.h"
#include "viz/Colour.hpp"
#include "viz/Palette.hpp"
#include "viz/Params.hpp"

using namespace geode::viz;
namespace pl = geode::viz::palette;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

bool near(colour::Rgb a, colour::Rgb b, float eps = 1.0f / 255.0f) {
    return std::fabs(a.r - b.r) <= eps && std::fabs(a.g - b.g) <= eps && std::fabs(a.b - b.b) <= eps;
}

int packed(colour::Rgb c) { return colour::pack(c); }

pl::Palette two(int a, int b) {
    const int c[] = {a, b};
    return pl::fromColours(pl::Source::Yours, "", "Two", c, 2);
}

pl::Palette one(int c, const char* single) {
    pl::Palette p = pl::fromColours(pl::Source::Yours, "", "One", &c, 1);
    pl::setOption(p, single);
    return p;
}

void oklabRoundTrips() {
    for (int rgb : {0xFF0000, 0x00FF00, 0x0000FF, 0x12AB7F, 0x808080, 0xFFFFFF, 0x000000}) {
        check(packed(colour::fromOklab(colour::toOklab(colour::unpack(rgb)))) == rgb, "OKLab round trips " + colour::hex(rgb));
    }
    check(std::fabs(colour::toOklab(colour::unpack(0xFFFFFF)).l - 1.0f) < 1e-3f, "white is lightness 1");
}

void theLibraryKeepsTheOldIndices() {
    const auto& lib = pl::library();
    check(lib.size() == 34, "21 classic ramps, 5 colour maps, 8 newer palettes");
    for (int i = 0; i < pl::kClassicCount; ++i) {
        check(lib[static_cast<size_t>(i)].palette.name == SceneParams::palettes()[static_cast<size_t>(i)].name,
              "library row " + std::to_string(i) + " is old palette " + std::to_string(i));
    }
    check(lib[pl::kColourMapFirst].id == "bamo" && lib[pl::kColourMapFirst + 4].id == "viko", "the colour maps follow, in LUT row order");
    check(std::string(geode_palette_colour_map_name(0)) == "bamO" && std::string(geode_palette_colour_map_name(4)) == "vikO" &&
              std::string(geode_palette_colour_map_name(5)).empty() && std::string(geode_palette_colour_map_name(-1)).empty(),
          "the shell's colour map names are the library's, in paletteLut order");
    for (size_t i = 0; i < lib.size(); ++i) {
        const auto& p = lib[i].palette;
        check(pl::valid(p) && static_cast<int>(p.stops.size()) <= pl::kMaxStops, lib[i].id + " is a palette of at most 16 colours");
        check(pl::libraryIndex(lib[i].id) == static_cast<int>(i), lib[i].id + ": one id, one row");
    }
    check(pl::libraryIndex("nothing") == -1, "an unknown id has no row");
    check(lib[0].palette.wrap == pl::Wrap::Loop, "Spectrum comes round the whole wheel, so it loops");
    check(lib[2].palette.wrap == pl::Wrap::Mirror, "Fire runs once and mirrors, like a new palette");
    // A colour map row is the LUT's entry 16·i.
    check(lib[pl::kColourMapFirst + 3].palette.stops[8].rgb == 0xCBE1B3, "romaO's ninth stop is LUT entry 128");
}

// The old ramps as stops: on each stop exactly the old cosine, between stops within the chord's sag.
void classicRampsDrawAsTheyDid() {
    for (int i = 0; i < pl::kClassicCount; ++i) {
        const auto& old = SceneParams::palettes()[static_cast<size_t>(i)];
        const pl::Palette p = pl::fromHueRamp(old.name, pl::Source::BuiltIn, "", old.base, old.range);
        check(p.wrap == pl::Wrap::Loop && p.space == pl::Space::Rgb && p.stops.size() == 16, std::string(old.name) + " loops in RGB");
        // Only Spectrum comes round whole turns; every other ramp ends on base + range, not back on base.
        const bool periodic = i == 0;
        check(p.stops.back().pos == (periodic ? 15.0f / 16.0f : 1.0f), std::string(old.name) + ": where its last stop sits");
        for (const pl::Stop& stop : p.stops) {
            check(near(pl::at(p, stop.pos == 1.0f ? 0.999999f : stop.pos), colour::procedural(old.base, old.range, stop.pos), 1.0f / 255.0f),
                  std::string(old.name) + " on its stop at " + std::to_string(stop.pos) + " is the old cosine");
        }
        for (int k = 0; k < 256; ++k) {
            const float t = static_cast<float>(k) / 256.0f;
            check(near(pl::at(p, t), colour::procedural(old.base, old.range, t), 4.0f / 255.0f),
                  std::string(old.name) + " at " + std::to_string(t) + " is the old cosine");
        }
        check(near(pl::at(p, 0.999f), colour::procedural(old.base, old.range, 0.999f), 4.0f / 255.0f),
              std::string(old.name) + " just short of 1 is the ramp's end, not its start");
    }
}

void gradientOptions() {
    pl::Palette p = two(0xFF0000, 0x0000FF);
    check(p.wrap == pl::Wrap::Mirror && p.space == pl::Space::Oklab && p.interp == pl::Interp::Smooth, "a new palette mirrors, smooth, in OKLab");
    check(p.stops[0].pos == 0.0f && p.stops[1].pos == 1.0f, "two stops spread end to end for a mirror");

    // Mirror: forward over the first half, back over the second, so t and 1 - t meet.
    check(packed(pl::at(p, 0.0f)) == 0xFF0000 && packed(pl::at(p, 0.5f)) == 0x0000FF, "mirror: the first stop at 0, the last at 0.5");
    for (float t : {0.1f, 0.2f, 0.37f}) check(near(pl::at(p, t), pl::at(p, 1.0f - t)), "mirror: t and 1 - t are one colour");

    // The colour space of the blend.
    pl::setOption(p, "RGB");
    check(packed(pl::at(p, 0.25f)) == 0x800080, "RGB: halfway is the mean");
    pl::setOption(p, "HUE");
    check(packed(pl::at(p, 0.25f)) == 0xFF00FF, "hue: halfway from red to blue the short way round is magenta");
    pl::setOption(p, "OKLAB");
    const int lab = packed(pl::at(p, 0.25f));
    check(lab != 0x800080 && lab != 0xFF00FF, "OKLab blends a third way");
    const colour::Rgb mid = colour::unpack(lab);
    check(mid.r > mid.g && mid.b > mid.g, "OKLab: still a purple");

    // Steps: the nearest stop's colour, no blend.
    pl::setOption(p, "STEPS");
    check(packed(pl::at(p, 0.2f)) == 0xFF0000 && packed(pl::at(p, 0.3f)) == 0x0000FF, "steps: the nearest stop wins");
    pl::setOption(p, "SMOOTH");

    // Loop: once round, the last stop blending back into the first; changing the wrap respreads even stops.
    check(pl::setOption(p, "LOOP") && p.stops[1].pos == 0.5f, "loop spreads two stops at 0 and 1/2");
    pl::setOption(p, "RGB");
    check(packed(pl::at(p, 0.5f)) == 0x0000FF && packed(pl::at(p, 0.75f)) == 0x800080, "loop: the last stop blends back into the first");
    check(near(pl::at(p, 0.999f), colour::unpack(0xFF0000), 0.01f), "loop: no seam at the wrap");
    check(!pl::setOption(p, "SIDEWAYS"), "an unknown option is refused");

    // A stop's own position moves where its colour lands.
    pl::Palette moved = pl::setStop(two(0x000000, 0xFFFFFF), 1, 0xFFFFFF, 0.5f);
    pl::setOption(moved, "RGB");
    check(packed(pl::along(moved, 0.5f)) == 0xFFFFFF && packed(pl::along(moved, 0.25f)) == 0x808080, "a stop at 0.5: white from halfway");
    check(packed(pl::along(moved, 0.9f)) == 0xFFFFFF, "past the last stop its colour holds");
    pl::Palette unsorted = pl::setStop(moved, 0, 0x000000, 0.75f);
    check(unsorted.stops[0].pos == 0.75f, "stops keep the order they were made in");
    check(packed(pl::along(unsorted, 0.25f)) == 0xFFFFFF && packed(pl::along(unsorted, 1.0f)) == 0x000000, "drawing sorts by position");
}

void oneColourDraws() {
    const int teal = 0x1F8A9E;
    const pl::Palette shades = one(teal, "SHADES");
    check(shades.single == pl::Single::Shades && shades.wrap == pl::Wrap::Mirror, "one colour: shades, mirrored");
    check(near(pl::along(shades, 0.5f), colour::unpack(teal)), "shades: the colour itself halfway");
    const float dark = colour::toOklab(pl::along(shades, 0.0f)).l;
    const float self = colour::toOklab(colour::unpack(teal)).l;
    const float light = colour::toOklab(pl::along(shades, 1.0f)).l;
    check(dark < self && self < light, "shades run dark to light");
    const auto hueOf = [](colour::Rgb c) { return colour::toHsv(c)[0]; };
    check(std::fabs(hueOf(pl::along(shades, 0.0f)) - hueOf(colour::unpack(teal))) < 0.05f &&
              std::fabs(hueOf(pl::along(shades, 1.0f)) - hueOf(colour::unpack(teal))) < 0.05f,
          "shades keep the colour's hue");
    const pl::Palette flat = one(teal, "FLAT");
    for (float t : {0.0f, 0.3f, 0.8f}) check(packed(pl::at(flat, t)) == teal, "flat: the colour everywhere");
    const pl::Palette toBlack = one(teal, "TO_BLACK");
    check(packed(pl::along(toBlack, 0.0f)) == teal && packed(pl::along(toBlack, 1.0f)) == 0x000000, "colour to black");
    check(packed(pl::at(toBlack, 0.5f)) == 0x000000 && packed(pl::at(toBlack, 0.0f)) == teal, "to black, mirrored back");
}

void theCaps() {
    std::vector<int> many;
    for (int i = 0; i < 20; ++i) many.push_back(0x101010 * (i % 15) + i);
    const pl::Palette p = pl::fromColours(pl::Source::Yours, "", "Many", many.data(), 20);
    check(p.stops.size() == 16, "at most 16 colours");
    check(pl::addStop(p, 0xFFFFFF, -1.0f).stops.size() == 16, "a 17th colour is refused");
    const int bad[] = {-1, 0x1000000};
    check(!pl::valid(pl::fromColours(pl::Source::Yours, "", "None", bad, 2)), "no valid colour, no palette");

    pl::Stack s;
    for (int i = 0; i < 10; ++i) s = pl::addLayer(s, two(0x100000 * i, 0xFFFFFF));
    check(s.layers.size() == 8, "the primary and seven more");
    check(pl::setLayer(s, 8, two(0, 1)).layers.size() == 8, "a ninth layer is refused");
}

void shareAndMix() {
    const pl::Palette red = one(0xFF0000, "FLAT");
    const pl::Palette blue = one(0x0000FF, "FLAT");
    pl::Stack s = pl::addLayer(pl::addLayer({}, red), blue);
    check(s.blend == pl::Blend::Share && s.edge == pl::Edge::Soft && !s.fullRange, "Share with soft edges by default, colour moves held to the stack");
    s.edge = pl::Edge::Hard;
    const pl::Amounts defaults = pl::defaultAmounts();
    check(defaults[0] == 1.0f && defaults[1] == 0.5f && defaults[7] == 0.5f, "the primary at 1, the rest at 0.5");
    SceneParams look;
    look.paletteAmount0 = 0.25f;
    look.paletteAmount3 = 0.75f;
    look.paletteAmount7 = 0.0f;
    const pl::Amounts read = pl::amountsOf(look);
    check(read[0] == 0.25f && read[3] == 0.75f && read[7] == 0.0f && read[1] == 0.5f, "a look's amounts are its paletteAmount rows, in layer order");
    // Share: red takes 1 / 1.5 of the ramp, blue the rest, both pure.
    check(packed(pl::at(s, defaults, 0.6f)) == 0xFF0000 && packed(pl::at(s, defaults, 0.7f)) == 0x0000FF, "share: each its part, pure");
    pl::Amounts zero{};
    check(packed(pl::at(s, zero, 0.9f)) == 0xFF0000, "every amount at 0 draws the primary");
    pl::Amounts nan = defaults;
    nan[1] = std::nanf("");
    check(packed(pl::at(s, nan, 0.9f)) == 0xFF0000, "a non-finite amount reads 0");
    pl::Amounts only2{};
    only2[1] = 1.0f;
    check(packed(pl::at(s, only2, 0.1f)) == 0x0000FF, "a palette at 0 takes no share");

    // Hard: a sharp line, red up to it and blue from it.
    float hardJump = 0.0f;
    for (int i = 0; i < 2000; ++i) {
        const colour::Rgb a = pl::at(s, defaults, static_cast<float>(i) / 2000.0f);
        const colour::Rgb b = pl::at(s, defaults, static_cast<float>(i + 1) / 2000.0f);
        hardJump = std::max(hardJump, std::fabs(a.r - b.r));
    }
    check(hardJump > 0.99f, "hard: the colours change at a line");

    // Soft: continuous all round (t = 1 meets t = 0 too), half each on the line, pure at each share's centre.
    s.edge = pl::Edge::Soft;
    float softStep = 0.0f;
    for (int i = 0; i < 2000; ++i) {
        const colour::Rgb a = pl::at(s, defaults, static_cast<float>(i) / 2000.0f);
        const colour::Rgb b = pl::at(s, defaults, static_cast<float>(i + 1) / 2000.0f);
        softStep = std::max({softStep, std::fabs(a.r - b.r), std::fabs(a.g - b.g), std::fabs(a.b - b.b)});
    }
    check(softStep < 0.02f, "soft: no step anywhere in the ramp");
    check(near(pl::at(s, defaults, 2.0f / 3.0f), {0.5f, 0.0f, 0.5f}, 2.0f / 255.0f), "soft: half each on the line");
    check(packed(pl::at(s, defaults, 1.0f / 3.0f)) == 0xFF0000 && packed(pl::at(s, defaults, 5.0f / 6.0f)) == 0x0000FF,
          "soft: each palette pure at its share's centre");
    check(packed(pl::at(s, defaults, 0.6f)) == 0xFF0000, "soft: the fade reaches no further than kSoftReach");
    pl::Amounts small{};
    small[0] = 1.0f;
    small[1] = 0.02f;
    check(packed(pl::at(s, small, (1.0f + 0.01f) / 1.02f)) == 0x0000FF && packed(pl::at(s, small, 0.5f)) == 0xFF0000,
          "soft: a small share keeps its centre pure");

    s.blend = pl::Blend::Mix;
    pl::Amounts even{};
    even[0] = 1.0f;
    even[1] = 1.0f;
    check(packed(pl::at(s, even, 0.3f)) == 0x800080, "mix: blended by amount");
    const auto ramp = pl::compose(s, even);
    check(static_cast<int>(ramp.size()) == pl::kRampSize, "the ramp is 256 colours");
}

void aPaletteAloneIsMirrored() {
    const int c[] = {0x2D00F7, 0xB100E8, 0xFFD500};
    const pl::Stack s = pl::addLayer({}, pl::fromColours(pl::Source::Yours, "", "Mine", c, 3));
    const auto ramp = pl::compose(s, pl::defaultAmounts(), 64);
    for (int i = 0; i < 32; ++i) check(near(ramp[static_cast<size_t>(i)], ramp[static_cast<size_t>(63 - i)]), "the primary alone is doubled in mirror");
    check(near(pl::at(s, pl::defaultAmounts(), 0.5f), colour::unpack(0xFFD500)), "its last colour at the fold");
}

void unsetDrawsTheStyleDefault() {
    const pl::Stack unset{};
    check(pl::isUnset(unset), "no layers: the style's own palette");
    check(packed(pl::at(unset, pl::defaultAmounts(), 0.0f)) == packed(colour::procedural(0.0f, 1.0f, 0.0f)), "until styles have theirs, Spectrum");
    const pl::Palette style = pl::fromColours(pl::Source::Style, "neon_chrome_sea", "Neon Chrome Sea", std::vector<int>{0x00FFEE, 0xFF00AA}.data(), 2);
    const pl::Stack resolved = pl::resolveDefault(unset, style);
    check(resolved.layers.size() == 1 && resolved.layers[0].source == pl::Source::Style && resolved.layers[0].ref == "neon_chrome_sea",
          "an unset stack takes the style's palette, named after it");
    const pl::Stack mine = pl::addLayer({}, two(0xFF0000, 0x00FF00));
    check(pl::resolveDefault(mine, style) == mine, "a stack the user set stays: a default, never a lock");
    check(pl::isUnset(pl::removeLayer(mine, 0)), "removing the last palette goes back to the style's");
}

void theTextRoundTrips() {
    pl::Stack s = pl::addLayer({}, two(0x123456, 0xABCDEF));
    pl::Palette milk = pl::fromColours(pl::Source::Milk, "Geiss - Cosmic Dust.milk", "Cosmic|Dust", std::vector<int>{0x112233}.data(), 1);
    pl::setOption(milk, "TO_BLACK");
    pl::setOption(milk, "STEPS");
    s = pl::addLayer(s, milk);
    s.blend = pl::Blend::Mix;
    s.edge = pl::Edge::Hard;
    s.fullRange = true;
    const std::string text = pl::encode(s);
    check(pl::decode(text) == pl::normalized(s), "a stack reads back as written");
    check(pl::encode(pl::decode(text)) == text, "and writes the same text again");
    check(milk.name == "CosmicDust" && pl::decode(text).layers[1].name == "CosmicDust", "a name loses the field separator");
    check(text.rfind("palettes|1|MIX|1|HARD\n", 0) == 0, "the header names the blend, the full-range switch and the edge");
    check(pl::decode(text).edge == pl::Edge::Hard, "the edge reads back");
    check(pl::isUnset(pl::decode("palettes|1|SHARE|0|FUZZY\n" + pl::encode(milk) + "\n")), "an unknown edge: unset");
    check(pl::isUnset(pl::decode("palettes|1|SHARE|0\n" + pl::encode(milk) + "\n")), "no edge: unset");
    check(pl::isUnset(pl::decode("")) && pl::isUnset(pl::decode("palettes|2|SHARE|0\n")), "no header, or another version: unset");
    check(pl::decode("palettes|1|SHARE|0|SOFT\nsomething|new\n" + pl::encode(milk) + "\n").layers.size() == 1, "an unknown row is skipped");
    check(!pl::valid(pl::decodePalette("palette|YOURS||x|SMOOTH|MIRROR|OKLAB|SHADES|nothing")), "a line with no readable stop is no palette");
    check(pl::encode(pl::Stack{}) == "palettes|1|SHARE|0|SOFT\n" && std::string(geode_palette_stack_unset()) == "palettes|1|SHARE|0|SOFT\n",
          "the unset stack's text");
}

void edits() {
    const pl::Palette a = two(0xFF0000, 0x00FF00);
    const pl::Palette b = two(0x0000FF, 0xFFFFFF);
    pl::Stack s = pl::addLayer(pl::addLayer({}, a), b);
    s = pl::moveLayer(s, 1, -1);
    check(s.layers[0] == b && s.layers[1] == a, "move puts a palette first");
    check(pl::moveLayer(s, 0, -3) == s && pl::removeLayer(s, 5) == s, "out of range changes nothing");
    const pl::Palette added = pl::addStop(a, 0x0000FF, -1.0f);
    check(added.stops.size() == 3 && added.stops[1].pos == 0.5f && added.stops[2].rgb == 0x0000FF, "an appended stop respreads the run");
    check(pl::addStop(a, 0x0000FF, 0.25f).stops[2].pos == 0.25f, "a stop added at a place keeps it");
    check(pl::removeStop(pl::removeStop(a, 0), 0).stops.size() == 1, "the last colour stays");
    check(pl::setStop(a, 0, 0x00FFFF, -1.0f).stops[0] == pl::Stop{0x00FFFF, 0.0f}, "a new colour, the same place");

    const std::string line = pl::encode(a);
    check(std::string(geode_palette_set_option(line.c_str(), "STEPS")).find("|STEPS|") != std::string::npos, "the ABI edits a palette line");
    check(std::string(geode_palette_set_stop("nonsense", 0, 0, 0.0f)).empty(), "not a palette line: \"\"");
    const std::string stack = pl::encode(s);
    check(geode_palette_stack_layer_count(stack.c_str()) == 2 && std::string(geode_palette_stack_layer(stack.c_str(), 2)).empty(), "layers by index");
    const std::string mixed = geode_palette_stack_set_blend(stack.c_str(), "MIX");
    check(mixed.rfind("palettes|1|MIX|0", 0) == 0, "the blend by name");
    const std::string full = geode_palette_stack_set_full_range(stack.c_str(), 1);
    check(full.rfind("palettes|1|SHARE|1", 0) == 0, "the full-range opt-in");
    const std::string hard = geode_palette_stack_set_edge(stack.c_str(), "HARD");
    check(pl::decode(hard).edge == pl::Edge::Hard, "the edge by name");
    check(std::string(geode_palette_stack_set_edge(hard.c_str(), "BLURRY")) == hard, "an unknown edge changes nothing");
    const int colours[] = {0x445566, 0x778899};
    const std::string fromStyle = geode_palette_from_colours("STYLE", "silk_satin", "Silk · Satin", colours, 2);
    check(pl::decodePalette(fromStyle).source == pl::Source::Style && pl::decodePalette(fromStyle).name == "Silk · Satin", "a style's own palette");
    int ramp[8] = {};
    geode_palette_stack_ramp(stack.c_str(), nullptr, 8, ramp);
    check(ramp[0] == packed(pl::compose(s, pl::defaultAmounts(), 8)[0]), "the ABI ramp is the composed ramp");
    int swatch[2] = {};
    geode_palette_swatch(line.c_str(), 2, swatch);
    check(swatch[0] == 0xFF0000 && swatch[1] == 0x00FF00, "a swatch runs first stop to last");
    check(std::string(geode_palette_library_entry(99)).empty() && geode_palette_library_index("fire") == 2, "the library by index and id");
}

void anOldLookKeepsItsColours() {
    pl::Amounts amounts{};
    SceneParams p;
    pl::Stack s = pl::fromLegacy(p, amounts);
    check(s.layers.size() == 1 && s.layers[0].ref == "spectrum" && s.layers[0].wrap == pl::Wrap::Loop, "the default look: Spectrum, looped");

    p.palette = 2;
    p.palette2 = 3;
    p.paletteMix = 0.3f;
    s = pl::fromLegacy(p, amounts);
    check(s.layers.size() == 2 && s.blend == pl::Blend::Mix && s.edge == pl::Edge::Hard, "a second palette: two layers, mixed, hard edges");
    check(std::fabs(amounts[0] - 0.7f) < 1e-6f && std::fabs(amounts[1] - 0.3f) < 1e-6f, "weighted 1 - mix and mix");
    for (float t : {0.0f, 0.25f, 0.5f, 0.8f}) {
        const colour::Rgb a = colour::procedural(p.paletteBase(), p.paletteRange(), t);
        const colour::Rgb b = colour::procedural(p.palette2Base(), p.palette2Range(), t);
        const colour::Rgb want{a.r + (b.r - a.r) * 0.3f, a.g + (b.g - a.g) * 0.3f, a.b + (b.b - a.b) * 0.3f};
        check(near(pl::at(s, amounts, t), want, 4.0f / 255.0f), "mix(first, second, paletteMix) at " + std::to_string(t));
    }

    SceneParams yours;
    yours.paletteStop0 = 0xFF0000;
    yours.paletteStop1 = 0x00FF00;
    yours.paletteStop2 = 0x0000FF;
    s = pl::fromLegacy(yours, amounts);
    const auto stops = yours.paletteStops();
    for (float t : {0.0f, 0.1f, 0.5f, 0.9f}) {
        check(near(pl::at(s, amounts, t), colour::ramp(stops.data(), 5, t)), "your colours loop as palStops drew them, t " + std::to_string(t));
    }
    yours.paletteStop1 = -1;
    check(packed(pl::at(pl::fromLegacy(yours, amounts), amounts, 0.4f)) == 0xFF0000, "one of your colours was flat");

    SceneParams mapped = p;
    mapped.paletteLut = 3;
    s = pl::fromLegacy(mapped, amounts);
    check(s.layers.size() == 1 && s.layers[0].ref == "romao" && amounts[0] == 1.0f, "a colour map replaced the whole ramp");

    float wire[SceneParams::kFieldCount];
    p.toWire(wire);
    float out[pl::kMaxLayers] = {};
    const std::string text = geode_palette_stack_from_params(wire, out);
    check(pl::decode(text).layers.size() == 2 && std::fabs(out[1] - 0.3f) < 1e-6f, "the ABI reads the params frame");
}

void recentColours() {
    std::vector<int> list;
    for (int i = 0; i < 30; ++i) list = colour::recent::push(list, i);
    check(list.size() == 24 && list.front() == 29 && list.back() == 6, "newest first, at most 24");
    list = colour::recent::push(list, 10);
    check(list.front() == 10 && std::count(list.begin(), list.end(), 10) == 1, "a repeat moves to the front");
    check(colour::recent::push(list, -1) == list, "not a colour, no change");
    const std::string text = geode_colour_recent_push("#00FF00,#FF0000", 0xFF0000);
    check(text == "#FF0000,#00FF00", "the ABI pushes and dedupes");
    int out[4] = {};
    check(geode_colour_recent_read("#123456,junk,#123456,#ABC", out, 4) == 2 && out[0] == 0x123456 && out[1] == 0xAABBCC,
          "junk and repeats are dropped on read");
    check(geode_colour_recent_read(text.c_str(), nullptr, 0) == 2, "a null out counts");
}

void libraryTextEqualsTheFixture(const char* path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    check(in.good() || in.eof(), std::string("read ") + path);
    check(ss.str() == pl::libraryText(),
          std::string("libraryText() differs from ") + path + "; after a deliberate change run palette_test --write " + path);
    check(std::string(geode_palette_library_text()) == pl::libraryText(), "the ABI hands out the same text");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "--write") == 0) {
        std::ofstream(argv[2], std::ios::binary) << pl::libraryText();
        std::printf("wrote %s\n", argv[2]);
        return 0;
    }
    oklabRoundTrips();
    theLibraryKeepsTheOldIndices();
    classicRampsDrawAsTheyDid();
    gradientOptions();
    oneColourDraws();
    theCaps();
    shareAndMix();
    aPaletteAloneIsMirrored();
    unsetDrawsTheStyleDefault();
    theTextRoundTrips();
    edits();
    anOldLookKeepsItsColours();
    recentColours();
    libraryTextEqualsTheFixture(argc > 1 ? argv[1] : "core/tests/palettes.txt");
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("palette_test: all checks passed\n");
    return 0;
}
