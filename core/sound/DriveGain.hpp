#pragma once
#include <algorithm>
#include <cmath>

#include "sound/SoundFrame.h"

namespace geode::sound {

// How much of its reach the music gets to spend: the one gain every consumer multiplies the MAGNITUDE of
// audio-driven motion by (fosfora's split: absolute loudness for "how much", relative ratios only for "what
// is changing now"). It is the frame's drive (SoundFrame.h), absolute, so a quiet ambient pad moves the
// picture a little and a loud dense track moves it fully, on every song without a per-song normalizer.
//
//   gain = floor + (1 - floor) * min(1, drive / full)
//
// `floor` keeps quiet music breathing; `full` is the drive of a loud, dense master (about -9 LUFS with many
// onsets and a locked beat reads 0.75), which spends everything, so a fat track moves as it always did.
// The user's own settings (a set LFO depth, a base value, the Motion dial) are never scaled by it.
inline constexpr float kDriveGainFloor = 0.15f;
inline constexpr float kDriveFull = 0.7f;

inline float driveGain(float drive) {
    const float d = std::isfinite(drive) ? std::clamp(drive / kDriveFull, 0.0f, 1.0f) : 1.0f;
    return kDriveGainFloor + (1.0f - kDriveGainFloor) * d;
}
inline float driveGain(const GeodeSoundFrame& f) { return driveGain(f.drive); }

}  // namespace geode::sound
