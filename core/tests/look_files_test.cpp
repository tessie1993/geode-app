// Host checks for the look files (core/viz/LookFiles: file names, saved palettes, presets, the template library) and
// the look's own palette in its params (core/viz/params/ParamsPalette). Each section is what a Kotlin store or helper decided
// before it moved here (PresetStore, PaletteStore, TemplateRepository, the SceneParams palette helpers).
// Built and run by CI (.github/workflows/native-host-tests.yml).
#include <cmath>
#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "util/Json.hpp"
#include "viz/Catalogue.hpp"
#include "viz/LookFiles.hpp"
#include "viz/params/Params.hpp"
#include "viz/params/ParamsJson.hpp"
#include "viz/params/ParamsPalette.hpp"

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

    const int six[] = {0x111111, 0x222222, 0x333333, 0x444444, 0x555555, 0x666666, 0x777777, 0x888888, 0x999999};
    SceneParams base = plain;
    base.palette = 4;
    base.paletteBaseOverride = 0.5f;
    const SceneParams stops = pp::withStops(base, six, 9);
    int out[8] = {};
    check(pp::stopsOf(stops, out) == 8, "a look takes eight stops");
    check(out[0] == 0x111111 && out[7] == 0x888888, "in order from the first");
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
    // A stem names one entry right under its folder, whatever the name says (EditorProjectStore's project ids too).
    for (const char* hostile : {"", ".", "..", "../x", "a/b", "a.b", "\xd0\xbf\xd1\x80\xd0\xbe", " "}) {
        const std::string stem = files::safeFileName(hostile);
        check(!stem.empty() && stem != "." && stem != ".." && stem.find('/') == std::string::npos, std::string("a safe stem for ") + hostile);
    }
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

    const auto old = files::readPalette(R"({"name":"Sea","baseHue":1.7,"stops":[16777216,-1,"255",255,1,2,3,4,5,6,7,8]})");
    check(old && old->id == "Sea", "a file without an id takes its name's");
    check(old && old->baseHue == 1.0f && old->hueSpan == 1.0f, "hues held to 0..1, span 1 when missing");
    check(old && old->stops == std::vector<int>({255, 255, 1, 2, 3, 4, 5, 6}), "only real colours, eight at most");
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

    check(files::framePaletteId("Dusk", stopped) == "Dusk", "a frame that draws its own palette keeps the id");
    check(files::framePaletteId("Dusk", SceneParams{}).empty(), "a frame on the built-in palette drops it");
    check(files::framePaletteId("", stopped).empty(), "no id, none kept");
}

