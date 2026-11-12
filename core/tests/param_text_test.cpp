// Host-side checks for the text a Customize control shows for its value (core/viz/ParamText) and its C ABI
// (geode_param_text). Built and run by CI ("Native host tests") with the system compiler; no device.
//
// What is pinned: every text the Kotlin formatter this replaced produced (its test's cases, one for one), the
// unit each control reads in, and the half rounding away from zero that Kotlin's "%.2f" did.
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

#include "api/geode_api.h"
#include "viz/ParamText.hpp"

using geode::viz::paramText;
using geode::viz::ParamUnit;
using geode::viz::paramUnitFor;

namespace {

int failures = 0;
const float kNone = std::numeric_limits<float>::quiet_NaN();

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

void reads(float value, ParamUnit unit, const std::string& want, bool bipolar = false, float def = kNone) {
    const std::string got = paramText(value, unit, def, bipolar);
    if (got != want) {
        std::printf("FAIL: wanted \"%s\", got \"%s\"\n", want.c_str(), got.c_str());
        ++failures;
    }
}

void percentages() {
    reads(0.35f, ParamUnit::Percent, "35 %");
    reads(0.35f, ParamUnit::Percent, "+35 %", true);
    reads(-0.2f, ParamUnit::Percent, "\xE2\x88\x92" "20 %", true);
    reads(0.0f, ParamUnit::Percent, "0 %", true);
    reads(-0.05f, ParamUnit::Percent, "-5 %");
}

void multipliers() {
    reads(1.8f, ParamUnit::Times, "1.8\xC3\x97");
    reads(0.25f, ParamUnit::Times, "0.25\xC3\x97");
    reads(2.0f, ParamUnit::Times, "2\xC3\x97");
    reads(-1.5f, ParamUnit::Times, "\xE2\x88\x92" "1.5\xC3\x97");
    reads(-0.001f, ParamUnit::Times, "0\xC3\x97");
    reads(100.0f, ParamUnit::Times, "100\xC3\x97");
}

void halvesRoundAwayFromZero() {
    reads(0.125f, ParamUnit::Plain, "0.13");
    reads(0.625f, ParamUnit::Plain, "0.63");
    reads(-0.125f, ParamUnit::Plain, "\xE2\x88\x92" "0.13");
    reads(1.25f, ParamUnit::Seconds, "1.3 s");
    reads(0.0625f, ParamUnit::Seconds, "63 ms");
    reads(-0.0625f, ParamUnit::Seconds, "\xE2\x88\x92" "62 ms");
}

void timesAndFrequencies() {
    reads(2.5f, ParamUnit::Seconds, "2.5 s");
    reads(0.035f, ParamUnit::Seconds, "35 ms");
    reads(1.0f, ParamUnit::Seconds, "1.0 s");
    reads(-2.0f, ParamUnit::Seconds, "\xE2\x88\x92" "2.0 s");
    reads(110.0f, ParamUnit::Hertz, "110 Hz");
    reads(0.25f, ParamUnit::Hertz, "0.25 Hz");
    reads(2300.0f, ParamUnit::Hertz, "2.3 kHz");
    reads(0.25f, ParamUnit::TurnDegrees, "90\xC2\xB0");
    reads(-0.5f, ParamUnit::TurnDegrees, "\xE2\x88\x92" "180\xC2\xB0");
    reads(12.5f, ParamUnit::Plain, "12.5");
}

void theDefaultReadsDefault() {
    reads(1.0f, ParamUnit::Times, "Default", false, 1.0f);
    reads(1.2f, ParamUnit::Times, "1.2\xC3\x97", false, 1.0f);
    reads(1.00005f, ParamUnit::Times, "Default", false, 1.0f);
}

void unitsComeFromTheControlThenItsRange() {
    check(paramUnitFor("Speed", 0.05f, 4.0f) == ParamUnit::Times, "Speed is a multiplier");
    check(paramUnitFor("Hue shift", 0.0f, 1.0f) == ParamUnit::TurnDegrees, "Hue shift is a turn");
    check(paramUnitFor("Fundamental (Hz)", 40.0f, 440.0f) == ParamUnit::Hertz, "the fundamental is a frequency");
    check(paramUnitFor("Particle life (s)", 0.5f, 20.0f) == ParamUnit::Seconds, "particle life is a time");
    check(paramUnitFor("Fluid curl", 0.0f, 50.0f) == ParamUnit::Plain, "fluid curl has no unit");
    check(paramUnitFor("Vignette", 0.0f, 1.0f) == ParamUnit::Percent, "0..1 is a percentage");
    check(paramUnitFor("Twist", -1.0f, 1.0f) == ParamUnit::Percent, "-1..1 is a percentage");
    check(paramUnitFor("Unknown gain", 0.0f, 2.0f) == ParamUnit::Times, "0..2 is a multiplier");
    check(paramUnitFor("Unknown big", 0.0f, 50.0f) == ParamUnit::Plain, "a wider range is plain");
}

std::string abi(const char* key, int unit, float value, float lo, float hi, float def) {
    char text[96];
    const int need = geode_param_text(key, unit, value, lo, hi, def, text, sizeof text);
    check(need == static_cast<int>(std::string(text).size()), "the C ABI returns the text's length");
    return text;
}

void theAbiPicksTheUnitAndTheSign() {
    check(abi("Twist", -1, -0.5f, -1.0f, 1.0f, 0.0f) == "\xE2\x88\x92" "50 %", "a control's own unit, signed below zero");
    check(abi("Twist", -1, 0.0f, -1.0f, 1.0f, 0.0f) == "Default", "the default by key");
    check(abi("Speed", -1, 1.8f, 0.05f, 4.0f, kNone) == "1.8\xC3\x97", "Speed by key");
    check(abi("", 2, 0.035f, 0.0f, 0.0f, kNone) == "35 ms", "a named unit needs no key");
    check(abi(nullptr, 99, 0.5f, 0.0f, 1.0f, kNone) == "50 %", "an unknown unit falls to the range");
    char small[4];
    check(geode_param_text("", 2, 2.5f, 0.0f, 0.0f, kNone, small, sizeof small) == 5 && std::string(small) == "2.5",
          "a short buffer gets what fits and the needed length");
    check(geode_param_text("", 2, 2.5f, 0.0f, 0.0f, kNone, nullptr, 0) == 5, "no buffer still measures");
}

}  // namespace

int main() {
    percentages();
    multipliers();
    halvesRoundAwayFromZero();
    timesAndFrequencies();
    theDefaultReadsDefault();
    unitsComeFromTheControlThenItsRange();
    theAbiPicksTheUnitAndTheSign();
    if (failures == 0) std::printf("param_text_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
