#version 300 es
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). uTexA/uTexB hold HDR scene colour; on GPUs honoring
// sampler precision (Mali) every read is clamped and quantized. (uFlow
// below keeps its own explicit highp qualifier.)
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uTexA;
uniform sampler2D uTexB;
/**
 * Customize › Layers: the layer style's own target, drawn over whatever the
 * transition stage shows (so a style change runs under a layer as it does
 * without one). Only read when uHasLayer is 1.
 */
uniform sampler2D uTexL;
uniform int uHasLayer;
// Blue-noise dither mask, sampled 1:1 with output pixels. See BlueNoise.kt for
// why the tile is blue rather than white noise and why it must not animate.
uniform sampler2D uNoise;
uniform float uDither;
/** width / height of the output, the `ratio` a spliced gl-transition reads. */
uniform float uRatio;
uniform float uProgress;
uniform int uStyle;
/**
 * Layers: how much the TOP layer (uTexL) contributes, 0..1. Photoshop's layer
 * opacity, not a crossfade position - at 0 you see the active style alone (the
 * base), at 1 the full blend.
 */
uniform float uLayerMix;
/** Layers: which blend function, matching BlendMode's ordinals. */
uniform int uBlendMode;
uniform float uTime;
// The shared camera rig (core/viz/CameraRig.cpp) seen through a flat image:
// every scene that does not march the rig's 3D pose itself is viewed as a
// picture on a plane the camera pans, tilts, rolls and dollies toward.
uniform vec4 uCamFlat;  // panX, panY, zoom, roll
// Universal geometric + color post effects, applied here so they work on
// EVERY scene type - including particle scenes, whose vertex pipeline can't
// honor screen-space shape params on its own.
uniform float uPostTile;
uniform vec2 uPostTwistCentre;
uniform float uPostBloom;
// Universal grading for scenes that grade NOTHING themselves (the fluid family: Fluid, Curl Flow, Water).
// Shader and particle scenes grade in their own shaders, so for them the texture's flags leave these stages out.
uniform float uPostSat;
uniform float uPostBright;   // brightness * intensity, as everywhere else
uniform float uPostContrast;
// FlowField fluidWarp: the shared fluid velocity field bends the sampling
// coordinate of ANY scene's output (particles, shaders) before the
// scene-texture fetch. A 1x1 zero texture is bound when disabled so the
// sampler is always valid.
uniform highp sampler2D uFlow;   // half-float velocities exceed lowp range
// Underlay (W00): a full-frame RGBA8 background layer, blended into the finished composite
// colour in main() below, just before the dither - so it applies once, identically, regardless
// of transitions or Layers. uUnderlayBlend selects the function (0 = screen: shows through where
// the scene is black while leaving bright scene pixels alone; 1 = multiply; 2 = add) and
// uUnderlayAmount (0..1) is how much of that blend result replaces the plain scene colour.
// uUnderlayAmount is 0 by default (GL's uniform default for a value never uploaded), so a program
// that never sets it renders identically to one with no underlay pass at all. See
// CompositePass.hpp for the full pass description.
uniform sampler2D uUnderlay;
uniform int uUnderlayBlend;
uniform float uUnderlayAmount;

float compHash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// What each stage reads that depends only on the params, the camera and the clock, and which stages run:
// derived once a frame on the CPU (core/viz/passes/CompositeTerms.hpp, the flags' bits are its constants).
uniform int uFxA;      // per texture: uTexA, uTexB, uTexL
uniform int uFxB;
uniform int uFxL;
uniform vec4 uRotAB;   // cos, sin of A's rotation, then B's
uniform vec2 uRotL;
uniform int uFxFrame;  // the lens stages every texture shares
uniform vec4 uCamTiltCS;  // cos, sin of yaw, then of pitch
uniform vec4 uCamRoll;    // cos, sin of roll, 1 / max(zoom, 0.05)
uniform vec4 uPostGeo;    // kaleido segment, twist * 2, warp * 0.06, ripple * 0.12
uniform vec4 uPostGeo2;   // drift offset, pixelate cells, 1 + twist bend
uniform vec4 uPostShade;  // posterize levels, 1 / max(gamma, 0.05), temperature * 0.12, 1 / max(zoom, 0.05)
uniform vec2 uPostHueCS;
uniform vec4 uPostLens;   // flow * 0.015, fisheye * 0.6, chroma * 0.03, scanlines * 0.35
uniform vec4 uPostLens2;  // grain * 0.25, vignette, time * 1.2, time * 3

