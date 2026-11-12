#pragma once

#include <array>
#include <cstddef>

#include "sound/SoundFrame.h"

// Settings › Audio's listening tuning: the one schema of GeodeSoundTuning. Every slot's default and
// range lives here and nowhere else, so the engine, the sliders (which read their ranges through
// geode_sound_tuning_ranges) and the saved settings (clamped through geode_sound_tuning_clamp) can
// never disagree. The shell only holds the saved floats.
//
// A slot is one float of GeodeSoundTuning in field order (gain, threshold, offset, attack, release,
// drum, drumDecay three each, then the nine scalars): the struct is all floats with no padding, so
// the slots are its bytes and the JNI wire form is the same array.
namespace geode::sound::tuning {

inline constexpr int kSlots = GEODE_SOUND_TUNING_FLOATS;
static_assert(sizeof(GeodeSoundTuning) == sizeof(float) * kSlots, "GeodeSoundTuning is its slots, in order");

// The shell names slots by these numbers; each must be its field's place in the struct.
constexpr bool slotIs(size_t offset, int slot) { return offset == sizeof(float) * static_cast<size_t>(slot); }
static_assert(slotIs(offsetof(GeodeSoundTuning, gain), GEODE_SOUND_TUNING_GAIN) &&
                  slotIs(offsetof(GeodeSoundTuning, threshold), GEODE_SOUND_TUNING_THRESHOLD) &&
                  slotIs(offsetof(GeodeSoundTuning, offset), GEODE_SOUND_TUNING_OFFSET) &&
                  slotIs(offsetof(GeodeSoundTuning, attack), GEODE_SOUND_TUNING_ATTACK) &&
                  slotIs(offsetof(GeodeSoundTuning, release), GEODE_SOUND_TUNING_RELEASE) &&
                  slotIs(offsetof(GeodeSoundTuning, drum), GEODE_SOUND_TUNING_DRUM) &&
                  slotIs(offsetof(GeodeSoundTuning, drumDecay), GEODE_SOUND_TUNING_DRUM_DECAY) &&
                  slotIs(offsetof(GeodeSoundTuning, reactivity), GEODE_SOUND_TUNING_REACTIVITY) &&
                  slotIs(offsetof(GeodeSoundTuning, speed), GEODE_SOUND_TUNING_SPEED) &&
                  slotIs(offsetof(GeodeSoundTuning, onsetThreshold), GEODE_SOUND_TUNING_ONSET_THRESHOLD) &&
                  slotIs(offsetof(GeodeSoundTuning, hardCutLevel), GEODE_SOUND_TUNING_HARD_CUT_LEVEL) &&
                  slotIs(offsetof(GeodeSoundTuning, hardCutHalfLife), GEODE_SOUND_TUNING_HARD_CUT_HALF_LIFE) &&
                  slotIs(offsetof(GeodeSoundTuning, pitchThreshold), GEODE_SOUND_TUNING_PITCH_THRESHOLD) &&
                  slotIs(offsetof(GeodeSoundTuning, noteHold), GEODE_SOUND_TUNING_NOTE_HOLD) &&
                  slotIs(offsetof(GeodeSoundTuning, silenceDb), GEODE_SOUND_TUNING_SILENCE_DB) &&
                  slotIs(offsetof(GeodeSoundTuning, inputTrimDb), GEODE_SOUND_TUNING_INPUT_TRIM_DB) &&
                  GEODE_SOUND_TUNING_INPUT_TRIM_DB == kSlots - 1,
              "the slot numbers are the fields' places");

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

// A saved tuning as the engine hears it, written to out (kSlots floats). A save of every slot, or of every
// slot but the input trim (written before it existed, so it reads 0 dB), is clamped; anything else, none
// included, is the defaults. True when the save was used.
bool resolve(const float* saved, int count, float* out);

}  // namespace geode::sound::tuning
