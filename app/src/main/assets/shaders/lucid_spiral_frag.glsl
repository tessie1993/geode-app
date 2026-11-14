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
//#include lib_lucid

// Lucid Spiral: a flight down a spiral staircase of ridged folds.
//
// The flat style took the plane to log-polar coordinates, sheared the angle by
// the log radius (the Droste twist) and mirrored it into six arms, so the arms
// wound inward forever. Here the log radius is simply depth: the scene is a
// tube around the flight axis whose wall is folded into six helical ridges,
// a staircase screwing down the tunnel. In every cross section a ridge is a
// mirrored arm of the 2D picture (the same v = distance from the arm's centre
// line), and along z the arms wind one full turn per 16 units, which is the
// Droste shear made literal. Each ridge has a sharp crest and a rounded
// trough, the profile of ridged noise, so the fold reads like the 2D creases
// built in paper. The family's nest is laid on the sheared strip (along the
// helix, across the arm) at the hit point and coloured exactly as before: an
// acid duotone split by the ridge value, alternate arms swapping their channel
// order. The far end of the tube is fogged to near black, the 2D style's
// infinitely far centre.
//
// The camera is the shared rig (camFly), flying +z down the tube. Every term
// of the geometry has a z period dividing 16 (the helix 16, the wobble 8), and
// the eye is rebased by 1024, a power of two that divides the rig's 8192
// travel wrap. The nest is not periodic, so its strip coordinate is crossfaded
// over the last LS_BLEND units of each 1024 into the copy one period back:
// at the rebase seam both sides read the same noise, and the seam never shows.
// The crossfade costs a second nest only inside that window.
//
// ---- audio ------------------------------------------------------------------
//
// Kick (SND_KICK_ENV) pushes the dive: each kick slides the staircase toward
// the eye along the helix, on top of its slow steady screw away into the
// depth, a geometric push, never a brightness change. It is shaped through
// lucidBump, so the push eases in as the envelope falls instead of stepping.
// SND_TREBLE_PRESENCE tightens the creases' width, as in 2D. Brightness
// follows (0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)) gently.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uTime (already Speed-integrated) runs the screw and the nest drift;
// the rig carries the flight. Complexity: uMorph past 0.5 adds a nest level
// and an octave (lucidDetail). Chaos: lucidChaos raises warp strength, the
// strip's jitter and a wobble of the ridges along z; uTwist (in lucidView)
// winds the view. Kaleido: when uKaleido is on, uSymmetry (rounded to even)
// sets the arm count. Color: every colour is pal(). Energy: uIntensity scales
// the kick push. Space: uZoom/uRotation through lucidView.
//
// ---- knobs ------------------------------------------------------------------
//
// k0 PULSE the kick's push; k1 TEXTURE the nest's density across an arm;
// k2 FLOW the warp strength; k3 TONE the hue; k4 BUILD the crease glow;
// k5 SPACE the tube's radius. All are offsets around 0.5 and none multiplies
// a clock, so a knob move never jumps the picture.
//
// ---- why the distance estimate is safe --------------------------------------
//
// The wall is r = rw(angle, z), so f = rw - r is positive inside the tube. Its
// gradient has a radial part of exactly 1 and tangential parts bounded in
// closed form: the ridge profile x^2 (x = 1 - 2v) has slope at most 4 per unit
// v, v moves 1 / seg per radian, the helix adds LS_KZ per unit z, and the
// wobble adds its amplitude times its rates. Dividing f by that bound (taken
// at the innermost wall radius, where the per-arc slope is largest) is a
// first-order lower bound on the distance; it is capped at LS_CAP and the
// march steps at 0.8 of it. A sphere around the eye is carved out of
// everything, so the eye never starts inside a ridge even when the rig's path
// wanders off the axis.

#define LS_MAX_STEPS 96
#define LS_FAR 40.0
/** Even, so arm parity survives the angle's wrap. */
#define LS_ARMS 6.0
/** The eye's rebase: a power of two that divides the rig's 8192 wrap. */
#define LS_ZWRAP 1024.0
/** Helix wind: one turn of the staircase per 16 world units. */
#define LS_KZ (LUCID_TAU / 16.0)
/** Ridge wobble along z: one period per 8 world units. */
#define LS_KW (LUCID_TAU / 8.0)
/** The nest's strip scale along z. */
#define LS_NZ 0.35
/** Units before each rebase seam over which the nest crossfades into its copy one period back. */
#define LS_BLEND 64.0
#define LS_BUBBLE 0.7
#define LS_CAP 1.2
#define LS_FOG 0.075
/** Ridge depth: how far the crests stand in from the wall. */
#define LS_RIDGE 1.5

