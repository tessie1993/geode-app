#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "viz/Params.hpp"

// What each Customize control draws as, on this look and this style: the widget, the scene param it writes,
// the words on it, its value, range, default, unit and steps, and whether it draws at all. Pure: no GL, no JNI.
//
// WHY native (ADR 0003, owner 2026-11-12): which widget a control is, which words it shows and when it hides
// (Catch pull with no catch points, Liquid's sliders with Liquid off, Fluid's glow sliders with its glow off)
// are decisions about the engine's params, so they sit beside the map that files the controls
// (CustomizeMap) and the shell only draws the rows this hands it. Host-tested in core/tests/customize_rows_test.cpp.
namespace geode::viz::customize {

// Bumped whenever a row type or field changes meaning.
constexpr int kRowsVersion = 1;

// What a control draws as.
enum class Widget {
    Slider,     // a float param
    IntSlider,  // a whole-number param, one tick per value
    Check,      // a switch
    Chips,      // a keepable pick from the param's choices
    Pick,       // a pick from the param's choices that no roll moves, so no kept mark
    Palette,    // Look's palette: the artwork button, then the stack or the palette list
    Geometry,   // Cymatics' plate: a pick, or a tip where the style holds it
    Liquid,     // Liquid's switch
    Piece,      // a panel piece the shell draws with a composable of its own ("@id")
};

// When a control draws, beyond reaching the style.
enum class Rule {
    Always,
    CatchPoints,   // with at least one catch point
    DiveOrLiquid,  // with the endless zoom on, or Liquid on (Liquid dives on any style)
    Liquid,        // with Liquid on
    FluidStyle,    // on a look of the Fluid solver
    FluidGlow,     // with Fluid's glow on
    Sunrays,       // with Fluid's sunrays on
    Unsearched,    // while the search box is empty: a whole page, no one row to match
    Transition,    // while a style transition is chosen
};

struct Control {
    const char* key;    // the Customize map's name: a ParamTable.inc meta label, or "@id"
    Widget widget;
    const char* field;  // the scene param it writes ("" for none)
    const char* label;  // the words on it; nullptr reads the key
    int unit;           // ParamUnit's wire (viz/ParamText.hpp) to name, or -1 for the control's own
    Rule rule;
    bool noDefault;     // no Reset and no "Default" on the value
};

// Every control the map places, in no particular order; customize_rows_test holds it to the map.
const std::vector<Control>& controls();

// The shell's state a row reads besides the look.
struct RowContext {
    std::string styleId;     // "" for any style
    std::string query;       // the panel's search text; blank shows everything
    bool transition = false;  // a style transition is chosen
    int liquidMode = 0;       // the renderer's Liquid mode: 0 off, 1 full
};

// The rows for keys (one control key a line, a section's order), skipping what does not draw here.
// Rows, '|'-separated, one per line, after a header:
//   rows|<kRowsVersion>
//   SLIDER|<key>|<field>|<label>|<value>|<min>|<max>|<default, empty for none>|<unit or -1>|<steps>
//   INT|<key>|<field>|<label>|<value>|<min>|<max>
//   CHECK|<key>|<field>|<label>|<0 or 1>
//   CHIPS|<key>|<field>|<label>|<selected>|<choice>|<choice>...
//   PICK|<key>|<field>|<label>|<selected>|<choice>|<choice>...
//   HINT|<key>|<text>                       a tip where a control would be (shown with Show tips)
//   NOTE|<key>|<text>                       a warning under the row above it
//   SHELL|<key>|<source>|<label>|<min>|<max>|<unit or -1>   a slider over a value the shell holds:
//                                           source ATTACK, DECAY or TRANSITION
//   PIECE|<id>|<lock key>|<label>|<text>    a piece the shell draws by id; label names it to Pro and the
//                                           screen reader, text is its button's words
// Numbers in viz/TableNumber's form.
std::string rows(std::string_view keys, const SceneParams& p, const RowContext& context);

// Whether text matches the panel's search: blank matches, else a case-blind part of it.
bool matches(std::string_view query, std::string_view text);

}  // namespace geode::viz::customize
