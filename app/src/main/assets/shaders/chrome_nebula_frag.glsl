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
//#include lib_shell

// Chrome Nebula: liquid-chrome metaballs drifting in space, each one a mirror
// for a procedural nebula sky. The blobs are smooth-min'd spheres on slow
// Lissajous orbits; the camera circles them on the shared rig (camAround).
//
// ---- audio ------------------------------------------------------------------
//
// Bass (sndSpectrum(0.23)) swells the blobs. An onset (SND_ONSET_ENV) buds a droplet out of
// one blob (picked by floor(SND_HIT_TIME)) that flies out and dissolves, and turns every
// orbit a step further: the turn is driven by floor(SND_HIT_TIME) - 1 plus the onset's
// progress (0 at the hit, 1 once the envelope has fallen), which is
// continuous across the hit, so the arrangement morphs rather than jumps.
// The progress is measured from 0.4 because an onset triggers to 0.4..1
// (viz/Reactions), never reliably to 1. A pitch change (SND_HELD_NOTE_CHANGE) shifts the
// nebula's hue. Overall level follows (0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)) only.
//
// ---- knobs ------------------------------------------------------------------
//
// Flame's, Opal's and Hollow's roles, each read as its offset from 0.5 so off
// is the tuned look: k0 PULSE the emission, k1 TEXTURE the width of the sky's
// galactic band (seen direct and in every blob), k2 FLOW how far the orbits
// bend out of their plane, k3 TONE the palette position and how the chrome
// flows together (a high melody melts the blobs into one long liquid body, a
// low one parts them into separate drops; a smooth union of any width is
// still a lower bound), k4 BUILD the halo off
// the chrome, k5 SPACE the eye's distance (lib_chrys's reach).
//
// ---- shape inputs (lib_shell) ----------------------------------------------
//
// Every tunable of the chrome is a normalized input, mapped to its range in
// one place (the top of main) and read from globals. 0.5 is the look as tuned:
// the knobs rest there exactly while Note & key is off. Only knobs drive them
// here: this style leaves Morph, Warp and Turbulence to view(), which already
// bends its picture with them. Music reaches these only through the knobs (the
// reaction matrix); none reads the sound.
//
//   input       driver           lo .. rest .. hi
//   gBandW      k1 TEXTURE       the galactic band's width 0.5 .. 1 .. 1.5
//   sBlobs      k1 TEXTURE       blobs 4 .. 6 .. 8 (each one past a whole number grows in from nothing)
//   sLift       k2 FLOW          how far the orbits bend out of their plane 0.5 .. 1 .. 1.5
//   sSpread     k2 FLOW          the orbits' radius 1.4 .. 1.9 .. 2.4
//   gK          k3 TONE          the blend between blobs 0.072 .. 0.36 .. 0.648
//   sBlobR      k4 BUILD         a blob's smallest radius 0.32 .. 0.46 .. 0.62
//   sCamR       k5 SPACE         the eye's distance 7.59 .. 6.6 .. 5.61
//
// A blob blends by gK only once it is at least 0.4 across, as the bud does, so
// one growing in never leaves a nub. The chrome goes through SHELL_FIELD3, so
// the shell's spawn, fractal, front and back hooks reach it.

#define CN_MAX_STEPS 96
/** The most blobs TEXTURE can ask for (6 at rest, sBlobs). */
#define CN_BLOBS 8
#define CN_CAM_R 6.6
#define CN_FAR 16.0
/** Blend radius between blobs, world units. */
#define CN_K 0.36

vec3 gBlob[CN_BLOBS];
float gRad[CN_BLOBS];
vec3 gBud;
float gBudR;
/** k3 TONE: the blend radius between blobs (CN_K at rest). */
float gK = CN_K;

