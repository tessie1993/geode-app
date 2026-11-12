#include "audio/dsp/PlaybackControls.hpp"

#include <cmath>

namespace geode::audio {

float pitchRatio(float semitones) {
    return static_cast<float>(std::pow(2.0, static_cast<double>(semitones) / 12.0));
}

float snapToStep(float value, float step) {
    if (!(step > 0.0f) || !std::isfinite(step) || !std::isfinite(value)) return value;
    return std::floor(value / step + 0.5f) * step;
}

ErrorAction errorAction(int consecutiveFailures, bool hasNext) {
    if (consecutiveFailures >= kMaxConsecutiveFailures) return ErrorAction::StopSourceUnavailable;
    return hasNext ? ErrorAction::SkipToNext : ErrorAction::StopEndOfQueue;
}

}  // namespace geode::audio