std::string fixture(const std::string& folder, const char* name) {
    std::ifstream in(folder + "/" + name, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    check(in.good() || in.eof(), std::string("fixture ") + name);
    return text.str();
}

std::vector<float> wire(const SceneParams& p) {
    std::vector<float> out(SceneParams::kFieldCount);
    p.toWire(out.data());
    return out;
}

// Presets and shared links (PresetStore, PresetGoldenTest and SharedLinkNumbersTest before): the envelope beside
// the params object, the golden files in app/src/test/resources/presets, hostile numbers
// and a style this build does not have. The params object's own rules are params_json_test's.
void presets(const std::string& folder) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    check(files::finiteOr(nan, 0.5f) == 0.5f && files::finiteOr(inf, 0.5f) == 0.5f && files::finiteOr(-inf, 0.5f) == 0.5f,
          "NaN and the infinities fall back");
    check(files::finiteOr(1e39, 0.5f) == 0.5f && files::finiteOr(-1e39, 0.5f) == 0.5f, "a float overflow falls back");
    check(files::finiteOr(2.25, 0.5f) == 2.25f, "a finite number stays");
    check(files::finiteIn(nan, files::kAttackMin, files::kAttackMax) == files::kAttackMin, "NaN is the range's low end");
    check(files::finiteIn(40.0, files::kDecayMin, files::kDecayMax) == files::kDecayMax, "held to the slider's range");
    check(files::finiteIn(0.3, files::kAttackMin, files::kAttackMax) == 0.3f, "inside the range it stays");

    const auto golden = files::readPreset(fixture(folder, "current.json"));
    check(golden.has_value(), "the golden preset reads");
    if (golden) {
        check(golden->name == "Golden" && golden->sceneId == "silk_web", "its name and style");
        check(golden->attack == 0.25f && golden->decay == 0.5f, "its envelope");
        check(golden->customShader == std::optional<std::string>("void main() {}"), "its shader");
        check(golden->customPaletteId == "palette-a", "its saved palette");
        check(golden->params.speed == 2.03125f && golden->params.paletteStop0 == 0xFF8800, "its params");
        const std::string written = files::writePreset(*golden);
        const auto again = files::readPreset(written);
        check(again && again->name == golden->name && again->sceneId == golden->sceneId && again->attack == golden->attack &&
                  again->decay == golden->decay && again->customShader == golden->customShader &&
                  again->customPaletteId == golden->customPaletteId &&
                  wire(again->params) == wire(golden->params),
              "it round-trips");
        check(written.rfind("{\n  \"name\": \"Golden\",\n  \"sceneId\": \"silk_web\",\n  \"attack\": 0.25,\n  \"decay\": 0.5,\n  \"speed\": 2.03125,", 0) == 0,
              "the envelope first, then the params object's keys, as org.json's toString(2)");
        check(written.find("\"customPaletteId\": \"palette-a\",\n  \"customPalette2Id\": \"\",\n  \"customShader\": \"void main() {}\"\n}") != std::string::npos,
              "the palette ids, then the shader last");
    }

    const auto legacy = files::readPreset(fixture(folder, "legacy.json"));
    check(legacy && legacy->name == "Old Kyanite" && legacy->params.speed == 1.5f, "an old preset reads its params");
    check(legacy && legacy->sceneId == "tunnel" && !legacy->customShader, "and has no shader");

    const auto hostile = files::readPreset(fixture(folder, "hostile.json"));
    check(hostile && hostile->attack == files::kAttackMax && hostile->decay == files::kDecayMin, "a hostile envelope is clamped");

    const auto water = files::readPreset(R"({"name":"w","sceneId":"water","attack":0.1,"decay":0.2,"waterDepth":0.7})");
    check(water && water->sceneId == "pool", "a look saved on the retired Water opens on Pool");
    check(!files::readPreset(R"({"name":"gone","sceneId":"no_such_style","attack":0.1,"decay":0.2})"),
          "a look on a style this build does not have is not read, so it is dropped rather than opened on another");

    const auto overflow = files::readPreset(R"({"name":"x","sceneId":"silk_web","attack":1e39,"decay":-1e999})");
    check(overflow && overflow->attack == files::kAttackMin && overflow->decay == files::kDecayMin, "overflowing numbers read as the low end");
    if (overflow) {
        const auto again = files::readPreset(files::writePreset(*overflow));
        check(again && again->attack == overflow->attack, "and it saves again");
        check(files::writePreset(*overflow).find("\"customPalette2Id\": \"\"\n}") != std::string::npos, "no shader key");
    }

    check(!files::readPreset(R"({"sceneId":"silk_web","attack":0.1,"decay":0.2})"), "no name, no preset");
    check(!files::readPreset(R"({"name":"x","sceneId":"silk_web","attack":0.1})"), "no decay, no preset");
    check(!files::readPreset("[1,2]"), "not an object, no preset");
    const auto texts = files::readPreset(R"({"name":7,"sceneId":"silk_web","attack":"0.5","decay":0.2})");
    check(texts && texts->name == "7" && texts->attack == 0.5f, "a number as the name and a string as a number read as org.json did");

    check(files::safeChars("a/b c") == "a_b c", "a folder name keeps no slash");
}

// The template library (TemplateRepository's TemplateStore, FileTemplateRepository and BundledTemplates before):
// a file's name, an import's new name and the starter set.
void templates() {
    const std::string suffix = ".geode.json";
    check(files::templateFileName("Reel Neon", "geode.starter.reel-neon", suffix) == "Reel Neon--geode_starter_reel-neon.geode.json",
          "name stem, then the id's tag");
    check(files::templateFileName("", "x", suffix) == "-e3b0c442--x.geode.json", "a blank name keeps its hashed stem");
    check(files::templateFileName(std::string(70, 'a'), "x", suffix) == std::string(60, 'a') + "--x.geode.json", "the stem is 60 at most");
    check(files::templateFileName("a", std::string(50, 'b') + "/c", suffix) == "a--" + std::string(40, 'b') + suffix, "the tag is 40 at most");
    check(files::templateFileTail("id/\xc3\xa9", suffix) == "--id__" + suffix, "one '_' per code point in the tag");

    check(files::uniqueTemplateName("  Mine ", {"Other"}) == "Mine", "a free name is the trimmed name");
    check(files::uniqueTemplateName("Mine", {"mine", "MINE 2"}) == "Mine 3", "taken names, caseless, count on from 2");
    check(files::uniqueTemplateName(" ", {}) == "Template", "a blank name is Template");

    check(files::starterCount() == 6, "six starters");
    check(files::starter(-1).empty() && files::starter(files::starterCount()).empty(), "past the end is empty");
    for (int i = 0; i < files::starterCount(); ++i) {
        const auto doc = json::parse(std::string(files::starter(i)));
        check(doc && doc->kind() == json::Value::Kind::Object, "starter " + std::to_string(i) + " is a JSON object");
        if (!doc) continue;
        check(doc->optString("format") == "geode.template", "starter " + std::to_string(i) + " is a template");
        const std::string id = doc->optString("id");
        check(id.rfind("geode.starter.", 0) == 0 && files::isStarter(id), "its id is a starter's: " + id);
        const json::Value* look = doc->get("look");
        const json::Value* params = look ? look->get("params") : nullptr;
        SceneParams read;
        std::string a, b;
        check(params && viz::readParamsJson(json::orgJsonText(*params).value_or(""), read, a, b), id + "'s params read");
    }
    const auto reel = json::parse(std::string(files::starter(0)));
    const json::Value* reelParams = reel && reel->get("look") ? reel->get("look")->get("params") : nullptr;
    SceneParams neon;
    std::string a, b;
    check(reelParams && viz::readParamsJson(json::orgJsonText(*reelParams).value_or(""), neon, a, b) && neon.speed == 1.3f &&
              neon.palette == 1 && neon.bassGain == 1.4f,
          "Reel Neon's look as it always was");
    check(!files::isStarter("geode.starter.") && !files::isStarter("my-template"), "other ids are not starters");
}

