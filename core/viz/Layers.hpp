#pragma once
#include <array>

#include "viz/Params.hpp"

namespace geode::viz {

// The layers every style gets in the composite pass, whatever it draws: the picture falls through
// itself (an endless zoom made of the same frame at octave scales, inward or back out), grows
// outward like a fractal (mirrored copies in generations, deeper toward the edge) and spawns copies
// of itself that grow and dissolve. Reaction routes drive them through SceneParams (fall, fractal,
// spawn), so the style DNA, the Customize grid and Events reach them like any other target, and the
// live view, the wallpaper, the dream and an export all draw the same layers.
//
// This steps the state the shader needs once per frame from the final params. Pure: no GL, no
// allocation, no locks; the same params/dt sequence gives the same uniforms, spawns included.
class Layers {
public:
    static constexpr int kSpawns = 6;
    static constexpr float kFallOctavesPerSecond = 0.6f;  // at fall 1
    static constexpr float kSpawnLifeSeconds = 1.8f;
    static constexpr float kSpawnGapSeconds = 0.12f;     // two edges closer than this spawn once

    struct Uniforms {
        float fallPhase = 0.0f;  // uLayerFall.x: 0..1, the octave stack's position
        float fallMix = 0.0f;    // uLayerFall.y: 0 off .. 1 fully fallen through
        float fractal = 0.0f;    // uLayerFractal.x: generations grown, 0..4
        float fractalTurn = 0.0f;  // uLayerFractal.y: radians each generation turns
        // uSpawn[i]: x, y (0..1 of the picture), age 0..1 (1 and past: gone), seed 0..1.
        std::array<std::array<float, 4>, kSpawns> spawn{{{0.5f, 0.5f, 1.0f, 0.0f}, {0.5f, 0.5f, 1.0f, 0.0f}, {0.5f, 0.5f, 1.0f, 0.0f}, {0.5f, 0.5f, 1.0f, 0.0f}, {0.5f, 0.5f, 1.0f, 0.0f}, {0.5f, 0.5f, 1.0f, 0.0f}}};
        bool any() const;
    };

    Layers() { reset(); }
    void reset();
    // reducedMotion halves the fall speed: an endless zoom is the most vestibular move there is.
    void step(const SceneParams& p, float dt, bool reducedMotion);
    const Uniforms& uniforms() const { return u_; }

private:
    Uniforms u_;
    float fallLevel_ = 0.0f;  // smoothed |fall|, for the mix
    float growth_ = 0.0f;
    float turnPhase_ = 0.0f;
    bool spawnHigh_ = false;
    float sinceSpawn_ = kSpawnGapSeconds;
    int nextSlot_ = 0;
    unsigned int seed_ = 0x6C8E9CF5u;
    float nextRandom();
};

}  // namespace geode::viz
