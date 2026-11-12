// Host-side checks for the Customize randomizer, Reset tab and the params wire
// (core/viz/ParamRandomizer, core/viz/Params). Built and run by CI
// (.github/workflows/android.yml, "Params host tests") with the system compiler; no GL.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "viz/Catalogue.hpp"
#include "viz/ParamRandomizer.hpp"
#include "viz/Params.hpp"

using namespace geode::viz;
using randomizer::Tab;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

const SceneParams kDefaults{};

std::vector<float> wire(const SceneParams& p) {
    std::vector<float> out(SceneParams::kFieldCount);
    p.toWire(out.data());
    return out;
}

// Every field moved off its default.
SceneParams everythingChanged() {
    SceneParams p;
    for (const char* field : SceneParams::fieldNames()) {
        const float before = p.get(field);
        p.set(field, before + 1.123f);
        if (p.get(field) == before) p.set(field, 0.0f);  // a flag that was on
    }
    return p;
}

std::set<std::string> fieldsOf(Tab tab) {
    std::set<std::string> out;
    for (const auto& roll : randomizer::rolledKeys()) {
        if (roll.tab != tab) continue;
        for (const char* f : roll.fields) {
            if (f) out.insert(f);
        }
    }
    return out;
}

void everyFieldIsRolledOnceOrSaysWhyNot() {
    std::multiset<std::string> owned;
    for (const auto& roll : randomizer::rolledKeys()) {
        for (const char* f : roll.fields) {
            if (f) owned.insert(f);
        }
    }
    for (const auto& [field, why] : randomizer::neverRolled()) {
        check(std::string(why).size() > 0, std::string(field) + " says why it is never rolled");
        owned.insert(field);
    }
    for (const char* field : SceneParams::fieldNames()) {
        check(owned.count(field) == 1, std::string(field) + " is rolled by exactly one control or never rolled");
    }
    check(owned.size() == static_cast<size_t>(SceneParams::kFieldCount), "no roll writes a field that does not exist");
    for (const ParamSlot slot : kRetiredParams) {
        const std::string retired = SceneParams::fieldNames()[static_cast<int>(slot)];
        bool listed = false;
        for (const auto& entry : randomizer::neverRolled()) listed = listed || std::string(entry.first) == retired;
        check(listed, retired + " is retired, so never rolled");
    }
}

void sameSeedSameLook() {
    const SceneParams a = randomizer::randomize(kDefaults, 42, std::nullopt, "", {});
    const SceneParams b = randomizer::randomize(kDefaults, 42, std::nullopt, "", {});
    const SceneParams c = randomizer::randomize(kDefaults, 43, std::nullopt, "", {});
    check(wire(a) == wire(b), "the same seed rolls the same look");
    check(wire(a) != wire(c), "another seed rolls another look");
    check(wire(a) != wire(kDefaults), "a roll changes something");
}

void lockedControlsNeverChange() {
    const SceneParams base = everythingChanged();
    for (const auto& roll : randomizer::rolledKeys()) {
        for (uint64_t seed = 1; seed <= 20; ++seed) {
            const SceneParams out = randomizer::randomize(base, seed, std::nullopt, "", {roll.key});
            for (const char* f : roll.fields) {
                if (f) check(out.get(f) == base.get(f), std::string(roll.key) + " is locked, so " + f + " holds");
            }
        }
    }
}

void neverRolledFieldsNeverChange() {
    const SceneParams base = everythingChanged();
    for (uint64_t seed = 1; seed <= 200; ++seed) {
        const SceneParams out = randomizer::randomize(base, seed, std::nullopt, "", {});
        for (const auto& [field, why] : randomizer::neverRolled()) {
            check(out.get(field) == base.get(field), std::string(field) + " is never rolled: " + why);
        }
    }
}

void aRollOnAStyleTouchesOnlyWhatItCanShow() {
    const SceneParams base = everythingChanged();
    for (const char* style : {"plasma", "vanishing", "silk_web", "fluid_ink", "milkdrop", "kundt_tube", "curlflow"}) {
        for (uint64_t seed = 1; seed <= 40; ++seed) {
            const SceneParams out = randomizer::randomize(base, seed, std::nullopt, style, {});
            for (const auto& roll : randomizer::rolledKeys()) {
                if (catalogue::reaches(catalogue::scopeOf(roll.key), style)) continue;
                for (const char* f : roll.fields) {
                    if (f) check(out.get(f) == base.get(f), std::string(roll.key) + " cannot move " + style + ", so " + f + " holds");
                }
            }
        }
    }
    // Detail is a performance budget, never rolled; Morph only reaches fragment styles.
    const SceneParams fluid = randomizer::randomize(kDefaults, 7, std::nullopt, "fluid_ink", {});
    check(fluid.morph == kDefaults.morph, "morph is not rolled on a fluid style");
}

