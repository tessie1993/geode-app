#version 300 es
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). uSoundTex is R32F; on GPUs honoring sampler precision
// (Mali) every read is clamped and quantized.
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

// Morph, Warp and Turbulence are spent on the blobs (see below), not on the rays.
#define VIEW_OWNS_SHAPE
//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3

// Metaballs: a flight down the inside of a lava lamp. Seven blobs of wax, one
// per spectrum band as the flat style had them, circle the flight path in a
// slow helix, swelling with their band and melting into each other where they
// pass. Every eight world units the seven come round again, each instance with
// its own hashed phase, so the flight meets an endless procession of them. The
// lamp's own wall, a wide tube of the same wax, bulges slowly around it all,
// and blobs that swing out far enough melt into it.
//
// The colour is the flat style's: the palette read at the field-weighted mean
// of the blobs' indices (each blob's r^2 / d^2, as the 2D field summed), so a
// seam between two bands takes the hue between them. The bodies glow from
// inside (a thickness probe behind the hit), and the white rim of the 2D field
// edge is the Fresnel rim here. Near misses along the ray leave a halo.
//
// The camera is the shared rig (camFly) down +z. Every z term tiles with a
// period dividing 64 (the blob procession 8, the wall bulge 8 and 16, the skin
// ripple 2), so the rebase of the eye by 64 is exact and the rig's 8192 travel
// wrap is invisible. A tube around the z axis is smoothly carved out as the
// flight channel and a sphere around the eye out of everything, so the eye is
// never inside the wax.
//
// ---- audio: one sound, one property -----------------------------------------
//
// Spectrum (sndSpectrum(i / 7))  blob i's radius, as in the flat style.
// Kick (SND_KICK_ENV)            the wax gets gooier: the blend radius grows,
//                                so neighbouring blobs reach out and fuse.
// Snare (SND_SNARE_ENV)          the rim flares wider and whiter.
// Drop (SND_HELD_SURGE)          the procession swings wider toward the wall and
//                                the palette swaps to its far side, on a hump.
// Pitch (SND_HELD_NOTE_CHANGE)   slides the hue, on a hump.
// SND_INTENSITY                  the inner glow (the flat style's 0.5 + volume).
// SND_LEVEL (shaped)             the halo between the blobs.
//
// ---- params and knobs -------------------------------------------------------
//
// Morph       gooeyness: the smooth-union radius.
// Warp        the skin's ripple depth.
// Turbulence  each blob's own jitter, beside its orbit.
// Intensity and the rest of the grade through lib_scene_grade.
// k0 PULSE    how hard the kick fuses.     k1 TEXTURE  a resting skin ripple.
// k2 FLOW     the orbit's in-and-out swing. k3 TONE   which blobs swell:
//             a high melody fattens the treble blobs and thins the bass ones,
//             a low melody the reverse (and a small hue offset).
// k4 BUILD    the blobs' size.             k5 SPACE    the orbit radius.
// Each knob is read as a continuous value about 0.5; none multiplies a clock,
// so a moving knob never makes the picture jump.
//
// ---- why the distance estimate is safe --------------------------------------
//
// Blob i's instances sit on a z lattice of period 8 offset by i * 8/7, each
// bobbing at most 0.6 in z and at most 1.3 in radius. For a point between
// lattice slots n0 and n0 + 1, every other instance is at least 8 - 0.6 - 1.3
// = 6.1 away, so the two evaluated instances, clamped to 5, are a true lower
// bound. smin (iq's quadratic form) never overestimates. The wall's bulge has
// a gradient under 0.6, so its term is divided by 1.6. The skin ripple's
// gradient is at most its depth times sqrt(9 + 9 + pi^2) < 5.3, so the field
// is divided by 1 + 5.3 * depth. The tube carve and the eye bubble are a smax
// and a max, which keep the bound.

#define MB_BALLS 7
#define MB_MAX_STEPS 96
/** Period of the procession along z; 8 slots per rebase. */
#define MB_P 8.0
#define MB_ZWRAP 64.0
#define MB_FAR 34.0
#define MB_BUBBLE 0.8
#define MB_TUBE 1.0
#define MB_WALL 6.0
#define MB_TAU 6.2831853
/** The clamp on a blob's evaluated distance (6.1 is the proven floor). */
#define MB_CLAMP 5.0

float gRad[MB_BALLS];
float gK;
float gSwing;
float gSpace;
float gJit;
float gSkin;
vec3 gEye;

