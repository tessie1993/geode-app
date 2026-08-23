#version 300 es
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). uTexA/uTexB hold HDR scene colour; on GPUs honoring
// sampler precision (Mali) every read is clamped and quantized. (uFlow
// and uRipple below keep their own explicit highp qualifiers.)
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
uniform float uBeat;
uniform float uChroma;
uniform float uVignette;
uniform float uScanline;
uniform float uGrain;
uniform float uGlitch;
uniform float uFisheye;
uniform float uStrobe;
// Strobe rate in flashes/second. Was the literal 9.0 below, which no user
// control could reach - so "less strobe" only ever meant a DIMMER 9 Hz
// flicker, never a slower one, and 9 Hz sits inside the band that provokes
// photosensitive seizures. VisualSafety.strobeHz owns this value: it stays
// 9.0 unless Safe visuals is on, so nothing changes by default.
uniform float uStrobeHz;
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
//                                 shake, flash, temp, solarize)
//   y = mirror + invert
//   z = colour grade + zoom/rotation  (the uPostZoom..uPostHue block below)
//   w = beat pulse
// A component is 1.0 when the COMPOSITE owns that group for that texture and
// 0.0 when the scene already applied it. Uploaded by VisualizerRenderer and
// FxCompositor from CompositeGrade.gateFor(); the export path uploads the
// same value in both slots (it never transitions).
uniform vec4 uGateA;
uniform vec4 uGateB;
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
uniform float uPostShake;
uniform float uPostFlash;
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
// Beat pulse for scenes that don't swell on the beat themselves (the fluid
// family AND milkdrop - a DIFFERENT set from the grading block above, which
// milkdrop is excluded from because pm_post_frag grades but never pulses).
// This is the pulse AMOUNT with the beat envelope already folded in on the
// CPU (CompositeGrade.pulseAmount), because the composite pass has no BPM
// phase clock of its own. Its gate component (w) is separate from the grade's
// (z) for that reason; the VALUE is still neutral at 0.0, which is also GL's
// default, so a program that never uploads it renders identically.
uniform float uPostPulse;
// FlowField fluidWarp: the shared fluid velocity field bends the sampling
// coordinate of ANY scene's output (particles, shaders, milkdrop) before the
// scene-texture fetch. A 1x1 zero texture is bound when disabled so the
// sampler is always valid.
uniform highp sampler2D uFlow;   // half-float velocities exceed lowp range
uniform float uFlowStrength;
// Ripple overlay (F2): the shared RippleSim heightfield refracts ANY scene's
// output and adds a specular glint on the wave crests, so the water style's
// surface physics can ride on top of particles, shaders and MilkDrop. Lives
// inside postFx() so transitions apply it to BOTH images. A 1x1 zero texture
// is bound when disabled (or when WATER is active - its own display already
// refracts) so the sampler is always valid.
uniform highp sampler2D uRipple; // half-float heights exceed lowp range
uniform vec2 uRippleTexel;
uniform float uRippleStrength;
uniform float uRippleSpecular;

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
vec2 geo(vec2 uv, vec4 gate) {
    bool geoOn = gate.x > 0.5;
    bool mirrorOn = gate.y > 0.5;
    bool gradeOn = gate.z > 0.5;
    bool pulseOn = gate.w > 0.5;
    vec2 c = uv - 0.5;
    // Rotation and sway share one angle, exactly like plasma_frag's view()
    // (a = uRotation + uSway * 0.35 * sin(uTime * 0.7)). uPostRotation is an
    // angle, not a rate: the renderer integrates rotation * dt so the slider
    // stays a SPEED here, matching ShaderScene/ParticleSceneBase/MilkdropScene.
    float sa = gradeOn ? uPostRotation : 0.0;
    if (geoOn && abs(uPostSway) > 0.001) sa += uPostSway * 0.35 * sin(uTime * 0.7);
    if (abs(sa) > 0.0001) {
        c = mat2(cos(sa), -sin(sa), sin(sa), cos(sa)) * c;
    }
    if (geoOn && uPostShake > 0.001) {
        c += uPostShake * uBeat * 0.03 * vec2(sin(uTime * 91.7), cos(uTime * 77.3));
    }
    // Zoom about the centre, same form as the shader scenes and the milkdrop
    // post pass (uv /= max(z, 0.05)), so a given slider value magnifies by the
    // same amount on a fluid style as on julia/mandel.
    if (gradeOn && abs(uPostZoom - 1.0) > 0.0001) {
        c /= max(uPostZoom, 0.05);
    }
    // Beat pulse: a swell about the centre, the same geometric form and 0.22
    // magnitude the shader scenes give it (plasma_frag: pulse = 1.0 + uPulse *
    // 0.22 * bump; z = uZoom * pulse; uv /= z), so one slider value swells the
    // image by the same amount on a fluid or milkdrop style as on julia. Kept
    // on its OWN gate component on purpose: milkdrop grades itself but does
    // not pulse, so it is graded elsewhere yet pulsed here.
    if (pulseOn && uPostPulse > 0.0001) {
        c /= 1.0 + uPostPulse * 0.22;
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
        p = fract(p * uPostTile);
    }
    if (geoOn && uPostPixelate > 0.001) {
        float px = mix(512.0, 40.0, uPostPixelate);
        p = (floor(p * px) + 0.5) / px;
    }
    return p;
}