const int kCamera = 1;
const int kCameraFold = 2;
const int kRotate = 4;
const int kZoom = 8;
const int kMirror = 16;
const int kKaleido = 32;
const int kTwist = 64;
const int kTwistBend = 128;
const int kWarp = 256;
const int kRipple = 512;
const int kBirths = 1024;
const int kDrift = 2048;
const int kTile = 4096;
const int kPixelate = 8192;
const int kBloom = 16384;
const int kPosterize = 32768;
const int kHue = 65536;
const int kSaturation = 131072;
const int kContrast = 262144;
const int kGamma = 524288;
const int kTemperature = 1048576;
const int kSolarize = 2097152;
const int kBrightness = 4194304;
const int kInvert = 8388608;
const int kFlow = 1;
const int kFisheye = 2;
const int kChroma = 4;
const int kScanlines = 8;
const int kGrain = 16;
const int kVignette = 32;

bool on(int flags, int stage) { return (flags & stage) != 0; }

// Hue rotation about the grey axis, by the angle whose cos and sin are cs.
vec3 hueRotate(vec3 c, vec2 cs) {
    const vec3 w = vec3(0.299, 0.587, 0.114);
    float lum = dot(c, w);
    return vec3(lum) + (c - vec3(lum)) * cs.x + cross(vec3(0.57735), c) * cs.y;
}

// Intersects the view ray through centred point c with the tilted image
// plane, then rolls, zooms and pans it.
vec2 camFlat(vec2 c) {
    vec3 d = vec3(c, 1.0);
    float cy = uCamTiltCS.x;
    float sy = uCamTiltCS.y;
    float cp = uCamTiltCS.z;
    float sp = uCamTiltCS.w;
    d = vec3(cy * d.x + sy * d.z, d.y, -sy * d.x + cy * d.z);
    d = vec3(d.x, cp * d.y - sp * d.z, sp * d.y + cp * d.z);
    c = d.xy / max(d.z, 0.2);
    c = mat2(uCamRoll.x, -uCamRoll.y, uCamRoll.y, uCamRoll.x) * c;
    return c * uCamRoll.z + uCamFlat.xy;
}

// Past the edge the picture is reflected, never the clamped edge texel smeared into stripes.
// Identity inside 0..1.
vec2 mirrorUv(vec2 p) { return 1.0 - abs(1.0 - mod(p, 2.0)); }

// Customize › Life on a scene with no shader of its own to run births in (the simulations, the fluids):
// core/viz/Births's births, laid over the finished picture the way lib_grow's growBirths2 lays
// them over a 2D style's (the composite does not include lib_grow, so this is its own copy of that law:
// birth units 0.3 of the half-height, a ball of 2 units, Implode's shrink, Fold back's reflection, Cave
// in's hollow). the texture's kBirths flag is off unless Life is up, and geo() then skips this (a bitwise no-op), as it does
// on every scene whose gate.x is 0: a shader style and a 3D present draw their own births.
uniform vec4 uPostBirths[6];