// Preset names (PresetFiltering, PresetActions, PresetLibraryController and BuiltInPresets before).
void presetNames() {
    const std::vector<std::string> taken = {"Glow", "Mine A"};
    using files::NameProblem;
    check(files::presetRenameProblem("  ", "Mine A", taken) == NameProblem::Blank, "a blank name is refused");
    check(files::presetRenameProblem("glow", "Mine A", taken) == NameProblem::Taken, "another's name, any case, is taken");
    check(files::presetRenameProblem("Mine A", "Mine A", taken) == NameProblem::None, "its own name is fine");
    check(files::presetRenameProblem("mine a", "Mine A", taken) == NameProblem::None, "its own name in another case is fine");
    check(files::presetRenameProblem("Fresh", "Mine A", taken) == NameProblem::None, "a free name is fine");
    check(files::presetRenameProblem("\xc3\x89t\xc3\xa9", "x", {"\xc3\xa9t\xc3\xa9"}) == NameProblem::Taken, "case is ignored past ASCII");
    check(files::presetRenameProblem("\xd0\x9d\xd0\x9e\xd0\xa7\xd0\xac", "x", {"\xd0\xbd\xd0\xbe\xd1\x87\xd1\x8c"}) == NameProblem::Taken,
          "and in Cyrillic");
    check(files::presetRenameProblem("\xc3\x89t\xc3\xa9", "x", {"Ete"}) == NameProblem::None, "accents still count");

    check(files::cleanPresetName("  Night \xc2\xb7 Rain ") == "Night - Rain", "the link separator is dropped");
    check(files::cleanPresetName(" \xc2\xb7 ") == "-", "a lone separator is a dash");
    check(files::cleanPresetName(" \t", "Untitled") == "Untitled", "a blank name takes the fallback");
    check(files::cleanPresetName(std::string(500, 'a')).size() == files::kMaxPresetNameBytes, "a long name is capped");
    std::string wide;
    for (int i = 0; i < 60; ++i) wide += "\xc3\xa9";
    check(files::cleanPresetName(wide).size() == files::kMaxPresetNameBytes, "a cap lands between code points");
    check(files::cleanFolderName(" Live\xe2\x80\x83") == "Live", "a folder name is trimmed");

    check(files::folderNameProblem(" ", std::nullopt, {"Live"}) == NameProblem::Blank, "a blank folder is refused");
    check(files::folderNameProblem(" live ", std::nullopt, {"Live"}) == NameProblem::Taken, "a new folder may not take one, any case");
    check(files::folderNameProblem("LIVE", std::string("Live"), {"Live"}) == NameProblem::None, "a folder renamed to itself is fine");
    check(files::folderNameProblem("Slow", std::string("Live"), {"Live", "slow"}) == NameProblem::Taken, "or to another's name");

    check(files::uniquePresetName("Glow copy", {"Glow"}) == "Glow copy", "a free name stays");
    check(files::uniquePresetName("Glow copy", {"glow copy", "Glow copy 2"}) == "Glow copy 3", "taken names, caseless, count on");
    check(files::presetCopyName("Glow", {"Glow", "GLOW COPY"}) == "Glow copy 2", "a copy's name");
    check(files::importedPresetName(" A \xc2\xb7 B ", "Shared", {"A - B", "A - B 2"}) == "A - B 3", "an import counts past its name");
    check(files::importedPresetName("glow", "Shared", {"Glow"}) == "glow", "an import's names are compared exactly");
    check(files::importedPresetName("  ", "Shared", {}) == "Shared", "a blank import takes the fallback");

    const std::vector<std::string> shelf = {"Glow \xc2\xb7 Cymatics", "Mine", "Glow"};
    check(files::isBuiltInPreset(shelf[0]) && !files::isBuiltInPreset("Mine - A"), "a built-in holds the separator");
    check(files::presetReplaceTarget(" Mine ", shelf) == std::optional<std::string>("Mine"), "a save over a user preset replaces it");
    check(!files::presetReplaceTarget("mine", shelf) && !files::presetReplaceTarget(" ", shelf), "only the exact name, never a blank");
    check(!files::presetReplaceTarget("Glow \xc2\xb7 Cymatics", shelf), "a built-in is never replaced");
    check(files::builtInNameClash("glow - cymatics", {"Glow - Cymatics \xc2\xb7 x", "glow - cymatics"}) == false,
          "a user preset's name is no clash");
    check(files::builtInNameClash("glow \xc2\xb7 cymatics", shelf) == false, "a cleaned name never holds the separator");
    check(files::builtInNameClash("x", {"X"}) == false && !files::builtInNameClash("", shelf), "nor a blank");

    const std::string style = "no_such_style";
    check(files::presetMatchesScene(style, style), "a built-in matches its own style");
    check(files::presetMatchesScene(viz::catalogue::presetStyle("fluid"), "fluid"), "and the shelf its style is filed under");
    check(!files::presetMatchesScene("other", style), "not another");
}

