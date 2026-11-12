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
// Blue-noise dither mask, sampled 1:1 with output pixels. See BlueNoise.kt for
// why the tile is blue rather than white noise and why it must not animate.
uniform sampler2D uNoise;
uniform float uDither;
/** width / height of the output, the `ratio` a spliced gl-transition reads. */
uniform float uRatio;
uniform float uProgress;
uniform int uStyle;
/**
 * Layers: how much the TOP layer (uTexA) contributes, 0..1. Photoshop's layer
 * opacity, not a crossfade position - at 0 you see the bottom layer alone, at
 * 1 the full blend. Only read when uStyle is 6.
 */
uniform float uLayerMix;
/** Layers: which blend function, matching BlendMode's ordinals. */
uniform int uBlendMode;
uniform float uTime;
uniform float uChroma;
uniform float uVignette;
uniform float uScanline;
uniform float uGrain;
uniform float uFisheye;
// PER-TEXTURE GATES. Which of the uPost* groups below the composite must
// apply is a property of the SCENE, not of the frame: shader scenes already
// warp/grade in view()/grade(), particle scenes in particle_vert/frag,
// milkdrop in pm_post_frag, and the fluid family (Fluid, Curl Flow, Water)
// applies nothing of its own. main() routes BOTH uTexA (incoming) and uTexB
// (outgoing) through postFx, and during a transition those two textures can
// come from DIFFERENT families - so one gate per texture, never one shared
// gate taken from the incoming scene. With a single gate a julia -> fluid
// fade graded the already-graded julia frame a second time (a white,
// over-zoomed flash for the whole fade) and the reverse dropped the fluid
// grade entirely.
//   x = geometry/stylize group   (warp, ripple, kaleido, pixelate, tile,
//                                 twist, bloom, posterize, drift, sway,
//                                 temp, solarize)
//   y = mirror + invert
//   z = colour grade + zoom/rotation  (the uPostZoom..uPostHue block below)
//   w = unused
// A component is 1.0 when the COMPOSITE owns that group for that texture and
// 0.0 when the scene already applied it. Uploaded by VisualizerRenderer and
// FxCompositor from CompositeGrade.gateFor(); the export path uploads the
// same value in both slots (it never transitions).
uniform vec4 uGateA;
uniform vec4 uGateB;
// The shared camera rig (core/viz/CameraRig.cpp) seen through a flat image:
// every scene that does not march the rig's 3D pose itself is viewed as a
// picture on a plane the camera pans, tilts, rolls and dollies toward.
// uCamGate is per texture (x = uTexA, y = uTexB), like the gates above:
// 0 for a scene that already drew the camera.
uniform vec4 uCamFlat;  // panX, panY, zoom, roll
uniform vec2 uCamTilt;  // yaw, pitch of the image plane, radians
uniform vec2 uCamGate;
// Universal geometric + color post effects, applied here so they work on
// EVERY scene type - including particle scenes, whose vertex pipeline can't
// honor screen-space shape params on its own.
uniform float uPostWarp;
uniform float uPostRipple;
uniform float uPostSymmetry;
uniform float uPostKaleido;
uniform float uPostPixelate;
uniform float uPostTile;
uniform float uPostTwist;
uniform float uPostMirror;
uniform float uPostBloom;
uniform float uPostPosterize;
uniform float uPostInvert;
// Motion + color params that particle/milkdrop pipelines can't honor natively
// (shader scenes do these in-shader, so the gate's x is 0 for them and every
// block below is skipped - the values themselves are uploaded raw).
uniform float uPostDriftX;
uniform float uPostDriftY;
uniform float uPostSway;
uniform float uPostTemp;
uniform float uPostSolarize;
// Universal grading + zoom/rotation for scenes that grade NOTHING themselves
// (the fluid family: Fluid, Curl Flow, Water). Shader scenes grade in
// view()/grade(), particle scenes in particle_vert/particle_frag and milkdrop
// in pm_post_frag, so for those the gate's z is 0 and the whole block below is
// skipped - a bitwise no-op, never a double grade.
// The gate is required (not just neutral values) because the neutral value of
// these uniforms is 1.0, not 0.0: a program that never uploads them would
// otherwise read the GL default 0 and render black at 20x zoom.
uniform float uPostZoom;
uniform float uPostRotation; // already-integrated angle (renderer sums rot*dt)
uniform float uPostSat;
uniform float uPostBright;   // brightness * intensity, as everywhere else
uniform float uPostContrast;
uniform float uPostGamma;
uniform float uPostHue;
// FlowField fluidWarp: the shared fluid velocity field bends the sampling
// coordinate of ANY scene's output (particles, shaders, milkdrop) before the
// scene-texture fetch. A 1x1 zero texture is bound when disabled so the
// sampler is always valid.
uniform highp sampler2D uFlow;   // half-float velocities exceed lowp range
uniform float uFlowStrength;
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

