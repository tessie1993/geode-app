#pragma once

#include <array>
#include <cstddef>

#include "sound/AnalysisSettings.hpp"
#include "sound/SoundFrame.h"

namespace geode::sound::interp {

// The frame's waveform (GeodeSoundFrame::waveform), drawn per frame from the audio a surface hears, since a
// published analysis step carries none: the newest ~13 ms of the mono mix as block means, slid over a little
// slack to best match the last drawing so a steady tone stands still (SoundEngine's drawing, at the input rate).
// One thread; no allocation.
class Waveform {
public:
    // `settings` are the analysis's (sound/AnalysisSettings): the block is the engine's, n / 512 analysis samples.
    explicit Waveform(AnalysisSettings settings = {});
    // Interleaved PCM in play order; channels past the second are ignored, mono feeds both.
    void write(const float* interleaved, int frames, int channels, int sampleRateHz);
    void draw(float* out);
    // The analysis's input trim (GeodeSoundTuning::inputTrimDb), which the engine's drawing heard too.
    void setTrimDb(float db);
    void reset();

private:
    static constexpr int kPoints = GEODE_SOUND_WAVEFORM_POINTS;
    static constexpr int kSlack = 32;
    static constexpr int kMaxBlock = 32;
    static constexpr int kHistory = (kPoints + kSlack) * kMaxBlock;

    std::array<float, kHistory> mono_{};
    int head_ = 0;
    int rateHz_ = 0;
    int fftSize_ = 0;
    int block_ = 1;
    float dcR_ = 0.0f;
    float dcX_ = 0.0f;
    float dcY_ = 0.0f;
    float trim_ = 1.0f;
    std::array<float, kPoints + kSlack> raw_{};
    std::array<float, kPoints> prev_{};
    std::array<float, kPoints> weight_{};
    bool hasPrev_ = false;
};

}  // namespace geode::sound::interp
