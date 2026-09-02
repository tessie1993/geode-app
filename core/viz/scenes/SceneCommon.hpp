#pragma once
#include <GLES3/gl3.h>

#include <algorithm>
#include <cmath>

#include "api/geode_api.h"
#include "viz/Program.hpp"
#include "viz/TouchField.hpp"

namespace geode::viz {

// Port of FluidHue.kt.
namespace hue {
constexpr float kMinHueRange = 0.1f;
constexpr float kMaxHueRange = 1.5f;
constexpr float kMaxPaletteCycle = 2.0f;

inline float range(float hueRange) { return std::clamp(hueRange, kMinHueRange, kMaxHueRange); }
inline float wrap01(float h) {
    const float w = h - std::floor(h);
    return w >= 1.0f ? 0.0f : w;
}
inline float base(float paletteBase) { return wrap01(paletteBase); }
inline float paletteCycleSpeed(float speed) { return std::clamp(speed, 0.0f, kMaxPaletteCycle); }
inline float span(float hueRange, float paletteRange) { return range(hueRange) * std::clamp(paletteRange, 0.0f, 1.0f); }

inline void hsv(float h, float s, float v, float* out) {
    const float hh = wrap01(h);
    const float sextant = hh * 6.0f;
    const int i = static_cast<int>(sextant) % 6;
    const float fr = sextant - static_cast<float>(static_cast<int>(sextant));
    const float p = v * (1.0f - s);
    const float q = v * (1.0f - fr * s);
    const float t = v * (1.0f - (1.0f - fr) * s);
    switch (i) {
        case 0: out[0] = v; out[1] = t; out[2] = p; break;
        case 1: out[0] = q; out[1] = v; out[2] = p; break;
        case 2: out[0] = p; out[1] = v; out[2] = t; break;
        case 3: out[0] = p; out[1] = q; out[2] = v; break;
        case 4: out[0] = t; out[1] = p; out[2] = v; break;
        default: out[0] = v; out[1] = p; out[2] = q; break;
    }
}
inline void rgb(float h, float saturation, float* out) { hsv(h, std::clamp(saturation, 0.0f, 1.0f), 1.0f, out); }
}  // namespace hue

// Port of PcmPulse.kt: the peak of the newest PCM block, decaying per second.
class PcmPulse {
public:
    explicit PcmPulse(float decayPerSecond = 4.0f, float ceiling = 1.5f) : decay_(decayPerSecond), ceiling_(ceiling) {}

    void accept(const float* samples, int count) {
        float peak = 0.0f;
        for (int i = 0; i < count; ++i) {
            const float s = samples[i];
            if (std::isfinite(s)) peak = std::max(peak, std::fabs(s));
        }
        if (peak > level_) level_ = std::min(peak, ceiling_);
    }

    float tick(float dt) {
        const float out = level_;
        level_ = std::max(level_ - dt * decay_, 0.0f);
        return out;
    }

private:
    float decay_;
    float ceiling_;
    float level_ = 0.0f;
};

// Port of ParticleLook.kt.
namespace particle_look {
constexpr float kStretchSeconds = 0.0025f;
inline float glow(float bloom) { return 0.85f + std::clamp(bloom, 0.0f, 1.0f) * 1.2f; }
inline float dpiScale(int viewportHeightPx) { return std::clamp(static_cast<float>(std::max(viewportHeightPx, 1)) / 1080.0f, 0.75f, 2.5f); }
}  // namespace particle_look

// Port of SceneTouch.upload for a linked program: the count, and the points only when fingers are down.
inline void uploadSceneTouch(UniformCache& locs, const TouchField* field, bool enabled = true) {
    const int count = (enabled && field) ? field->count() : 0;
    glUniform1i(locs.loc("uTouchCount"), count);
    if (count <= 0 || !field) return;
    glUniform4fv(locs.loc("uTouchPoints"), locs.arrayCount("uTouchPoints", TouchField::kMaxPoints), field->points().data());
}

// The envelope slew every simulation scene shares: fast rise, slow fall, per second.
inline float slewEnvelope(float current, float target, float dt, float risePerSec, float fallPerSec) {
    const float limit = target > current ? risePerSec : fallPerSec;
    return current + std::clamp(target - current, -limit * dt, limit * dt);
}

// Port of PcmRow.fill: each destination cell keeps the largest-magnitude sample of its span.
inline void fillPcmRow(float* dst, int dstSize, const float* source, int count) {
    if (count <= 0) {
        std::fill(dst, dst + dstSize, 0.0f);
        return;
    }
    for (int i = 0; i < dstSize; ++i) {
        const int from = i * count / dstSize;
        const int to = std::min(std::max((i + 1) * count / dstSize, from + 1), count);
        float extreme = 0.0f;
        for (int j = from; j < to; ++j) {
            const float v = std::isfinite(source[j]) ? source[j] : 0.0f;
            if (std::fabs(v) > std::fabs(extreme)) extreme = v;
        }
        dst[i] = extreme;
    }
}

constexpr float kSceneTimeWrapSeconds = 628.31853f;
constexpr float kTwoPi = 6.2831853f;

inline float safeAudioDrive(float raw) { return std::isfinite(raw) ? std::clamp(raw, 0.0f, 4.0f) : 0.0f; }

// Blends fresh features toward this scene's audio-drive clamp, as every Kotlin scene did inline.
inline float clampedBand(float value) { return std::clamp(value, 0.0f, 1.5f); }

}  // namespace geode::viz
