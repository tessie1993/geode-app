#pragma once

#include <cstdint>

#include "signal/params/ParamSchema.hpp"

// The param state machine: what one scene param's base value is doing right now, and how each input
// moves it. One cell per param, held by ParamStore; the machine itself is two pure functions over a cell
// (apply an input, advance by dt), so every transition is host-tested one at a time.
//
// The base is the value this frame's modulation starts from. The committed value is the one that is
// saved: equal to the base at rest, the target during a glide, what a trial springs back to.
//
// Phases:
//   Rest    the base is the committed value.
//   Glide   the base is easing toward the committed value (a preset applied, a morph, a reset).
//   Held    a finger is on the param's control: the user's value is shown exactly (ParamStore mutes
//           modulation) and nothing but that finger may change it.
//   Trial   a value the user may not keep (a Pro control tried by a free user) is shown; on release, or
//           after a few seconds untouched, the base glides back to the committed value.
//   Driven  a timeline (a Studio keyframe track or a take) owns the base at programme time; the user's
//           committed value waits underneath and comes back, gliding, when the timeline lets go.
//
// Who may change what (an input's Origin):
//   a hand (User) outranks the engine; a timeline outranks both while it drives; a preset may change
//   what a trial springs back to but never takes a param from a finger.
//
// Transitions (rows: phase now; columns: input; cell: next phase, or R = rejected, - = ignored):
//
//            Set        GlideTo     Hold     Release    Try       Drive     Undrive   Reset
//   Rest     Rest       Glide       Held(5)  -          Trial(5)  Driven(6) -         Glide
//   Glide    Rest       Glide       Held(5)  -          Trial(5)  Driven(6) -         Glide
//   Held     Held(1)    R           -        Rest       Trial(5)  R         -         R
//   Trial    Trial(2)   Trial(2)    Trial(5) Glide(3)   Trial(5)  Driven(6) -         Trial(2)
//   Driven   Driven(4)  Driven(4)   R        -          R         -         Glide(6)  R
//
//   (1) only the User's Set; any other origin is rejected: the finger wins.
//   (2) the User's Set or Try moves the tried value; a Preset's Set, GlideTo or Reset, or the User's
//       Reset, moves what it springs back to; anything else is rejected.
//   (3) back to the committed value, over kSpringBackSeconds.
//   (4) only the Timeline's; any other origin is rejected. A timeline's GlideTo lands at once: it
//       samples its own curve every frame.
//   (5) only the User's: a finger is the only thing that holds or tries. Any other origin is rejected.
//   (6) only the Timeline's: only a timeline takes a param or lets it go (Undrive glides back as in (3)).
//       Any other origin is rejected.
//   The factories set these origins; the checks guard an input built by hand (a replay, a wire format).
//   Where a cell is "-" (the input means nothing in that phase) it is ignored whatever its origin.
//
// A GlideTo or a Reset whose seconds is negative or not finite is rejected in every phase, before the
// table is read: a glide that never lands, or lands before it starts, is a caller's bug, not a value.
// Seconds 0 is valid and lands at once (Rest), exactly like a Set.
//
// Int and Bool params never interpolate: a GlideTo or a spring back on one lands at once (Rest).

namespace geode::signal {

enum class ParamPhase : std::uint8_t { Rest, Glide, Held, Trial, Driven };

// Who sent an input. Engine covers every automatic writer: event routes, the director, randomize.
enum class Origin : std::uint8_t { User, Preset, Timeline, Engine };

enum class Ease : std::uint8_t {
    Linear,
    Smooth,  // smoothstep: eases in and out, the default for anything the user watches move
};

struct ParamInput {
    enum class Kind : std::uint8_t { Set, GlideTo, Hold, Release, Try, Drive, Undrive, Reset };

    Kind kind = Kind::Set;
    Origin origin = Origin::User;
    float value = 0.0f;
    // GlideTo and Reset: how long the glide takes (0 lands at once; negative or non-finite is rejected).
    // Try: how long an untouched trial shows before it springs back (0 means kTrialSeconds).
    float seconds = 0.0f;
    Ease ease = Ease::Smooth;

    static ParamInput set(float v, Origin o = Origin::User) { return {Kind::Set, o, v, 0.0f, Ease::Smooth}; }
    static ParamInput glideTo(float v, float s, Origin o = Origin::Preset, Ease e = Ease::Smooth) {
        return {Kind::GlideTo, o, v, s, e};
    }
    static ParamInput hold() { return {Kind::Hold, Origin::User, 0.0f, 0.0f, Ease::Smooth}; }
    static ParamInput release() { return {Kind::Release, Origin::User, 0.0f, 0.0f, Ease::Smooth}; }
    static ParamInput tryValue(float v, float s = 0.0f) { return {Kind::Try, Origin::User, v, s, Ease::Smooth}; }
    static ParamInput drive() { return {Kind::Drive, Origin::Timeline, 0.0f, 0.0f, Ease::Smooth}; }
    static ParamInput undrive() { return {Kind::Undrive, Origin::Timeline, 0.0f, 0.0f, Ease::Smooth}; }
    static ParamInput reset(float s = 0.0f, Origin o = Origin::User) { return {Kind::Reset, o, 0.0f, s, Ease::Smooth}; }
};

enum class Outcome : std::uint8_t {
    Applied,   // the cell changed (or was set to the value it already had)
    Ignored,   // the input means nothing in this phase (a Release with no finger down)
    Rejected,  // someone with a stronger claim owns the param (a finger, a timeline), the origin may not
               // send this input, or the input is malformed (a glide of negative or non-finite seconds)
};

struct ParamCell {
    ParamPhase phase = ParamPhase::Rest;
    float base = 0.0f;
    float committed = 0.0f;
    // Glide: from `from` to `committed`, `elapsed` of `duration` seconds so far.
    float from = 0.0f;
    float elapsed = 0.0f;
    float duration = 0.0f;
    Ease ease = Ease::Smooth;
    // Trial: a finger is still on the control; seconds left before an untouched trial springs back.
    bool trialHeld = false;
    float trialLeft = 0.0f;
};

constexpr float kSpringBackSeconds = 0.35f;
constexpr float kTrialSeconds = 2.0f;

// A cell at rest on the param's default.
ParamCell restingCell(const ParamDef& def);
// One input. Values are clamped to the param's range before they are stored.
Outcome apply(ParamCell& cell, const ParamInput& input, const ParamDef& def);
// Moves a glide and an untouched trial's timer on by dt seconds (negative or non-finite dt is 0).
void advance(ParamCell& cell, float dt, const ParamDef& def);

const char* phaseName(ParamPhase phase);
const char* inputName(ParamInput::Kind kind);

}  // namespace geode::signal