// Centred c (-0.5..0.5) in, centred c out: inside each living birth's ball the picture is read from its
// middle at the birth's scale, eased in at the rim. Translation, scale and reflection only.
vec2 postBirths(vec2 c) {
    vec2 uv = vec2(c.x * uRatio, c.y) * 2.0;
    vec2 q = uv;
    const float ball = 0.6;
    for (int b = 0; b < 3; b++) {
        vec4 at = uPostBirths[2 * b];
        vec4 how = uPostBirths[2 * b + 1];
        if (!(at.w > 0.0)) continue;
        float s = max(how.x < 0.5 ? at.w * (1.0 - how.y) : at.w, 1.0e-3);
        vec2 r = (uv - at.xy * 0.3) / s;
        if (how.x > 0.5 && how.x < 1.5) {
            float h = ball * (1.0 - 2.0 * how.y);
            float t = dot(r, how.zw);
            if (t > h) r -= 2.0 * (t - h) * how.zw;
        }
        float m = 1.0 - smoothstep(0.8 * ball, ball, length(r));
        float cave = how.x > 1.5 ? 2.0 * ball * how.y : 0.0;
        if (cave > 0.0) m *= smoothstep(0.0, 0.2 * ball, length(r - how.zw * ball) - cave);
        q = mix(q, r, m);
    }
    return vec2(q.x / uRatio, q.y) * 0.5;
}

// Applies geometric transforms to a [0,1] uv, operating in centered space. fx is this texture's flags,
// rot the cos and sin of its rotation.
vec2 geo(vec2 uv, int fx, vec2 rot) {
    vec2 c = uv - 0.5;
    if (on(fx, kCamera)) c = camFlat(c);
    // Rotation and sway share one angle, exactly like plasma_frag's view()
    // (a = uRotation + uSway * 0.35 * sin(uTime * 0.7)), worked out on the CPU.
    if (on(fx, kRotate)) c = mat2(rot.x, -rot.y, rot.y, rot.x) * c;
    // Zoom about the centre, same form as the shader scenes (uv /= max(z, 0.05)).
    if (on(fx, kZoom)) c *= uPostShade.w;
    if (on(fx, kMirror)) c.x = abs(c.x);
    if (on(fx, kKaleido)) {
        float rad = length(c);
        float ang = rad > 1e-5 ? atan(c.y, c.x) : 0.0; // atan(0, 0) is undefined in GLSL ES
        float seg = uPostGeo.x;
        ang = abs(mod(ang, seg) - seg * 0.5);
        c = vec2(cos(ang), sin(ang)) * rad;
    }
    if (on(fx, kTwist)) {
        vec2 tc = c - uPostTwistCentre;
        float tl = length(tc);
        if (on(fx, kTwistBend)) tl = pow(max(tl, 1e-5), uPostGeo2.w);
        float tr = tl * uPostGeo.y;
        c = uPostTwistCentre + mat2(cos(tr), -sin(tr), sin(tr), cos(tr)) * tc;
    }
    if (on(fx, kWarp)) {
        c += uPostGeo.z * vec2(sin(c.y * 8.0 + uTime), cos(c.x * 8.0 + uPostLens2.z));
    }
    if (on(fx, kRipple)) {
        float r = length(c);
        c *= 1.0 + uPostGeo.w * sin(r * 16.0 - uPostLens2.w);
    }
    if (on(fx, kBirths)) c = postBirths(c);
    vec2 p = c + 0.5;
    // Wrap so the image scrolls instead of smearing at the clamped edge.
    if (on(fx, kDrift)) p = fract(p + uPostGeo2.xy);
    if (on(fx, kTile)) {
        // Mirrored repeats about the centre: fract() cracked the frame along every
        // tile boundary (one sat exactly on the centre line at Tile 2), and the
        // Clean macro moves uPostTile live.
        p = 1.0 - abs(1.0 - mod((p - 0.5) * uPostTile + 0.5, 2.0));
    }
    if (on(fx, kPixelate)) {
        float px = uPostGeo2.z;
        p = (floor(p * px) + 0.5) / px;
    }
    if (on(fx, kCameraFold)) p = mirrorUv(p);
    return p;
}

