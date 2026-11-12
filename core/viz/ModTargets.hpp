#pragma once
#include <bitset>
#include <cstdint>
#include <string>

#include "viz/Catalogue.hpp"
#include "viz/Lfo.hpp"

// The modulation-target table (#642): what the shell needs to know about every LfoTarget, ModSource,
// ModPolarity, ModCurve and LfoWave to offer them, and nothing it would have to keep a copy of.
// Per target: its wire name (the name the saved waves and routes hold), the catalogue scope that
// says which styles it reaches, the slot a chained target steers and whether it is offered at all.
// Per source: the polarity that makes it useful the moment it is picked. Plus a slot's defaults and
// rate range (LfoConfig). Pure: the host tests build it beside the catalogue.
//
// viz/ModDocument and viz/ModCatalog answer the editors from it; text() is the whole table in one piece,
// which mod_targets_test holds to its fixture.
namespace geode::viz::modtargets {

// Bumped whenever a row type or field changes meaning.
constexpr int kTextVersion = 3;
constexpr int kTargetCount = static_cast<int>(kLastLfoTarget) + 1;

// The wire name; "" outside the enum.
const char* name(LfoTarget target);
// The names a saved wave carries for its source, direction, curve and shape; "" outside the enum.
const char* sourceName(ModSource source);
const char* polarityName(ModPolarity polarity);
const char* curveName(ModCurve curve);
const char* waveName(LfoWave wave);
catalogue::Scope scopeOf(LfoTarget target);
// False for the retired Pulse and Glitch slots, which only hold later ordinals in place. Macro (a knob) is offered
// to the waves and the envelopes on every style, never to a reaction route.
bool offered(LfoTarget target);
// The slot a chained target steers (its rate or depth), -1 for one that moves a scene param.
int chainSlot(LfoTarget target);
// Whether moving `target` changes anything on `styleId`. A chained target steers another slot, so it
// reaches every style; a retired slot and anything outside the enum reach none.
bool reaches(LfoTarget target, const std::string& styleId);
// Who is picking a target, for offeredTo: a reaction route, an envelope, or a wave slot by its index
// (0..LfoEngine::kSlots-1). Anything else is offered nothing.
constexpr int kPickerReaction = -2;
constexpr int kPickerEnvelope = -1;
// Whether `picker` may move `target` on `styleId`: what reaches the style, minus what the engine would
// not run for that picker. A reaction route moves scene params only (ReactionEngine::apply skips a
// chained route); an envelope may steer any wave; a wave only a LATER wave, because LfoEngine::step
// runs the slots in order and a chain back to itself or an earlier slot would land a frame late.
bool offeredTo(LfoTarget target, const std::string& styleId, int picker);
// Bit per LfoTarget ordinal that offeredTo says yes to.
using Mask = std::bitset<kTargetCount>;
Mask offerMask(const std::string& styleId, int picker);
// A mask in lowercase hex, the highest ordinal's digit first, no leading zeros ("0" for none).
std::string hex(const Mask& mask);
// Bit per MusicAction (viz/ReactionDna) whose target reaches `styleId`: Customize › Music hides the rest.
std::uint32_t musicActionsFor(const std::string& styleId);
// An oscillator swings both ways; a follower reads 0 in silence, so a bipolar follower would shove the
// param negative every time the music stops: followers start up only (stereo pan is centred, so both).
ModPolarity naturalPolarity(ModSource source);

// Rows, '|'-separated, one per line, in this order:
//   modulation|<kTextVersion>
//   config|<default rate s>|<min rate s>|<max rate s>|<default depth>|<slots>
//   target|<ordinal>|<NAME>|<SCOPE>|<chain slot, -1 none>|<offered 0/1>     every LfoTarget, wire order
//   source|<ordinal>|<NAME>|<natural POLARITY>                              every ModSource
//   polarity|<ordinal>|<NAME>
//   curve|<ordinal>|<NAME>
//   wave|<ordinal>|<NAME>
//   reach|<style id>|<reaction>|<envelope>|<wave 1>|<wave 2>|<wave 3>         offerMask per picker, hex(), wider
//        than 64 bits once the targets pass ordinal 63;
//        one row per catalogue id in allIds() order, then "*", the answer for an id the catalogue lacks
const std::string& text();

}  // namespace geode::viz::modtargets
