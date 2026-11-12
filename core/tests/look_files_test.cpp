// Host checks for the look files (core/viz/LookFiles: file names, saved palettes, presets, the template library) and
// the look's own palette in its params (core/viz/ParamsPalette). Each section is what a Kotlin store or helper decided
// before it moved here (PresetStore, PaletteStore, TemplateRepository, the SceneParams palette helpers).
// Built and run by CI (.github/workflows/native-host-tests.yml).
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "viz/LookFiles.hpp"
#include "viz/Params.hpp"
#include "viz/ParamsPalette.hpp"

using namespace geode;
using viz::SceneParams;
namespace pp = viz::palette_params;
namespace files = viz::files;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

void paletteParams() {
    const SceneParams plain{};
    check(!pp::usesCustomPalette(plain), "the defaults draw the built-in palette");
    SceneParams p = plain;
    p.paletteBaseOverride = 0.2f;
    check(pp::usesCustomPalette(p), "a base override is the look's own palette");
    p = plain;
    p.paletteRangeOverride = 0.0f;
    check(pp::usesCustomPalette(p), "a span override of 0 is set");
    p = plain;
    p.paletteStop0 = 0x000000;
    check(pp::usesCustomPalette(p), "black is a colour");
    p = plain;
    p.paletteStop1 = 0xFF0000;
    check(!pp::usesCustomPalette(p), "a stop after an unset first does not count");

    const int six[] = {0x111111, 0x222222, 0x333333, 0x444444, 0x555555, 0x666666};
    SceneParams base = plain;
    base.palette = 4;
    base.paletteBaseOverride = 0.5f;
    const SceneParams stops = pp::withStops(base, six, 6);
    int out[5] = {};
    check(pp::stopsOf(stops, out) == 5, "a look takes five stops");
    check(out[0] == 0x111111 && out[4] == 0x555555, "in order from the first");
    check(stops.paletteBaseOverride == SceneParams::kUnsetOverride, "your colours clear the hue override");
    check(stops.palette == 4, "the built-in index is left alone");
    const SceneParams two = pp::withStops(stops, six, 2);
    check(pp::stopsOf(two, nullptr) == 2 && two.paletteStop2 == SceneParams::kNoStop, "fewer stops unset the rest");

    const SceneParams gradient = pp::withGradient(stops, 1.5f, -0.25f);
    check(gradient.paletteBaseOverride == 1.0f && gradient.paletteRangeOverride == 0.0f, "the gradient is held to 0..1");
    check(pp::stopsOf(gradient, nullptr) == 0, "a gradient drops your colours");
    const SceneParams nan = pp::withGradient(plain, std::numeric_limits<float>::quiet_NaN(), 0.3f);
    check(nan.paletteBaseOverride == 0.0f && std::fabs(nan.paletteRangeOverride - 0.3f) < 1e-6f, "NaN reads as 0");

    const SceneParams cleared = pp::withoutCustomPalette(gradient);
    check(!pp::usesCustomPalette(cleared), "clearing gives the built-in palette back");
    check(cleared.paletteRangeOverride == SceneParams::kUnsetOverride, "both overrides unset");
}

// A name as a file stem (PresetStore.safeFileName before): kept when every code point is safe and it is not blank,
// else '_' per code point and four bytes of the name's SHA-256.
void fileNames() {
    check(files::safeFileName("My Look-2_b") == "My Look-2_b", "a safe name is its own stem");
    check(files::safeFileName("a/b") == "a_b-c14cddc0", "an unsafe name gets its hash");
    check(files::safeFileName("") == "-e3b0c442", "a blank name never names the folder");
    check(files::safeFileName("   ") == "   -0aad7da7", "spaces alone are blank");
    check(files::safeFileName("\xc3\x89t\xc3\xa9") == "_t_-adca3dd5", "one '_' per code point, the hash of the UTF-8");
    check(files::trimmed(" \t\xc2\xa0x y\xe3\x80\x80 ") == "x y", "trim takes Kotlin's whitespace, NBSP and ideographic space too");
    check(files::blank("\xe2\x80\xa8") && !files::blank("._"), "a line separator is blank, punctuation is not");
    const std::vector<int> caseless = files::order({"beta", "Alpha", "alpha", "Gamma"}, true);
    check(caseless == std::vector<int>({1, 2, 0, 3}), "caseless order keeps ties in place");
    check(files::order({"beta", "Alpha", "alpha"}, false) == std::vector<int>({1, 2, 0}), "by code point, capitals first");
}

