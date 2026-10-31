#pragma once

#include <algorithm>
#include <cmath>

#include "sound/SoundFrame.h"

namespace geode::viz::endless {

// Endless zoom: "a dive that never arrives".
//
// One magnification cannot do this. A law that only ever grows must jump back
// at the phase wrap, and that jump is a visible scale pop (the old sawtooth
// `pow(2, phase)`). A law that is continuous across the wrap must come back down
// (the old triangle `pow(2, 1 - |2 phase - 1|)`), so the picture dived in to 2x
// and then backed out again: the bug users saw as "Endless zoom is broken".
//
// The fix is the Shepard-tone trick drawn in space: two copies of the picture an
// octave apart, both always zooming IN. The nearer copy fades out as it grows past
// the frame while the farther copy fades in, so the pair at phase 1 is exactly the
// pair at phase 0 and the wrap is invisible. The visible scale stays within
// 0.5x..2x of the Zoom slider, centred on 1x, so the subject keeps its size on
// average and nothing ever needs float precision beyond one octave.
//
// The core computes the layers and uploads them as `uZoomLayers`
// (lib_zoom.glsl: x = first layer's magnification, y = second's, z = second's
// weight); the shaders hold no zoom maths of their own.

/** Crossfade window inside the octave: a single layer outside it, so most frames draw once. */
inline constexpr float kFadeStart = 0.2f;
inline constexpr float kFadeEnd = 0.8f;
/** Below this the second layer is skipped: it would not change a pixel at 8-bit output. */
inline constexpr float kSkipWeight = 0.002f;

struct Layers {
    float mag0 = 1.0f;  // the dominant layer, always drawn
    float mag1 = 1.0f;  // the other layer
    float weight1 = 0.0f;  // the other layer's share; 0 draws only the first
};

/**
 * One frame of the dive: phase in octaves, wrapped into [0, 1). A negative speed
 * dives back out just as seamlessly; non-finite input holds the phase rather than
 * poisoning it, since the sound engine and the morph both drive the speed.
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
/** The dive never stalls or backs out, however far below its mean the bass falls. */
inline constexpr float kMinRate = 0.25f;

/**
 * The music's hand on the dive rate, a multiplier on Dive speed. MilkDrop dives by a per-frame
 * zoom that presets nudge with the bass, `zoom = 1 + k (bass_att - 1)`; this is that idiom on the
 * sound engine's own bassAtt (core/sound/SoundFrame.h), the MilkDrop 3 relative level: the band
 * over its own ~4 s running mean, 1.0 typical for this song right now. So the dive surges on the
 * hits and eases between them, and over a song it averages back to the slider. It reads no
 * loudness: a loud song dives no faster than a quiet one. presence fades it out over silence,
 * and Audio drive scales it like the rest of the music's reach.
 */
inline float musicRate(const GeodeSoundFrame& s, float audioDrive) {
    auto val = [](float v, float fallback) { return std::isfinite(v) ? v : fallback; };
    const float presence = std::clamp(val(s.presence, 0.0f), 0.0f, 1.0f);
    const float bass = std::clamp(val(s.bassAtt, 1.0f), 0.0f, 4.0f);
    const float drive = std::clamp(val(audioDrive, 1.0f), 0.0f, 2.0f);
    return std::max(1.0f + kBassPush * drive * presence * (bass - 1.0f), kMinRate);
}

inline float fadeIn(float phase) {
    const float t = std::clamp((phase - kFadeStart) / (kFadeEnd - kFadeStart), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/**
 * The two layers at `phase` (octaves, [0, 1)). The outgoing layer grows 1x -> 2x and
 * fades out; the incoming one grows 0.5x -> 1x and fades in. Phase 0 (and Endless zoom
 * off) is a single layer at exactly 1x.
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

}  // namespace geode::viz::endless
