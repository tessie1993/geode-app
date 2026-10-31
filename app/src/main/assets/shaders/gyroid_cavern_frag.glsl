#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3

// Gyroid Cavern: a flight through a triply periodic minimal surface. The
// walls are a thin sheet around the surface's zero set (|g| < thickness), so
// the cavern is two interleaved labyrinths of channels with soap-film walls
// between them. The walls are iridescent thin film (the palette read at
// staggered offsets per channel, steered by the view angle), and glowing veins
// crawl inside them: a second surface at three times the frequency, drawn
// where it crosses the wall. Motes drift in the open air.
//
// The surface has period 8 world units on every axis, which divides the 16 the
// eye is rebased by, so the rig's 8192 travel wrap is invisible. A tube around
// the z axis is smoothly carved out of the sheet as the flight channel, and a
// sphere around the eye out of everything, so the eye is never in a wall.
//
// ---- audio: one sound, one property -----------------------------------------
//
// Kick (SND_KICK_ENV)       the walls swell thicker, narrowing the channels.
// Snare (SND_SNARE_ENV)      the veins flare wider and brighter in a contrasting hue.
// Hat (SND_HAT_ENV)        the motes sparkle: they twinkle up from a dim drift.
// Onset (SND_ONSET_ENV)      the surface's frequency morphs: a new hashed stretch per
//                      axis, gliding from the old one over the onset envelope.
// Drop (SND_HELD_SURGE)     the cavern opens: the frequency falls (wider tunnels),
//                      the walls thin, the flight tube widens, and the palette
//                      swaps to its far side; all on a hump, so it glides.
// Pitch (SND_HELD_NOTE_CHANGE)    shifts the iridescence hue, as a hump.
// High bands (vec4(sndLevel(SND_MID_HIGH_ATT), sndSpectrum(0.80), sndLevel(SND_TREBLE_ATT), sndLevel(SND_FLUX))) vein speed: a slow and a fast vein flow crossfade, so
//                      the veins race on bright passages and creep otherwise.
// Section count        the surface itself: gyroid, Schwarz-P, diamond or a
//                      gyroid-diamond blend, crossfaded over the section envelope.
// SND_INTENSITY               overall level, as everywhere.
//
// ---- why the morphs are wrap-safe ------------------------------------------
//
// The frequency morph is not a change of frequency: it is a per-axis scale
// about the eye, q = eye + (p - eye) * k. Rebasing the eye and the ray by 16
// shifts q by exactly 16, a whole number of periods, so a morph that never
// settles back to 1 still never shows the wrap. The estimate is divided by the
// largest k, the scale's Lipschitz bound.

#define GC_MAX_STEPS 104
#define GC_ZWRAP 16.0
/** Base angular frequency: period 8 world units. */
#define GC_F 0.7853982
/** Gradient bound of the blended field per unit of GC_F * k. */
#define GC_LIP 1.7
#define GC_FAR 30.0
#define GC_BUBBLE 0.8
#define GC_TAU 6.2831853

/** Weights of gyroid, Schwarz-P and diamond. */
vec3 gW;
vec3 gK;
float gKMax;
float gTh;
float gTube;
vec3 gEye;
/** Set by map(): the surface-space point, for the veins. */
vec3 gQ;

float tpms(vec3 x) {
    vec3 s = sin(x);
    vec3 c = cos(x);
    float gy = dot(s, c.yzx);
    // Schwarz-P and diamond rescaled to roughly the gyroid's +-1.5 range.
    float sp = (c.x + c.y + c.z) * 0.5;
    float di = (s.x * s.y * s.z + s.x * c.y * c.z + c.x * s.y * c.z + c.x * c.y * s.z) * 1.06;
    return gW.x * gy + gW.y * sp + gW.z * di;
}

