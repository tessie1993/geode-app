#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "viz/Params.hpp"

// Customize › Look's palette pages as the shell draws them: the stack card (Palettes), the palette list on
// a style the stack does not draw, and Your colours' stop editor. Pure: no GL, no JNI.
//
// WHY native (ADR 0003, owner 2026-11-12): how many stops a palette may hold, which stop a tap selects, what
// a new stop copies, how many stops an old hue gradient opens as, which rows a stack shows (Blend with two
// layers or more, Add while there is room), which amount param a layer reads and what the list selects are
// decisions about the engine's palettes, so they sit beside them (viz/Palette, viz/Colour) and the shell
// only draws the rows. Host-tested in core/tests/palette_panel_test.cpp.
namespace geode::viz::palettepanel {

constexpr int kTextVersion = 1;
// Colours a swatch strip draws.
constexpr int kSwatchSamples = 24;
// The stops an old hue gradient opens as in Your colours, and as a stack layer.
constexpr int kMakerHueStops = 4;
constexpr int kStackHueStops = 5;

// Your colours' editor: the stops being made and the one selected. Text, so the shell holds it as one value:
//   <selected>|<RRGGBB>,<RRGGBB>,...
struct Maker {
    std::vector<int> stops;  // 1..colour::kMaxStops packed colours
    int selected = 0;
};
std::string encode(const Maker& m);
// Unreadable text, or text with no colour, is one white stop.
Maker decodeMaker(std::string_view text);

// A palette opened in the editor: its stops when it has any, else its hue gradient as kMakerHueStops stops.
Maker open(const int* stops, int count, float base, float span);

enum class Edit { Select = 0, Set = 1, Add = 2, Remove = 3 };
// Select: arg is the stop. Set: arg is the selected stop's new colour. Add: a copy of the selected stop
// goes last and is selected, up to colour::kMaxStops. Remove: the selected stop goes, never the last one.
Maker edit(Maker m, Edit op, int arg);

// The editor as the shell draws it:
//   maker|<kTextVersion>|<selected>|<removable 0/1>|<addable 0/1>|<swatch>
//   stop|<index>|<RRGGBB>|<selected 0/1>|<what a screen reader hears>
// swatch: kSwatchSamples colours of the loop, "RRGGBB" joined by ','.
std::string makerView(const Maker& m);

// A saved palette's colours as a stack layer: its stops, else its hue gradient as kStackHueStops stops.
std::vector<int> stackColours(const int* stops, int count, float base, float span);

// A saved palette's strip in Your palettes: its stops' loop, else its hue gradient; "RRGGBB" joined by ','.
std::string savedSwatch(const int* stops, int count, float base, float span);

// Look's palette list on a style the stack does not draw: the built-in palettes the schema lists, then the
// user's saved ones (savedIds, one id a line, in the shell's order), and which one the look holds.
//   list|<kTextVersion>|<selected or -1>|<placeholder>|<swatch>
//   option|<built-in index or -1>|<saved index or -1>|<name>
// savedNames is one name a line, in savedIds' order. The swatch is what the look's own palette paints.
std::string listView(const SceneParams& p, std::string_view customId, std::string_view savedIds, std::string_view savedNames);

// The stack card (viz/Palette's stack text) on a look:
//   card|<kTextVersion>|<heading>|<tip>
//   layer|<index>|<name>|<amount param>|<amount>|<min>|<max>|<movable up 0/1>|<up label>|<remove label>|<amount label>|<swatch>
//   blend|<label>|<selected>                      with two layers or more
//   blendoption|<SHARE or MIX>|<words>
//   add|<label>|<mark>                            while there is room
//   pick|<library index>|<name>
//   yours|<label>|<mark>                          while there is room and the user has a saved palette
std::string stackCard(std::string_view stack, const SceneParams& p, int savedCount);

}  // namespace geode::viz::palettepanel
