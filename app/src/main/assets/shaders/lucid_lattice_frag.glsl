#version 300 es
#define VIEW_OWNS_SHAPE
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
//#include lib_lucid

// Lucid Lattice: a quasicrystal folded through itself, now a crystal you fly
// through. The 2D style summed seven plane waves at equal angles into a
// pattern with seven-fold symmetry that never repeats; here the same seven
// waves are tilted out of the screen into space. Each keeps its direction in
// the xy plane (k * pi / N, as before) and gains a small z wavenumber, a whole
// multiple of 2 pi / 16, so every slice across the flight still shows the
// Penrose-like rosettes while the crystal stacks and twists along the flight.
// It never repeats sideways; along z it repeats every 16 world units, which is
// what the rig's travel wrap needs.
//
// The sum is the nest's field, as in 2D: a coarser read of the same crystal
// (its analytic gradient, so no extra taps) displaces where the surface reads,
// and the rosettes buckle into one another and drift as the phases travel.
// Two solids come out of one field: BEADS where the waves add up (the
// crystal's bright nodes, F above an iso) and SHEETS around its nodal surface
// (|F| below a half-width), thin cracked membranes between the beads.
//
// Coloured as holographic foil, as before: the direction the surface faces
// (its normal, which is the field's gradient) picks the hue, so every facet
// catches a different colour the way embossed foil does; the view angle adds a
// thin-film shift, and a bright sheen rides the grazing edges and highlights.
// A faint haze gathers where rays pass close to the crystal.
//
// ---- camera and the wrap ------------------------------------------------------
//
// camFly: the eye rides the rig's path down +z. A tube around the z axis is
// smoothly carved out of the crystal as the flight channel, and a sphere
// around the eye out of everything, so the eye is never inside a bead or a
// sheet. Every z term is a wave whose z wavenumber is m * 2 pi / 16 with m an
// integer (-2..2), and the phases depend on time only, so the field tiles with
// period 16 along z; the eye is rebased by mod(z, 16), which is exact, keeps
// z precise, and 16 divides the rig's 8192 wrap, so neither ever shows. x and
// y are not wrapped: the crystal is aperiodic across the flight, as it should be.
//
// ---- audio ------------------------------------------------------------------
//
// HARMONICITY (uHarmony, smoothed over 1 s) chooses the crystal's grain, as in
// 2D: tonal, sustained music grows the beads and dissolves the sheets (a field
// of smooth glowing nodes); noisy, percussive passages crack the crystal open
// into glassy nodal sheets as the beads shrink away, and the warp folds with
// the same morph (ridged gradient). A tonal break visibly heals the lattice.
// The beat (SND_KICK_ENV through lucidBump, so it eases in instead of stepping)
// swells the crystal: its xy wavenumber drops a little, the rosettes open
// around the flight axis. Brightness follows
// (0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)) gently, as before.
//
// ---- macros and knobs ---------------------------------------------------------
//
// Speed: uTime and uFlowTime drive the wave phases (never times Speed).
// Complexity: uMorph adds two waves (seven to nine) past 0.5, and a nest level
// (lucidDetail). Chaos: uTurbulence/uWarp raise the warp strength
// (lucidChaos, capped at 2.5 for the march). Kaleido: when uKaleido is on,
// uSymmetry (3..11) sets the number of waves, the crystal's symmetry order.
// Color: the foil is half pal(), half the lucidFilm interference ramp. Energy:
// uIntensity scales the beat swell and how hard harmony re-grains the crystal.
// Space: uZoom/uRotation/uTwist through lucidView (the screen, so the rays).
// Knobs (offsets from 0.5, so off is the tuned look): k0 PULSE the beat swell,
// k1 TEXTURE the crystal's scale, k2 FLOW the warp strength, k3 TONE the hue,
// k4 BUILD the sheen and haze, k5 SPACE the flight channel's radius.
//
// ---- why the distance estimate is safe ------------------------------------------
//
// F = (1/N) sum cos(d_k . p + phi_k), d_k = (s u_k, c_k). Its gradient is
// (1/N) sum a_k d_k with |a_k| <= 1. The xy part is at most
// s * max_e (1/N) sum |u_k . e|, and for N directions evenly spread over a
// half turn that sum peaks at 1 / sin(pi / 2N) (0.64..0.67 of N, computed per
// frame); the z part is at most (1/N) sum |c_k|. So |grad F| <= G0, their
// root-sum-square. A nest level returns grad F_l / G_l, a unit-bounded vector
// whose own Lipschitz bound is H_l / G_l, H_l = s_l^2 + (1/N) sum c_k^2 the
// Hessian bound; the ridged fold (abs - 0.3) and the mix with the smooth one
// keep it, and a rotation of xy is an isometry. Chaining the levels gives the
// warp's bound Lw, and the surface field F(p + w(p)) is G0 * (1 + Lw)-
// Lipschitz. Beads (iso - F) and sheets (|F| - hw) share that bound, and so
// does their min; the distance is divided by it. The tube is smoothly
// subtracted (smax, a slight overestimate in the blend) and the eye sphere
// joins through max. The march over-relaxes (omega 1.4) and falls back to a
// plain step whenever two consecutive spheres fail to overlap (Keinert et
// al.'s check), so the extra speed never skips a surface the bound guards.

