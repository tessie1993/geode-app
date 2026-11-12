#pragma once
#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace geode::viz::director {

// Every number the auto-director tunes itself by, as Auto Director settings (owner: "nothing hardcoded").
// The table is native: each setting's saved key, label, help text, range and default, which the Visuals
// settings page draws as it is given, and which AutoDirectorStore saves as the text native hands it. The
// defaults are the director's own numbers, so an untouched table directs exactly as before.
//
// Process-wide like the director's switch (setTuning), and pinned with it by an export
// (signal::EngineSettings), so a render in progress never hears an edit.
enum class Tune : int {
    LeanSpeed,        // the build's lean: how much faster
    LeanCloser,       // how much closer (the Space axis, down)
    LeanRough,        // how much rougher (the Chaos axis)
    LeanInBars,       // bars the lean takes to come in
    LeanOutBars,      // bars it takes to let go when a build fades without a drop
    GlideDrivenBars,  // how a look lands for the most driven feeling (power, joy)
    GlideCalmBars,    // ... and for the calmest (peace, tenderness, transcendence)
    ReturnGrowth,     // how far a returning section's look steps on, each return
    ReturnMorphBars,  // the shortest morph into a returning section's look (a return never cuts)
    ArcDepth,         // how far the song's arc travels
    ArcComplexity,    // the arc's share on the Complexity axis
    ArcDetail,        // ... on Detail
    ArcFractal,       // ... on Fractal
    ArcFormula,       // ... on Formula
    ArcPhrases,       // phrases until the arc stands half way to its full climb
    ArcBars,          // bars the arc takes to move to a new section's level
    LongSeconds,      // seconds with no section or drop before a new look anyway
    StylesProgress,   // 1: sections step the style along the catalogue; 0: it stays (a return brings its own back)
    kCount
};
constexpr int kTunes = static_cast<int>(Tune::kCount);

struct TuneSpec {
    const char* key;    // the saved name: never renamed, so a saved table keeps reading
    const char* label;  // what the settings page shows
    const char* help;   // one line under it
    float min;
    float max;
    float def;
    float step;         // 0 continuous; a 0..1 range with step 1 is a switch
    const char* unit;   // after the value: "", " bars", " s"
};
std::span<const TuneSpec> tuneSpecs();
const TuneSpec& spec(Tune t);
// An on/off row: 0..1 in steps of 1.
constexpr bool isSwitch(const TuneSpec& s) { return s.min == 0.0f && s.max == 1.0f && s.step == 1.0f; }

struct Tuning {
    std::array<float, kTunes> v{};
    Tuning();  // every default
    float operator[](Tune t) const { return v[static_cast<size_t>(t)]; }
};
const Tuning& defaultTuning();

// Reads saved text, one `key=value` a line. An unknown key or a bad number is skipped (a newer table's
// row on an older app, a hand edit), a missing one is its default, and each value is held to its range
// and snapped to its step.
Tuning parseTuning(std::string_view text);
// Every setting as `key=value` lines, in table order: what the store saves.
std::string tuningText(const Tuning& t);
// The saved text with setting `index` (tuneSpecs() order) set to `value`, held and snapped; an index
// outside the table returns the text as read.
std::string tuningWith(std::string_view text, int index, float value);
// The value of setting `index` as the page shows it ("0.30", "4 bars", "On"); "" outside the table.
std::string valueText(const Tuning& t, int index);

// Process-wide: the saved text (Visuals › Settings › Auto Director), and the table every live renderer's
// director reads. Never null.
void setTuning(std::string_view text);
std::shared_ptr<const Tuning> tuning();

}  // namespace geode::viz::director
