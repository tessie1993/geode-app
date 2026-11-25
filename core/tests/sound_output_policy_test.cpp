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

// A row by name; a name that is gone fails a check and reads a blank row, so the rest still runs.
const Row& row(const char* name) {
    static const Row kMissing{"", 0, 0, {}};
    const Row* r = find(name);
    if (r == nullptr) {
        std::printf("  missing: %s
", name);
        check(false, "every row the test reads exists");
        return kMissing;
    }
    return *r;
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
    int scalars = 0;
    while (scalars < kOutputRows && kOutputs[scalars].count == 1) ++scalars;
    for (int i = scalars; i < kOutputRows; ++i) check(kOutputs[i].count > 1, "once an array comes, only arrays follow");
    check(scalars > 0 && scalars < kOutputRows, "the table has scalars, then arrays");
    check(row("waveform").params.norm == Norm::Drawn, "the waveform is drawn");
    check(row("fadeGain").params.norm == Norm::Told, "fadeGain is told");

    // The rows carry the constants the stages read.
    check(row("bass").params.norm == Norm::RelativeToMean && row("eq").params.norm == Norm::RelativeToMean,
          "bands and the EQ are relative to their running mean");
    check(row("bassAtt").params.attack == 0.2f && row("bassAtt").params.release == 0.5f, "the band envelope's poles");
    check(row("bassAtt").params.meanWarm == 0.9f && row("bassAtt").params.meanSteady == 0.992f, "and its mean's");
    check(row("kickEnv").params.attack == 0.005f, "a drum envelope rises in 5 ms");
    check(row("onsetEnv").params.decay == 0.2f && row("transientEnv").params.decay == 0.2f, "onset envelopes fall in 0.2 s");
    check(row("drive").params.attack == 0.5f && row("drive").params.release == 1.5f, "drive: 0.5 s up, 1.5 s down");
    check(row("kickToggle").params.attack == 0.06f && row("noteRandom").params.release == 0.06f, "toggles ease over 60 ms");
    check(row("runDensity").params.decay == 1.0f && row("busy").params.decay == 2.0f, "the rate counters' time constants");
    check(row("low").params.attack == 0.03f && row("high").params.release == 0.15f, "the shaped bands' default attack and release");
    check(row("kick").params.smoothing == Smoothing::Internal, "a trigger's smoothing is its detector's own");

    if (failures == 0) std::printf("sound_output_policy_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
