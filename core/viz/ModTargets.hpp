#pragma once
#include <cstdint>
#include <string>

#include "viz/Catalogue.hpp"
#include "viz/Lfo.hpp"

// The modulation-target table (#642): what the shell needs to know about every LfoTarget, ModSource,
// ModPolarity, ModCurve and LfoWave to offer them, and nothing it would have to keep a copy of.
// Per target: its wire name (the name saved presets and LfoStore write), the catalogue scope that
// says which styles it reaches, the slot a chained target steers and whether it is offered at all.
// Per source: the polarity that makes it useful the moment it is picked. Plus a slot's defaults and
// rate range (LfoConfig). Pure: the host tests build it beside the catalogue.
//
// Kotlin reads all of it once through text() (geode_mod_targets_text), the way render/scene/Catalogue
// reads the style catalogue; its enums keep only their labels, which Android translates.
namespace geode::viz::modtargets {

// Bumped whenever a row type or field changes meaning; Kotlin refuses a table it cannot read.
constexpr int kTextVersion = 1;
constexpr int kTargetCount = static_cast<int>(LfoTarget::EndlessZoomSpeed) + 1;

// The wire name; "" outside the enum.
const char* name(LfoTarget target);
catalogue::Scope scopeOf(LfoTarget target);
// False for the retired Glitch slot, which only holds later ordinals in place.
bool offered(LfoTarget target);
// The slot a chained target steers (its rate or depth), -1 for one that moves a scene param.
int chainSlot(LfoTarget target);
// Whether moving `target` changes anything on `styleId`. A chained target steers another slot, so it
// reaches every style; the retired slot and anything outside the enum reach none.
bool reaches(LfoTarget target, const std::string& styleId);
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
const std::string& text();

}  // namespace geode::viz::modtargets
