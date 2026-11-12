// Host-side checks for the config params schema (core/viz/ConfigTable.inc, expanded by core/viz/Config.hpp
// and written out by core/viz/ConfigFields.cpp, #423): the LFO and envelope slots, the palette maker's
// gradient, the look's envelope and the preset morph. The rows are well formed,
// viz/Lfo and viz/Adsr clamp exactly as their old literals did, the store
// conversions in viz/ConfigMath.hpp equal the Kotlin formulas, and the text reads back to the rows.
// Built and run by CI (.github/workflows/android.yml, "Native host tests") with the system compiler; no GL.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "api/geode_api.h"
#include "viz/Adsr.hpp"
#include "viz/Config.hpp"
#include "viz/ConfigMath.hpp"
#include "viz/Lfo.hpp"

using namespace geode::viz;
using config::Kind;
using config::Slot;
using config::Spec;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

bool near(float a, float b) { return std::fabs(a - b) <= 1e-5f; }

bool integral(float v) { return std::floor(v) == v; }

std::string label(const Spec& s) { return std::string(s.group) + "." + s.name; }

int rowsIn(const std::string& group) {
    int n = 0;
    for (const Spec& s : config::kTable) n += group == s.group ? 1 : 0;
    return n;
}

void everyRowIsWellFormed() {
    std::set<std::string> seen;
    std::set<std::string> groupsDone;
    std::string lastGroup;
    for (int i = 0; i < config::kCount; ++i) {
        const Spec& s = config::kTable[static_cast<size_t>(i)];
        const std::string name = label(s);
        check(std::string(s.name).size() > 0, "row " + std::to_string(i) + " has a name");
        check(seen.insert(name).second, name + " is named once");
        // A group's rows sit together, so indexInGroup is a position on its wire.
        if (s.group != lastGroup) {
            check(groupsDone.insert(s.group).second, name + ": its group's rows are contiguous");
            lastGroup = s.group;
        }
        check(s.min <= s.max, name + ": min <= max");
        check(s.defaultValue >= s.min && s.defaultValue <= s.max, name + ": the default lies within [min, max]");
        if (s.kind == Kind::Int || s.kind == Kind::Bool) {
            check(integral(s.min) && integral(s.max) && integral(s.defaultValue), name + ": a count has whole bounds");
        }
        if (s.kind == Kind::Bool) check(s.min == 0.0f && s.max == 1.0f, name + ": a flag is 0..1");
    }
}

void theGroupsAreTheWires() {
    check(rowsIn("lfo") == GEODE_LFO_CONFIG_FLOATS, "one lfo row per geode_viz_set_lfo float");
    check(rowsIn("adsr") == GEODE_ADSR_CONFIG_FLOATS, "one adsr row per geode_viz_set_adsr float before the targets");
    // The six macro rows went with the old knobs: the macro bank (viz/MacroBank) keeps its own text.
    check(rowsIn("macro") == 0, "no macro rows");
    // geode_viz_set_lfo: enabled, source, target, wave, rate_seconds, depth, polarity, curve, beats, knob, moment.
    check(config::indexInGroup(Slot::lfo_enabled) == 0, "lfo enabled is float 0");
    check(config::indexInGroup(Slot::lfo_rateSeconds) == 4, "lfo rate is float 4");
    check(config::indexInGroup(Slot::lfo_depth) == 5, "lfo depth is float 5");
    check(config::indexInGroup(Slot::lfo_curve) == 7, "lfo curve is float 7");
    check(config::indexInGroup(Slot::lfo_beats) == 8, "lfo beats is float 8");
    check(config::indexInGroup(Slot::lfo_knob) == 9 && config::indexInGroup(Slot::lfo_moment) == 10, "lfo knob and moment are floats 9 and 10");
    // geode_viz_set_adsr: enabled, attack, decay, sustain, release, amount, band, gate, sustain track, retrigger.
    check(config::indexInGroup(Slot::adsr_attack) == 1, "adsr attack is float 1");
    check(config::indexInGroup(Slot::adsr_band) == 6, "adsr band is float 6");
    check(config::indexInGroup(Slot::adsr_gateThreshold) == 7, "adsr gate is float 7");
    check(config::indexInGroup(Slot::adsr_retrigger) == 9, "adsr retrigger is float 9");
    check(config::indexInGroup(Slot::adsr_knob) == 10 && config::indexInGroup(Slot::adsr_moment) == 11, "adsr knob and moment are floats 10 and 11");
}

