#pragma once
#include <algorithm>
#include <cmath>

#include "sound/SoundFrame.h"

namespace geode::sound {

// How much of its reach the music gets to spend: the one gain every consumer multiplies the MAGNITUDE of
// audio-driven motion by (the split: absolute loudness for "how much", relative ratios only for "what is
// changing now"). It is the frame's drive (SoundFrame.h), absolute, so a quiet ambient pad moves the
// picture a little and a loud dense track moves it fully, on every song without a per-song normalizer.
//
//   gain = floor + (1 - floor) * min(1, drive / full)
//
// `floor` keeps quiet music breathing; `full` is where the reach is all spent. A hit is weighed by the drive
// once already, in the engine (its strength and worth, SoundEngine's kHitDriveFloor), and every route, the
// motion field and the camera scale by this gain on top, so the drive counts twice on a hit: `full` sits at
// 0.5, an ordinary well-mastered song (a loud, dense one at about -9 LUFS reads 0.75), so music of everyday
// loudness reaches the whole picture instead of a fraction of a fraction of it.
// The user's own settings (a set LFO depth, a base value, the Motion dial) are never scaled by it.
inline constexpr float kDriveGainFloor = 0.15f;
inline constexpr float kDriveFull = 0.5f;

inline float driveGain(float drive) {
    const float d = std::isfinite(drive) ? std::clamp(drive / kDriveFull, 0.0f, 1.0f) : 1.0f;
    return kDriveGainFloor + (1.0f - kDriveGainFloor) * d;
}
inline float driveGain(const GeodeSoundFrame& f) { return driveGain(f.drive); }

// The clock the picture runs on: every renderer multiplies its frame step by this. 1 while music plays and
// exactly 0 in silence (owner's rule: no sound, no movement). presence fades over about two seconds, so a
// pause eases the picture to a stop and the first sound eases it back in.
inline constexpr float kMotionStill = 0.05f;
inline constexpr float kMotionFull = 0.5f;

inline float motionRate(const GeodeSoundFrame& f) {
    if (!std::isfinite(f.presence)) return 0.0f;
    const float t = std::clamp((f.presence - kMotionStill) / (kMotionFull - kMotionStill), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

}  // namespace geode::sound
