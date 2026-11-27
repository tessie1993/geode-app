#pragma once
#include <array>
#include <cstdint>

#include "viz/params/Params.hpp"
#include "viz/passes/CompositeGrade.hpp"
#include "viz/passes/CompositeTerms.hpp"
#include "viz/passes/TwistShape.hpp"

namespace geode::viz::composite {

// What the renderer knows about one frame's composite: the params it resolved, the clock and the integrated
// angle, the camera rig's pose, and for each texture (A, B, the layer) its scene's gate and whether the rig's
// flat projection is the composite's to apply.
struct Source {
    SceneParams fx;
    float rotation = 0.0f;
    float flowStrength = 0.0f;
    float time = 0.0f;
    std::array<float, 4> camFlat{0.0f, 0.0f, 1.0f, 0.0f};
    std::array<float, 2> camTilt{0.0f, 0.0f};
    std::uint32_t twistSeed = 0u;
    grade::Gate gateA{false, false, false};
    grade::Gate gateB{false, false, false};
    grade::Gate gateL{false, false, false};
    bool cameraA = false;
    bool cameraB = false;
    bool cameraL = false;
};

// The frame's published terms: derived once by the renderer, uploaded as they are by CompositePass.
struct Descriptor {
    TextureTerms a;
    TextureTerms b;
    TextureTerms l;
    FrameTerms frame;
    std::array<float, 4> camFlat{0.0f, 0.0f, 1.0f, 0.0f};
    float tile = 1.0f;
    float bloom = 0.0f;
    float saturation = 1.0f;
    float contrast = 1.0f;
    float brightness = 1.0f;
    // The composite's picture spans half a unit each side of the middle where the shader scenes' spans one.
    float twistCentreX = 0.0f;
    float twistCentreY = 0.0f;
};

inline Descriptor describe(const Source& s) {
    const SceneParams& fx = s.fx;
    const SceneParams::KaleidoUniforms kaleido = SceneParams::kaleidoUniforms(fx.kaleidoFolds);
    const TwistShape twist = twistShape(s.twistSeed, s.time);

    Look look;
    look.chroma = fx.chromaAb;
    look.vignette = fx.vignette;
    look.scanlines = fx.scanlines;
    look.grain = fx.grain;
    look.fisheye = fx.fisheye;
    look.warp = fx.warp;
    look.ripple = fx.ripple;
    look.kaleidoFolds = kaleido.folds;
    look.kaleidoOn = kaleido.on;
    look.pixelate = fx.pixelate;
    look.tile = fx.tile;
    look.twist = fx.twist;
    look.twistBend = twist.bend;
    look.bloom = fx.bloom;
    look.posterize = fx.posterize;
    look.driftX = fx.driftX;
    look.driftY = fx.driftY;
    look.sway = fx.sway;
    look.temperature = fx.temperature;
    look.solarize = fx.solarize;
    look.mirror = fx.mirror;
    look.invert = fx.invert;
    look.zoom = fx.zoom;
    look.life = fx.life;
    look.saturation = fx.saturation;
    look.brightness = grade::brightness(fx.brightness, fx.intensity);
    look.contrast = fx.contrast;
    look.gamma = fx.gamma;
    look.hue = fx.colorShift;
    look.rotation = s.rotation;
    look.flowStrength = s.flowStrength;
    look.time = s.time;
    look.camFlat = s.camFlat;
    look.camTilt = s.camTilt;

    Descriptor d;
    d.a = textureTerms(look, s.gateA, s.cameraA);
    d.b = textureTerms(look, s.gateB, s.cameraB);
    d.l = textureTerms(look, s.gateL, s.cameraL);
    d.frame = frameTerms(look);
    d.camFlat = s.camFlat;
    d.tile = fx.tile;
    d.bloom = fx.bloom;
    d.saturation = fx.saturation;
    d.contrast = fx.contrast;
    d.brightness = look.brightness;
    d.twistCentreX = twist.x * 0.5f;
    d.twistCentreY = twist.y * 0.5f;
    return d;
}

}  // namespace geode::viz::composite