// Hue rotation about the grey axis, byte-for-byte the same formula as
// pm_post_frag's hueRotate (and mirrored by CompositeGrade.hueRotate).
vec3 hueRotate(vec3 c, float a) {
    const vec3 w = vec3(0.299, 0.587, 0.114);
    float angle = a * 6.2831;
    float cs = cos(angle);
    float sn = sin(angle);
    return vec3(dot(c, w)) + (c - vec3(dot(c, w))) * cs + cross(vec3(0.57735), c) * sn;
}

// Applies geometric transforms to a [0,1] uv, operating in centered space.
// [gate] is this texture's uGateA/uGateB (see their declaration).
// Intersects the view ray through centred point c with the tilted image
// plane, then rolls, zooms and pans it.
vec2 camFlat(vec2 c) {
    vec3 d = vec3(c, 1.0);
    float cy = cos(uCamTilt.x);
    float sy = sin(uCamTilt.x);
    float cp = cos(uCamTilt.y);
    float sp = sin(uCamTilt.y);
    d = vec3(cy * d.x + sy * d.z, d.y, -sy * d.x + cy * d.z);
    d = vec3(d.x, cp * d.y - sp * d.z, sp * d.y + cp * d.z);
    c = d.xy / max(d.z, 0.2);
    float r = uCamFlat.w;
    c = mat2(cos(r), -sin(r), sin(r), cos(r)) * c;
    return c / max(uCamFlat.z, 0.05) + uCamFlat.xy;
}

// Past the edge the picture is reflected, never the clamped edge texel smeared into stripes.
// Identity inside 0..1.
vec2 mirrorUv(vec2 p) { return 1.0 - abs(1.0 - mod(p, 2.0)); }

vec2 geo(vec2 uv, vec4 gate, float camOn) {
    bool geoOn = gate.x > 0.5;
    bool mirrorOn = gate.y > 0.5;
    bool gradeOn = gate.z > 0.5;
    vec2 c = uv - 0.5;
    if (camOn > 0.5) c = camFlat(c);
    // Rotation and sway share one angle, exactly like plasma_frag's view()
    // (a = uRotation + uSway * 0.35 * sin(uTime * 0.7)). uPostRotation is an
    // angle, not a rate: the renderer integrates rotation * dt so the slider
    // stays a SPEED here, matching ShaderScene/ParticleSceneBase/MilkdropScene.
    float sa = gradeOn ? uPostRotation : 0.0;
    if (geoOn && abs(uPostSway) > 0.001) sa += uPostSway * 0.35 * sin(uTime * 0.7);
    if (abs(sa) > 0.0001) {
        c = mat2(cos(sa), -sin(sa), sin(sa), cos(sa)) * c;
    }
    // Zoom about the centre, same form as the shader scenes and the milkdrop
    // post pass (uv /= max(z, 0.05)), so a given slider value magnifies by the
    // same amount on a fluid style as on julia/mandel.
    if (gradeOn && abs(uPostZoom - 1.0) > 0.0001) {
        c /= max(uPostZoom, 0.05);
    }
    if (mirrorOn && uPostMirror > 0.5) c.x = abs(c.x);
    if (geoOn && uPostKaleido > 0.5 && uPostSymmetry >= 2.0) {
        float ang = atan(c.y, c.x);
        float rad = length(c);
        float seg = 6.2831853 / uPostSymmetry;
        ang = abs(mod(ang, seg) - seg * 0.5);
        c = vec2(cos(ang), sin(ang)) * rad;
    }
    if (geoOn && abs(uPostTwist) > 0.001) {
        float tr = length(c) * uPostTwist * 2.0;
        c = mat2(cos(tr), -sin(tr), sin(tr), cos(tr)) * c;
    }
    if (geoOn && uPostWarp > 0.001) {
        c += uPostWarp * 0.06 * vec2(sin(c.y * 8.0 + uTime), cos(c.x * 8.0 + uTime * 1.2));
    }
    if (geoOn && uPostRipple > 0.001) {
        float r = length(c);
        c *= 1.0 + uPostRipple * 0.12 * sin(r * 16.0 - uTime * 3.0);
    }
    vec2 p = c + 0.5;
    if (geoOn && abs(uPostDriftX) + abs(uPostDriftY) > 0.001) {
        // Wrap so the image scrolls instead of smearing at the clamped edge.
        p = fract(p + vec2(uPostDriftX, uPostDriftY) * uTime * 0.1);
    }
    if (geoOn && uPostTile > 1.01) {
        // Mirrored repeats about the centre: fract() cracked the frame along every
        // tile boundary (one sat exactly on the centre line at Tile 2), and the
        // Clean macro moves uPostTile live.
        p = 1.0 - abs(1.0 - mod((p - 0.5) * uPostTile + 0.5, 2.0));
    }
    if (geoOn && uPostPixelate > 0.001) {
        float px = mix(512.0, 40.0, uPostPixelate);
        p = (floor(p * px) + 0.5) / px;
    }
    if (camOn > 0.5) p = mirrorUv(p);
    return p;
}

