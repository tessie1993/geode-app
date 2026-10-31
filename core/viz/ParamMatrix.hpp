#pragma once
#include <array>
#include <cstddef>
#include <string>

#include "sound/SoundFrame.h"
#include "viz/Lfo.hpp"
#include "viz/Params.hpp"
#include "viz/Reactions.hpp"

namespace geode::viz {

// The parameter matrix: every sound value the engine hears (the rows) and every visual parameter a
// route, an LFO or an envelope can move (the columns), listed once, here. Everything that wires sound
// to picture reads these two tables: the reaction matrix (Reactions.cpp) for how a source is shaped,
// applyLfoTarget for how a target is written, the safety layer for which targets are luminance, the
// style DNA and the Customize grid (over JNI, geode_param_matrix_catalog) for names, labels and
// groups, and the grid's smart mode for what a tap on a cell means.
//
// To wire a new sound value: append it to ReactSource (and the Kotlin mirror) and add one row to
// kSources. To wire a new visual parameter: append it to LfoTarget (and the Kotlin mirror) and add one
// row to kTargets. The static_asserts in ParamMatrix.cpp fail the build if a row is missing or out of
// order. Both enums are wire ordinals: append only.
namespace matrix {

// How a GeodeSoundFrame field becomes a route's 0..1.
enum class SourceKind {
    Relative,  // ~1.0 typical, 2 a strong hit, never past 4: halved, so typical reads 0.5
    Unit,      // already 0..1
    Signed,    // already -1..1
    Clock,     // a clock that only moves forward: its rate per second, times `scale`
    Trigger,   // > 0 on the one frame it fires: a held envelope (instant rise, `scale` seconds fall)
};

struct SourceRow {
    ReactSource id;
    const char* name;   // the stable saved name
    const char* label;  // what the picker and the grid show
    const char* group;  // LEVELS, DRUMS, EVENTS, MELODY, CLOCKS, TEXTURE, EQ
    size_t offset;      // the field in GeodeSoundFrame
    SourceKind kind;
    float scale;        // Clock: rate scale; Trigger: fall seconds
    bool event() const { return kind == SourceKind::Trigger; }
};

// How a route's output (amount × shaped source, about -2..2) lands on a SceneParams field:
// field += v × scale, clamped to [min, max] unless `wraps` (a hue goes round).
struct TargetRow {
    LfoTarget id;
    const char* name;   // the stable saved name
    const char* label;
    const char* group;  // MOTION, SHAPE, COLOUR, FX, FLUID, CAMERA, LAYERS, SLOTS
    float SceneParams::*field;  // null for the slot chains, which steer an LFO instead
    float scale;
    float min;
    float max;
    bool luminance;     // moves brightness or colour: slower envelopes and the flash budget apply
    bool wraps;
};

constexpr int kTargetCount = static_cast<int>(LfoTarget::CamShake) + 1;

const SourceRow& source(ReactSource s);
const TargetRow& target(LfoTarget t);
const std::array<SourceRow, kReactSourceCount>& sources();
const std::array<TargetRow, kTargetCount>& targets();

// What a tap on a grid cell means for this source: a hit pulses (Follow on its held envelope), a level
// follows, a clock keeps turning (Spin), a big moment or a counted hit jumps somewhere new (Jump), a
// toggle swaps (Toggle). The attack and release suit the source's own speed.
struct SmartRoute {
    ReactMode mode;
    float attackSeconds;
    float releaseSeconds;
};
SmartRoute smart(ReactSource s);

// One route from a cell: smart mode and timing, the given depth.
ReactRoute cell(ReactSource s, LfoTarget t, float amount);

// "S|NAME|Label|GROUP|event\n" per source, then "T|NAME|Label|GROUP|luminance\n" per target, in wire
// order: what Kotlin parses (ParamMatrixCatalog).
std::string catalogueText();

}  // namespace matrix
}  // namespace geode::viz
