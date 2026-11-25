#pragma once
#include <string>

#include "viz/modulation/Responder.hpp"
#include "viz/params/Params.hpp"

// What the Song events page needs to know about responses and does not keep: the families and the tiles of the
// "+ Add response" sheet, each kind's own controls (labels, units, ranges, choices), the envelope presets (Snap,
// Pulse, Swell, Stay as fade in / hold / fade out), the row names, and the words a response's numbers read as,
// an amount as a percentage of the style's own value. The shell reads text() once and asks fieldText() and
// title() for what it draws; it holds no list, range or default of its own. Pure: the host tests build it.
namespace geode::viz::responder::catalog {

// Bumped whenever a row type or field changes meaning.
constexpr int kTextVersion = 1;
constexpr int kFamilyCount = 7;
constexpr int kPresetCount = 4;
constexpr int kCurveCount = 4;

// How a control's number reads, by wire ordinal; append only.
//   Amount    a parameter response's amount: a percentage of the style's own value (fieldText)
//   Percent   0..1 as 0 to 100 %
//   Count     a whole number
//   CountOff  a whole number, "Off" at 0
//   Seconds   2.5 s, or 35 ms under one second
//   Choice    one of the control's words, by ordinal
enum class Unit { Amount = 0, Percent, Count, CountOff, Seconds, Choice };

struct Preset {
    const char* name;
    float fadeIn;
    float hold;
    float fadeOut;
};

const Preset& preset(int index);
// The preset a response's envelope is, or -1 when it is none of them.
int presetOf(const Response& r);
// Sets fade in, hold and fade out; false for an index that is no preset.
bool applyPreset(Response& r, int index);

int entryCount();
// The response a tile adds on `row`, with its kind, target, kind-params, envelope and drive; false for no such tile
// (or one whose target this build lacks).
bool make(int entry, int row, Response& out);

// What a chip says: the parameter it moves, or the kind and what it draws or does.
std::string title(const Response& r);
// One field of a response as words, on a look (the style's own values the amount is a percentage of).
std::string fieldText(const Response& r, int field, const SceneParams& look);

// One line per row, fields split by '|', in this order:
//   responses|<kTextVersion>
//   family|<ordinal>|<name>
//   preset|<ordinal>|<name>|<fade in s>|<hold s>|<fade out s>
//   curve|<ordinal>|<name>|<sentence>              the strength curve, ModCurve's ordinals
//   drive|<ordinal>|<name>|<sentence>
//   category|<ordinal>|<name>                      the rows' groups
//   row|<Moment ordinal>|<NAME>|<label>|<category>|<1 when it needs a heard beat>     in the page's order
//   entry|<ordinal>|<family>|<kind>|<name>|<one line>                                the tiles
//   kind|<kind>|<name>
//   control|<kind>|<field>|<label>|<Unit>|<min>|<max>|<step>|<choice>...             a kind's own controls
//   common|<field>|<label>|<Unit>|<min>|<max>|<step>                                the controls every response has
const std::string& text();

}  // namespace geode::viz::responder::catalog
