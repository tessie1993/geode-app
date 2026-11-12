#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "audio/dsp/EqPresets.hpp"

// The sound effects' ranges, the one place the shell and the chain take them from: a band level in
// millibels, the bass boost in permille, the loudness makeup in millibels.
namespace geode::audio::fx {

enum Control : int { kBand = 0, kBassBoost = 1, kLoudness = 2 };

inline constexpr int kMaxLoudnessMb = 1000;

struct Range {
    int min = 0;
    int max = 0;
};

// {0, 0} for a control that is not one.
constexpr Range rangeOf(int control) {
    switch (control) {
        case kBand: return {Equalizer::kMinMillibels, Equalizer::kMaxMillibels};
        case kBassBoost: return {0, Equalizer::kMaxBassBoost};
        case kLoudness: return {0, kMaxLoudnessMb};
        default: return {};
    }
}

constexpr int clamp(int control, int value) {
    const Range r = rangeOf(control);
    return std::clamp(value, r.min, r.max);
}

// The band levels a session starts on, into out[kBands]: the saved curve's when `preset` names one, else
// the saved levels, a band with neither at 0; each held to the band range.
constexpr void restoreBands(int preset, const int32_t* stored, int storedCount, int32_t* out) {
    const bool curve = preset >= 0 && preset < kEqPresets;
    for (int band = 0; band < Equalizer::kBands; ++band) {
        int level = 0;
        if (curve) {
            level = kEqPresetLevels[static_cast<size_t>(preset)][static_cast<size_t>(band)];
        } else if (stored && band < storedCount) {
            level = stored[band];
        }
        out[band] = clamp(kBand, level);
    }
}

}  // namespace geode::audio::fx
