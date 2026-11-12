#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

// The shape controls are spent on the hive itself (lib_neon), not on the rays.
#define VIEW_OWNS_SHAPE
//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_neon

// Hex Hive: a flight down a tunnel whose wall is a honeycomb of chrome hex
// pillars with neon-wire top edges, the pillars rising with the spectrum,
// neon coils spiralling down the bore, and a kaleidoscopic mandala portal at
// the far end.
//
// ---- what moves it -----------------------------------------------------------
//
// The style was drawn around six looks (wire glow, facets, rise, hue, portal,
// bore); each is now driven by the shared controls (lib_neon) or the sound,
// so Customize, the macros, the director and the DNA all reach it:
//
// - wire glow: SND_KICK_ENV flashes the hex rims; SND_SNARE_ENV the coils.
// - facets (Morph, neonForm): crystal facet lines on the pillar tops and
//   more coils down the bore. A hard cut (SND_CUT_COUNT) adds or drops a coil.
// - rise (Warp, neonFlow): how far the spectrum lifts the pillars; the drop
//   lifts them further.
// - Turbulence (neonChaos) makes the pillars restless: each jitters on its
//   own clock.
// - hue: the key (neonKeyTurn) turns the ramp; SND_CHORD_HUE colours the
//   portal.
// - portal: SND_BUILD_UP opens the mandala at the end and lifts the fog; the
//   drop (SND_HELD_SURGE) throws it wide. The mandala turns on time alone
//   (owner 2026-11-12, no style spins to the music).
// - bore: the bore widens in silence (neonQuiet) and narrows on the drop.
// - SND_HAT_ENV sparkles the facets. Intensity (neonGlow) is the emission.
//
// Pillar heights read sndSpectrum by the cell's angle (mirrored so the lows sit
// at the bottom of the bore and the highs on top): that is drawing the
// spectrum itself, so it reads the signal directly.
//
// Provenance: written from scratch for BeatRealms from general techniques - a hex
// lattice as the nearer of two offset rectangular grids; a hexagon SDF; the
// lattice wrapped onto a cylinder (arc length by angle, depth by radius); a
// helix tube distance in cylinder coordinates; sphere tracing with a
// conservative step; 1/(1 + k d^2) glow accumulated along the ray for the
// wires; tetrahedral normals; a cosine thin-film tint; and a kaleidoscopic
// polar fold for the portal. No code was taken from any published shader.
//
// The lattice repeats every HH_CELL (0.5) along z and the coils every 8.0, both
// dividing 8192, so the rig's travel wrap is invisible; the eye's z is rebased
// into one 8-unit tile.

#define HH_MAX_STEPS 110
#define HH_TAU 6.2831853
#define HH_CELL 0.5
/** Hex cells around the bore (pairs of offset rows). */
#define HH_AROUND 18.0
#define HH_FAR 40.0
#define HH_PITCH 8.0

float gR;
float gRise;
float gTone;
float gPulse;
float gTex;
float gChaos;
/** Coil flash on the snare. */
float gSnare;
vec3 gGlow;
/** The march distance of the current sample, for fading glow with depth. */
float gT;

vec3 neon(float t) {
    vec3 c = 0.5 + 0.5 * cos(HH_TAU * (t + gTone + vec3(0.60, 0.78, 0.98)));
    c = c * c * c * vec3(1.1, 1.0, 1.35) + vec3(0.02, 0.0, 0.06);
    return neonPal(c, t + gTone);
}

/** Hex distance (flat side across = 1 at r = 0.5) in the cell's local frame. */
float sdHex(vec2 p, float r) {
    p = abs(p);
    return max(p.x * 0.8660254 + p.y * 0.5, p.y) - r;
}

/**
 * The nearest hex centre on a lattice with spacing s: the nearer of two offset
 * rectangular grids. Returns (local offset xy, centre zw).
 */
vec4 hexCell(vec2 p, float s) {
    vec2 g = vec2(s * 1.7320508, s);
    vec2 a = mod(p, g) - 0.5 * g;
    vec2 b = mod(p - 0.5 * g, g) - 0.5 * g;
    return dot(a, a) < dot(b, b) ? vec4(a, p - a) : vec4(b, p - b);
}

