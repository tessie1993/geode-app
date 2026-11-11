#pragma once
#include <cstdint>
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
// The bank is process-wide and the user's own: knobs can be added, renamed and removed, and what a
// knob moves is edited link by link. The shell keeps it as one opaque string (encode) and hands it
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

// One param a knob moves. up is the offset at a full turn up and down the offset at a full turn
// down, each a signed fraction (-1..1) of the target's range, so the two directions are set apart:
// a field resting at its low end can still be moved by turning down, or left alone with 0.
struct Link {
    LfoTarget target = LfoTarget::None;
    float up = 0.0f;
    float down = 0.0f;
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

// The four built-in knobs, all at centre: Glow, Heat, Bend, Flow.
const Bank& defaultBank();
// The bank made valid: ids unique and in range, names trimmed and capped, values and weights in
// -1..1 (a non-finite one is 0), unlinkable targets dropped, one link per target, the caps held.
Bank normalized(const Bank& bank);

// Edits. Each answers the normalized bank with the change made, or just normalized when the edit is
// refused: an unknown id, a non-finite number, an unlinkable target, an empty name, a full bank.
Bank setValue(const Bank& bank, int id, float value);
Bank rename(const Bank& bank, int id, std::string_view name);
// The new knob is last, at centre with no links; an empty name gives "Knob <id>".
Bank addKnob(const Bank& bank, std::string_view name);
Bank removeKnob(const Bank& bank, int id);
// Adds the link, or replaces the knob's link on the same target.
Bank setLink(const Bank& bank, int id, LfoTarget target, float up, float down);
Bank removeLink(const Bank& bank, int id, LfoTarget target);

// Bit per LfoTarget ordinal a link may move on styleId (modtargets::reaches): ask once per style.
std::uint64_t reachMask(const std::string& styleId);
// p with every knob's offsets added: per link (value >= 0 ? value * up : -value * down) x the
// target's range, summed per target over the whole bank, then held to that range (the hue, a
// Circular field, adds unclamped). A link whose target is not in reach is skipped, and a field no
// live link moves is returned untouched, so a bank at centre is the identity.
SceneParams apply(const SceneParams& p, const Bank& bank, std::uint64_t reach);
SceneParams apply(const SceneParams& p, const Bank& bank, const std::string& styleId);

// The bank as the shell stores it. Rows, '|'-separated, one per line:
//   macrobank|<kTextVersion>|<next id>
//   knob|<id>|<value>|<name>|<hint>
//   link|<TARGET>|<up>|<down>          the links of the knob row above, by ModTargets name
// encode writes the normalized bank. decode never fails: text without this version's header is the
// default bank, and a row it cannot read (an unknown target, a bad number) is dropped.
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

}  // namespace geode::viz::macrobank