/** Blob `i`'s instance in lattice slot `n` (a whole number): its centre. */
vec3 ballCentre(int i, float n) {
    float fi = float(i) / 7.0;
    // The slot wraps at 8 (64 / 8), so the procession repeats with the rebase.
    uint slot = uint(int(mod(n, MB_ZWRAP / MB_P) + 0.5) % 8);
    uint h = uhash(slot * 7u + uint(i) + 0x51au);
    float h1 = uhashUnit(h);
    float h2 = uhashUnit(uhash(h));
    float dir = (i % 2 == 0) ? 1.0 : -1.0;
    float a = fi * MB_TAU + h1 * MB_TAU + dir * uTime * (0.22 + 0.14 * fi);
    float r = (2.6 + 0.4 * h2 + gSwing * sin(uTime * (0.31 + 0.17 * fi) + h2 * 6.0)) * gSpace;
    vec2 xy = r * vec2(cos(a), sin(a))
            + gJit * vec2(sin(uTime * 1.3 + h2 * 20.0), cos(uTime * 1.1 + h1 * 20.0));
    // |bob| <= 0.35 + 0.25 = 0.6, the bound the estimate leans on.
    float bob = 0.35 * sin(uTime * 0.4 + h1 * 9.0) + 0.25 * (gJit * 2.0) * sin(uTime * 1.7 + h2 * 13.0);
    return vec3(xy, n * MB_P + float(i) * (MB_P / 7.0) + bob);
}

