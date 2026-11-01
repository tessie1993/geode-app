#pragma once

#include <algorithm>
#include <cmath>
#include <string_view>

#include "sound/SoundFrame.h"

namespace geode::viz::endless {

// Endless zoom: a dive that never arrives, looped like a Shepard tone.
//
// One magnification cannot do this. A law that only ever grows has to jump back
// at the phase wrap, and that jump is a visible scale pop (the old sawtooth
// `pow(2, phase)`). A law that stays continuous across the wrap has to come back
// down (the triangle `pow(2, 1 - |2 phase - 1|)` that replaced it), so the
// picture dived in to 2x and then backed out again.
//
// The fix is the Shepard-tone trick drawn in space: two copies of the picture an
// octave apart, both always zooming IN. The nearer copy fades out as it grows
// while the farther copy fades in, so the pair at phase 1 is exactly the pair at
// phase 0 and the wrap is invisible. The visible scale stays within 0.5x..2x of
// the Zoom slider, so the subject keeps its size on average and nothing needs
// float precision beyond one octave.
//
// The core computes the layers and uploads them as `uZoomLayers` (lib_zoom.glsl:
// x = first layer's magnification, y = second's, z = second's weight); the
// shaders hold no zoom maths of their own. The 3D styles (lib_camera) do not
// draw two layers: they would march every ray twice. They dive by flying the
// CameraRig further along its path instead (CameraRig::step).

/**
 * Crossfade window inside the octave. Outside it a single layer is drawn, so the
 * second pass (a whole second run of the style's main()) costs only ~40% of the
 * frames while Endless zoom is on, and nothing while it is off.
 */
inline constexpr float kFadeStart = 0.3f;
inline constexpr float kFadeEnd = 0.7f;
/** Below this the second layer is skipped: it would not change a pixel at 8-bit output. */
inline constexpr float kSkipWeight = 0.002f;

struct Layers {
    float mag0 = 1.0f;     // the dominant layer, always drawn
    float mag1 = 1.0f;     // the other layer
    float weight1 = 0.0f;  // the other layer's share; 0 draws only the first
};

/**
 * One frame of the dive: phase in octaves, wrapped into [0, 1). A negative speed
 * dives back out just as seamlessly; non-finite input holds the phase rather than
 * poisoning it, since the sound engine and the LFOs both reach the speed.
 */
inline float advance(float phase, float octavesPerSecond, float dt) {
    const float step = octavesPerSecond * dt;
    if (!std::isfinite(step) || !std::isfinite(phase)) return std::isfinite(phase) ? phase : 0.0f;
    const float next = phase + step;
    const float wrapped = next - std::floor(next);
    return wrapped >= 1.0f ? 0.0f : wrapped;  // -1e-9 floors to 1.0 in float
}

/** How hard a bass hit pushes the dive: bassAtt 2 (a strong hit) dives 1.35x as fast. */
inline constexpr float kBassPush = 0.35f;
/** Silence drifts at this share of Dive speed: slow, but never a full stop (owner's call). */
inline constexpr float kSilenceRate = 0.3f;
/** However far below its mean the bass falls, the dive never stalls or backs out. */
inline constexpr float kMinRate = 0.25f;

/**
 * The music's hand on the dive, a multiplier on Dive speed. MilkDrop dives by a
 * per-frame zoom that presets nudge with the bass, `zoom = 1 + k (bass_att - 1)`;
 * this is that idiom on the sound engine's own bassAtt (core/sound/SoundFrame.h),
 * the relative level: the band over its own running mean, 1.0 typical for this
 * song right now. So the dive surges on the hits and eases between them, and over
 * a song it averages back to the slider. It reads no loudness: a loud song dives
 * no faster than a quiet one.
 *
 * presence (0..1, ~2 s fades) blends between the music's rate and the silent
 * drift, so a pause eases the dive down to kSilenceRate instead of freezing it.
 * Audio drive scales the music's reach like everywhere else; at 0 the dive runs
 * at exactly Dive speed whatever plays.
 */
inline float musicRate(const GeodeSoundFrame& s, float audioDrive) {
    auto val = [](float v, float fallback) { return std::isfinite(v) ? v : fallback; };
    const float presence = std::clamp(val(s.presence, 0.0f), 0.0f, 1.0f);
    const float bass = std::clamp(val(s.bassAtt, 1.0f), 0.0f, 4.0f);
    const float drive = std::clamp(val(audioDrive, 1.0f), 0.0f, 2.0f);
    const float silenceDrop = std::min(drive, 1.0f) * (1.0f - presence) * (1.0f - kSilenceRate);
    const float push = kBassPush * drive * presence * (bass - 1.0f);
    return std::max(1.0f - silenceDrop + push, kMinRate);
}

inline float fadeIn(float phase) {
    const float t = std::clamp((phase - kFadeStart) / (kFadeEnd - kFadeStart), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/**
 * The two layers at `phase` (octaves, [0, 1)). The outgoing layer grows 1x -> 2x
 * and fades out; the incoming one grows 0.5x -> 1x and fades in. Phase 0 (and
 * Endless zoom off) is a single layer at exactly 1x.
 */
inline Layers layers(float phase) {
    phase = std::isfinite(phase) ? std::clamp(phase, 0.0f, 1.0f) : 0.0f;
    const float outgoing = std::exp2(phase);
    const float incoming = std::exp2(phase - 1.0f);
    const float in = fadeIn(phase);
    Layers l;
    if (in <= 0.5f) {
        l = {outgoing, incoming, in};
    } else {
        l = {incoming, outgoing, 1.0f - in};
    }
    if (l.weight1 < kSkipWeight) l.weight1 = 0.0f;
    return l;
}

/**
 * The style families still on the old in-and-out law: Glass and Truchet are being
 * redone in their own branch, which adopts lib_zoom itself. Until then they read
 * one layer at the old triangle magnification and their camera does not dive, so
 * this change leaves them exactly as they were. Delete a prefix to move it over.
 */
inline bool keepsTriangle(std::string_view styleId) {
    return styleId.rfind("glass_", 0) == 0 || styleId.rfind("truchet_", 0) == 0;
}

/** The old law, 1x -> 2x -> 1x, kept only for keepsTriangle styles. */
inline Layers triangle(float phase) {
    phase = std::isfinite(phase) ? std::clamp(phase, 0.0f, 1.0f) : 0.0f;
    return {std::exp2(1.0f - std::fabs(2.0f * phase - 1.0f)), 1.0f, 0.0f};
}

}  // namespace geode::viz::endless
