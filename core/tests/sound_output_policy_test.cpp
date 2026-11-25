// Host test for the sound output policy table (core/sound/OutputPolicy, read with interp/FrameBlend's kFieldKinds).
// Built with the runner's own compiler in CI:
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore core/tests/sound_output_policy_test.cpp -o sound_output_policy_test
//   ./sound_output_policy_test
//
// Why: every GeodeSoundFrame output has exactly one row saying how it is ranged, smoothed and decayed, and the
// constants the engine's stages read are those rows' values. A field added to the frame and not to the table, a
// name twice, or a row that drifts from its constants fails here (and the asserts in FrameBlend.hpp fail the build).
#include <cstdio>
#include <cstring>

#include "sound/OutputPolicy.hpp"
#include "sound/SoundFrame.h"

using namespace geode::sound::policy;

namespace {

int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        failures++;
    }
}

const Row* find(const char* name) {
    for (const Row& r : kOutputs) {
        if (std::strcmp(r.name, name) == 0) return &r;
    }
    return nullptr;
}

}  // namespace

int main() {
    std::printf("sound output policy: one row per frame output\n");

    // Every float of the frame belongs to exactly one row, in order, with no gap.
    size_t at = 0;
    for (const Row& r : kOutputs) {
        check(r.offset == at, "rows follow the frame's layout with no gap or overlap");
        at += static_cast<size_t>(r.count) * sizeof(float);
    }
    check(at == sizeof(GeodeSoundFrame), "the rows cover the whole frame");
    check(rowOf(0) == &kOutputs[0], "rowOf the first float is the first row");
    check(rowOf(GEODE_SOUND_FRAME_FLOATS - 1) == &kOutputs[kOutputRows - 1], "and of the last float the last row");
    check(rowOf(GEODE_SOUND_FRAME_FLOATS) == nullptr, "past the frame there is none");

    // Names are present and unique.
    for (int i = 0; i < kOutputRows; ++i) {
        check(kOutputs[i].name != nullptr && kOutputs[i].name[0] != '\0', "every row has a name");
        for (int j = i + 1; j < kOutputRows; ++j) {
            if (std::strcmp(kOutputs[i].name, kOutputs[j].name) == 0) {
                std::printf("  twice: %s\n", kOutputs[i].name);
                check(false, "every name is listed once");
            }
        }
    }

    // The scalars are one float each; the arrays close the table.
    for (int i = 0; i < kOutputRows - 6; ++i) check(kOutputs[i].count == 1, "a scalar is one float");
    check(find("waveform") != nullptr && find("waveform")->params.norm == Norm::Drawn, "the waveform is drawn");
    check(find("fadeGain") != nullptr && find("fadeGain")->params.norm == Norm::Told, "fadeGain is told");

    // The rows carry the constants the stages read.
    check(find("bass")->params.norm == Norm::RelativeToMean && find("eq")->params.norm == Norm::RelativeToMean,
          "bands and the EQ are relative to their running mean");
    check(find("bassAtt")->params.attack == 0.2f && find("bassAtt")->params.release == 0.5f, "the band envelope's poles");
    check(find("bassAtt")->params.meanWarm == 0.9f && find("bassAtt")->params.meanSteady == 0.992f, "and its mean's");
    check(find("kickEnv")->params.attack == 0.005f, "a drum envelope rises in 5 ms");
    check(find("onsetEnv")->params.decay == 0.2f && find("transientEnv")->params.decay == 0.2f, "onset envelopes fall in 0.2 s");
    check(find("drive")->params.attack == 0.5f && find("drive")->params.release == 1.5f, "drive: 0.5 s up, 1.5 s down");
    check(find("kickToggle")->params.attack == 0.06f && find("noteRandom")->params.release == 0.06f, "toggles ease over 60 ms");
    check(find("runDensity")->params.decay == 1.0f && find("busy")->params.decay == 2.0f, "the rate counters' time constants");
    check(find("low")->params.attack == 0.03f && find("high")->params.release == 0.15f, "the shaped bands' default attack and release");
    check(find("kick")->params.smoothing == Smoothing::Internal, "a trigger's smoothing is its detector's own");

    if (failures == 0) std::printf("sound_output_policy_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
