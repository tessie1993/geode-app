// Host test for the renderer's drawn waveform (core/sound/interp/Waveform): its block is the engine's, the
// configured FFT size / 512 analysis samples, at every input rate. Built with the runner's own compiler in CI:
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore core/tests/waveform_test.cpp core/sound/interp/Waveform.cpp
//     core/sound/*.cpp -o waveform_test
//   ./waveform_test
//
// Why: the block came from a size picked by the input rate, not from the engine's settings, so at 16 or 22.05 kHz
// the live drawing spanned half the time the engine's (and an export's) did. A one-sample click drawn as block means
// is 1 / block high, which reads the block back.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

#include "sound/AnalysisSettings.hpp"
#include "sound/interp/Waveform.hpp"

using geode::sound::AnalysisSettings;
using geode::sound::interp::Waveform;

namespace {

int failures = 0;

void check(bool ok, const char* what, int rate, int fft, int block, float peak) {
    if (ok) return;
    failures++;
    std::printf("FAIL %s: %d Hz, FFT %d: expected a block of %d, the click drew %.4f\n", what, rate, fft, block,
                static_cast<double>(peak));
}

// The click's height in the first drawing: 1 / block, less the DC blocker's small tail.
float clickPeak(int rate, AnalysisSettings settings, int block) {
    Waveform w(settings);
    // Forty blocks after the click, so it sits well inside the drawing whatever the block is.
    std::vector<float> pcm(static_cast<size_t>(1 + 40 * block), 0.0f);
    pcm[0] = 1.0f;
    w.write(pcm.data(), static_cast<int>(pcm.size()), 1, rate);
    float out[GEODE_SOUND_WAVEFORM_POINTS];
    w.draw(out);
    float peak = 0.0f;
    for (float v : out) peak = std::max(peak, v);
    return peak;
}

// The player's thread writes while the frame's draws: each call holds the object's own lock, so nothing tears and nothing
// waits on anything else (the renderer's soundLock_ is not involved). Every drawn point stays finite.
void writerAndDrawer() {
    Waveform w;
    std::atomic<bool> done{false};
    std::thread writer([&] {
        std::vector<float> pcm(1024, 0.25f);
        for (int i = 0; i < 4000; i++) w.write(pcm.data(), static_cast<int>(pcm.size()), 1, 48000);
        done.store(true);
    });
    float out[GEODE_SOUND_WAVEFORM_POINTS];
    bool finite = true;
    while (!done.load()) {
        w.draw(out);
        for (float v : out) finite = finite && std::isfinite(v);
    }
    writer.join();
    if (!finite) {
        failures++;
        std::printf("FAIL a drawing read while the writer wrote held a non-finite point\n");
    }
}

}  // namespace

int main() {
    struct Case {
        int rate;
        AnalysisSettings settings;
        int block;  // input samples: fftSize / 512 analysis samples, times the input's halvings to the analysis rate
    };
    const Case cases[] = {
        {16000, {}, 4},           {22050, {}, 4},           {44100, {}, 4},          {48000, {}, 4},
        {96000, {}, 8},           {44100, {4096, 4}, 8},    {16000, {1024, 4}, 2},   {22050, {8192, 8}, 16},
    };
    for (const Case& c : cases) {
        const float peak = clickPeak(c.rate, c.settings, c.block);
        const float expected = 1.0f / static_cast<float>(c.block);
        std::printf("  %6d Hz, FFT %5d: click %.4f (block %d: %.4f)\n", c.rate, c.settings.fftSize, static_cast<double>(peak), c.block,
                    static_cast<double>(expected));
        check(std::fabs(peak / expected - 1.0f) < 0.05f, "the drawn block is the engine's", c.rate, c.settings.fftSize, c.block, peak);
    }
    writerAndDrawer();
    if (failures > 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("waveform_test: all passed\n");
    return 0;
}