float map(vec3 w) {
    vec3 q = gEye + (w - gEye) * gK;
    gQ = q;
    float g = tpms(q * GC_F);
    float d = (abs(g) - gTh) / (GC_F * GC_LIP * gKMax);
    // The flight channel, blended into the sheet so it reads as cave, not cut.
    d = smax(d, gTube - length(w.xy), 0.9);
    d = max(d, GC_BUBBLE - length(w - gEye));
    return d;
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

/** The surface for section `n`: weights of gyroid, Schwarz-P, diamond. */
vec3 gcVariant(float n) {
    uint pick = uhash(uint(n) + 0x3d1u) % 4u;
    if (pick == 1u) return vec3(0.0, 1.0, 0.0);
    if (pick == 2u) return vec3(0.0, 0.0, 1.0);
    if (pick == 3u) return vec3(0.55, 0.0, 0.45);
    return vec3(1.0, 0.0, 0.0);
}

/** A per-onset stretch per axis, 0.8..1.25. */
vec3 gcOnsetStretch(float n) {
    uint h = uhash(uint(n) + 0x6b5u);
    vec3 r = vec3(uhashUnit(h), uhashUnit(uhash(h)), uhashUnit(uhash(h ^ 0x27d4u)));
    return 0.8 + 0.45 * r;
}

/** The vein surface: a gyroid at three times the frequency, flowing along `flow`. */
float vein(vec3 q, vec3 flow) {
    vec3 x = q * (GC_F * 3.0) + flow;
    return abs(dot(sin(x), cos(x.yzx)));
}

void main() {
    vec2 uv = view();

    float swell = clamp(SND_INTENSITY, 0.0, 1.5);
    float kick = clamp(SND_KICK_ENV, 0.0, 1.0);
    float snare = clamp(SND_SNARE_ENV, 0.0, 1.0);
    float hat = clamp(SND_HAT_ENV, 0.0, 1.0);
    float drop = clamp(SND_HELD_SURGE, 0.0, 1.0);
    float pitch = clamp(SND_HELD_NOTE_CHANGE, 0.0, 1.0);
    float high = clamp(0.5 * sndLevel(SND_MID_HIGH_ATT) + 0.3 * sndSpectrum(0.80) + 0.2 * sndLevel(SND_TREBLE_ATT), 0.0, 1.0);
    float dropBump = 4.0 * drop * (1.0 - drop);
    float pitchBump = 4.0 * pitch * (1.0 - pitch);

    // Section crossfade: starts at the old surface the frame the count steps.
    float sw = 1.0 - clamp(SND_HELD_HARD_CUT, 0.0, 1.0);
    sw = sw * sw * (3.0 - 2.0 * sw);
    gW = mix(gcVariant(mod(SND_CUT_COUNT - 1.0, 4096.0)), gcVariant(SND_CUT_COUNT), sw);

    // Onset glide from the previous count's stretch to the new one.
    float og = (1.0 - smoothstep(0.05, 0.4, clamp(SND_ONSET_ENV, 0.0, 1.0)));
    vec3 stretch = mix(gcOnsetStretch(mod(floor(SND_HIT_TIME) - 1.0, 4096.0)), gcOnsetStretch(floor(SND_HIT_TIME)), og);
    // A slow idle breath so silence still moves; the drop opens everything.
    vec3 idle = 1.0 + 0.04 * vec3(sin(uTime * 0.07), sin(uTime * 0.05 + 2.0), sin(uTime * 0.06 + 4.0));
    gK = stretch * idle * mix(1.0, 0.62, dropBump);
    gKMax = max(gK.x, max(gK.y, gK.z));
    gTh = (0.14 + 0.34 * kick) * (1.0 - 0.45 * dropBump);
    gTube = 1.9 + 1.4 * dropBump;

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // The surface, the veins and the motes all tile with periods dividing 16
    // along z, so this rebase is exact and invisible, and z stays precise.
    ro.z = mod(ro.z, GC_ZWRAP);
    gEye = ro;

    float t = 0.03;
    float hitT = -1.0;
    float used = 0.0;
    vec3 hitQ = vec3(0.0);
    for (int i = 0; i < GC_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (t > GC_FAR) break;
        used = float(i);
        float d = map(ro + rd * t);
        float eps = 0.0008 * t + 0.0005;
        if (d < eps) {
            hitT = t;
            hitQ = gQ;
            break;
        }
        t += max(d * 0.85, eps);
    }

    float hue = 0.48 + 0.03 * sin(uTime * 0.017) + 0.5 * dropBump;
    float iri = 0.28 * pitchBump + 0.04 * sin(uTime * 0.09);
    float fogK = 0.055;
    float ahead = pow(max(rd.z, 0.0), 8.0);
    vec3 fogCol = pal(hue + 0.6) * (0.035 + 0.16 * ahead) + vec3(0.004, 0.003, 0.007);
    vec3 col = fogCol;
    float sceneT = hitT > 0.0 ? hitT : GC_FAR;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, max(0.0015, 0.0012 * hitT));
        float ndv = clamp(dot(n, -rd), 0.0, 1.0);
        float fres = pow(1.0 - ndv, 3.0);
        float ao = clamp(1.0 - used / max(uSteps, 1.0) * 1.2, 0.25, 1.0);
        float spec = pow(max(dot(reflect(rd, n), -rd), 0.0), 40.0);

        // Thin film: the optical path grows toward grazing angles, and the
        // film's own thickness ripples across the wall (its z rate is 2 * 2pi/16,
        // so the rebase lands on a whole ripple). Each channel reads the
        // palette at its own offset, which is the interference fringe.
        float film = 1.6 * (1.0 - ndv) + 0.35 * sin(dot(hitQ, vec3(0.9, 1.3, 0.7853982))) + iri;
        float fp = hue + film;
        vec3 thin = vec3(pal(fp).r, pal(fp + 0.13).g, pal(fp + 0.26).b);
        vec3 wall = pal(hue + 0.1) * (0.04 + 0.16 * ndv) * ao
                  + thin * (0.12 + 0.75 * fres) * (0.4 + 0.6 * ao)
                  + vec3(spec) * 0.3 * ao;

        // Veins: two flows of the same lattice, slow and fast, crossfaded by
        // the high bands. Both are plain multiples of uTime, so the speed can
        // change every frame and nothing ever jumps.
        float vs = vein(hitQ, vec3(0.0, 0.0, uTime * 0.25));
        float vf = vein(hitQ, vec3(uTime * 0.6, 0.0, uTime * 1.5));
        float v = mix(vs, vf, smoothstep(0.1, 0.8, high));
        float px = hitT * 2.0 / (max(uCamFocal, 0.1) * uResolution.y);
        float vw = 0.07 + 0.12 * snare;
        // The vein field's gradient is ~3 * GC_F per world unit; widen by the pixel.
        float vline = 1.0 - smoothstep(vw, vw + px * GC_F * 3.0 * 1.5 + 0.02, v);
        vec3 veinCol = mix(pal(hue + 0.33), pal(hue + 0.83) * 1.25, snare);
        // Seen through the film: strongest face-on, where the wall is thinnest to the eye.
        wall += veinCol * vline * (0.35 + 0.5 * ndv) * (0.6 + 1.4 * snare) * (0.7 + 0.3 * swell);

        col = mix(wall, fogCol, 1.0 - exp(-hitT * fogK));
    }

    // Motes: points on world z slices every 2 units ahead of the eye, one
    // hashed candidate per 1.2-unit cell. Slices sit at fixed world z (index
    // wrapped by 16), so they hold still under the rebase and parallax is true.
    if (abs(rd.z) > 0.08) {
        float dir = rd.z > 0.0 ? 1.0 : -1.0;
        float base = rd.z > 0.0 ? floor(ro.z * 0.5) + 1.0 : ceil(ro.z * 0.5) - 1.0;
        vec3 moteCol = mix(pal(hue + 0.15), vec3(1.0), 0.35);
        for (int i = 0; i < 6; i++) {
            float zi = (base + dir * float(i)) * 2.0;
            float ti = (zi - ro.z) / rd.z;
            if (ti < 0.5 || ti > sceneT) continue;
            vec2 xy = ro.xy + rd.xy * ti;
            vec2 cell = floor(xy / 1.2);
            int slice = int(mod(zi, GC_ZWRAP) * 0.5 + 0.5);
            float h = hashCell(ivec3(int(cell.x), int(cell.y), slice));
            if (h < 0.55) continue;
            float h2 = hashCell(ivec3(int(cell.x), int(cell.y), slice + 97));
            vec2 c = (cell + 0.5 + (vec2(h, h2) - 0.5) * 0.6) * 1.2
                   + 0.12 * vec2(sin(uTime * 0.4 + h * 30.0), cos(uTime * 0.33 + h2 * 30.0));
            float pxi = ti * 2.0 / (max(uCamFocal, 0.1) * uResolution.y);
            float r = 0.035 + pxi;
            float m = (1.0 - smoothstep(0.0, r, length(xy - c))) * min(1.0, 0.035 / r);
            float tw = 0.5 + 0.5 * sin(uTime * (7.0 + 11.0 * h2) + h * 50.0);
            col += moteCol * m * (0.12 + 1.6 * hat * tw) * exp(-ti * fogK);
        }
    }

    col *= 0.85 + 0.2 * swell;
    col *= 0.72 + 0.28 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
