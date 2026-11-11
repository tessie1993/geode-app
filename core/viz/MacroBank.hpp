#pragma once
#include <cstdint>
#include <memory>
#include <optional>
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
// removed, and what a knob moves is edited link by link. The four built-in knobs can be edited and put back
// (resetKnob) but not removed, so a bank always has the four the app is described with. The shell keeps it as one opaque string (encode) and hands it
// back (decode); every edit is a pure function here, so Kotlin parses and decides nothing (ADR 0003).
// Pure: no GL, no JNI.
namespace geode::viz::macrobank {

// Bumped whenever a row type or field changes meaning; decode answers the default bank to any other.
constexpr int kTextVersion = 1;
constexpr int kMaxKnobs = 8;
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

// One param a knob moves. up is the offset at a full turn up and down the offset at a full turn
// down, each a signed fraction (-1..1) of the target's range: the link's own range, set apart each way,
// so a field resting at its low end can still be moved by turning down, or left alone with 0.
struct Link {
    LfoTarget target = LfoTarget::None;
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

// A target a link may name: one that writes a live scene param. The chained targets (they steer an
// LFO slot), the retired slot and the inert Pulse are not.
bool linkable(LfoTarget target);
// What a link's fractions are of: the field's schema range, its top held to what safety::apply lets
// through, so no part of a knob's travel is clamped away afterwards. {0, 0} for a target not linkable.
Range rangeOf(LfoTarget target);
// By its ModTargets wire name; None for an unknown or unlinkable one.
LfoTarget targetNamed(std::string_view name);

// The four built-in knobs, all at centre: Shape, Motion, Colour, Depth.
const Bank& defaultBank();
// Whether id is one of the built-in knobs' (ids are never handed out twice, so a user's knob never has one).
bool isBase(int id);
// The bank made valid: ids unique and in range, names trimmed and capped, values and weights in
// -1..1 (a non-finite one is 0), unlinkable targets dropped, one link per target, the caps held.
Bank normalized(const Bank& bank);

// Edits. Each answers the normalized bank with the change made, or just normalized when the edit is
// refused: an unknown id, a non-finite number, an unlinkable target, an empty name, a full bank.
Bank setValue(const Bank& bank, int id, float value);
Bank rename(const Bank& bank, int id, std::string_view name);
// The new knob is last, at centre with no links; an empty name gives "Knob <id>".
Bank addKnob(const Bank& bank, std::string_view name);
// A copy of the knob right after it: a new id, the name with " 2", the same links and value, no hint.
Bank duplicateKnob(const Bank& bank, int id);
// The knob `delta` places along the bank (negative is earlier), stopping at either end.
Bank moveKnob(const Bank& bank, int id, int delta);
// Refused for a built-in knob.
Bank removeKnob(const Bank& bank, int id);
// A built-in knob's name, hint and links as defaultBank has them; its value stays. Refused for a user's knob.
Bank resetKnob(const Bank& bank, int id);
// Adds the link (Linear), or replaces the range of the knob's link on the same target, keeping its curve.
Bank setLink(const Bank& bank, int id, LfoTarget target, float up, float down);
// A new link at the weights a picked target starts on: a quarter of its range each way, up up and down
// down, so the knob moves it at once and neither end is reached by one knob alone. Refused when the knob
// already links the target.
constexpr float kNewLinkWeight = 0.25f;
Bank addLink(const Bank& bank, int id, LfoTarget target);
Bank removeLink(const Bank& bank, int id, LfoTarget target);
// A new random set of links for the knob, in place of the ones it has: 2 to 5 distinct targets, each with its
// own direction, range (a full turn moves it 15-60 % of its range, the other way none or up to as much
// back) and curve. The same seed rolls the same set, so the shell passes a new seed for each press. Ranges are
// fractions of rangeOf, which already stops at the safety maxima, so no roll reaches past VisualSafety.
Bank roll(const Bank& bank, int id, std::uint64_t seed);
// Refused when the knob does not link the target.
Bank setCurve(const Bank& bank, int id, LfoTarget target, Curve curve);

// Customize's sliders, by their ParamKeys name (customize::fieldsOf): the target a knob links for one, None
// when the control writes no linkable param.
LfoTarget targetOfControl(std::string_view key);
// A new knob, last, that moves the control's param at the starting weights. Native names it after the
// control (its key without a unit suffix, " 2", " 3" when a knob has that name already). Refused for a
// control with no target or a full bank.
Bank addKnobFor(const Bank& bank, std::string_view key);
// The name native offers when the user saves a knob: after the control its first link moves (as addKnobFor
// names one), "Knob <id>" when it moves none a slider shows; told apart from every other knob's name.
std::string autoName(const Bank& bank, int id);

// Bit per LfoTarget ordinal a link may move on styleId (modtargets::reaches): ask once per style.
std::uint64_t reachMask(const std::string& styleId);
// p with every knob's offsets added: per link shape(curve, |value|) x (value >= 0 ? up : down) x the
// target's range, summed per target over the whole bank, then held to that range (the hue, a
// Circular field, adds unclamped). A link whose target is not in reach is skipped, and a field no
// live link moves is returned untouched, so a bank at centre is the identity.
SceneParams apply(const SceneParams& p, const Bank& bank, std::uint64_t reach);
SceneParams apply(const SceneParams& p, const Bank& bank, const std::string& styleId);

// The process-wide bank every live renderer reads (geode_macro_bank_set). Any thread. A renderer takes
// the shared copy once per frame (signal::EngineSettings), and an export keeps the one it pinned at
// prepare, so an edit never changes a bank a frame is already using. The default bank until set.
void setGlobal(const Bank& bank);
std::shared_ptr<const Bank> global();

// The bank as the shell stores it. Rows, '|'-separated, one per line:
//   macrobank|<kTextVersion>|<next id>
//   knob|<id>|<value>|<name>|<hint>
//   link|<TARGET>|<up>|<down>[|<CURVE>]   the links of the knob row above, by ModTargets name; no CURVE is
//                                         Linear (encode leaves it out then, so text from before curves reads alike)
// encode writes the normalized bank. decode never fails: text without this version's header is the
// default bank, and a row it cannot read (an unknown target, a bad number) is dropped; an unknown curve is Linear.
// A built-in knob saved exactly as it first shipped (Glow, Heat, Bend, Flow) comes back as the knob that now has
// its id, centred; an edited one, and every knob the user made, comes back as saved.
std::string encode(const Bank& bank);
Bank decode(std::string_view text);

// What a link may name, for the knob editor. Rows, '|'-separated, one per line:
//   macrotargets|<kTextVersion>
//   caps|<max knobs>|<max links per knob>|<max name bytes>
//   target|<TARGET>|<param name>|<min>|<max>|<default>|<SCOPE>     every linkable target, wire order
// Labels stay in the shell, keyed by TARGET like the modulation pickers'. Built once.
const std::string& targetsText();
// Which way each knob can move the picture on styleId, so the shell dims a knob that cannot:
//   knob|<id>|<up 0/1>|<down 0/1>      one row per knob, in bank order
std::string liveText(const Bank& bank, const std::string& styleId);
// Everything the knob editor draws and offers on styleId, so the shell decides none of it. Rows:
//   macroeditor|<kTextVersion>
//   caps|<max knobs>|<max links per knob>|<max name bytes>
//   bank|<a knob can be added 0/1>|<equals the default bank, values aside 0/1>
//   knob|<id>|<built-in 0/1>|<as built 0/1>|<a link can be added 0/1>|<value>|<name>|<hint>     bank order
//   link|<TARGET>|<up>|<down>|<reaches the style 0/1>|<CURVE>|<offset at a full turn up>|<offset at a full turn down>
//                                                              the links of the knob row above; the offsets are
//                                                              in the param's own units (up or down x its range)
//   target|<TARGET>|<reaches the style 0/1>                    every linkable target, wire order
//   curve|<CURVE>                                              every curve, in the order the editor offers them
// A built-in knob can be reset unless it is as built; any other knob can be removed.
std::string editorText(const Bank& bank, const std::string& styleId);
// Which Customize sliders a knob can move, and which knobs move each now, so the shell marks a slider and
// offers "Add to knob" by asking for none of it. Rows:
//   macrocontrols|<kTextVersion>
//   control|<ParamKeys name>|<TARGET>|<CATEGORY>|<knob id>,<knob id>...   every linkable control, panel order;
//                                                                         its Customize category; ids in bank order
std::string controlsText(const Bank& bank);

}  // namespace geode::viz::macrobank