#define LAT_MAX_WAVES 11
/** Nest levels the march can afford: one normally, two with Complexity. */
#define LAT_LEVELS 2
#define LAT_MAX_STEPS 96
#define LAT_FAR 30.0
#define LAT_ZWRAP 16.0
/** 2 pi / 16: the z wavenumber unit. Every z rate is an integer multiple. */
#define LAT_ZU 0.39269908
/** xy wavenumber at rest: a rosette every ~4.5 world units. */
#define LAT_S 1.4
#define LAT_TUBE 1.9
#define LAT_BUBBLE 0.75

int gN;
float gInvN;
vec2 gU[LAT_MAX_WAVES];
float gCz[LAT_MAX_WAVES];
float gPh[LAT_MAX_WAVES];
int gDepth;
float gLvS[LAT_LEVELS];
float gLvG[LAT_LEVELS];
float gLvP[LAT_LEVELS];
mat2 gLvR[LAT_LEVELS];
float gS;
float gLip;
float gWarpA;
float gSmooth;
float gSheet;
float gBead;
float gTube;
vec3 gEye;

/** The crystal at xy wavenumber `sxy`, phases scaled by `ps`. -1..1. */
float quasi(vec3 q, float sxy, float ps) {
    float s = 0.0;
    for (int k = 0; k < LAT_MAX_WAVES; k++) {
        if (k >= gN) break;
        vec3 d = vec3(gU[k] * sxy, gCz[k]);
        s += cos(dot(q, d) + gPh[k] * ps);
    }
    return s * gInvN;
}

/** The crystal's analytic gradient at the same wavenumber and phases. */
vec3 quasiGrad(vec3 q, float sxy, float ps) {
    vec3 g = vec3(0.0);
    for (int k = 0; k < LAT_MAX_WAVES; k++) {
        if (k >= gN) break;
        vec3 d = vec3(gU[k] * sxy, gCz[k]);
        g -= sin(dot(q, d) + gPh[k] * ps) * d;
    }
    return g * gInvN;
}

/** The style's world at p: its distance, and in `found` what is there (a.x the field at the warped point). */
float mapAt(vec3 p, inout LucidHit found) {
    vec3 w = vec3(0.0);
    for (int i = 0; i < LAT_LEVELS; i++) {
        if (i >= gDepth) break;
        vec3 q = p + w;
        q.xy = gLvR[i] * q.xy;
        vec3 g = quasiGrad(q, gLvS[i], gLvP[i]) / gLvG[i];
        // Ridged mode folds the warp too: the gradient's sign flips at the creases.
        w = gWarpA * mix(abs(g) - 0.3, g, gSmooth);
    }
    float f = quasi(p + w, gS, 1.0);
    found.a.x = f;
    float d = min(gBead - f, abs(f) - gSheet) / gLip;
    d = smax(d, gTube - length(p.xy), 0.8);
    return max(d, LAT_BUBBLE - length(p - gEye));
}

