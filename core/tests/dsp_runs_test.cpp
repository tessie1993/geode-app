// Which formats the playback DSP runs (geode_dsp_runs, audio/dsp/DspProcessor::runs). A 5.1 or 7.1 file used
// to fail the sink's configure (the Kotlin processor threw), and ExoPlayer then called the whole file
// unplayable; the shell's processor now steps aside for any format this answers 0 for. The cases are the
// Kotlin NativeDspProcessorTest's, one for one. Encodings are numbered as Media3's C.ENCODING_PCM_*.
#include <cstdio>
#include <initializer_list>

#include "api/geode_api.h"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

constexpr int kPcm8 = 3;
constexpr int kPcm24 = 21;
constexpr int kPcm32 = 22;

void monoAndStereo16BitAndFloatRunTheChain() {
    for (int encoding : {GEODE_PCM_S16, GEODE_PCM_FLOAT}) {
        check(geode_dsp_runs(encoding, 1) == 1, "mono runs");
        check(geode_dsp_runs(encoding, 2) == 1, "stereo runs");
    }
}

void moreThanTwoChannelsStepAside() {
    for (int channels = 3; channels <= 8; ++channels) {
        check(geode_dsp_runs(GEODE_PCM_S16, channels) == 0, "16-bit, more than two channels, steps aside");
        check(geode_dsp_runs(GEODE_PCM_FLOAT, channels) == 0, "float, more than two channels, steps aside");
    }
    check(geode_dsp_runs(GEODE_PCM_S16, 0) == 0, "no channels steps aside");
}

void otherEncodingsStepAside() {
    check(geode_dsp_runs(kPcm24, 2) == 0, "24-bit steps aside");
    check(geode_dsp_runs(kPcm32, 2) == 0, "32-bit steps aside");
    check(geode_dsp_runs(kPcm8, 2) == 0, "8-bit steps aside");
}

void aSixChannelStreamConfiguresInactive() {
    geode_dsp* d = geode_dsp_create();
    check(geode_dsp_configure(d, 48000, 6) == 0, "six channels configure to pass-through");
    check(geode_dsp_configure(d, 48000, 2) == 1, "stereo configures a chain");
    geode_dsp_destroy(d);
}

// The shell's configure (geode_dsp_configure_input), which held the rule in Kotlin before.
void theShellsConfigureRunsOrStepsAside() {
    geode_dsp* d = geode_dsp_create();
    check(geode_dsp_configure_input(d, GEODE_PCM_S16, 44100, 2) == 1, "16-bit stereo configures a chain");
    check(geode_dsp_configure_input(d, GEODE_PCM_FLOAT, 48000, 1) == 1, "float mono configures a chain");
    check(geode_dsp_configure_input(d, GEODE_PCM_S16, 48000, 6) == 0, "six channels step aside");
    check(geode_dsp_configure_input(d, kPcm24, 48000, 2) == 0, "24-bit steps aside");
    check(geode_dsp_configure_input(nullptr, GEODE_PCM_S16, 48000, 2) == 0, "no dsp steps aside");
    geode_dsp_destroy(d);
}

// The shell's block (geode_dsp_process_into): copied from where it lies into the output, held to its capacity.
void theShellsBlockIsCopiedThenRun() {
    const unsigned char in[10] = {9, 9, 1, 2, 3, 4, 5, 6, 7, 8};
    unsigned char out[8] = {};
    check(geode_dsp_process_into(nullptr, in + 2, 8, out, sizeof out, GEODE_PCM_S16, 2) == 8, "two stereo frames pass");
    bool same = true;
    for (int i = 0; i < 8; ++i) same = same && out[i] == in[i + 2];
    check(same, "the bytes from the position are the output's from 0");
    check(geode_dsp_process_into(nullptr, in, 10, out, 6, GEODE_PCM_S16, 2) == 4, "held to the output's capacity, whole frames");
    check(geode_dsp_process_into(nullptr, in, 7, out, sizeof out, GEODE_PCM_S16, 2) == 4, "a part frame is dropped");
    check(geode_dsp_process_into(nullptr, nullptr, 8, out, sizeof out, GEODE_PCM_S16, 2) == 0, "no input copies nothing");
    check(geode_dsp_process_into(nullptr, in, 8, nullptr, 8, GEODE_PCM_S16, 2) == 0, "no output copies nothing");
    check(geode_dsp_process_into(nullptr, in, 0, out, sizeof out, GEODE_PCM_S16, 2) == 0, "no bytes is 0");
    geode_dsp_set_up_next(nullptr, 1, GEODE_REPEAT_ONE);
}

}  // namespace

int main() {
    monoAndStereo16BitAndFloatRunTheChain();
    moreThanTwoChannelsStepAside();
    otherEncodingsStepAside();
    aSixChannelStreamConfiguresInactive();
    theShellsConfigureRunsOrStepsAside();
    theShellsBlockIsCopiedThenRun();
    if (failures != 0) return 1;
    std::printf("dsp_runs_test: ok\n");
    return 0;
}