void aTabRollTouchesOnlyThatTab() {
    const SceneParams base = everythingChanged();
    for (Tab tab : {Tab::Shape, Tab::Color, Tab::Motion, Tab::Fx, Tab::Reactivity, Tab::Scene, Tab::Fluid, Tab::Cymatics}) {
        const auto owned = fieldsOf(tab);
        check(!owned.empty(), std::string(randomizer::name(tab)) + " owns fields");
        for (uint64_t seed = 1; seed <= 20; ++seed) {
            const SceneParams out = randomizer::randomize(base, seed, tab, "", {});
            for (const char* f : SceneParams::fieldNames()) {
                if (!owned.count(f)) check(out.get(f) == base.get(f), std::string(randomizer::name(tab)) + " leaves " + f);
            }
        }
    }
}

void resetTabRestoresExactlyItsFields() {
    const SceneParams base = everythingChanged();
    for (Tab tab : {Tab::Shape, Tab::Color, Tab::Motion, Tab::Fx, Tab::Reactivity, Tab::Scene, Tab::Fluid, Tab::Cymatics}) {
        const auto owned = fieldsOf(tab);
        const SceneParams out = randomizer::resetTab(base, tab);
        for (const char* f : SceneParams::fieldNames()) {
            const float want = owned.count(f) ? kDefaults.get(f) : base.get(f);
            check(out.get(f) == want, std::string("reset ") + randomizer::name(tab) + ": " + f);
        }
    }
}

void rollsStayUnderTheFlashClamp() {
    for (uint64_t seed = 0; seed < 300; ++seed) {
        const SceneParams p = randomizer::randomize(kDefaults, seed, std::nullopt, "", {});
        check(p.bloom <= 0.6f, "bloom stays under its safety cap");
        check(p.brightness <= 1.25f && p.contrast <= 1.25f && p.intensity <= 1.25f, "levels stay under 1 + flash depth");
        check(p.reactSeed >= 1 && p.reactSeed < (1 << 24), "the DNA seed survives the float wire");
        check(p.symmetry >= 0 && p.cymaticsModes >= 2 && p.cymaticsModes <= 8, "counts stay in range");
        const auto& folds = SceneParams::kSymmetryFolds;
        check(!p.kaleidoscope || std::find(folds.begin(), folds.end(), p.symmetry) != folds.end(), "a kaleidoscope gets a fold on the menu");
    }
}

// Every roll lands inside the field's own range in the schema (core/viz/ParamTable.inc), so a rolled
// look is one the controls can show and set back.
void rollsStayInsideTheSchemasRanges() {
    for (uint64_t seed = 0; seed < 300; ++seed) {
        const SceneParams p = randomizer::randomize(kDefaults, seed, std::nullopt, "", {});
        for (const ParamSpec& s : SceneParams::schema()) {
            const float v = p.get(s.name);
            check(v >= s.min && v <= s.max, std::string(s.name) + " rolls inside [min, max], seed " + std::to_string(seed));
        }
    }
}

// A choice roll picks from the whole schema range, so a value added to ParamTable.inc is rolled too
// (the camera's drive, fall and rise paths were left out by a count kept beside the schema).
void choiceRollsReachEverySchemaValue() {
    for (const ParamSlot slot : {ParamSlot::palette, ParamSlot::palette2, ParamSlot::camPath}) {
        const ParamSpec& spec = SceneParams::spec(slot);
        std::set<int> rolled;
        for (uint64_t seed = 0; seed < 2000; ++seed) {
            rolled.insert(static_cast<int>(randomizer::randomize(kDefaults, seed, std::nullopt, "", {}).get(spec.name)));
        }
        const int count = static_cast<int>(spec.max - spec.min) + 1;
        check(static_cast<int>(rolled.size()) == count,
              std::string(spec.name) + " rolls " + std::to_string(rolled.size()) + " of its " + std::to_string(count) + " values");
        check(*rolled.begin() == static_cast<int>(spec.min) && *rolled.rbegin() == static_cast<int>(spec.max),
              std::string(spec.name) + " rolls from min to max");
    }
}

void aPaletteRollDropsACustomPalette() {
    SceneParams custom;
    custom.paletteBaseOverride = 0.3f;
    custom.paletteRangeOverride = 0.2f;
    const SceneParams out = randomizer::randomize(custom, 9, Tab::Color, "", {});
    check(out.paletteBaseOverride == SceneParams::kUnsetOverride && out.paletteRangeOverride == SceneParams::kUnsetOverride,
          "a rolled palette replaces the custom one");
}