float gSeg;
float gPhase;
float gR;
float gWobA;
float gWobT;
/** Tangential slope bound per radian, and per unit z (see the header). */
float gGa;
float gGz;
vec3 gEye;

/** The style's world at p: its distance, and in `found` what is there (no slots). */
float mapAt(vec3 w, inout LucidHit found) {
    float r = length(w.xy);
    // atan(0, 0) is undefined in GLSL ES; the guard only matters on the axis,
    // deep in the open core where the wall is far.
    float ang = r > 2e-3 ? atan(w.y, w.x) : 0.0;
    // The Droste shear: the angle wound by depth. The 2*pi jump of atan is a
    // whole number of (even) arms, so neither v nor the parity seams.
    float a = ang + LS_KZ * w.z + gPhase;
    float v = abs(mod(a, gSeg) - 0.5 * gSeg) / gSeg;
    float x = 1.0 - 2.0 * v;
    float wob = gWobA * sin(LS_KW * w.z + 3.0 * ang + gWobT);
    float rw = gR - LS_RIDGE * x * x + wob;
    float rr = max(r, gR - LS_RIDGE - gWobA);
    float gt = gGa / rr;
    float g = sqrt(1.0 + gt * gt + gGz * gGz);
    float d = min((rw - r) / g, LS_CAP);
    return max(d, LS_BUBBLE - length(w - gEye));
}

/** The style's world at p as a hit, for lib_lucid_field. */
LucidHit lucidMap(vec3 p) {
    LucidHit h = lucidHit(0.0);
    float d = mapAt(p, h);
    h.d = d;
    return h;
}

//#include lib_lucid_field

/** The 2D style's field on the strip: smooth simplex warp, creases read ridged at the end. */
vec4 lsField(vec2 q, int depth, int oct, float strength, float t) {
    q += lucidJitter(q, t);
    // A ridged field fed back through the nest has unbounded slope at every
    // crease and turned the arms into static, so the nest warps with simplex.
    vec4 n = lucidNest(q, depth, strength, t * 0.2, LUCID_SIMPLEX, oct);
    n.z = lucidFbm(q + strength * n.xy + vec2(1.7, 9.2), LUCID_RIDGED, oct);
    return n;
}

