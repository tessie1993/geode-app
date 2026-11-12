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

}  // namespace

int main() {
    monoAndStereo16BitAndFloatRunTheChain();
    moreThanTwoChannelsStepAside();
    otherEncodingsStepAside();
    aSixChannelStreamConfiguresInactive();
    if (failures != 0) return 1;
    std::printf("dsp_runs_test: ok\n");
    return 0;
}
