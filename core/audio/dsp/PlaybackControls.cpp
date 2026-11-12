#include "audio/dsp/PlaybackControls.hpp"

#include <algorithm>
#include <cmath>

namespace geode::audio {

float pitchRatio(float semitones) {
    return static_cast<float>(std::pow(2.0, static_cast<double>(semitones) / 12.0));
}

float snapToStep(float value, float step) {
    if (!(step > 0.0f) || !std::isfinite(step) || !std::isfinite(value)) return value;
    return std::floor(value / step + 0.5f) * step;
}

ControlRange controlRange(int control) {
    switch (control) {
        case kFadeMs: return {0.0f, 6000.0f, 250.0f, 0.0f};
        case kCrossfadeMs: return {0.0f, 12000.0f, 1000.0f, 0.0f};
        case kRepeatMode: return {0.0f, 2.0f, 0.0f, 0.0f};
        case kSpeed: return {0.5f, 2.0f, 0.05f, 1.0f};
        case kPitchSemitones: return {-6.0f, 6.0f, 0.5f, 0.0f};
        case kReplayGainMode: return {0.0f, 2.0f, 0.0f, 0.0f};
        case kReplayGainPreampDb: return {-15.0f, 15.0f, 0.5f, 0.0f};
        default: return {};
    }
}

float clampControl(int control, float value) {
    if (control < 0 || control >= kPlaybackControls) return value;
    const ControlRange r = controlRange(control);
    return std::isnan(value) ? r.fallback : std::clamp(value, r.min, r.max);
}

int crossfadeMsFromSeconds(float seconds) {
    if (std::isnan(seconds)) return 0;
    const ControlRange r = controlRange(kCrossfadeMs);
    // Clamped in seconds first, so a huge value cannot overflow the multiply.
    const float whole = std::floor(std::clamp(seconds, r.min, r.max / r.step) + 0.5f);
    return static_cast<int>(whole * r.step);
}

}  // namespace geode::audio