// Screen-space FX chain applied to the final composited frame, so it works
// identically for shader and particle scenes and during
// transitions. Order: geometry -> distortion (fisheye) -> sample
// (chromatic aberration) -> shading (posterize, scanlines, grain, vignette,
// bloom, invert).
vec3 postFx(sampler2D tex, vec2 uv, int fx, vec2 rot) {
    vec2 p = geo(uv, fx, rot);
    if (on(uFxFrame, kFlow)) {
        // Soft-limit the field to the +-6 range the 0.015 scale was tuned
        // for: emitter velocities reach ~36 (splat force x audio), and an
        // unbounded warp displaces the fetch by half the screen per frame -
        // perceived as full-screen colour flashing, not a fluid bend.
        vec2 flow = texture(uFlow, p).xy;
        flow *= 6.0 / (6.0 + length(flow));
        p -= flow * uPostLens.x;
    }
    if (on(uFxFrame, kFisheye)) {
        vec2 c = p - 0.5;
        float r = length(c) * 2.0;
        p = 0.5 + c * (1.0 + uPostLens.y * (r * r - 0.5));
    }
    vec3 col;
    if (on(uFxFrame, kChroma)) {
        vec2 dir = (p - 0.5) * uPostLens.z;
        col.r = texture(tex, mirrorUv(p + dir)).r;
        col.g = texture(tex, mirrorUv(p)).g;
        col.b = texture(tex, mirrorUv(p - dir)).b;
    } else {
        col = texture(tex, mirrorUv(p)).rgb;
    }
    if (on(fx, kBloom)) col += uPostBloom * col * col;
    if (on(fx, kPosterize)) {
        float levels = uPostShade.x;
        col = floor(col * levels + 0.5) / levels;
    }
    // Colour grade: identical chain and order to plasma_frag's grade()
    // (hue -> saturation -> contrast -> gamma, brightness last,
    // just before invert), so a slider value lands the same on every style.
    if (on(fx, kHue)) col = hueRotate(col, uPostHueCS);
    if (on(fx, kSaturation)) {
        float lum = dot(col, vec3(0.299, 0.587, 0.114));
        col = mix(vec3(lum), col, uPostSat);
    }
    if (on(fx, kContrast)) col = (col - 0.5) * uPostContrast + 0.5;
    if (on(fx, kGamma)) col = pow(max(col, 0.0), vec3(uPostShade.y));
    if (on(uFxFrame, kScanlines)) {
        col *= 1.0 - uPostLens.w * (0.5 + 0.5 * sin(p.y * 900.0));
    }
    if (on(uFxFrame, kGrain)) {
        col += (compHash12(p * 1913.0 + uTime) - 0.5) * uPostLens2.x;
    }
    if (on(uFxFrame, kVignette)) {
        float d = length(uv - 0.5) * 1.4142;
        col *= 1.0 - uPostLens2.y * smoothstep(0.5, 1.05, d);
    }
    if (on(fx, kTemperature)) {
        col.r += uPostShade.z;
        col.b -= uPostShade.z;
    }
    if (on(fx, kSolarize)) col = abs(1.0 - 2.0 * col);
    if (on(fx, kBrightness)) col *= uPostBright;
    if (on(fx, kInvert)) col = max(vec3(1.0) - col, 0.0);
    return col;
}