// The share link's framing (PresetLink before); the gzip and base64 step is the shell's.
void shareLinks() {
    const std::string payload = "H4sIAAAAAAAA_6tWyk";
    const std::string link = files::presetLink(payload);
    check(link == "geode://preset/" + payload, "a link is the prefix and the payload");
    check(files::isPresetLink(link), "a minted link is recognised as one");
    check(files::isPresetLink("  GEODE://PRESET/abc  "), "recognition ignores case and surrounding whitespace");
    check(!files::isPresetLink("https://example.com/preset/abc"), "another link is not one");
    check(files::isPresetLink("geode://pre\xc5\xbf" "et/x"), "a long s matches s, as the shell's caseless match did");

    check(files::presetLinkPayload(" " + link + "\n") == payload, "the payload of a trimmed link");
    check(files::presetLinkPayload(link + "#fragment") == payload, "a fragment is not part of it");
    check(files::presetLinkPayload(link + "?utm=share") == payload, "nor a query");
    check(files::presetLinkPayload(link + "?a#b") == payload, "the first of either ends it");
    check(files::presetLinkPayload("GEODE://Preset/" + payload) == payload, "an upper-case prefix gives the same payload");

    const std::string overlong = "geode://preset/" + std::string(files::kMaxLinkLength, 'A');
    check(!files::presetLinkPayload(overlong), "a link past the length cap is refused before it is inflated");
    const std::string longest = "geode://preset/" + std::string(files::kMaxLinkLength - 15, 'A');
    check(files::presetLinkFits(longest) && files::presetLinkPayload(longest), "a link at the cap is accepted");
    check(!files::presetLinkFits(longest + "A"), "one past it is not shared");
    std::string emoji;
    for (int i = 0; i < files::kMaxLinkLength / 2; ++i) emoji += "\xf0\x9f\x8e\xb5";
    check(files::presetLinkFits(emoji) && !files::presetLinkFits(emoji + "A"), "lengths count UTF-16 units");

    check(!files::presetLinkPayload("https://example.com/") && !files::presetLinkPayload(""), "no link, no payload");

    check(files::findPresetLink("look at this " + link + " isn't it nice") == link, "a link is found inside surrounding chat text");
    check(files::findPresetLink("see:" + link + "\xe2\x80\xa8next") == link, "a link starts at its prefix and ends at a line separator");
    check(files::findPresetLink("\xc3\xa9 GEODE://PRESET/x\ty") == std::optional<std::string>("GEODE://PRESET/x"), "found in any case");
    check(!files::findPresetLink("no link here"), "text with no link in it yields none");

    check(files::presetLinkBodyAccepted("{}") && !files::presetLinkBodyAccepted(" \n"), "a blank body is no preset");
}

}  // namespace

int main(int argc, char** argv) {
    paletteParams();
    fileNames();
    savedPalettes();
    presets(argc > 1 ? argv[1] : "app/src/test/resources/presets");
    templates();
    presetNames();
    shareLinks();
    if (failures == 0) std::printf("look files: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