float map(vec3 w) {
    float d = MB_CLAMP;
    for (int i = 0; i < MB_BALLS; i++) {
        float n0 = floor((w.z - float(i) * (MB_P / 7.0)) / MB_P);
        float di = min(length(w - ballCentre(i, n0)), length(w - ballCentre(i, n0 + 1.0))) - gRad[i];
        di = min(di, MB_CLAMP);
        d = i == 0 ? di : smin(d, di, gK);
    }
    // The lamp wall. atan has no angle on the axis; the wall term is far from
    // there anyway, so the guard only keeps the value finite.
    float rr = length(w.xy);
    float ang = rr > 1e-4 ? atan(w.y, w.x) : 0.0;
    float bulge = 0.35 * sin(5.0 * ang + w.z * (MB_TAU / 16.0) + uTime * 0.2)
                * sin(w.z * (MB_TAU / 8.0) - uTime * 0.15);
    float dw = (MB_WALL + bulge - rr) / 1.6;
    d = smin(d, dw, gK * 1.4);
    // The skin ripple: period 2 along z.
    float rip = sin(3.0 * w.x + uTime * 0.7) * sin(3.0 * w.y - uTime * 0.5) * sin(w.z * (MB_TAU / 2.0) + uTime * 0.6);
    d = (d + gSkin * rip) / (1.0 + 5.3 * gSkin);
    // The flight channel, blended so it reads as wax pushed aside, not cut.
    d = smax(d, MB_TUBE - rr, 0.5);
    d = max(d, MB_BUBBLE - length(w - gEye));
    return d;
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

/** The flat style's colour rule: the field-weighted mean blob index at `p`; the wall reads as the far side. */
float blobHue(vec3 p) {
    float field = 0.0;
    float acc = 0.0;
    for (int i = 0; i < MB_BALLS; i++) {
        float n0 = floor((p.z - float(i) * (MB_P / 7.0)) / MB_P);
        vec3 a = p - ballCentre(i, n0);
        vec3 b = p - ballCentre(i, n0 + 1.0);
        float dd = max(min(dot(a, a), dot(b, b)), 1e-3);
        float c = gRad[i] * gRad[i] / dd;
        field += c;
        acc += float(i) / 7.0 * c;
    }
    float wallW = 2.0 * smoothstep(MB_WALL - 2.5, MB_WALL - 0.3, length(p.xy));
    return (acc + 0.5 * wallW) / max(field + wallW, 1e-3);
}

void main() {
    vec2 uv = view();

    float swell = clamp(SND_INTENSITY, 0.0, 1.5);
    float level = clamp(sndShaped(SND_LEVEL), 0.0, 1.5);
    float kick = clamp(SND_KICK_ENV, 0.0, 1.0);
    float snare = clamp(SND_SNARE_ENV, 0.0, 1.0);
    float drop = clamp(SND_HELD_SURGE, 0.0, 1.0);
    float pitch = clamp(SND_HELD_NOTE_CHANGE, 0.0, 1.0);
    float dropBump = 4.0 * drop * (1.0 - drop);
    float pitchBump = 4.0 * pitch * (1.0 - pitch);

    float kPulse = clamp(uKnob[0], 0.0, 1.0);
    float kTexture = clamp(uKnob[1], 0.0, 1.0);
    float kFlow = clamp(uKnob[2], 0.0, 1.0);
    float kTone = clamp(uKnob[3], 0.0, 1.0);
    float kBuild = clamp(uKnob[4], 0.0, 1.0);
    float kSpace = clamp(uKnob[5], 0.0, 1.0);

    for (int i = 0; i < MB_BALLS; i++) {
        float b = clamp(sndSpectrum(float(i) / 7.0), 0.0, 1.0);
        // Capped at 1.3: the lattice bound above assumes it.
        float tilt = max(1.0 + 1.3 * (kTone - 0.5) * (2.0 * float(i) / 6.0 - 1.0), 0.35);
        gRad[i] = min((0.3 + 0.85 * b) * (0.8 + 0.4 * kBuild) * tilt, 1.3);
    }
    gK = min(0.25 + 0.55 * clamp(uMorph, 0.0, 1.0) + 0.45 * kick * (0.5 + kPulse), 1.2);
    gSwing = 0.25 + 0.3 * kFlow;
    gSpace = (0.8 + 0.4 * kSpace) * (1.0 + 0.2 * dropBump);
    // Half of the 0.25 z bob share above: gJit * 2 <= 1.
    gJit = 0.5 * clamp(uTurbulence, 0.0, 1.0);
    gSkin = 0.08 * clamp(uWarp, 0.0, 1.5) + 0.05 * kTexture;

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term tiles with a period dividing 64, so this rebase is exact.
    ro.z = mod(ro.z, MB_ZWRAP);
    gEye = ro;

    float hueBase = 0.03 * sin(uTime * 0.013) + 0.25 * pitchBump + 0.5 * dropBump + 0.3 * (kTone - 0.5);

    float t = 0.03;
    float hitT = -1.0;
    float used = 0.0;
    float halo = 0.0;
    for (int i = 0; i < MB_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (t > MB_FAR) break;
        used = float(i);
        float d = map(ro + rd * t);
        float eps = 0.0008 * t + 0.0006;
        if (d < eps) {
            hitT = t;
            break;
        }
        float adv = max(d * 0.85, eps);
        // Glow from near misses, weighted by the step so it does not depend on Detail.
        halo += exp(-max(d, 0.0) * 4.0) * min(adv, 0.6) * exp(-t * 0.06);
        t += adv;
    }

    float ahead = pow(max(rd.z, 0.0), 6.0);
    vec3 fogCol = pal(hueBase + 0.62) * (0.03 + 0.14 * ahead) + vec3(0.006, 0.003, 0.004);
    vec3 col = fogCol;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, max(0.0015, 0.0012 * hitT));
        float hue = hueBase + blobHue(p);
        float ndv = clamp(dot(n, -rd), 0.0, 1.0);
        float fres = pow(1.0 - ndv, 3.0);
        float ao = clamp(1.0 - used / max(uSteps, 1.0) * 1.1, 0.3, 1.0);

        vec3 L = normalize(vec3(-0.3, 0.8, 0.5));
        float dif = max(dot(n, L), 0.0);
        float spec = pow(max(dot(reflect(rd, n), L), 0.0), 36.0);
        // Thickness behind the hit: deep wax glows, thin edges let the fog through.
        float thick = clamp(-map(p - n * 0.45) / 0.45, 0.0, 1.0);

        vec3 wax = pal(hue);
        vec3 body = wax * (0.06 + 0.32 * dif) * ao
                  + wax * thick * (0.5 + swell) * 0.45;
        // The flat style's white rim, flaring on the snare.
        float rimW = mix(0.25, 0.65, snare);
        float rim = smoothstep(1.0 - rimW, 1.0, 1.0 - ndv);
        body += mix(vec3(1.0), wax, 0.25) * rim * (0.25 + 0.6 * snare) * (0.5 + 0.5 * ao);
        body += mix(wax, vec3(1.0), 0.6) * spec * 0.45 * ao + wax * fres * 0.12;

        col = mix(body, fogCol, 1.0 - exp(-hitT * 0.055));
    }

    col += pal(hueBase + 0.35) * halo * (0.02 + 0.05 * level);
    col *= 0.85 + 0.2 * swell;
    col *= 0.72 + 0.28 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
