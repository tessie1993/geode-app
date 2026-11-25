// Host test: every value the sound analysis puts in a GeodeSoundFrame is a reaction source the user can pick in
// a route, or is named below with the reason it is not. A field added to SoundFrame.h with neither fails here, and
// so does a reason left behind once its field has a source.
// Built with the runner's own compiler in CI (core/tests/host-tests.txt: sound_sources_test).
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "sound/SoundFrame.h"
#include "tests/SoundFields.hpp"
#include "viz/modulation/Reactions.hpp"

using geode::sound::test::kFieldCount;
using geode::sound::test::kFields;
using geode::viz::kReactSourceCount;
using geode::viz::ReactSource;
using geode::viz::reactSourceField;
using geode::viz::reactSourceInfo;

namespace {

int failures = 0;

void fail(const std::string& what) {
    std::printf("FAIL: %s\n", what.c_str());
    ++failures;
}

struct Unrouted {
    const char* field;
    const char* why;
};

// The scalars no route reads directly.
constexpr Unrouted kUnrouted[] = {
    {"rms", "linear level; Loudness is the same measure on the 0..1 scale a route reads"},
    {"pitch", "a MIDI note number; PitchClass and PitchHeight are its 0..1 views, and the melody sources read it"},
    {"beatCount", "a count that wraps; BeatPhase and Beat are what a route reads, and the waves' beat timing reads it"},
    {"barCount", "a count that wraps; it numbers the values of the bar-count sources"},
    {"phraseIndex", "a count that wraps; it numbers the values of the phrase-count sources"},
    {"scale", "a catalogue index, not a magnitude; ScaleBrightness, ScaleTension and ScaleFit are its views"},
    {"sectionBars", "a count that wraps; SectionProgress is worked out from it"},
};

// The arrays no route reads: a route moves one value, and these are pictures of the sound a style draws.
constexpr Unrouted kUnroutedArrays[] = {
    {"spectrum", "64 points a style draws as a texture"},
    {"waveform", "128 points a style draws as a texture"},
    {"chroma", "12 pitch classes; KeyHue, ChordHue and Tension are worked out from them"},
    {"chromaFast", "12 pitch classes; Harmony's own working value"},
    {"chromaSlow", "12 pitch classes; the key is read from them"},
};

int indexOf(float GeodeSoundFrame::* member) {
    static const GeodeSoundFrame frame{};
    const auto* base = reinterpret_cast<const unsigned char*>(&frame);
    const auto* at = reinterpret_cast<const unsigned char*>(&(frame.*member));
    return static_cast<int>(static_cast<size_t>(at - base) / sizeof(float));
}

}  // namespace

int main() {
    std::vector<int> sources(GEODE_SOUND_FRAME_FLOATS, 0);
    std::set<std::string> names;
    int derived = 0;
    for (int i = 0; i < kReactSourceCount; ++i) {
        const auto source = static_cast<ReactSource>(i);
        const char* name = reactSourceInfo(source).name;
        if (name == nullptr || name[0] == '\0') fail("source " + std::to_string(i) + " has no saved name");
        else if (!names.insert(name).second) fail(std::string("two sources share the saved name ") + name);
        const int field = reactSourceField(source);
        if (field < 0) {
            ++derived;
        } else if (field >= GEODE_SOUND_FRAME_FLOATS) {
            fail(std::string(name ? name : "?") + " reads past the end of the frame");
        } else {
            ++sources[static_cast<size_t>(field)];
        }
    }

    int routed = 0;
    for (int k = 0; k < kFieldCount; ++k) {
        const int index = indexOf(kFields[k].member);
        const bool has = sources[static_cast<size_t>(index)] > 0;
        const Unrouted* reason = nullptr;
        for (const Unrouted& u : kUnrouted) {
            if (std::strcmp(u.field, kFields[k].name) == 0) reason = &u;
        }
        if (has) ++routed;
        if (!has && reason == nullptr) {
            fail(std::string(kFields[k].name) + " is an analysis output with no reaction source and no reason here");
        }
        if (has && reason != nullptr) {
            fail(std::string(kFields[k].name) + " has a reaction source now: take its line out of kUnrouted");
        }
        if (reason != nullptr && reason->why[0] == '\0') fail(std::string(kFields[k].name) + " is unrouted with no reason");
    }
    for (const Unrouted& u : kUnrouted) {
        if (geode::sound::test::fieldIndex(u.field) < 0) fail(std::string(u.field) + " in kUnrouted is not a frame field");
    }

    const int eqFirst = static_cast<int>(offsetof(GeodeSoundFrame, eq) / sizeof(float));
    for (int band = 0; band < GEODE_SOUND_EQ_BANDS; ++band) {
        if (sources[static_cast<size_t>(eqFirst + band)] == 0) fail("EQ band " + std::to_string(band) + " has no reaction source");
    }
    // Everything after the EQ is the arrays kUnroutedArrays names; a source on one of them needs its line changed.
    const int arraysFirst = eqFirst + GEODE_SOUND_EQ_BANDS;
    if (arraysFirst != static_cast<int>(offsetof(GeodeSoundFrame, spectrum) / sizeof(float))) {
        fail("the frame's arrays moved: the spectrum no longer follows the EQ");
    }
    const int arrayFloats =
        GEODE_SOUND_SPECTRUM_POINTS + GEODE_SOUND_WAVEFORM_POINTS + 3 * GEODE_SOUND_CHROMA;
    if (arraysFirst + arrayFloats != GEODE_SOUND_FRAME_FLOATS) fail("the frame gained an array kUnroutedArrays does not name");
    for (int i = arraysFirst; i < GEODE_SOUND_FRAME_FLOATS; ++i) {
        if (sources[static_cast<size_t>(i)] > 0) fail("a reaction source reads frame float " + std::to_string(i) + ", in an array kUnroutedArrays calls unrouted");
    }
    for (const Unrouted& u : kUnroutedArrays) {
        if (u.why[0] == '\0') fail(std::string(u.field) + " is unrouted with no reason");
    }

    const int unrouted = static_cast<int>(sizeof(kUnrouted) / sizeof(kUnrouted[0]));
    if (routed + unrouted != GEODE_SOUND_SCALARS) fail("routed and unrouted scalars do not add up to the frame's");
    std::printf("analysis outputs: %d scalars, %d with a reaction source, %d without (each with a reason); %d EQ bands, all sources; "
                "%d sources worked out from others; %d sources in all\n",
                GEODE_SOUND_SCALARS, routed, unrouted, GEODE_SOUND_EQ_BANDS, derived, kReactSourceCount);
    if (failures != 0) {
        std::printf("%d FAILED\n", failures);
        return 1;
    }
    std::printf("sound_sources_test OK\n");
    return 0;
}
