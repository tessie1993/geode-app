// The playback DSP's stages start clean when turned back on: a limiter, crossfeed or equalizer switched off (or
// an equalizer band set flat) forgets its delay line and filter state, so turning it on again never plays old
// audio or a tick from it. Built and run by CI (.github/workflows/native-host-tests.yml); no device.
//
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore -Icore/tests/stub core/tests/dsp_stage_state_test.cpp
//     core/audio/dsp/Equalizer.cpp core/audio/dsp/Crossfeed.cpp core/audio/dsp/Limiter.cpp -o dsp_stage_state_test
#include <cstdio>
#include <vector>

#include "audio/dsp/Crossfeed.hpp"
#include "audio/dsp/Equalizer.hpp"
#include "audio/dsp/Limiter.hpp"

using geode::audio::Crossfeed;
using geode::audio::Equalizer;
using geode::audio::Limiter;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

constexpr float kRate = 48000.0f;
constexpr size_t kFrames = 1024;

// A loud stereo block: a square wave on the left, its opposite on the right.
std::vector<float> loud() {
    std::vector<float> b(kFrames * 2);
    for (size_t i = 0; i < kFrames; ++i) {
        const float v = (i / 24) % 2 == 0 ? 1.5f : -1.5f;
        b[i * 2] = v;
        b[i * 2 + 1] = -v;
    }
    return b;
}

bool silent(const std::vector<float>& b) {
    for (float v : b) {
        if (v != 0.0f) return false;
    }
    return true;
}

template <typename Run>
void offThenOnStartsClean(Run run, const char* what) {
    std::vector<float> a = loud();
    run(a, true);
    std::vector<float> b = loud();
    run(b, false);
    std::vector<float> quiet(kFrames * 2, 0.0f);
    run(quiet, true);
    check(silent(quiet), what);
}

void limiter() {
    Limiter l(kRate, 2);
    offThenOnStartsClean(
        [&](std::vector<float>& b, bool on) {
            l.setEnabled(on);
            l.process(b.data(), kFrames);
        },
        "a limiter turned back on plays nothing from before it was off");
}

void crossfeed() {
    Crossfeed c(kRate);
    offThenOnStartsClean(
        [&](std::vector<float>& b, bool on) {
            c.setEnabled(on);
            c.process(b.data(), kFrames, 2);
        },
        "a crossfeed turned back on starts from silence");
}

void equalizer() {
    Equalizer e(kRate);
    for (int band = 0; band < Equalizer::kBands; ++band) e.setBand(band, 1200);
    e.setBassBoost(Equalizer::kMaxBassBoost);
    offThenOnStartsClean(
        [&](std::vector<float>& b, bool on) {
            e.setEnabled(on);
            e.process(b.data(), kFrames, 2);
        },
        "an equalizer turned back on starts from silence");

    // A band set flat and raised again, the equalizer on throughout.
    Equalizer f(kRate);
    f.setEnabled(true);
    for (int band = 0; band < Equalizer::kBands; ++band) f.setBand(band, 1200);
    f.setBassBoost(Equalizer::kMaxBassBoost);
    std::vector<float> a = loud();
    f.process(a.data(), kFrames, 2);
    for (int band = 0; band < Equalizer::kBands; ++band) f.setBand(band, 0);
    f.setBassBoost(0);
    std::vector<float> b = loud();
    f.process(b.data(), kFrames, 2);
    for (int band = 0; band < Equalizer::kBands; ++band) f.setBand(band, 1200);
    f.setBassBoost(Equalizer::kMaxBassBoost);
    std::vector<float> quiet(kFrames * 2, 0.0f);
    f.process(quiet.data(), kFrames, 2);
    check(silent(quiet), "a band raised again after it was flat starts from silence");
}

}  // namespace

int main() {
    limiter();
    crossfeed();
    equalizer();
    if (failures) {
        std::printf("%d dsp stage state check(s) failed\n", failures);
        return 1;
    }
    std::printf("dsp stage state: all checks passed\n");
    return 0;
}