void main() {
    LucidHit fld = lucidHit(0.0);
    vec2 uv = lucidView(false);
    float t = uTime;  // uTime already integrates Speed (ShaderScene::update)
    float treb = clamp(SND_TREBLE_PRESENCE, 0.0, 1.5);
    float swell = clamp((0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)), 0.0, 1.5);
    // The normalization shell (lib_lucid): every variable from its driver, mapped to its range at its one site.
    LucidIn N = lucidIn();
    float hue = lucidSpan(N.tone, 0.0, -0.15, 0.15);

    int depth = lucidDetail(2, 1);
    int oct = lucidDetail(3, 2);

    float arms = (uKaleido > 0.5 && uSymmetry >= 2.0) ? 2.0 * floor(uSymmetry * 0.5 + 0.5) : LS_ARMS;
    gSeg = LUCID_TAU / arms;
    float chaos = lucidChaos();
    // The kick's push, eased: 0 at the hit, peak mid-decay, gone as it dies.
    float push = 1.8 * clamp(uIntensity, 0.0, 2.0) * lucidBump(SND_KICK_ENV) * lucidSpan(N.emit, 1.0, 0.5, 1.5);
    // The steady screw away into the depth. The pattern repeats every 2*pi of
    // phase (arm parity included, the arm count being even), so the wrap of
    // the mod is exact and keeps the phase small for highp.
    gPhase = LS_KZ * push - mod(t * 0.15, LUCID_TAU);
    gR = lucidSpan(N.reach, 4.2, 3.5, 4.9);
    gWobA = 0.1 + 0.16 * (chaos - 1.0);
    gWobT = mod(t * 0.5, LUCID_TAU);
    float ridgeSlope = LS_RIDGE * 4.0 / gSeg;
    gGa = ridgeSlope + 3.0 * gWobA;
    gGz = ridgeSlope * LS_KZ + gWobA * LS_KW;

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every geometry z term tiles with a period dividing 16, so this rebase
    // is exact; the nest's crossfade below hides it for the colour too.
    ro.z = mod(ro.z, LS_ZWRAP);
    // Grow and layers (lib_grow) measure from the eye in the frame the world is read in.
    growEye(ro);
    gEye = ro;

    float tm = 0.03;
    float hitT = -1.0;
    for (int i = 0; i < LS_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (tm > LS_FAR) break;
        fld = lucidField(ro + rd * tm);
        float d = fld.d;
        float eps = 0.0008 * tm + 0.0005;
        if (d < eps) {
            hitT = tm;
            break;
        }
        tm += max(d * 0.8, eps);
    }

    // The centre is infinitely far down the staircase: the fog is near black.
    vec3 fogCol = pal(0.08 + hue) * 0.03;
    vec3 col = fogCol;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = lucidNormal(p, max(0.002, 0.0015 * hitT));
        float r = length(p.xy);
        float ang = r > 2e-3 ? atan(p.y, p.x) : 0.0;
        float a = ang + LS_KZ * p.z + gPhase;
        float arm = floor(a / gSeg);
        float v = abs(mod(a, gSeg) - 0.5 * gSeg) / gSeg;

        // The strip: along the helix (z) and across the arm (v), as the 2D
        // style's (u, v). Crossfaded into its copy one period back before the
        // rebase seam, so the noise is continuous across it.
        float zz = mod(p.z, LS_ZWRAP);
        vec2 q = vec2(zz * LS_NZ, v * lucidSpan(N.scale, 1.9, 1.3, 2.5));
        float strength = 0.9 * chaos * lucidSpan(N.flow, 1.0, 0.75, 1.25);
        vec4 f = lsField(q, depth, oct, strength, t);
        float s = smoothstep(LS_ZWRAP - LS_BLEND, LS_ZWRAP, zz);
        if (s > 0.0) {
            vec4 f2 = lsField(q - vec2(LS_ZWRAP * LS_NZ, 0.0), depth, oct, strength, t);
            // Renormalised so two independent fields averaged keep their contrast.
            f = (f * (1.0 - s) + f2 * s) / sqrt((1.0 - s) * (1.0 - s) + s * s);
        }

        // The 2D colour law.
        float ridge = 0.5 + 0.5 * f.z;
        float crease = smoothstep(0.55 - 0.1 * clamp(treb, 0.0, 1.0), 0.98, ridge);
        vec3 base = mix(pal(0.08 + hue) * 0.15, pal(0.62 + hue), smoothstep(0.2, 0.8, ridge));
        base = mix(base, pal(0.95 + hue) * 1.2, crease);
        // The fold's own crest line, lit like a crease.
        float crest = 1.0 - smoothstep(0.0, 0.05, v);
        base = mix(base, pal(0.95 + hue), 0.35 * crest);
        base = lucidChannelTurn(base, mod(arm, 2.0) + 0.4 * f.w);
        base = lucidCubic(base, 0.85);

        float occ = 0.0;
        float sc = 1.0;
        for (int k = 0; k < 2; k++) {
            float hk = 0.12 + 0.22 * float(k);
            occ += (hk - lucidField(p + n * hk).d) * sc;
            sc *= 0.6;
        }
        float ao = clamp(1.0 - 1.5 * occ, 0.3, 1.0);
        float dif = max(dot(n, -rd), 0.0);
        float spec = pow(max(dot(reflect(rd, n), -rd), 0.0), 24.0);
        vec3 lit = base * (0.45 + 0.65 * dif) * ao
                 + base * (crease + 0.5 * crest) * lucidSpan(N.glow, 0.4, 0.15, 0.65)
                 + pal(0.95 + hue) * spec * 0.2 * ao;
        col = mix(lit, fogCol, 1.0 - exp(-hitT * LS_FOG));
    }

    col *= 0.85 + 0.12 * swell;
    fragColor = vec4(grade(col), 1.0);
}
