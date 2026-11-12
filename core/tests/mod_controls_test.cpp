// Host-side checks for which wave and envelope controls Customize shows (core/viz/ModControls through
// geode_mod_lfo_shows and geode_mod_adsr_shows): a control shows while the engine reads it in the slot's state.
// Built and run by CI (.github/workflows/native-host-tests.yml, "Modulation document host tests"); no GL.
#include <cstdio>
#include <string>

#include "api/geode_api.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

constexpr unsigned kWave = GEODE_MOD_SHOWS_WAVE;
constexpr unsigned kRate = GEODE_MOD_SHOWS_RATE;
constexpr unsigned kKnob = GEODE_MOD_SHOWS_KNOB;

std::string wave(const char* source, const char* target, int beats, int moment) {
    return std::string("{\"enabled\":true,\"source\":\"") + source + "\",\"target\":\"" + target +
           "\",\"wave\":\"SINE\",\"rateSeconds\":2,\"depth\":0.3,\"polarity\":\"BIPOLAR\",\"curve\":\"LINEAR\",\"beats\":" +
           std::to_string(beats) + ",\"knob\":3,\"moment\":" + std::to_string(moment) + "}";
}

unsigned waveShows(const std::string& slot) { return geode_mod_lfo_shows(("[" + slot + "]").c_str(), 0); }

std::string envelope(const char* targets) {
    return std::string("{\"enabled\":true,\"targets\":[") + targets +
           "],\"attack\":0.05,\"decay\":0.25,\"sustain\":0.5,\"release\":0.35,\"amount\":0.5,\"band\":\"BASS\","
           "\"gateThreshold\":0.25,\"sustainTrack\":false,\"retrigger\":true,\"knob\":3,\"moment\":-1}";
}

unsigned envelopeShows(const char* targets) { return geode_mod_adsr_shows(("[" + envelope(targets) + "]").c_str(), 0); }

void waves() {
    check(geode_mod_lfo_shows(nullptr, 0) == (kWave | kRate), "a wave nothing saved runs free: shape, timing and period");
    check(geode_mod_lfo_shows("not a list", 2) == (kWave | kRate), "an unreadable text reads as the defaults");
    check(waveShows(wave("LFO", "ZOOM", 0, -1)) == (kWave | kRate), "a free steady wave shows its period");
    check(waveShows(wave("LFO", "ZOOM", 4, -1)) == kWave, "a beat-timed wave follows the music clock: no period");
    check(waveShows(wave("LFO", "ZOOM", 4, 2)) == (kWave | kRate), "a beat-timed wave on a moment runs its own period");
    check(waveShows(wave("LFO", "ZOOM", 0, 5)) == (kWave | kRate), "a free wave on a moment runs its own period");
    check(waveShows(wave("BASS", "ZOOM", 0, -1)) == 0u, "a sound source has no shape, timing, moment or period");
    check(waveShows(wave("STEREO_PAN", "ZOOM", 8, 2)) == 0u, "a sound source ignores its timing and moment");
    check(waveShows(wave("LFO", "MACRO", 0, -1)) == (kWave | kRate | kKnob), "a steady wave on a knob picks the knob");
    check(waveShows(wave("TREBLE", "MACRO", 0, -1)) == kKnob, "a sound source on a knob picks the knob alone");
    check(waveShows(wave("LFO", "NONE", 16, -1)) == kWave, "a wave that moves nothing still shows its own controls");

    const std::string three = "[" + wave("BASS", "ZOOM", 0, -1) + "," + wave("LFO", "ZOOM", 4, -1) + "," +
                              wave("LFO", "MACRO", 8, 4) + "]";
    check(geode_mod_lfo_shows(three.c_str(), 0) == 0u, "the first slot is read on its own");
    check(geode_mod_lfo_shows(three.c_str(), 1) == kWave, "the second slot is read on its own");
    check(geode_mod_lfo_shows(three.c_str(), 2) == (kWave | kRate | kKnob), "the third slot is read on its own");
    check(geode_mod_lfo_shows(three.c_str(), -1) == 0u && geode_mod_lfo_shows(three.c_str(), GEODE_LFO_SLOTS) == 0u,
          "a slot outside the waves shows nothing");
}

void envelopes() {
    check(geode_mod_adsr_shows(nullptr, 0) == 0u, "an envelope nothing saved moves no knob");
    check(envelopeShows("") == 0u, "an envelope that moves nothing has no knob");
    check(envelopeShows("\"ZOOM\",\"BLOOM\"") == 0u, "an envelope on params has no knob");
    check(envelopeShows("\"MACRO\"") == kKnob, "an envelope on a knob picks the knob");
    check(envelopeShows("\"ZOOM\",\"MACRO\"") == kKnob, "a knob among other targets still picks the knob");
    const std::string two = "[" + envelope("\"ZOOM\"") + "," + envelope("\"MACRO\"") + "]";
    check(geode_mod_adsr_shows(two.c_str(), 0) == 0u && geode_mod_adsr_shows(two.c_str(), 1) == kKnob,
          "each envelope is read on its own");
    check(geode_mod_adsr_shows(two.c_str(), -1) == 0u && geode_mod_adsr_shows(two.c_str(), GEODE_ADSR_SLOTS) == 0u,
          "a slot outside the envelopes shows nothing");
}

}  // namespace

int main() {
    waves();
    envelopes();
    if (failures) {
        std::printf("%d modulation control check(s) failed\n", failures);
        return 1;
    }
    std::printf("modulation control checks passed\n");
    return 0;
}
