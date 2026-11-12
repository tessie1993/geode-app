#pragma once
#include <bitset>
#include <compare>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "viz/Lfo.hpp"
#include "viz/Params.hpp"

// The macro bank: the user's knobs, each a named value that moves a set of scene params the user
// picked. A knob decides nothing by itself: its only effect is a weighted offset on the fields its
// links name, added to whatever the look already holds (base plus modulation, so the slider under a
// knob still moves the picture) and held to the field's range. No sound routes, no sound tuning, no
// camera side path.
//
// The bank is process-wide (setGlobal) and the user's own: knobs can be added, copied, renamed, reordered and
// removed, and what a knob moves is edited link by link. A knob is on screen or in the bank's library (hideKnob,
// showKnob): one in the library is centred and moves nothing until it is put back. The four built-in knobs can be
// edited, put back as built (resetKnob) and taken off screen, never deleted, so the library always offers them. The shell keeps it as one opaque string (encode) and hands it
// back (decode); every edit is a pure function here, so Kotlin parses and decides nothing (ADR 0003).
// Pure: no GL, no JNI.
namespace geode::viz::macrobank {

// Bumped whenever a row type or field changes meaning; decode answers the default bank to any other.
constexpr int kTextVersion = 1;
// Knobs in the bank, on screen and in the library together; at most kMaxShown on screen.
constexpr int kMaxKnobs = 16;
constexpr int kMaxShown = 8;
constexpr int kMaxLinks = 12;
// UTF-8 bytes, cut on a character boundary.
constexpr int kMaxNameBytes = 24;
constexpr int kMaxHintBytes = 80;
constexpr int kMaxId = 999999;

// How a link follows the knob between centre and a full turn, the same each way: Linear moves it evenly,
// EaseIn barely at first and fast near the end, EaseOut the other way round, SCurve slow at both ends.
enum class Curve { Linear, EaseIn, EaseOut, SCurve };
constexpr int kCurveCount = 4;
const char* name(Curve curve);
std::optional<Curve> curveNamed(std::string_view name);
// t (0..1, how far the knob is turned) through the curve; 0 at 0 and 1 at 1 for every curve.
float shape(Curve curve, float t);

// What a link moves: a scene param, by its wire slot. Every live param a knob can offset is one: a float the look
// glides (Float or Circular, Fade) that a Customize control sets. Left out: Int fields (a picker, a count, a seed,
// a colour stop: an offset would jump between unrelated choices or rebuild a simulation), Bool switches, Hold
// floats (the palette's unset sentinels, the glide time), retired slots, fields no control sets, and the Note & key
// strengths, which NoteResponse reads before the bank runs, so a knob on them would move nothing. A param a
// modulation target writes is named as that target (MORPH); every other by its schema name (cymaticsRing).
struct Target {
    int slot = -1;
    Target() = default;
    // Implicit, so a link reads as the modulation target it is; None, a chained or a retired target is no param.
    Target(LfoTarget target);  // NOLINT(google-explicit-constructor)
    static Target ofSlot(int slot);
    bool operator==(const Target&) const = default;
    auto operator<=>(const Target&) const = default;
};
// The wire name; "" for no param.
std::string name(Target target);
// The Customize control that sets the param (its ParamKeys key), the label the editor shows; "" for none.
const char* controlOf(Target target);

// One param a knob moves. up is the offset at a full turn up and down the offset at a full turn
// down, each a signed fraction (-1..1) of the target's range: the link's own range, set apart each way,
// so a field resting at its low end can still be moved by turning down, or left alone with 0.
struct Link {
    Target target;
    float up = 0.0f;
    float down = 0.0f;
    Curve curve = Curve::Linear;
    bool operator==(const Link&) const = default;
};

struct Knob {
    // Stable for the knob's life and never handed out twice, so the shell can key by it.
    int id = 0;
    std::string name;
    // One line on what the knob does; only the built-in knobs have one, and an edit clears it.
    std::string hint;
    // -1..1; 0 moves nothing.
    float value = 0.0f;
    // False while the knob is in the library: centred, drawn nowhere, applied nowhere.
    bool shown = true;
    std::vector<Link> links;
    bool operator==(const Knob&) const = default;
};

struct Bank {
    std::vector<Knob> knobs;
    int nextId = 1;
    bool operator==(const Bank&) const = default;
};

struct Range {
    float min;
    float max;
};

// Whether a link may name the target (see Target), with a range safety::apply leaves room in.
bool linkable(Target target);
// What a link's fractions are of: the field's schema range, its top held to what safety::apply lets
// through, so no part of a knob's travel is clamped away afterwards. {0, 0} for a target not linkable.
Range rangeOf(Target target);
// By its wire name (name(Target)); a param a modulation target writes is also found by its schema name. No
// param for an unknown or unlinkable one.
Target targetNamed(std::string_view name);
// Every linkable target in the order the editor lists them: the modulation targets' params in wire order,
// then the rest in schema order. Built once.
const std::vector<Target>& linkableTargets();
// Bit per param slot a link may move on a style.
using Reach = std::bitset<SceneParams::kFieldCount>;

// The four built-in knobs, all at centre: Shape, Motion, Colour, Depth.
const Bank& defaultBank();
// Whether id is one of the built-in knobs' (ids are never handed out twice, so a user's knob never has one).
bool isBase(int id);
// The bank made valid: ids unique and in range, names trimmed and capped, values and weights in
// -1..1 (a non-finite one is 0), unlinkable targets dropped, one link per target, the caps held (a knob past
// kMaxShown on screen goes to the library), a knob in the library centred.
Bank normalized(const Bank& bank);
int shownCount(const Bank& bank);
// Whether a new knob fits: room in the bank and on screen, and an id left.
bool roomForNew(const Bank& bank);

// Edits. Each answers the normalized bank with the change made, or just normalized when the edit is
// refused: an unknown id, a non-finite number, an unlinkable target, an empty name, a full bank.
Bank setValue(const Bank& bank, int id, float value);
Bank rename(const Bank& bank, int id, std::string_view name);
// The new knob is last on screen, at centre with no links; an empty name gives "Knob <id>".
Bank addKnob(const Bank& bank, std::string_view name);
// A copy of the knob right after it: a new id, the name with " 2", the same links and value, no hint.
Bank duplicateKnob(const Bank& bank, int id);
// The knob `delta` places along the screen (negative is earlier, the library's knobs not counted), stopping at
// either end.
Bank moveKnob(const Bank& bank, int id, int delta);
// Deletes the knob, on screen or in the library. Refused for a built-in knob, which hideKnob takes off screen.
Bank removeKnob(const Bank& bank, int id);
// Off screen into the library, centred, its name and links kept.
Bank hideKnob(const Bank& bank, int id);
// From the library back on screen, last and centred; a built-in knob missing from the bank comes back as built.
// Refused when the screen is full or nothing in the library has that id.
Bank showKnob(const Bank& bank, int id);
// A built-in knob's name, hint and links as defaultBank has them; its value stays. Refused for a user's knob.
Bank resetKnob(const Bank& bank, int id);
// Adds the link (Linear), or replaces the range of the knob's link on the same target, keeping its curve.
Bank setLink(const Bank& bank, int id, Target target, float up, float down);
// A new link at the weights a picked target starts on: a quarter of its range each way, up up and down
// down, so the knob moves it at once and neither end is reached by one knob alone. Refused when the knob
// already links the target.
constexpr float kNewLinkWeight = 0.25f;
Bank addLink(const Bank& bank, int id, Target target);
Bank removeLink(const Bank& bank, int id, Target target);
// A new random set of links for the knob, in place of the ones it has: 2 to 5 distinct targets, each with its
// own direction, range (a full turn moves it 15-60 % of its range, the other way none or up to as much
// back) and curve. The same seed rolls the same set, so the shell passes a new seed for each press. Ranges are
// fractions of rangeOf, which already stops at the safety maxima, so no roll reaches past VisualSafety.
Bank roll(const Bank& bank, int id, std::uint64_t seed);
// The knob editor's second Random: the links the knob has keep their params and get a new direction, range and
// curve each, drawn as roll draws them. The same seed rolls the same ranges; a knob with no links is unchanged.
Bank rollRanges(const Bank& bank, int id, std::uint64_t seed);
// The knob editor's Add random: `count` more params linked to the knob, picked at random from the ones it does not
// move yet that the style reaches (`reach`) and Customize does not keep (`kept`, control keys as expandLocks gives
// them), each with a range drawn as roll draws one. Stops at kMaxLinks or when no such param is left; the links the
// knob had stay as they were. The same seed adds the same params.
constexpr int kAddRandomDefault = 3;
Bank addRandom(const Bank& bank, int id, int count, const Reach& reach, const std::set<std::string>& kept, std::uint64_t seed);
// How many params Add random can link to the knob on the style: room under kMaxLinks, and params left to pick.
int addRandomRoom(const Bank& bank, int id, const Reach& reach);
// A new turn for every knob on screen that can move the picture on the style (`reach`, reachMask): at least
// kRollMin of a full turn, up or down at random, or the one way the knob moves there. A knob that moves nothing on
// the style keeps its turn, the library's stay centred, and the links are untouched. The same seed rolls the same
// turns, and a knob's draw does not depend on the others'.
constexpr float kRollMin = 0.3f;
Bank rollValues(const Bank& bank, const Reach& reach, std::uint64_t seed);
Bank rollValues(const Bank& bank, const std::string& styleId, std::uint64_t seed);
// Refused when the knob does not link the target.
Bank setCurve(const Bank& bank, int id, Target target, Curve curve);

// Customize's sliders, by their ParamKeys name (customize::fieldsOf): the target a knob links for one, no param
// when the control writes no linkable param.
Target targetOfControl(std::string_view key);
// A new knob, last, that moves the control's param at the starting weights. Native names it after the
// control (its key without a unit suffix, " 2", " 3" when a knob has that name already). Refused for a
// control with no target or a full bank.
Bank addKnobFor(const Bank& bank, std::string_view key);
// The name native offers when the user saves a knob: after the control its first link moves (as addKnobFor
// names one), "Knob <id>" when it moves none a slider shows; told apart from every other knob's name.
std::string autoName(const Bank& bank, int id);

// The params a link may move on styleId (the scope of the control that sets each): ask once per style.
Reach reachMask(const std::string& styleId);
// p with every knob's offsets added: per link shape(curve, |value|) x (value >= 0 ? up : down) x the
// target's range, summed per target over the whole bank, then held to that range (the hue, a
// Circular field, adds unclamped). A link whose target is not in reach is skipped, and a field no
// live link moves is returned untouched, so a bank at centre is the identity.
SceneParams apply(const SceneParams& p, const Bank& bank, const Reach& reach);
SceneParams apply(const SceneParams& p, const Bank& bank, const std::string& styleId);

// The process-wide bank every live renderer reads (geode_macro_bank_set). Any thread. A renderer takes
// the shared copy once per frame (signal::EngineSettings), and an export keeps the one it pinned at
// prepare, so an edit never changes a bank a frame is already using. The default bank until set.
void setGlobal(const Bank& bank);
std::shared_ptr<const Bank> global();

// The bank as the shell stores it. Rows, '|'-separated, one per line:
//   macrobank|<kTextVersion>|<next id>
//   knob|<id>|<value>|<name>|<hint>
//   link|<TARGET>|<up>|<down>[|<CURVE>]   the links of the knob row above, by name(Target); no CURVE is
//                                         Linear (encode leaves it out then, so text from before curves reads alike)
//   shelf|<id>                            the knob is in the library; text from before the library has none, so
//                                         every knob it holds stays on screen
// encode writes the normalized bank. decode never fails: text without this version's header is the
// default bank, and a row it cannot read (an unknown target, a bad number) is dropped; an unknown curve is Linear.
// A built-in knob saved exactly as it first shipped (Glow, Heat, Bend, Flow) comes back as the knob that now has
// its id, centred; an edited one, and every knob the user made, comes back as saved.
std::string encode(const Bank& bank);
Bank decode(std::string_view text);

// What a link may name, for the knob editor. Rows, '|'-separated, one per line:
//   macrotargets|<kTextVersion>
//   caps|<max knobs>|<max links per knob>|<max name bytes>
//   target|<TARGET>|<param name>|<min>|<max>|<default>|<SCOPE>     every linkable target, linkableTargets() order
// Labels stay in the shell, keyed by TARGET like the modulation pickers'. Built once.
const std::string& targetsText();
// Which way each knob can move the picture on styleId, so the shell dims a knob that cannot:
//   knob|<id>|<up 0/1>|<down 0/1>      one row per knob on screen, in bank order
std::string liveText(const Bank& bank, const std::string& styleId);
// Everything the knob editor draws and offers on styleId, so the shell decides none of it. Rows:
//   macroeditor|<kTextVersion>
//   caps|<max knobs>|<max links per knob>|<max name bytes>
//   bank|<a knob can be added 0/1>|<equals the default bank, values aside 0/1>
//   screen|<a knob can be put back from the library 0/1>
//   knob|<id>|<built-in 0/1>|<as built 0/1>|<a link can be added 0/1>|<value>|<name>|<hint>     on screen, bank order
//   link|<TARGET>|<up>|<down>|<reaches the style 0/1>|<CURVE>|<offset at a full turn up>|<offset at a full turn down>
//                                                              the links of the knob row above; the offsets are
//                                                              in the param's own units (up or down x its range)
//   addrandom|<count to offer>|<most Add random can add>       Add random's stepper for the knob row above
//   target|<TARGET>|<reaches the style 0/1>|<control key>      every linkable target, linkableTargets() order
//   curve|<CURVE>                                              every curve, in the order the editor offers them
//   library|<id>|<built-in 0/1>|<name>|<hint>                  what Add macro offers: the built-in knobs off screen
//                                                              by id, then the user's in bank order
// caps' max knobs is kMaxShown, the screen's. A built-in knob can be reset unless it is as built; any other knob can be removed.
std::string editorText(const Bank& bank, const std::string& styleId);
// Which Customize sliders a knob can move, and which knobs move each now, so the shell marks a slider and
// offers "Add to knob" by asking for none of it. Rows:
//   macrocontrols|<kTextVersion>
//   control|<ParamKeys name>|<TARGET>|<CATEGORY>|<knob id>,<knob id>...   every linkable control, panel order;
//                                                                         its Customize category; on-screen knobs' ids in bank order
std::string controlsText(const Bank& bank);

}  // namespace geode::viz::macrobank