// Screen-space FX chain applied to the final composited frame, so it works
// identically for shader, particle and milkdrop scenes and during
// transitions. Order: geometry -> distortion (fisheye, glitch) -> sample
// (chromatic aberration) -> shading (posterize, scanlines, grain, vignette,
// bloom, strobe, invert).
vec3 postFx(sampler2D tex, vec2 uv, vec4 gate) {
    bool geoOn = gate.x > 0.5;
    bool invertOn = gate.y > 0.5;
    bool gradeOn = gate.z > 0.5;
    vec2 p = geo(uv, gate);
    if (uFlowStrength > 0.001) {
        // Soft-limit the field to the +-6 range the 0.015 scale was tuned
        // for: emitter velocities reach ~36 (splat force x audio), and an
        // unbounded warp displaces the fetch by half the screen per frame -
        // perceived as full-screen colour flashing, not a fluid bend.
        vec2 flow = texture(uFlow, p).xy;
        flow *= 6.0 / (6.0 + length(flow));
        p -= flow * uFlowStrength * 0.015;
    }
    // Ripple overlay refraction: the height gradient bends the scene fetch,
    // soft-capped with the same idiom as the uFlow block above (and lockstep
    // with RippleMath.refractionOffset / water_display_frag: cap 0.08 UV).
    vec2 rippleGrad = vec2(0.0);
    if (uRippleStrength > 0.001) {
        float hL = texture(uRipple, p - vec2(uRippleTexel.x, 0.0)).x;
        float hR = texture(uRipple, p + vec2(uRippleTexel.x, 0.0)).x;
        float hT = texture(uRipple, p + vec2(0.0, uRippleTexel.y)).x;
        float hB = texture(uRipple, p - vec2(0.0, uRippleTexel.y)).x;
        rippleGrad = vec2(hR - hL, hT - hB);
        vec2 off = rippleGrad * uRippleStrength * 0.05;
        off *= 0.08 / (0.08 + length(off));
        p -= off;
    }
    if (abs(uFisheye) > 0.001) {
        vec2 c = p - 0.5;
        float r = length(c) * 2.0;
        p = 0.5 + c * (1.0 + uFisheye * 0.6 * (r * r - 0.5));
    }
    if (uGlitch > 0.001) {
        float band = floor(p.y * 24.0);
        float jump = compHash12(vec2(band, floor(uTime * 12.0)));
        if (jump > 1.0 - uGlitch * 0.35 * (0.4 + uBeat)) {
            p.x += (compHash12(vec2(band, uTime)) - 0.5) * uGlitch * 0.3;
        }
    }
    vec3 col;
    if (uChroma > 0.001) {
        vec2 dir = (p - 0.5) * uChroma * 0.03;
        col.r = texture(tex, clamp(p + dir, 0.0, 1.0)).r;
        col.g = texture(tex, clamp(p, 0.0, 1.0)).g;
        col.b = texture(tex, clamp(p - dir, 0.0, 1.0)).b;
    } else {
        col = texture(tex, clamp(p, 0.0, 1.0)).rgb;
    }
    // Ripple overlay glint: Blinn specular from the wave normal (same light
    // rig as water_display_frag) plus a small gradient-magnitude shimmer so
    // crests catch light on any scene underneath.
    if (uRippleStrength > 0.001 && uRippleSpecular > 0.001) {
        vec3 rn = normalize(vec3(-rippleGrad * 24.0, 1.0));
        vec3 lightDir = normalize(vec3(-0.4, 0.6, 0.8));
        vec3 halfVec = normalize(lightDir + vec3(0.0, 0.0, 1.0));
        // Fade in with gradient magnitude: a calm (flat) surface must add
        // nothing, or the overlay would wash every frame with constant spec.
        float wave = smoothstep(0.003, 0.03, length(rippleGrad));
        float spec = pow(max(dot(rn, halfVec), 0.0), 24.0) * wave;
        float shimmer = min(length(rippleGrad) * 0.6, 0.35);
        col += uRippleSpecular * (spec + shimmer) * vec3(1.0, 0.98, 0.92);
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
    if (uStrobe > 0.001) {
        // max() so an unset uniform (0.0) cannot freeze the strobe on a
        // permanently-dark half-cycle; the default upload is 9.0.
        col *= 1.0 - uStrobe * 0.85 * step(0.5, fract(uTime * max(uStrobeHz, 0.1))) * (1.0 - uBeat * 0.5);
    }
    if (geoOn) {
        col.r += uPostTemp * 0.12;
        col.b -= uPostTemp * 0.12;
        if (uPostSolarize > 0.5) col = abs(1.0 - 2.0 * col);
        col += uPostFlash * uBeat * 0.6;
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
vec4 getFromColor(vec2 uv) { return vec4(postFx(uTexB, clamp(uv, 0.0, 1.0), uGateB), 1.0); }
vec4 getToColor(vec2 uv) { return vec4(postFx(uTexA, clamp(uv, 0.0, 1.0), uGateA), 1.0); }
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
 * otherwise hand values well over 1 to the dither and the strobe below, where
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
    vec3 a = postFx(uTexA, vUv, uGateA);
    // uStyle 6 is Layers, and it is checked BEFORE the uProgress short-circuit
    // below: uProgress is a transition's clock and sits at 1.0 whenever no
    // transition is running, which is precisely when a layer IS running.
    // Reusing uProgress as the mix would have made the two mutually exclusive
    // for no reason and coupled a persistent setting to a timer.
    if (uStyle == 6) {
        vec3 b = postFx(uTexB, vUv, uGateB);
        return mix(b, layerBlend(b, a, uBlendMode), clamp(uLayerMix, 0.0, 1.0));
    }
    if (uStyle == 0 || uProgress >= 1.0) {
        return a;
    }
    if (uStyle == 1) {
        vec3 b = postFx(uTexB, vUv, uGateB);
        return mix(b, a, uProgress);
    } else if (uStyle == 3) {
        float sh = smoothstep(0.0, 1.0, uProgress);
        vec3 aSlide = postFx(uTexA, clamp(vUv + vec2(1.0 - sh, 0.0), 0.0, 1.0), uGateA);
        vec3 bSlide = postFx(uTexB, clamp(vUv - vec2(sh, 0.0), 0.0, 1.0), uGateB);
        return vUv.x < sh ? aSlide : bSlide;
    } else if (uStyle == 4) {
        float sh = smoothstep(0.0, 1.0, uProgress);
        vec2 zUv = (vUv - 0.5) / (1.0 + sh * 2.5) + 0.5;
        vec3 bZoom = postFx(uTexB, clamp(zUv, 0.0, 1.0), uGateB);
        vec2 aUv = (vUv - 0.5) * (1.0 + (1.0 - sh) * 0.35) + 0.5;
        vec3 aZoom = postFx(uTexA, clamp(aUv, 0.0, 1.0), uGateA);
        return mix(bZoom, aZoom, sh);
    } else {
        float lumB = dot(texture(uTexB, vUv).rgb, vec3(0.299, 0.587, 0.114));
        vec2 melted = vUv + vec2(0.0, uProgress * (0.25 + lumB * 0.6));
        vec3 bMelt = postFx(uTexB, clamp(melted, 0.0, 1.0), uGateB);
        float reveal = smoothstep(0.0, 1.0, uProgress * 1.4 - lumB * 0.4);
        return mix(bMelt, a, clamp(reveal, 0.0, 1.0));
    }
}

void main() {
    vec3 col = blended();
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