// The literals Lfo and Adsr held before they read the table, and the Kotlin defaults beside them.
void theOldLiteralsHold() {
    check(LfoConfig::kDefaultRateSeconds == 2.0f, "LFO default period 2 s");
    check(LfoConfig::kMinRateSeconds == 0.05f, "LFO shortest period 0.05 s");
    check(LfoConfig::kMaxRateSeconds == 60.0f, "LFO longest period 60 s");
    const LfoConfig lfo{};
    check(lfo.rateSeconds == 2.0f && lfo.depth == 0.3f, "LfoConfig{} is 2 s at depth 0.3 (LfoStore's 0.3)");
    check(LfoEngine::kMaxModulatedDepth == 2.0f, "a modulated depth still tops out at 2");
    const AdsrConfig env{};
    check(env.attack == 0.05f && env.decay == 0.25f && env.sustain == 0.5f && env.release == 0.35f, "AdsrConfig{} stages");
    check(env.amount == 0.5f && env.gateThreshold == 0.25f, "AdsrConfig{} amount and gate");
    check(!env.enabled && !env.sustainTrack && env.retrigger, "AdsrConfig{} flags");
    check(AdsrEngine::kMinStageSeconds == 0.005f, "an envelope stage is at least 5 ms");
    check(AdsrEngine::kMinGateThreshold == 0.05f, "a tracking sustain divides by at least 0.05");
}

// One frame of slot 0 as a saw on Zoom (not a luminance target, so no flash cap), from phase 0.
float sawAfter(float rateSeconds, float depth, float dt) {
    LfoEngine engine;
    LfoConfig& c = engine.configs[0];
    c.enabled = true;
    c.target = LfoTarget::Zoom;
    c.wave = LfoWave::Saw;
    c.rateSeconds = rateSeconds;
    c.depth = depth;
    const GeodeSoundFrame sound{};
    return engine.tick(dt, sound)[0];
}

void lfoClampsAsBefore() {
    // 0.01 s is held to 0.05 s (20 Hz): 0.01 s in, the saw is at phase 0.2, so -0.6.
    check(near(sawAfter(0.01f, 1.0f, 0.01f), sawAfter(0.05f, 1.0f, 0.01f)), "a period under 0.05 s runs at 0.05 s");
    check(near(sawAfter(0.05f, 1.0f, 0.01f), -0.6f), "0.05 s is 20 Hz");
    check(near(sawAfter(600.0f, 1.0f, 1.0f), sawAfter(60.0f, 1.0f, 1.0f)), "a period over 60 s runs at 60 s");
    // Depth: up to 2 with no modulation, never past it, never below 0.
    check(near(sawAfter(0.05f, 5.0f, 0.01f), -1.2f), "depth 5 runs at 2");
    check(near(sawAfter(0.05f, 2.0f, 0.01f), -1.2f), "depth 2 runs at 2");
    check(near(sawAfter(0.05f, -1.0f, 0.01f), 0.0f), "a negative depth runs at 0");
}

// One envelope on Zoom, hit by a note start on the first frame.
struct Env {
    AdsrEngine engine;
    GeodeSoundFrame hit{};
    GeodeSoundFrame quiet{};
    Env(float attack, float decay, float release) {
        AdsrConfig& c = engine.configs[0];
        c.enabled = true;
        c.targets = {LfoTarget::Zoom};
        c.attack = attack;
        c.decay = decay;
        c.sustain = 0.0f;
        c.release = release;
        hit.onset = 1.0f;
        hit.bassAtt = 2.0f;
        quiet.bassAtt = 2.0f;
    }
    float step(const GeodeSoundFrame& f, float dt) { return engine.tick(dt, f)[0]; }
};

