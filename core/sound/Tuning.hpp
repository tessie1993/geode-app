#pragma once

#include <array>

#include "sound/SoundFrame.h"

// Settings › Audio's listening tuning: the one schema of GeodeSoundTuning. Every slot's default and
// range lives here and nowhere else, so the engine, the sliders (which read their ranges through
// geode_sound_tuning_ranges) and the saved settings (read and clamped through
// geode_sound_tuning_from_saved) can never disagree. Kotlin's SoundTuning only holds the floats.
//
// A slot is one float of GeodeSoundTuning in field order (gain, threshold, offset, attack, release,
// drum, drumDecay three each, then the eight scalars): the struct is all floats with no padding, so
// the slots are its bytes and the JNI wire form is the same array.
namespace geode::sound::tuning {

inline constexpr int kSlots = GEODE_SOUND_TUNING_FLOATS;
static_assert(sizeof(GeodeSoundTuning) == sizeof(float) * kSlots, "GeodeSoundTuning is its slots, in order");

struct Slot {
    float def;
    float lo;
    float hi;
};

// Every slot in field order.
const std::array<Slot, kSlots>& slots();

GeodeSoundTuning defaults();

// Each slot within its range. A NaN or an infinity, which no slider can set but a corrupt save or a
// bad caller can, becomes the slot's default rather than an end of the range.
GeodeSoundTuning clamp(const GeodeSoundTuning& t);

// The same on the wire form: the first count slots of values, in place (slots past kSlots are ignored).
void clamp(float* values, int count);

// A tuning as Settings › Audio saved it, into `out` (kSlots floats), clamped. A save of every slot is
// read as it is; one a slot short is from before the input trim and gets the 0 dB it was heard at; any
// other length (or none) is not a tuning this engine wrote, so `out` gets the defaults and the answer
// is false.
bool fromSaved(const float* saved, int count, float* out);

}  // namespace geode::sound::tuning
