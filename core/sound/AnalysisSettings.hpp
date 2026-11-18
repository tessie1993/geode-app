#pragma once
#include <algorithm>

namespace geode::sound {

// The one setting that owns the sound engine's step: the FFT window and how many windows overlap.
//
// The hop is fftSize / overlap samples at the analysis rate: a sample count, never a time. At the default
// (2048 points, overlap 4) it is 512 samples at 44.1, 48 and 96 kHz (analysed at 48): 11.6 ms at 44.1 kHz,
// 10.7 ms at 48. Every detector is handed the hop's duration (hopSamples / rate) at configure and derives its
// own counts from durations in seconds, so another size or overlap changes the cadence and nothing else.
struct AnalysisSettings {
    int fftSize = 2048;  // a power of two, 256 … 16384
    int overlap = 4;     // windows over each sample, 1 … fftSize

    constexpr int hopSamples() const { return fftSize / overlap; }

    // The nearest settings the engine can run: a power of two in range, an overlap that leaves a hop.
    constexpr AnalysisSettings checked() const {
        int n = 256;
        while (n < 16384 && n < fftSize) n *= 2;
        return {n, std::clamp(overlap, 1, n)};
    }
};

static_assert(AnalysisSettings{}.hopSamples() == 512, "the default hop is 512 samples at every rate");
static_assert(AnalysisSettings{}.checked().fftSize == AnalysisSettings{}.fftSize, "the default runs as written");

}  // namespace geode::sound