void adsrClampsAsBefore() {
    // Attack: 1 ms of a 5 ms rise is 0.2; anything shorter is floored to 5 ms.
    for (float attack : {0.0f, 0.002f, 0.005f}) {
        Env e(attack, 0.25f, 0.35f);
        check(near(e.step(e.hit, 0.001f), 0.2f), "attack " + std::to_string(attack) + " s rises as 5 ms");
    }
    {
        Env e(0.01f, 0.25f, 0.35f);
        check(near(e.step(e.hit, 0.001f), 0.1f), "attack 10 ms rises half as fast");
    }
    // Decay: reach the top in one 5 ms frame, then 1 ms of decay toward sustain 0.
    for (float decay : {0.0f, 0.002f, 0.005f}) {
        Env e(0.005f, decay, 0.35f);
        e.step(e.hit, 0.005f);
        check(near(e.step(e.quiet, 0.001f), 0.8f), "decay " + std::to_string(decay) + " s falls as 5 ms (the slider starts at 0.01 s)");
    }
    {
        Env e(0.005f, 0.01f, 0.35f);
        e.step(e.hit, 0.005f);
        check(near(e.step(e.quiet, 0.001f), 0.9f), "decay 10 ms falls half as fast");
    }
}

// The Kotlin formulas, written as Kotlin writes them, to hold the native conversions to.
float kotlinRateFromHz(float hz) {
    if (hz <= 0.0f) return 2.0f;  // LfoConfig.DEFAULT_RATE_SECONDS
    const float s = 1.0f / hz;
    return s < 0.05f ? 0.05f : (s > 60.0f ? 60.0f : s);  // coerceIn(MIN_RATE_SECONDS, MAX_RATE_SECONDS)
}

float kotlinSampleHue(float baseHue, float hueSpan, int index, int stops) {
    const float t = stops <= 1 ? 0.0f : static_cast<float>(index) / static_cast<float>(stops - 1);
    const float h = baseHue + hueSpan * t;
    return std::fmod(std::fmod(h, 1.0f) + 1.0f, 1.0f);  // ((h % 1f) + 1f) % 1f
}

void conversionsMatchKotlin() {
    for (float hz : {0.5f, 4.0f, 0.25f, 3.0f, 100.0f, 20.0f, 0.001f, 1.0f / 60.0f, 0.0f, -1.0f}) {
        check(config::lfoRateSecondsFromHz(hz) == kotlinRateFromHz(hz), "rateHz " + std::to_string(hz) + " migrates as LfoStore does");
    }
    check(config::lfoRateSecondsFromHz(0.5f) == 2.0f && config::lfoRateSecondsFromHz(4.0f) == 0.25f, "0.5 Hz is 2 s, 4 Hz is 0.25 s");
    check(config::lfoRateSecondsFromHz(100.0f) == 0.05f && config::lfoRateSecondsFromHz(0.001f) == 60.0f, "held to 0.05..60 s");
    check(config::lfoRateSecondsFromHz(std::nanf("")) == 2.0f, "a NaN rate reads as the default period");

    struct Hue {
        float base, span;
        int index, stops;
        float want;
    };
    for (const Hue& h : {Hue{0.0f, 1.0f, 0, 9, 0.0f}, Hue{0.0f, 1.0f, 8, 9, 0.0f}, Hue{0.25f, 0.5f, 4, 9, 0.5f},
                         Hue{0.9f, 0.5f, 4, 9, 0.15f}, Hue{0.2f, -0.5f, 8, 9, 0.7f}, Hue{0.6f, 1.0f, 3, 1, 0.6f},
                         Hue{1.0f, 0.0f, 0, 9, 0.0f}, Hue{0.1f, 0.3f, 2, 5, 0.25f}}) {
        const float got = config::paletteSampleHue(h.base, h.span, h.index, h.stops);
        const std::string what = "sampleHue(" + std::to_string(h.base) + ", " + std::to_string(h.span) + ", " + std::to_string(h.index) + ")";
        check(got == kotlinSampleHue(h.base, h.span, h.index, h.stops), what + " is PaletteStore's");
        check(std::fabs(got - h.want) <= 1e-5f, what + " lands on its hue");
        check(got >= 0.0f && got < 1.0f, what + " wraps into 0..1");
    }
    check(config::paletteSampleHue(0.25f, 0.5f, 4) == config::paletteSampleHue(0.25f, 0.5f, 4, 9), "nine preview stops by default");
}