// Every sample below goes through postFx, INCLUDING the outgoing texture:
// sampling uTexB raw made the whole FX chain (vignette, chroma, scanlines,
// kaleido, bloom...) pop off on the old scene for the entire transition and
// snap back at the end - perceived as a flash on every preset/scene switch.
// SLIDE/ZOOM previously resampled BOTH textures raw, dropping FX everywhere.
// Each texture carries its OWN gate: uTexA is the incoming scene, uTexB the
// outgoing one, and a cross-family switch (julia -> fluid) must not grade the
// outgoing frame under the incoming scene's rule.
// ---------------------------------------------------------------------------
// gl-transitions splice point. VisualizerRenderer/FxCompositor build a variant
// of this shader per selected transition: `#define MV_TRANSITION 1` after the
// #version line, and the transition's own source substituted for the marker
// below. The base program (built-in styles only) compiles with neither, so
// nothing here costs anything until a library transition is chosen.
//
// The gl-transitions contract is `vec4 transition(vec2 uv)` reading `progress`,
// `ratio`, `getFromColor()` and `getToColor()`. Those two samplers are wired to
// postFx rather than to raw texture fetches ON PURPOSE: every transition then
// blends frames that already carry the FX chain and the per-texture grade
// gates, exactly as the built-in styles do. Sampling raw would make the whole
// FX chain pop off for the length of every switch.
//
// NOTE the direction. In gl-transitions, `from` is the OUTGOING image and `to`
// the incoming one; here uTexA is the ACTIVE (incoming) scene and uTexB the
// outgoing one, so from -> B and to -> A. Getting this backwards plays every
// transition in reverse, which reads as "the new scene wipes away to reveal
// the old one".
#ifdef MV_TRANSITION
#define progress uProgress
#define ratio uRatio
vec4 getFromColor(vec2 uv) { return vec4(postFx(uTexB, clamp(uv, 0.0, 1.0), uFxB, uRotAB.zw), 1.0); }
vec4 getToColor(vec2 uv) { return vec4(postFx(uTexA, clamp(uv, 0.0, 1.0), uFxA, uRotAB.xy), 1.0); }
// __GL_TRANSITION_SOURCE__
#endif

/**
 * The separable blend functions for Layers, on two already-graded layers.
 *
 * `base` is the bottom layer and `top` the one being laid over it. Both are
 * opaque, so these are pure per-channel functions of two colours and not
 * Porter-Duff compositing.
 *
 * Every one of them clamps. The scenes upstream are emissive and routinely
 * exceed 1 before the composite grades them down, so ADD in particular would
 * otherwise hand values well over 1 to the dither below, where
 * they stop behaving like colours. The switch order is BlendMode's ordinals.
 */
vec3 layerBlend(vec3 base, vec3 top, int mode) {
    vec3 b = max(base, vec3(0.0));
    vec3 t = max(top, vec3(0.0));
    vec3 r;
    if (mode == 1) {
        r = vec3(1.0) - (vec3(1.0) - b) * (vec3(1.0) - t);
    } else if (mode == 2) {
        r = b + t;
    } else if (mode == 3) {
        r = b * t;
    } else if (mode == 4) {
        r = abs(b - t);
    } else if (mode == 5) {
        // Overlay: multiply where the BASE is dark, screen where it is light,
        // which is what preserves the base's own contrast rather than the top's.
        vec3 lo = 2.0 * b * t;
        vec3 hi = vec3(1.0) - 2.0 * (vec3(1.0) - b) * (vec3(1.0) - t);
        r = mix(lo, hi, step(vec3(0.5), b));
    } else if (mode == 6) {
        r = max(b, t);
    } else if (mode == 7) {
        r = min(b, t);
    } else {
        r = t;
    }
    return clamp(r, 0.0, 1.0);
}

/**
 * The transition stage: the incoming scene, or a blend of it with the outgoing
 * one. Split out of main() so every path returns a colour rather than writing
 * fragColor itself - the dither below has to be the last thing that touches the
 * output, and an early `return` in main() used to skip it.
 */