/** Scene distance; m receives (hexDist, heightAboveTop, cellHeight, cellId). */
float map(vec3 p, out vec4 m) {
    float r = length(p.xy);
    float ang = atan(p.y, p.x);
    float circ = HH_AROUND * HH_CELL * 1.7320508;
    vec2 q = vec2(ang / HH_TAU * circ, p.z);
    vec4 hc = hexCell(q, HH_CELL);
    float hd = sdHex(hc.xy, HH_CELL * 0.43);
    // The cell's place around the bore, mirrored: lows at the bottom.
    float around = fract(hc.z / circ + 0.25);
    float sx = abs(2.0 * around - 1.0);
    float spec = sndSpectrum(0.04 + 0.85 * (1.0 - sx));
    float h = 0.06 + gRise * (0.05 + 1.6 * spec * spec) * (0.75 + 0.25 * sin(hc.w * 1.7 + hc.z));
    // Turbulence: every pillar jitters on its own clock.
    h += gChaos * 0.18 * (0.5 + 0.5 * sin(uTime * 7.0 + hc.z * 12.9 + hc.w * 7.3));
    float depth = gR - r;
    float pillar = max(hd, depth - h);
    float floorD = depth + 0.05;
    m = vec4(hd, depth - h, h, hc.z * 7.31 + hc.w * 3.17);

    // Neon coils down the bore: glow only, never hit.
    float coilD = 1e3;
    for (int i = 0; i < 4; i++) {
        float fi = float(i);
        float vis = clamp(2.0 + gTex * 2.0 + mod(floor(SND_CUT_COUNT + 0.5), 2.0) - fi, 0.0, 1.0);
        if (vis <= 0.0) break;
        // Inside the pillar tips, so they are seen, not buried.
        float rc = 1.05 + 0.18 * fi;
        float ph = ang + fi * HH_TAU / 4.0;
        float zz = p.z - ph / HH_TAU * HH_PITCH;
        float dz = (fract(zz / HH_PITCH) - 0.5) * HH_PITCH;
        // Perpendicular distance to the helix: the z gap times the sine of the
        // helix's angle to the axis.
        float circ = HH_TAU * rc;
        float dd = max(length(vec2(r - rc, dz * circ * inversesqrt(circ * circ + HH_PITCH * HH_PITCH))) - 0.035, 0.0);
        float g = 1.0 / (1.0 + dd * dd * mix(9000.0, 4000.0, gPulse));
        gGlow += neon(0.3 * fi + 0.1 * p.z) * g * vis * (0.3 + 0.6 * gSnare) * exp(-gT * 0.08) * smoothstep(0.6, 2.5, gT);
        coilD = min(coilD, dd);
    }
    // Neon wire along every pillar's top rim.
    float rim = length(vec2(hd, depth - h));
    gGlow += neon(0.08 + 0.8 * sx) * 0.00012 / (0.001 + rim * rim * (1.4 - 0.6 * gPulse));
    // The coils slow the march near them (so their glow is sampled) but are
    // never hit: the floor of 0.04 lets the ray pass straight through.
    return min(min(pillar, floorD), max(coilD, 0.022));
}

vec3 calcNormal(vec3 p) {
    vec4 m;
    vec3 sg = gGlow;
    vec2 e = vec2(0.002, -0.002);
    vec3 n = normalize(e.xyy * map(p + e.xyy, m) + e.yyx * map(p + e.yyx, m) + e.yxy * map(p + e.yxy, m) + e.xxx * map(p + e.xxx, m));
    gGlow = sg;
    return n;
}

vec3 mandala(vec2 p) {
    float r = length(p);
    float a = atan(p.y, p.x);
    float seg = HH_TAU / 12.0;
    float fa = abs(mod(a + uTime * 0.08, seg) - 0.5 * seg);
    float lr = log(max(r, 1e-4));
    vec3 col = vec3(0.0);
    col += neon(0.1 + lr * 0.3) * exp(-abs(fract(lr * 3.0 - uTime * 0.2) - 0.5) * 30.0) * 0.7;
    for (int i = 0; i < 4; i++) {
        float fi = float(i);
        float R = 0.05 * pow(1.8, fi);
        float petal = R * (1.0 + 0.55 * cos(fa * (4.0 + 2.0 * fi) + uTime * 0.1 * (fi - 1.5)));
        col += neon(0.25 * fi + 0.4) * exp(-abs(r - petal) / R * 26.0) * 1.2;
    }
    col += neon(0.55) * exp(-r * 25.0) * 2.0;
    return col * smoothstep(0.6, 0.3, r);
}