void theWireRoundTrips() {
    const SceneParams p = everythingChanged();
    const auto w = wire(p);
    check(wire(SceneParams::fromWire(w.data())) == w, "fromWire(toWire(p)) == p");
}

constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

// A frame as the preset reader hands it to geode_params_sanitize: the defaults, with `values` over them.
SceneParams sanitizedOver(std::initializer_list<std::pair<const char*, float>> values) {
    std::vector<float> in = wire(kDefaults);
    for (const auto& [name, v] : values) in[static_cast<size_t>(SceneParams::slotOf(name))] = v;
    return SceneParams::sanitized(in.data());
}

// presets/hostile.json as the shell reads it (an int through optInt, so 2.75 is 2; "abc" and NaN as NaN;
// 1e39 overflows to infinity as a float): held to the schema's ranges, not the narrower slider stops.
void aHostilePresetIsClampedToTheSchemaRanges() {
    const SceneParams p = sanitizedOver({
        {"speed", 99.0f},
        {"zoom", -5.0f},
        {"rotation", kInf},
        {"warp", -kInf},
        {"endlessZoomSpeed", kNaN},
        {"trails", 1.0f},
        {"symmetry", 40.0f},
        {"palette", -3.0f},
        {"particleShape", 2.0f},
        {"reactSeed", 99999999.0f},
        {"paletteLut", 9.0f},
        {"brightness", 1.875f},
        {"bloom", 0.625f},
        {"intensity", 3.0f},
        {"cymaticsFundamental", 10.0f},
        {"flowCurl", 75.0f},
        {"colorShift", 1.5f},
        {"camFov", kNaN},
    });
    check(p.speed == 4.0f && p.zoom == 0.3f, "hostile: speed and zoom held to their ranges");
    check(p.rotation == kDefaults.rotation && p.warp == kDefaults.warp, "hostile: infinities read as the default");
    check(p.endlessZoomSpeed == kDefaults.endlessZoomSpeed && p.camFov == kDefaults.camFov, "hostile: NaN reads as the default");
    check(p.trails, "hostile: a flag over 0.5 is on");
    check(p.symmetry == 16 && p.palette == 0 && p.particleShape == 2, "hostile: ints held to their ranges");
    check(p.reactSeed == 16777215 && p.paletteLut == 4, "hostile: wide ints held to their ranges");
    check(p.brightness == 1.875f && p.bloom == 0.625f && p.intensity == 2.0f, "hostile: the schema's range, not the slider's");
    check(p.cymaticsFundamental == 40.0f && p.flowCurl == 50.0f, "hostile: floats held to their ranges");
    check(p.colorShift == 1.5f, "hostile: a circular value wraps where it is drawn and is not clamped");
    const auto once = wire(p);
    check(wire(SceneParams::sanitized(once.data())) == once, "a sanitized frame saves and reads back the same");
}

// A shared link's overflowing numbers (1e39, 1e999, -1e999) read as each row's default.
void overflowingNumbersReadAsTheDefaults() {
    const SceneParams p = sanitizedOver({{"zoom", kInf}, {"speed", kInf}, {"warp", -kInf}, {"bloom", -kInf}});
    check(p.zoom == kDefaults.zoom && p.speed == kDefaults.speed, "overflow: zoom and speed read as the default");
    check(p.warp == kDefaults.warp && p.bloom == kDefaults.bloom, "overflow: warp and bloom read as the default");
}

// A retired row's key is not read: its slot keeps the default.
void aRetiredSlotReadsAsItsDefault() {
    const auto slot = static_cast<size_t>(ParamSlot::pulse);
    std::vector<float> in = wire(kDefaults);
    in[slot] += 0.5f;
    check(wire(SceneParams::sanitized(in.data()))[slot] == wire(kDefaults)[slot], "a retired slot reads as its default");
}

}  // namespace

int main() {
    everyFieldIsRolledOnceOrSaysWhyNot();
    sameSeedSameLook();
    lockedControlsNeverChange();
    neverRolledFieldsNeverChange();
    aRollOnAStyleTouchesOnlyWhatItCanShow();
    aTabRollTouchesOnlyThatTab();
    resetTabRestoresExactlyItsFields();
    rollsStayUnderTheFlashClamp();
    rollsStayInsideTheSchemasRanges();
    choiceRollsReachEverySchemaValue();
    aPaletteRollDropsACustomPalette();
    theWireRoundTrips();
    aHostilePresetIsClampedToTheSchemaRanges();
    overflowingNumbersReadAsTheDefaults();
    aRetiredSlotReadsAsItsDefault();
    if (failures == 0) std::printf("params_test: all checks passed\n");
    return failures;
}
