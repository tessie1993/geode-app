// Host-side checks for the Customize randomizer, Reset tab and the A/B blend
// (core/viz/ParamRandomizer, core/viz/Params). Built and run by CI
// (.github/workflows/android.yml, "Params host tests") with the system compiler; no GL.
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
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

// Equal up to the rounding of a + (b - a) * k.
bool near(const std::vector<float>& x, const std::vector<float>& y) {
    for (size_t i = 0; i < x.size(); ++i) {
        if (std::fabs(x[i] - y[i]) > 1e-5f * std::fmax(1.0f, std::fabs(y[i]))) return false;
    }
    return x.size() == y.size();
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
    for (const char* inert : {"pulse", "shake", "beatResponse", "flash", "strobe", "formDrive"}) {
        bool listed = false;
        for (const auto& entry : randomizer::neverRolled()) listed = listed || std::string(entry.first) == inert;
        check(listed, std::string(inert) + " is inert, so never rolled");
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
    for (const char* style : {"plasma", "vanishing", "silk_web", "fluid_ink", "water", "milkdrop", "kundt_tube", "curlflow"}) {
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
        check(p.bloom <= 0.25f && p.glitch <= 0.25f, "bloom and glitch stay under the flash depth");
        check(p.brightness <= 1.25f && p.contrast <= 1.25f && p.intensity <= 1.25f, "levels stay under 1 + flash depth");
        check(p.reactSeed >= 1 && p.reactSeed < (1 << 24), "the DNA seed survives the float wire");
        check(p.symmetry >= 0 && p.cymaticsModes >= 2 && p.cymaticsModes <= 8, "counts stay in range");
        check(!p.kaleidoscope || p.symmetry >= 2, "a kaleidoscope gets real folds");
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

void blendEndpointsAreTheSnapshots() {
    const SceneParams a = kDefaults;
    const SceneParams b = everythingChanged();
    check(near(wire(blendParams(a, b, 0.0f)), wire(a)), "blend at 0 is A");
    check(near(wire(blendParams(a, b, 1.0f)), wire(b)), "blend at 1 is B");
    const SceneParams mid = blendParams(a, b, 0.5f);
    check(std::fabs(mid.speed - (a.speed + b.speed) * 0.5f) < 1e-5f, "floats glide");
    check(mid.palette == b.palette && blendParams(a, b, 0.49f).palette == a.palette, "choices snap at the halfway point");
    check(mid.paramFadeSec == b.paramFadeSec, "the fade's own time constant snaps");
}

void theWireRoundTrips() {
    const SceneParams p = everythingChanged();
    const auto w = wire(p);
    check(wire(SceneParams::fromWire(w.data())) == w, "fromWire(toWire(p)) == p");
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
    aPaletteRollDropsACustomPalette();
    blendEndpointsAreTheSnapshots();
    theWireRoundTrips();
    if (failures == 0) std::printf("params_test: all checks passed\n");
    return failures;
}