vec3 blended() {
#ifdef MV_TRANSITION
    // uStyle 5 is "a spliced library transition is running". The variant stays
    // bound outside transitions too - swapping programs per frame would cost
    // more than the branch - so it must still render a plain CUT at uStyle 0.
    if (uStyle == 5 && uProgress < 1.0) {
        return transition(vUv).rgb;
    }
#endif
    if (uStyle == 0 || uProgress >= 1.0) {
        return postFx(uTexA, vUv, uFxA, uRotAB.xy);
    }
    if (uStyle == 1) {
        vec3 a = postFx(uTexA, vUv, uFxA, uRotAB.xy);
        vec3 b = postFx(uTexB, vUv, uFxB, uRotAB.zw);
        return mix(b, a, uProgress);
    } else if (uStyle == 3) {
        float sh = smoothstep(0.0, 1.0, uProgress);
        vec3 aSlide = postFx(uTexA, clamp(vUv + vec2(1.0 - sh, 0.0), 0.0, 1.0), uFxA, uRotAB.xy);
        vec3 bSlide = postFx(uTexB, clamp(vUv - vec2(sh, 0.0), 0.0, 1.0), uFxB, uRotAB.zw);
        return vUv.x < sh ? aSlide : bSlide;
    } else if (uStyle == 4) {
        float sh = smoothstep(0.0, 1.0, uProgress);
        vec2 zUv = (vUv - 0.5) / (1.0 + sh * 2.5) + 0.5;
        vec3 bZoom = postFx(uTexB, clamp(zUv, 0.0, 1.0), uFxB, uRotAB.zw);
        vec2 aUv = (vUv - 0.5) * (1.0 + (1.0 - sh) * 0.35) + 0.5;
        vec3 aZoom = postFx(uTexA, mirrorUv(aUv), uFxA, uRotAB.xy);
        return mix(bZoom, aZoom, sh);
    } else {
        vec3 a = postFx(uTexA, vUv, uFxA, uRotAB.xy);
        float lumB = dot(texture(uTexB, vUv).rgb, vec3(0.299, 0.587, 0.114));
        vec2 melted = vUv + vec2(0.0, uProgress * (0.25 + lumB * 0.6));
        vec3 bMelt = postFx(uTexB, clamp(melted, 0.0, 1.0), uFxB, uRotAB.zw);
        float reveal = smoothstep(0.0, 1.0, uProgress * 1.4 - lumB * 0.4);
        return mix(bMelt, a, clamp(reveal, 0.0, 1.0));
    }
}

void main() {
    vec3 col = blended();
    // Layers: the layer style over the active one (or over the transition while
    // the style changes), the active style the base. Its own uniform, not a
    // uStyle value: a layer is a setting, a transition a timer, and the two run
    // at once. uLayerMix 0 is the active style alone, 1 the full blend.
    if (uHasLayer == 1) {
        vec3 l = postFx(uTexL, vUv, uFxL, uRotL);
        col = mix(col, layerBlend(col, l, uBlendMode), clamp(uLayerMix, 0.0, 1.0));
    }
    // Underlay (W00): folded in here, after every transition/Layers/postFx blend above and
    // before the dither, so it is the one place both the live composite and the offscreen export
    // apply it - see the uUnderlay* declarations above and CompositePass.hpp.
    if (uUnderlayAmount > 0.0001) {
        vec3 u = texture(uUnderlay, vUv).rgb;
        vec3 underlayBlended;
        if (uUnderlayBlend == 1) {
            underlayBlended = col * u;
        } else if (uUnderlayBlend == 2) {
            underlayBlended = col + u;
        } else {
            underlayBlended = vec3(1.0) - (vec3(1.0) - col) * (vec3(1.0) - u);
        }
        col = mix(col, clamp(underlayBlended, 0.0, 1.0), clamp(uUnderlayAmount, 0.0, 1.0));
    }
    // Dither LAST, after every grade and effect, sampled one texel per output
    // pixel: this is fighting the 8-bit quantization of the surface being
    // written, so it has to be measured in that surface's own steps. Every
    // smooth ramp in the app - plasma, aurora, solar, glow falloffs, the fluid
    // pressure field - banded visibly on an OLED panel in a dark room without
    // it. uDither is 0 when the mask could not be loaded, making this an exact
    // no-op rather than a failure.
    col += (texture(uNoise, gl_FragCoord.xy / 64.0).r - 0.5) * uDither;
    fragColor = vec4(col, 1.0);
}