/** The style's world at p as a hit, for lib_lucid_field. */
LucidHit lucidMap(vec3 p) {
    LucidHit h = lucidHit(0.0);
    float d = mapAt(p, h);
    h.d = d;
    return h;
}

//#include lib_lucid_field

void main() {
    LucidHit fld = lucidHit(0.0);
    vec2 uv = lucidView(false);
    float t = uTime;  // uTime already integrates Speed (ShaderScene::update)
    float flow = uFlowTime;

    // The normalization shell (lib_lucid): every variable from its driver, mapped to its range at its one site.
    LucidIn N = lucidIn();

    float swell = clamp((0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)), 0.0, 1.5);
    // 1 = smooth crystal (beads), 0 = fully ridged (sheets).
    float smoothness = clamp(mix(1.0, clamp(uHarmony, 0.0, 1.0), clamp(uIntensity, 0.0, 1.5)), 0.0, 1.0);
    gSmooth = smoothness;
    float beat = lucidBump(SND_KICK_ENV) * clamp(uIntensity, 0.0, 1.5);

    float waves = (uKaleido > 0.5 && uSymmetry >= 2.0) ? clamp(floor(uSymmetry + 0.5), 3.0, 11.0)
                                                       : 7.0 + (N.depth >= 0.5 ? 2.0 : 0.0);
    gN = int(waves);
    gInvN = 1.0 / waves;
    gDepth = min(lucidDetail(1, 0), LAT_LEVELS);

    // The waves: xy directions k * pi / N as in 2D, z wavenumbers m * 2 pi / 16
    // with m = (2k mod 5) - 2, and phases travelling in alternate directions.
    float phase = t * 0.35 + flow * 0.4;
    float czAbs = 0.0;
    float czSq = 0.0;
    for (int k = 0; k < LAT_MAX_WAVES; k++) {
        if (k >= gN) break;
        float fk = float(k);
        float a = fk * LUCID_PI / waves;
        gU[k] = vec2(cos(a), sin(a));
        gCz[k] = (mod(2.0 * fk, 5.0) - 2.0) * LAT_ZU;
        gPh[k] = phase * (mod(fk, 2.0) * 2.0 - 1.0) * (1.0 + 0.13 * fk);
        czAbs += abs(gCz[k]);
        czSq += gCz[k] * gCz[k];
    }
    czAbs *= gInvN;
    czSq *= gInvN;
    float xyBound = gInvN / sin(LUCID_PI / (2.0 * waves));

    // The beat swells the crystal: a lower xy wavenumber opens the rosettes.
    gS = LAT_S * lucidSpan(N.scale, 1.0, 0.75, 1.25) * (1.0 - clamp(0.08 + lucidSpan(N.emit, 0.0, -0.06, 0.06), 0.0, 0.2) * beat);
    gWarpA = 0.28 * min(lucidChaos(), 2.5) * lucidSpan(N.flow, 1.0, 0.5, 1.5);

    // Nest levels, coarsest first, and the warp's Lipschitz bound (header).
    float lw = 0.0;
    for (int i = 0; i < LAT_LEVELS; i++) {
        if (i >= gDepth) break;
        float lv = float(gDepth - i);
        float sl = gS * exp2(-0.5 * lv);
        float gl = sqrt(sl * sl * xyBound * xyBound + czAbs * czAbs);
        float hl = sl * sl + czSq;
        gLvS[i] = sl;
        gLvG[i] = gl;
        gLvP[i] = 1.0 - 0.2 * lv;
        gLvR[i] = lucidRot(0.4 * lv);
        lw = gWarpA * (hl / gl) * (1.0 + lw);
    }
    gLip = sqrt(gS * gS * xyBound * xyBound + czAbs * czAbs) * (1.0 + lw);

    // Beads vanish (iso above any F) as the music turns noisy, sheets vanish
    // (negative half-width) as it turns tonal; in between both are there.
    gBead = mix(1.6, 0.3, smoothstep(0.0, 0.65, smoothness));
    gSheet = 0.11 - 0.6 * smoothstep(0.35, 1.0, smoothness);
    gTube = LAT_TUBE * lucidSpan(N.reach, 1.0, 0.7, 1.3);

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term has a period dividing 16 (header), so this is exact.
    ro.z = mod(ro.z, LAT_ZWRAP);
    gEye = ro;

    float hue0 = lucidSpan(N.tone, 0.0, -0.15, 0.15) + 0.03 * sin(t * 0.021);
    float build = lucidSpan(N.glow, 1.0, 0.5, 1.5);

    float tm = 0.03;
    float hitT = -1.0;
    float used = 0.0;
    float haze = 0.0;
    float omega = 1.4;
    float prevR = 0.0;
    float stepLen = 0.0;
    for (int i = 0; i < LAT_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (tm > LAT_FAR) break;
        used = float(i);
        fld = lucidField(ro + rd * tm);
        float d = fld.d;
        float r = abs(d);
        bool fail = omega > 1.0 && (r + prevR) < stepLen;
        if (fail) {
            // The spheres stopped overlapping: step back inside the last safe one.
            stepLen -= omega * stepLen;
            omega = 1.0;
        } else {
            float eps = 0.0008 * tm + 0.0006;
            if (d < eps) {
                hitT = tm;
                break;
            }
            stepLen = max(d * omega * 0.9, eps);
            haze += 1.0 / (1.0 + 300.0 * d * d);
        }
        prevR = r;
        tm += stepLen;
    }

    float ahead = pow(max(rd.z, 0.0), 8.0);
    vec3 fogCol = mix(lucidFilm(hue0 + 0.6), pal(hue0 + 0.6), 0.6) * (0.03 + 0.13 * ahead) + vec3(0.004, 0.003, 0.008);
    vec3 col = fogCol;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = lucidNormal(p, max(0.0015, 0.0012 * hitT));
        // The hit at p again for the shading, after the normal taps.
        fld = lucidField(p);
        float field = fld.a.x;
        float ndv = clamp(dot(n, -rd), 0.0, 1.0);
        float fres = pow(1.0 - ndv, 3.0);
        float ao = clamp(1.0 - used / max(uSteps, 1.0) * 1.1, 0.25, 1.0);
        vec3 L = normalize(vec3(-0.4, 0.7, 0.55));
        float dif = 0.5 + 0.5 * dot(n, L);
        // Cracked sheets read as glass: a tighter, harder highlight.
        float spec = pow(max(dot(reflect(rd, n), L), 0.0), mix(64.0, 24.0, smoothness));

        // Foil: the facing direction picks the hue, the view angle shifts it.
        float hue = hue0 + atan(n.y, n.x + 1e-5) / LUCID_TAU + 0.35 * field + 0.25 * (1.0 - ndv);
        vec3 foil = mix(lucidFilm(hue), pal(hue), 0.5);
        col = foil * (0.12 + 0.5 * dif) * ao;
        col += mix(vec3(1.0), pal(hue + 0.5), 0.4) * (0.45 * spec + 0.3 * fres) * build * (0.4 + 0.6 * ao);
        col = lucidCubic(col, 0.6);
        col = mix(col, fogCol, 1.0 - exp(-hitT * 0.06));
    }
    col += pal(hue0 + 0.2) * clamp(haze * 0.004, 0.0, 0.3) * build;

    col *= 0.85 + 0.12 * swell;
    col *= 0.75 + 0.25 * (1.0 - smoothstep(0.5, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