void clampHelpers() {
    check(config::clampTo(Slot::look_decay, 10.0f) == 1.5f && config::clampTo(Slot::look_decay, 0.0f) == 0.01f, "clampTo holds the range");
    check(config::clampTo(Slot::look_attack, 0.3f) == 0.3f, "clampTo keeps a value inside");
    // The six Studio clip-edit rows went with the Studio project render (ADR 0007): no store held them.
    check(rowsIn("clip") == 0, "no clip rows");
    check(std::isnan(config::clampTo(Slot::look_attack, std::nanf(""))), "clampTo lets a NaN through, as coerceIn does");
    check(config::sanitized(Slot::look_attack, std::nanf("")) == 0.6f, "sanitized reads a NaN as the default");
    check(config::sanitized(Slot::look_decay, 9.0f) == 1.5f, "sanitized clamps a finite value");
}

std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> out;
    std::stringstream in(line);
    for (std::string part; std::getline(in, part, '|');) out.push_back(part);
    return out;
}

void theTextReadsBack() {
    std::istringstream in(config::schemaText());
    std::string line;
    std::getline(in, line);
    check(line == "configs|" + std::to_string(config::kTextVersion) + "|" + std::to_string(config::kCount), "the header names the version and count");
    int row = 0;
    while (std::getline(in, line)) {
        const auto f = split(line);
        check(f.size() == 9 && f[0] == "config", "line " + std::to_string(row) + " is a config row: " + line);
        if (f.size() != 9 || row >= config::kCount) {
            ++row;
            continue;
        }
        const Slot slot = static_cast<Slot>(row);
        const Spec& s = config::spec(slot);
        const std::string name = label(s);
        check(f[1] == s.group && f[3] == s.name, name + ": group and name");
        check(f[2] == std::to_string(config::indexInGroup(slot)), name + ": index in group");
        const char* kind = s.kind == Kind::Float ? "FLOAT" : s.kind == Kind::Int ? "INT" : "BOOL";
        check(f[4] == kind, name + ": kind");
        check(std::strtof(f[5].c_str(), nullptr) == s.defaultValue, name + ": the default reads back");
        check(std::strtof(f[6].c_str(), nullptr) == s.min, name + ": min reads back");
        check(std::strtof(f[7].c_str(), nullptr) == s.max, name + ": max reads back");
        ++row;
    }
    check(row == config::kCount, "one line per row");
    const std::string text = config::schemaText();
    check(text.find("config|lfo|4|rateSeconds|FLOAT|2|0.05|60|SECONDS\n") != std::string::npos, "the LFO period row as the shell reads it");
    check(text.find("config|macro|") == std::string::npos, "no knob rows");
    check(text.find("config|adsr|9|retrigger|BOOL|1|0|1|NONE\n") != std::string::npos, "a flag row");
}

}  // namespace

int main() {
    everyRowIsWellFormed();
    theGroupsAreTheWires();
    theOldLiteralsHold();
    lfoClampsAsBefore();
    adsrClampsAsBefore();
    conversionsMatchKotlin();
    clampHelpers();
    theTextReadsBack();
    if (failures) {
        std::printf("%d config schema check(s) failed\n", failures);
        return 1;
    }
    std::printf("config schema: %d rows OK\n", config::kCount);
    return 0;
}
