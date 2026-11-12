// Host-side checks for how a saved Settings › Audio tuning is read (core/sound/Tuning, through the C ABI).
//
// What is pinned: a save of every slot is clamped and used; a save one slot short (written before the input
// trim) reads the trim as 0 dB; a save of any other length, or none, is the defaults; the slot numbers the
// shell names are the struct's fields.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#include "api/geode_api.h"
#include "sound/Tuning.hpp"

namespace tuning = geode::sound::tuning;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool isDefaults(const float* v) {
    for (int i = 0; i < tuning::kSlots; ++i) {
        if (v[i] != tuning::slots()[static_cast<size_t>(i)].def) return false;
    }
    return true;
}

}  // namespace

int main() {
    constexpr int n = GEODE_SOUND_TUNING_FLOATS;
    check(geode_sound_tuning_slots() == n, "the ABI counts every slot");

    float out[n];
    check(geode_sound_tuning_resolve(nullptr, 0, out) == 0 && isDefaults(out), "no save is the defaults");

    float whole[n];
    std::memcpy(whole, out, sizeof whole);
    whole[GEODE_SOUND_TUNING_REACTIVITY] = 2.0f;
    whole[GEODE_SOUND_TUNING_SPEED] = 9.0f;
    whole[GEODE_SOUND_TUNING_ATTACK + 1] = std::numeric_limits<float>::quiet_NaN();
    whole[GEODE_SOUND_TUNING_INPUT_TRIM_DB] = -6.0f;
    check(geode_sound_tuning_resolve(whole, n, out) == 1, "a save of every slot is used");
    check(out[GEODE_SOUND_TUNING_REACTIVITY] == 2.0f, "a value in range is kept");
    check(out[GEODE_SOUND_TUNING_SPEED] == 4.0f, "a value past the top is clamped");
    check(out[GEODE_SOUND_TUNING_ATTACK + 1] == 0.03f, "a NaN is the slot's default");
    check(out[GEODE_SOUND_TUNING_INPUT_TRIM_DB] == -6.0f, "the trim is read");

    float older[n - 1];
    for (int i = 0; i < n - 1; ++i) older[i] = whole[i];
    older[GEODE_SOUND_TUNING_SILENCE_DB] = -45.0f;
    check(geode_sound_tuning_resolve(older, n - 1, out) == 1, "a save from before the trim is used");
    check(out[GEODE_SOUND_TUNING_SILENCE_DB] == -45.0f, "its last slot is still the silence level");
    check(out[GEODE_SOUND_TUNING_INPUT_TRIM_DB] == 0.0f, "the missing trim reads 0 dB");

    float three[3] = {2.0f, 2.0f, 2.0f};
    check(geode_sound_tuning_resolve(three, 3, out) == 0 && isDefaults(out), "a short save is the defaults");
    float longer[n + 1] = {};
    check(geode_sound_tuning_resolve(longer, n + 1, out) == 0 && isDefaults(out), "a long save is the defaults");
    check(geode_sound_tuning_resolve(whole, n, nullptr) == 0, "no output, nothing done");

    // The struct read through the slot numbers is the same struct.
    GeodeSoundTuning t;
    geode_sound_tuning_resolve(whole, n, out);
    std::memcpy(&t, out, sizeof t);
    check(t.reactivity == 2.0f && t.speed == 4.0f && t.inputTrimDb == -6.0f && t.attack[1] == 0.03f,
          "the slot numbers are the struct's fields");
    check(t.drum[0] == out[GEODE_SOUND_TUNING_DRUM] && t.drumDecay[2] == out[GEODE_SOUND_TUNING_DRUM_DECAY + 2],
          "a drum's slots are kick, snare, hat in a row");

    if (failures == 0) std::printf("sound_tuning_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