// Saved palettes (PaletteStore before): the file text org.json wrote, its defaults and clamps, the id rule and
// what a look keeps when its palette is deleted.
void savedPalettes() {
    check(files::paletteIdFor("Dusk") == "Dusk", "a safe name is its id");
    check(files::paletteIdFor(" Dusk ") == "Dusk", "the id is trimmed");
    check(files::paletteIdFor("a/b") == "a_b-c14cddc0", "an unsafe name is hashed");

    files::SavedPalette dusk;
    dusk.id = "Dusk";
    dusk.name = "Dusk";
    dusk.baseHue = 0.2f;
    dusk.hueSpan = 0.4f;
    const std::string text = files::writePalette(dusk);
    check(text == "{\n  \"id\": \"Dusk\",\n  \"name\": \"Dusk\",\n  \"baseHue\": 0.20000000298023224,\n"
                  "  \"hueSpan\": 0.4000000059604645,\n  \"stops\": []\n}",
          "the file is org.json's toString(2): " + text);
    files::SavedPalette two = dusk;
    two.stops = {0xFF8800, 0x000000};
    check(files::writePalette(two).find("\"stops\": [\n    16746496,\n    0\n  ]\n}") != std::string::npos, "stops one per line");

    const auto back = files::readPalette(files::writePalette(two));
    check(back && back->id == "Dusk" && back->name == "Dusk" && back->baseHue == 0.2f && back->stops == two.stops, "it reads back");

    const auto old = files::readPalette(R"({"name":"Sea","baseHue":1.7,"stops":[16777216,-1,"255",255,1,2,3,4,5]})");
    check(old && old->id == "Sea", "a file without an id takes its name's");
    check(old && old->baseHue == 1.0f && old->hueSpan == 1.0f, "hues held to 0..1, span 1 when missing");
    check(old && old->stops == std::vector<int>({255, 255, 1, 2, 3}), "only real colours, five at most");
    check(!files::readPalette(R"({"id":"x"})"), "no name, no palette");
    check(!files::readPalette("not json"), "not JSON, no palette");
    const auto nan = files::readPalette(R"({"name":"n","baseHue":"NaN","hueSpan":"oops"})");
    check(nan && nan->baseHue == 0.0f && nan->hueSpan == 1.0f, "NaN and unreadable hues take their defaults");
    const auto number = files::readPalette(R"({"name":5,"id":"  "})");
    check(number && number->name == "5" && number->id == "5", "any value reads as its text; a blank id is the name's");

    files::SavedPalette messy;
    messy.id = "";
    messy.name = "   ";
    messy.baseHue = -2.0f;
    messy.hueSpan = std::numeric_limits<float>::quiet_NaN();
    messy.stops = {0x1000000, 7};
    const files::SavedPalette clean = files::sanitized(messy);
    check(clean.name == "Palette" && clean.id == "-0aad7da7", "a blank name is Palette; the id is the name's");
    check(clean.baseHue == 0.0f && clean.hueSpan == 0.0f && clean.stops == std::vector<int>({7}), "sanitized hues and colours");

    const auto migrated = files::migratedPalette(R"({"id":"old id","name":"New/Name"})");
    check(migrated && migrated->find("\"id\": \"New_Name-") != std::string::npos, "migration renames to the name's id");

    SceneParams look{};
    look.paletteBaseOverride = 0.9f;
    const SceneParams coloured = files::applyPalette(look, two);
    check(coloured.paletteStop0 == 0xFF8800 && coloured.paletteStop1 == 0 && coloured.paletteStop2 == SceneParams::kNoStop,
          "a palette with colours applies them");
    check(coloured.paletteBaseOverride == SceneParams::kUnsetOverride, "and clears the gradient");
    SceneParams stopped{};
    stopped.paletteStop0 = 0x123456;
    const SceneParams graded = files::applyPalette(stopped, dusk);
    check(graded.paletteStop0 == SceneParams::kNoStop && graded.paletteBaseOverride == 0.2f, "one without applies its gradient");

    check(files::keptPaletteId("Dusk", "Dusk").empty(), "a look forgets the palette deleted");
    check(files::keptPaletteId("Dusk", "Dawn") == "Dusk", "and keeps another");
    check(files::keptPaletteId("", "").empty(), "nothing deleted, nothing named");
}

}  // namespace

int main() {
    paletteParams();
    fileNames();
    savedPalettes();
    if (failures == 0) std::printf("look files: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
