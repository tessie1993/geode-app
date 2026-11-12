#pragma once
#include "viz/Adsr.hpp"
#include "viz/Lfo.hpp"

// Which of a wave's or an envelope's controls do anything as the slot stands, so Customize shows only those
// (geode_mod_lfo_shows, geode_mod_adsr_shows). The rules are the engine's: a control the engine reads in this
// state is shown, one it ignores is not. Pure: no GL, the host tests build it.
namespace geode::viz::modcontrols {

// Wave shape, timing and moment: only the steady wave runs an oscillator; a sound source ignores all three.
constexpr unsigned kWave = 1u;
// The cycle time: an oscillator on its own period, i.e. free timing or restarted on a moment (beat timing gives
// way to a moment, as LfoEngine::oscillator runs it).
constexpr unsigned kRate = 2u;
// The macro knob picker: the slot moves a Macro knob, the one LfoConfig::knob / AdsrConfig::knob names.
constexpr unsigned kKnob = 4u;

unsigned ofWave(const LfoConfig& c);
unsigned ofEnvelope(const AdsrConfig& c);

}  // namespace geode::viz::modcontrols