void main() {
    vec2 uv = view();
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    ro.z = mod(ro.z, 8.0);

    // The six looks the hive was drawn around, each 0..1 with 0.5 as tuned,
    // now driven: see "what moves it" above.
    float surge = neonSurge();
    float quiet = neonQuiet();
    float glow = neonGlow();
    float kPulse = clamp(0.2 + 0.8 * neonKick() + 0.4 * surge, 0.0, 1.0) * (1.0 - 0.6 * quiet);
    float kTex = mix(0.5, 1.0, neonForm());
    float kFlow = clamp(0.5 + 0.5 * neonFlow() + 0.35 * surge, 0.0, 1.0);
    float kBuild = clamp(0.4 + 0.6 * neonBuild() + 0.6 * surge, 0.0, 1.0);
    float kSpace = clamp(0.5 + 0.4 * quiet - 0.3 * surge, 0.0, 1.0);
    // Note & key reactions' style knobs ride on top (lib_neon).
    kPulse = clamp(kPulse + neonKnob(0), 0.0, 1.0);
    kTex = clamp(kTex + neonKnob(1), 0.0, 1.0);
    kFlow = clamp(kFlow + neonKnob(2), 0.0, 1.0);
    kBuild = clamp(kBuild + neonKnob(4), 0.0, 1.5);
    kSpace = clamp(kSpace + neonKnob(5), 0.0, 1.0);
    gR = mix(3.3, 4.6, kSpace);
    gRise = mix(0.45, 1.25, kFlow);
    gTone = 0.3 + neonKeyTurn() + 0.6 * neonKnob(3);
    gPulse = kPulse;
    gTex = kTex;
    gChaos = neonChaos();
    gSnare = sndShaped(clamp(SND_SNARE_ENV, 0.0, 1.0)) * (1.0 - quiet);
    gGlow = vec3(0.0);
    // Keep the eye well inside the pillars, whatever the rig's wander.
    float er = length(ro.xy);
    float maxR = gR - 1.9;
    if (er > maxR) ro.xy *= maxR / er;

    float t = 0.05;
    vec4 m = vec4(0.0);
    bool hit = false;
    for (int i = 0; i < HH_MAX_STEPS; i++) {
        if (float(i) >= uSteps) break;
        vec3 p = ro + rd * t;
        gT = t;
        float d = map(p, m);
        if (d < 0.0015 * t) {
            hit = true;
            break;
        }
        t += d * 0.7;
        if (t > HH_FAR) break;
    }

    // Behind everything: violet depth with the mandala portal down the bore.
    vec3 bg = vec3(0.03, 0.006, 0.07);
    if (rd.z > 0.0) {
        vec2 pp = rd.xy / rd.z / mix(0.18, 0.4, kBuild);
        float keep = gTone;
        gTone += 0.4 * SND_CHORD_HUE;
        bg += mandala(pp) * (0.5 + 0.9 * kBuild) * glow;
        gTone = keep;
    }

    vec3 fogCol = vec3(0.05, 0.01, 0.11);
    vec3 col = bg;
    if (hit) {
        vec3 p = ro + rd * t;
        vec3 n = calcNormal(p);
        float cosV = max(dot(n, -rd), 0.0);
        float fres = 0.04 + 0.96 * pow(1.0 - cosV, 5.0);
        // Chrome with a thin-film sheen: reflect the neon bore (the portal
        // colour and the coils' hues), tinted by interference.
        vec3 refl = reflect(rd, n);
        float film = 1.3 + 0.8 * fract(m.w * 0.13) + 2.0 * m.z;
        vec3 irid = 0.5 + 0.5 * cos(HH_TAU * (film * (0.5 + cosV) + gTone + vec3(0.0, 0.33, 0.67)));
        vec3 env = neon(0.45 + 0.3 * refl.z + 0.2 * refl.y) * (0.25 + 0.75 * smoothstep(-0.2, 1.0, refl.z));
        bool top = m.y > -0.01;
        vec3 surf = env * irid * (0.25 + 0.9 * fres);
        // Ambient occlusion from depth below the rims: the gaps go dark.
        float occ = top ? 1.0 : clamp(0.25 + 0.75 * (1.0 + m.y / max(m.z, 0.05)), 0.15, 1.0);
        surf *= occ;
        if (top) {
            // Crystal facets on the pillar tops: lines radiating from the centre.
            float fa = atan(m.x, m.y);
            float facet = exp(-abs(fract(m.x * 26.0) - 0.5) * 18.0) * smoothstep(0.0, 0.6, kTex);
            float glint = step(0.8, fract(m.w * 0.71 + floor(uTime * 8.0) * 0.37)) * neonSparkle();
            surf += neon(0.7 + m.z * 0.4) * facet * (0.25 + 1.2 * glint);
            // The rim: bright neon wire.
            float rim = exp(-abs(m.x) * mix(90.0, 45.0, kPulse));
            // The wire takes the spectrum's own hue round the bore: the lows
            // warm along the bottom, the highs cool across the top.
            float sxs = abs(2.0 * fract(atan(p.y, p.x) / HH_TAU + 0.25) - 1.0);
            surf += neon(0.08 + 0.8 * sxs + 0.3 * m.z) * rim * (1.2 + 2.5 * kPulse) * glow;
        }
        col = surf;
        float fog = 1.0 - exp(-t * mix(0.11, 0.05, kBuild));
        col = mix(col, fogCol, fog);
    }
    col += gGlow * (0.5 + 0.9 * kPulse) * exp(-t * 0.02) * glow;

    fragColor = vec4(neonFinish(col, 1.3), 1.0);
}
