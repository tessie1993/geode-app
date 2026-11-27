#pragma once
#include <algorithm>
#include <array>
#include <cmath>

#include "viz/passes/CompositeGrade.hpp"

namespace geode::viz::composite {

// What composite_frag.glsl's geo() and postFx() run, and every value they read that depends only on the params,
// the time and the camera, derived once a frame here so no pixel works it out again (owner rule: compute as little
// as possible; only the outcome runs per pixel). The shader only applies these. Each stage's flag carries the very
// test the shader used to make per pixel (the gate for that texture, then the param's own threshold), so a stage
// runs exactly when it ran before; a stage whose params are neutral costs one uniform branch.
//
// Flags, not #defines: every one of these values can modulate (a wave, a knob, Auto Director), and a modulated
// value must never compile a program on a frame.

// Per texture (uFxA, uFxB, uFxL): the stages its gate and the params ask for.
inline constexpr int kCamera = 1 << 0;      // camFlat: the camera rig's flat projection, not at rest
inline constexpr int kCameraFold = 1 << 1;  // the mirror fold after the camera (on whenever the camera applies)
inline constexpr int kRotate = 1 << 2;      // rotation and sway
inline constexpr int kZoom = 1 << 3;
inline constexpr int kMirror = 1 << 4;
inline constexpr int kKaleido = 1 << 5;
inline constexpr int kTwist = 1 << 6;
inline constexpr int kTwistBend = 1 << 7;   // the twist's angle grows with distance^(1 + bend), bend not 0
inline constexpr int kWarp = 1 << 8;
inline constexpr int kRipple = 1 << 9;
inline constexpr int kBirths = 1 << 10;
inline constexpr int kDrift = 1 << 11;
inline constexpr int kTile = 1 << 12;
inline constexpr int kPixelate = 1 << 13;
inline constexpr int kBloom = 1 << 14;
inline constexpr int kPosterize = 1 << 15;
inline constexpr int kHue = 1 << 16;
inline constexpr int kSaturation = 1 << 17;
inline constexpr int kContrast = 1 << 18;
inline constexpr int kGamma = 1 << 19;
inline constexpr int kTemperature = 1 << 20;
inline constexpr int kSolarize = 1 << 21;
inline constexpr int kBrightness = 1 << 22;
inline constexpr int kInvert = 1 << 23;

// For the frame (uFxFrame): the lens stages every texture shares.
inline constexpr int kFlow = 1 << 0;
inline constexpr int kFisheye = 1 << 1;
inline constexpr int kChroma = 1 << 2;
inline constexpr int kScanlines = 1 << 3;
inline constexpr int kGrain = 1 << 4;
inline constexpr int kVignette = 1 << 5;

// What the composite reads of the frame's params, the camera and the clock.
struct Look {
    // SceneParams' post fields
    float chroma = 0.0f, vignette = 0.0f, scanlines = 0.0f, grain = 0.0f, fisheye = 0.0f;
    float warp = 0.0f, ripple = 0.0f, kaleidoFolds = 0.0f, kaleidoOn = 0.0f, pixelate = 0.0f, tile = 1.0f;
    float twist = 0.0f, twistBend = 0.0f;
    float bloom = 0.0f, posterize = 0.0f, driftX = 0.0f, driftY = 0.0f, sway = 0.0f, temperature = 0.0f;
    bool solarize = false, mirror = false, invert = false;
    float zoom = 1.0f, life = 0.0f, saturation = 1.0f, brightness = 1.0f, contrast = 1.0f, gamma = 1.0f, hue = 0.0f;
    // The renderer's: the integrated rotation angle, the flow smear's strength, the clock
    float rotation = 0.0f;
    float flowStrength = 0.0f;
    float time = 0.0f;
    // CameraRig's flat projection: pan x, pan y, zoom, roll; and the image plane's yaw, pitch
    std::array<float, 4> camFlat{0.0f, 0.0f, 1.0f, 0.0f};
    std::array<float, 2> camTilt{0.0f, 0.0f};
};

// One texture's flags and its rotation (cos, sin of rotation plus sway).
struct TextureTerms {
    int flags = 0;
    float rotCos = 1.0f;
    float rotSin = 0.0f;
};

struct FrameTerms {
    int frameFlags = 0;
    // camFlat: cos, sin of yaw; cos, sin of pitch; and the roll's cos, sin with 1 / max(zoom, 0.05)
    std::array<float, 4> camTiltCS{1.0f, 0.0f, 1.0f, 0.0f};
    std::array<float, 4> camRoll{1.0f, 0.0f, 1.0f, 0.0f};
    // kaleido segment, twist * 2, warp * 0.06, ripple * 0.12
    std::array<float, 4> geo{0.0f, 0.0f, 0.0f, 0.0f};
    // drift offset x, y (drift * time * 0.1), pixelate cells, 1 + twist bend
    std::array<float, 4> geo2{0.0f, 0.0f, 512.0f, 1.0f};
    // posterize levels, 1 / max(gamma, 0.05), temperature * 0.12, 1 / max(zoom, 0.05)
    std::array<float, 4> shade{24.0f, 1.0f, 0.0f, 0.0f};
    // the hue rotation's cos, sin
    std::array<float, 2> hueCS{1.0f, 0.0f};
    // flow * 0.015, fisheye * 0.6, chroma * 0.03, scanlines * 0.35
    std::array<float, 4> lens{0.0f, 0.0f, 0.0f, 0.0f};
    // grain * 0.25, vignette, time * 1.2, time * 3
    std::array<float, 4> lens2{0.0f, 0.0f, 0.0f, 0.0f};
};

// GLSL's mix(): x (1 - a) + y a.
inline float mix(float x, float y, float a) { return x * (1.0f - a) + y * a; }

// The sway's angle this frame: plasma_frag's view() term, uSway * 0.35 * sin(uTime * 0.7).
inline float swayAngle(float sway, float time) { return sway * 0.35f * std::sin(time * 0.7f); }

// Whether the camera's flat projection is the identity: camFlat() then returns its input exactly
// (cos 0 = 1, sin 0 = 0, a division by 1 and an add of 0 are exact).
inline bool cameraAtRest(const Look& l) {
    return l.camTilt[0] == 0.0f && l.camTilt[1] == 0.0f && l.camFlat[0] == 0.0f && l.camFlat[1] == 0.0f &&
           l.camFlat[2] == 1.0f && l.camFlat[3] == 0.0f;
}

// One texture's stages, for its gate (grade::gateFor) and whether the camera applies to it.
inline TextureTerms textureTerms(const Look& l, const grade::Gate& g, bool camera) {
    TextureTerms t;
    int f = 0;
    if (camera) f |= kCameraFold | (cameraAtRest(l) ? 0 : kCamera);
    float sa = g.grade ? l.rotation : 0.0f;
    if (g.geo && std::fabs(l.sway) > 0.001f) sa += swayAngle(l.sway, l.time);
    if (std::fabs(sa) > 0.0001f) {
        f |= kRotate;
        t.rotCos = std::cos(sa);
        t.rotSin = std::sin(sa);
    }
    if (g.grade && std::fabs(l.zoom - 1.0f) > 0.0001f) f |= kZoom;
    if (g.mirrorInvert && l.mirror) f |= kMirror;
    if (g.geo && l.kaleidoOn > 0.5f && l.kaleidoFolds >= 2.0f) f |= kKaleido;
    if (g.geo && std::fabs(l.twist) > 0.001f) f |= kTwist | (l.twistBend != 0.0f ? kTwistBend : 0);
    if (g.geo && l.warp > 0.001f) f |= kWarp;
    if (g.geo && l.ripple > 0.001f) f |= kRipple;
    if (g.geo && l.life > 0.0f) f |= kBirths;
    if (g.geo && std::fabs(l.driftX) + std::fabs(l.driftY) > 0.001f) f |= kDrift;
    if (g.geo && l.tile > 1.01f) f |= kTile;
    if (g.geo && l.pixelate > 0.001f) f |= kPixelate;
    if (g.geo && l.bloom > 0.001f) f |= kBloom;
    if (g.geo && l.posterize > 0.001f) f |= kPosterize;
    if (g.grade && std::fabs(l.hue) > 0.0001f) f |= kHue;
    if (g.grade && std::fabs(l.saturation - 1.0f) > 0.0001f) f |= kSaturation;
    if (g.grade && std::fabs(l.contrast - 1.0f) > 0.0001f) f |= kContrast;
    if (g.grade && std::fabs(l.gamma - 1.0f) > 0.0001f) f |= kGamma;
    // Adding 0 and multiplying by 1 are exact, so a neutral temperature or brightness is left out.
    if (g.geo && l.temperature != 0.0f) f |= kTemperature;
    if (g.geo && l.solarize) f |= kSolarize;
    if (g.grade && l.brightness != 1.0f) f |= kBrightness;
    if (g.mirrorInvert && l.invert) f |= kInvert;
    t.flags = f;
    return t;
}

// The frame's terms every texture shares.
inline FrameTerms frameTerms(const Look& l) {
    FrameTerms t;
    int f = 0;
    if (l.flowStrength > 0.001f) f |= kFlow;
    if (std::fabs(l.fisheye) > 0.001f) f |= kFisheye;
    if (l.chroma > 0.001f) f |= kChroma;
    if (l.scanlines > 0.001f) f |= kScanlines;
    if (l.grain > 0.001f) f |= kGrain;
    if (l.vignette > 0.001f) f |= kVignette;
    t.frameFlags = f;
    t.camTiltCS = {std::cos(l.camTilt[0]), std::sin(l.camTilt[0]), std::cos(l.camTilt[1]), std::sin(l.camTilt[1])};
    t.camRoll = {std::cos(l.camFlat[3]), std::sin(l.camFlat[3]), 1.0f / std::max(l.camFlat[2], 0.05f), 0.0f};
    t.geo = {l.kaleidoFolds >= 2.0f ? 6.2831853f / l.kaleidoFolds : 0.0f, l.twist * 2.0f, l.warp * 0.06f, l.ripple * 0.12f};
    t.geo2 = {l.driftX * l.time * 0.1f, l.driftY * l.time * 0.1f, mix(512.0f, 40.0f, l.pixelate), 1.0f + l.twistBend};
    t.shade = {mix(24.0f, 3.0f, l.posterize), 1.0f / std::max(l.gamma, grade::kMinGamma), l.temperature * 0.12f,
               1.0f / std::max(l.zoom, grade::kMinZoom)};
    const float hueAngle = l.hue * 6.2831f;
    t.hueCS = {std::cos(hueAngle), std::sin(hueAngle)};
    t.lens = {l.flowStrength * 0.015f, l.fisheye * 0.6f, l.chroma * 0.03f, l.scanlines * 0.35f};
    t.lens2 = {l.grain * 0.25f, l.vignette, l.time * 1.2f, l.time * 3.0f};
    return t;
}

}  // namespace geode::viz::composite