// Screen-space FX chain applied to the final composited frame, so it works
// identically for shader, particle and milkdrop scenes and during
// transitions. Order: geometry -> distortion (fisheye) -> sample
// (chromatic aberration) -> shading (posterize, scanlines, grain, vignette,
// bloom, invert).
vec3 postFx(sampler2D tex, vec2 uv, vec4 gate, float camOn) {
    bool geoOn = gate.x > 0.5;
    bool invertOn = gate.y > 0.5;
    bool gradeOn = gate.z > 0.5;
    vec2 p = geo(uv, gate, camOn);
    if (uFlowStrength > 0.001) {
        // Soft-limit the field to the +-6 range the 0.015 scale was tuned
        // for: emitter velocities reach ~36 (splat force x audio), and an
        // unbounded warp displaces the fetch by half the screen per frame -
        // perceived as full-screen colour flashing, not a fluid bend.
        vec2 flow = texture(uFlow, p).xy;
        flow *= 6.0 / (6.0 + length(flow));
        p -= flow * uFlowStrength * 0.015;
    }
    if (abs(uFisheye) > 0.001) {
        vec2 c = p - 0.5;
        float r = length(c) * 2.0;
        p = 0.5 + c * (1.0 + uFisheye * 0.6 * (r * r - 0.5));
    }
    vec3 col;
    if (uChroma > 0.001) {
        vec2 dir = (p - 0.5) * uChroma * 0.03;
        col.r = texture(tex, mirrorUv(p + dir)).r;
        col.g = texture(tex, mirrorUv(p)).g;
        col.b = texture(tex, mirrorUv(p - dir)).b;
    } else {
        col = texture(tex, mirrorUv(p)).rgb;
    }
    if (geoOn && uPostBloom > 0.001) col += uPostBloom * col * col;
    if (geoOn && uPostPosterize > 0.001) {
        float levels = mix(24.0, 3.0, uPostPosterize);
        col = floor(col * levels + 0.5) / levels;
    }
    // Colour grade: identical chain and order to plasma_frag's grade() and
    // pm_post_frag (hue -> saturation -> contrast -> gamma, brightness last,
    // just before invert), so a slider value lands the same on every style.
    // Mirrored on the CPU by CompositeGrade for the headless test.
    if (gradeOn) {
        if (abs(uPostHue) > 0.0001) col = hueRotate(col, uPostHue);
        if (abs(uPostSat - 1.0) > 0.0001) {
            float lum = dot(col, vec3(0.299, 0.587, 0.114));
            col = mix(vec3(lum), col, uPostSat);
        }
        if (abs(uPostContrast - 1.0) > 0.0001) col = (col - 0.5) * uPostContrast + 0.5;
        if (abs(uPostGamma - 1.0) > 0.0001) col = pow(max(col, 0.0), vec3(1.0 / max(uPostGamma, 0.05)));
    }
    if (uScanline > 0.001) {
        col *= 1.0 - uScanline * 0.35 * (0.5 + 0.5 * sin(p.y * 900.0));
    }
    if (uGrain > 0.001) {
        col += (compHash12(p * 1913.0 + uTime) - 0.5) * uGrain * 0.25;
    }
    if (uVignette > 0.001) {
        float d = length(uv - 0.5) * 1.4142;
        col *= 1.0 - uVignette * smoothstep(0.5, 1.05, d);
    }
    if (geoOn) {
        col.r += uPostTemp * 0.12;
        col.b -= uPostTemp * 0.12;
        if (uPostSolarize > 0.5) col = abs(1.0 - 2.0 * col);
    }
    if (gradeOn) col *= uPostBright;
    if (invertOn && uPostInvert > 0.5) col = max(vec3(1.0) - col, 0.0);
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
vec4 getFromColor(vec2 uv) { return vec4(postFx(uTexB, clamp(uv, 0.0, 1.0), uGateB, uCamGate.y), 1.0); }
vec4 getToColor(vec2 uv) { return vec4(postFx(uTexA, clamp(uv, 0.0, 1.0), uGateA, uCamGate.x), 1.0); }
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
    vec3 a = postFx(uTexA, vUv, uGateA, uCamGate.x);
    // uStyle 6 is Layers, and it is checked BEFORE the uProgress short-circuit
    // below: uProgress is a transition's clock and sits at 1.0 whenever no
    // transition is running, which is precisely when a layer IS running.
    // Reusing uProgress as the mix would have made the two mutually exclusive
    // for no reason and coupled a persistent setting to a timer.
    if (uStyle == 6) {
        vec3 b = postFx(uTexB, vUv, uGateB, uCamGate.y);
        return mix(b, layerBlend(b, a, uBlendMode), clamp(uLayerMix, 0.0, 1.0));
    }
    if (uStyle == 0 || uProgress >= 1.0) {
        return a;
    }
    if (uStyle == 1) {
        vec3 b = postFx(uTexB, vUv, uGateB, uCamGate.y);
        return mix(b, a, uProgress);
    } else if (uStyle == 3) {
        float sh = smoothstep(0.0, 1.0, uProgress);
        vec3 aSlide = postFx(uTexA, clamp(vUv + vec2(1.0 - sh, 0.0), 0.0, 1.0), uGateA, uCamGate.x);
        vec3 bSlide = postFx(uTexB, clamp(vUv - vec2(sh, 0.0), 0.0, 1.0), uGateB, uCamGate.y);
        return vUv.x < sh ? aSlide : bSlide;
    } else if (uStyle == 4) {
        float sh = smoothstep(0.0, 1.0, uProgress);
        vec2 zUv = (vUv - 0.5) / (1.0 + sh * 2.5) + 0.5;
        vec3 bZoom = postFx(uTexB, clamp(zUv, 0.0, 1.0), uGateB, uCamGate.y);
        vec2 aUv = (vUv - 0.5) * (1.0 + (1.0 - sh) * 0.35) + 0.5;
        vec3 aZoom = postFx(uTexA, mirrorUv(aUv), uGateA, uCamGate.x);
        return mix(bZoom, aZoom, sh);
    } else {
        float lumB = dot(texture(uTexB, vUv).rgb, vec3(0.299, 0.587, 0.114));
        vec2 melted = vUv + vec2(0.0, uProgress * (0.25 + lumB * 0.6));
        vec3 bMelt = postFx(uTexB, clamp(melted, 0.0, 1.0), uGateB, uCamGate.y);
        float reveal = smoothstep(0.0, 1.0, uProgress * 1.4 - lumB * 0.4);
        return mix(bMelt, a, clamp(reveal, 0.0, 1.0));
    }
}

void main() {
    vec3 col = blended();
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
