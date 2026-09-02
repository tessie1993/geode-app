#include "analysis/PulseReplay.hpp"

#include <algorithm>
#include <cmath>

#include "analysis/BeatGrid.hpp"
#include "analysis/OnsetPeakPicker.hpp"
#include "analysis/TempoTracker.hpp"

namespace geode::analysis::pulse {

namespace {
constexpr float kMacroPeakSeconds = 20.0f;
}

void decide(const float* flux, size_t fluxCount, const float* rms, size_t rmsCount, float hopRateHz, float sensitivity,
            float refractoryMs, Frame* out) {
    OnsetPeakPicker picker(hopRateHz, 1.5f, sensitivity, refractoryMs / 1000.0f);
    TempoTracker tempo(hopRateHz);
    BeatGrid grid;

    const float decay = std::exp(-1.0f / (hopRateHz * kMacroPeakSeconds));
    float peak = 0.0f;

    for (size_t i = 0; i < fluxCount; i++) {
        const bool isOnset = picker.accept(flux[i]);
        out[i].transient = picker.strength();
        tempo.step(flux[i]);
        const bool beat = grid.step(tempo.periodFrames(), tempo.confidence(), isOnset);
        out[i].beat = beat ? 1.0f : 0.0f;
        out[i].strength = beat ? picker.strength() : 0.0f;
        out[i].phase = grid.phase();
        out[i].confidence = tempo.confidence();

        const float level = i < rmsCount ? rms[i] : 0.0f;
        peak = std::max(level, peak * decay);
        out[i].energy = peak <= 1e-6f ? 0.0f : std::clamp(level / peak, 0.0f, 1.0f);
    }
}

}  // namespace geode::analysis::pulse