float mapField(vec3 p) {
    float d = sdSphere(p - gBlob[0], gRad[0]);
    for (int i = 1; i < CN_BLOBS; i++) {
        // A blob TEXTURE has not asked for is no blob.
        if (gRad[i] <= 0.0) continue;
        // A small one (growing in) blends less, so it never leaves a nub; 1 for every blob at rest.
        d = smin(d, sdSphere(p - gBlob[i], gRad[i]), gK * clamp(gRad[i] * 2.5, 0.0, 1.0));
    }
    // The blend shrinks with the bud, so a vanishing bud is a hard min with a
    // point (never hit) rather than a nub left on its parent.
    return smin(d, sdSphere(p - gBud, gBudR), gK * clamp(gBudR * 2.5, 0.0, 1.0));
}

/** The chrome as the shell sees it: spawn, fractal, front and back route through here (identity at rest). */
float map(vec3 p) {
    return SHELL_FIELD3(p, mapField);
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

/** k1 TEXTURE: the galactic band's width, 0.5..1.5 (1 at rest). */
float gBandW = 1.0;

/** The sky: two layers of fbm cloud on the palette, a galactic band, and (for the direct view only) stars. */
vec3 nebula(vec3 d, float hue, float stars) {
    vec3 q = d * 2.1 + vec3(0.0, uTime * 0.015, uTime * 0.02);
    float n = fbm3(q, 3);
    float n2 = fbm3(q * 2.3 + vec3(5.2, 1.3, 7.7), 2);
    float cloud = smoothstep(0.38, 0.85, n);
    vec3 c = vec3(0.012, 0.010, 0.022);
    c += pal(hue + n * 0.35) * cloud * 0.55;
    c += pal(hue + 0.45 + n2 * 0.2) * smoothstep(0.52, 0.9, n2) * cloud * 0.35;
    c += pal(hue + 0.2) * 0.10 * exp(-abs(d.y + 0.35 * (n - 0.5)) * 4.0 / gBandW);
    if (stars > 0.5) {
        vec3 sc = d * 110.0;
        vec3 cell = floor(sc);
        float h = hashCell(ivec3(cell));
        float s = step(0.986, h) * (1.0 - smoothstep(0.0, 0.32, length(fract(sc) - 0.5)));
        c += vec3(s) * (0.55 + 0.45 * sin(uTime * 1.7 + h * 60.0));
    }
    return c;
}

void main() {
    // ---- shape inputs: each normalized input mapped to its range, here only (see the header).
    gBandW = shellMap(shellKnob(1), 0.5, 1.0, 1.5);
    float sBlobs = shellMap(shellKnob(1), 4.0, 6.0, 8.0);
    float sLift = shellMap(shellKnob(2), 0.5, 1.0, 1.5);
    float sSpread = shellMap(shellKnob(2), 1.4, 1.9, 2.4);
    gK = shellMap(shellKnob(3), CN_K * 0.2, CN_K, CN_K * 1.8);
    float sBlobR = shellMap(shellKnob(4), 0.32, 0.46, 0.62);
    float sCamR = shellMap(shellKnob(5), CN_CAM_R * 1.15, CN_CAM_R, CN_CAM_R * 0.85);

    vec2 uv = view();
    float kPulse = clamp(uKnob[0], 0.0, 1.0) - 0.5;
    float kTone = clamp(uKnob[3], 0.0, 1.0) - 0.5;
    float kBuild = clamp(uKnob[4], 0.0, 1.0) - 0.5;

    float swell = clamp((0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)), 0.0, 1.5);
    float bass = clamp(sndSpectrum(0.23), 0.0, 1.0);
    float onset = clamp(SND_ONSET_ENV, 0.0, 1.0);
    float hue = 0.58 + 0.03 * sin(uTime * 0.05) + 0.16 * clamp(SND_HELD_NOTE_CHANGE, 0.0, 1.0) + 0.6 * kTone;

    // ---- the blobs ----------------------------------------------------------
    // 0 on the hit frame (the envelope is >= 0.4 then), 1 once it has fallen.
    float prog = (1.0 - smoothstep(0.05, 0.4, onset));
    float morph = floor(SND_HIT_TIME) - 1.0 + prog;
    float grow = 1.0 + 0.26 * bass + 0.06 * swell;
    float tm = uTime * 0.22 + uFlowPhase * 0.04;
    float seed = fract(floor(SND_HIT_TIME) * 0.618034);
    float parentF = floor(seed * floor(sBlobs));
    vec3 parentPos = vec3(0.0);
    float parentR = 0.5;
    for (int i = 0; i < CN_BLOBS; i++) {
        float fi = float(i);
        float h1 = hash11(fi * 3.17 + 0.5);
        float h2 = hash11(fi * 7.31 + 1.7);
        float h3 = hash11(fi * 1.93 + 4.1);
        float dir = mod(fi, 2.0) < 0.5 ? 1.0 : -1.0;
        float ph = fi * 1.047 + morph * (0.45 + 0.35 * h1) * dir;
        vec3 c = vec3(sin(tm * (0.7 + 0.5 * h1) + ph) * sSpread,
                      sin(tm * (0.5 + 0.6 * h2) + ph * 1.3 + 2.0) * 1.1 * sLift,
                      cos(tm * (0.6 + 0.4 * h3) + ph * 0.7) * sSpread);
        // TEXTURE's count: blob i grows in as sBlobs passes i (all of the first six whole at rest).
        float r = (sBlobR + 0.28 * h2) * grow * clamp(sBlobs - fi, 0.0, 1.0);
        gBlob[i] = c;
        gRad[i] = r;
        if (abs(fi - parentF) < 0.5) {
            parentPos = c;
            parentR = r;
        }
    }
    // The bud: born inside its parent at the onset, flung out as the envelope
    // falls, and zero-sized at both ends so neither end of its life pops.
    float life = prog;
    vec3 budDir = normalize(vec3(hash11(seed * 91.0 + 1.0), hash11(seed * 53.0 + 2.0), hash11(seed * 17.0 + 3.0)) - 0.5 + 1e-3);
    gBud = parentPos + budDir * parentR * (0.3 + 1.5 * life);
    gBudR = 0.38 * grow * sin(3.14159 * pow(life, 0.6)) * step(0.5, floor(SND_HIT_TIME));

    // ---- march --------------------------------------------------------------
    vec3 ro;
    vec3 rd;
    camAround(uv, sCamR, 3.0 * CAM_SPIN_UNIT, ro, rd);

    float t = 0.05;
    float hitT = -1.0;
    float near = 1e9;
    for (int i = 0; i < CN_MAX_STEPS; i++) {
        if (float(i) >= uSteps) break;
        if (t > CN_FAR) break;
        float d = map(ro + rd * t);
        float eps = 0.0007 * t + 0.0005;
        if (d < eps) {
            hitT = t;
            break;
        }
        near = min(near, d);
        t += d * 0.9;
    }

    vec3 col;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, 0.0012 * hitT + 0.0006);
        vec3 r = reflect(rd, n);
        // Chrome: a high base reflectance that goes to mirror at grazing.
        float fres = 0.62 + 0.38 * pow(1.0 - clamp(dot(n, -rd), 0.0, 1.0), 5.0);
        vec3 tint = mix(vec3(1.0), pal(hue + 0.08), 0.22);
        col = nebula(r, hue, 0.0) * 2.4 * tint * fres;
        vec3 L = normalize(vec3(0.5, 0.8, -0.3));
        col += tint * pow(max(dot(r, L), 0.0), 64.0) * 1.3;
        col += pal(hue + 0.5) * pow(max(dot(r, -L), 0.0), 16.0) * 0.25;
        // Cheap occlusion in the creases where blobs meet.
        float ao = clamp(map(p + n * 0.3) / 0.3, 0.0, 1.0);
        col *= 0.45 + 0.55 * ao;
        // A glint where the bud is leaving: local, never full frame.
        col += pal(hue + 0.3) * onset * 0.7 * exp(-length(p - gBud) * 3.5);
    } else {
        col = nebula(rd, hue, 1.0);
        // Halo off the chrome the ray slid past.
        col += pal(hue + 0.15) * 0.12 * exp(-near * 5.0) * (1.0 + kBuild);
    }

    col *= 0.85 + 0.2 * swell;
    col *= 0.72 + 0.28 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(col * (1.0 + 0.5 * kPulse)), 1.0);
}
