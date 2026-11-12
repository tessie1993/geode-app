#pragma once
#include <string>
#include <string_view>

#include "viz/Adsr.hpp"
#include "viz/Lfo.hpp"
#include "viz/ReactionDna.hpp"
#include "viz/Reactions.hpp"

// What the modulation editors show: every wave shape, source, direction, curve, envelope band, reaction
// source and mode, target and ready-made recipe, with the name a saved document carries and the words a
// picker draws; the editors' slider ranges and the slot counts. The shell reads it once through text()
// (geode_mod_catalog_text) and keeps no list, range or default of its own. Pure: the host tests build it.
namespace geode::viz::modcatalog {

// Bumped whenever a row type or field changes meaning.
constexpr int kTextVersion = 1;

constexpr int kSourceCount = static_cast<int>(ModSource::StereoPan) + 1;
constexpr int kPolarityCount = static_cast<int>(ModPolarity::Negative) + 1;
constexpr int kCurveCount = static_cast<int>(ModCurve::Smooth) + 1;
constexpr int kWaveCount = static_cast<int>(LfoWave::RandomGlide) + 1;
constexpr int kBandCount = static_cast<int>(EnvBand::Width) + 1;

// The periods a beat-timed wave may take, in the picker's order; 0 is free timing.
constexpr int kBeatChoices = 7;
float beatChoice(int index);

// The route editor's slider reach; a saved route may hold more (viz/ReactionText).
constexpr float kRouteRiseMaxSeconds = 1.0f;
constexpr float kRouteFallMaxSeconds = 3.0f;

// The name a saved envelope carries for its band; "" outside the enum.
const char* bandName(EnvBand band);

// The ordinal a saved name stands for; -1 for a name nothing carries, so the reader keeps its default.
int targetNamed(std::string_view name);
int sourceNamed(std::string_view name);
int polarityNamed(std::string_view name);
int curveNamed(std::string_view name);
int waveNamed(std::string_view name);
int bandNamed(std::string_view name);

// The words a picker draws for a target; "" for a retired one, which nothing offers.
const char* targetLabel(LfoTarget target);

// One line per row, fields split by '|':
//   catalog|<version>
//   count|<waves|envelopes|routes>|<n>
//   range|<name>|<default>|<min>|<max>
//   beats|<index>|<beats>|<label>
//   target|<ordinal>|<NAME>|<label>
//   source|<ordinal>|<NAME>|<label>|<natural polarity ordinal>
//   polarity|curve|wave|band|mode|<ordinal>|<NAME>|<label>
//   group|<index>|<KEY>|<label>            reaction-source groups, in the picker's order
//   react|<ordinal>|<NAME>|<label>|<group KEY>|<1 when it is an event, else 0>
//   recipe|<ordinal>|<NAME>|<title>|<sentence>
const std::string& text();

}  // namespace geode::viz::modcatalog
