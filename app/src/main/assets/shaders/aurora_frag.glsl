#version 300 es
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). uSoundTex is R32F; on GPUs honoring sampler precision
// (Mali) every read is clamped and quantized.
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

// Morph, Warp and Turbulence fold the curtains themselves (below), so view()
// must not bend the screen with them a second time.
#define VIEW_OWNS_SHAPE
//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_shell
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3

// Aurora: a flight through the auroral band itself. Four curtains hang in
// rows beside and over the flight line, each a glowing sheet that folds in
// long waves along its length, with a bright lower rim, vertical rays
// shimmering up through it and a soft tail fading into the dark. Below lies
// frozen snow that the curtains light in pools of their own colour, and stars
// sit above. The 2D style's identity carries over: noise curtains waving
// with the mids, shimmering with the treble, coloured by the palette from
// the rim upward, brightening with the level.
//
// The camera is the shared rig (camFly): the eye flies down +z between the
// curtains and, where the path wanders, straight through one. The curtains
// fade out within a couple of units of the eye, so passing through is a
// soft veil, never a white frame.
//
// Every z term (folds, rims, rays, snow dunes, snow sparkle cells) has a
// wavenumber that is a whole multiple of 2pi/64, so the world tiles along z
// with period 64. The eye is rebased by mod 64 (power of two, divides the
// rig's 8192 wrap), so the wrap is invisible and z keeps its precision.
//
// ---- audio ------------------------------------------------------------------
//
// Mids (SND_MID_ATT)        fold amplitude and sheet thickness: the curtains wave.
// Treble (SND_TREBLE_ATT)   ray contrast, and the ray drift runs on the treble
//                           clock (uSoundTimeSped.z), so bright music shimmers fast.
// Bass (sndShaped(SND_LOW)) the curtains reach lower toward the snow.
// Level (SND_VOL_ATT)       overall glow, as the 2D style did with SND_VOL.
// Energy (SND_ENERGY_TREND) the tails grow taller over a building passage.
// Pitch (SND_HELD_NOTE_CHANGE) slides the curtain hue.
// Knobs: k0 PULSE glow, k1 TEXTURE ray contrast, k2 FLOW fold amplitude,
// k3 TONE how high the curtains hang (a high melody lifts their rims up into
// the sky, a low one lets them down to graze the snow; and a hue offset),
// k4 BUILD tail height, k5 SPACE curtain spacing; each is read as
// its offset from 0.5, so with Note & key reactions off nothing changes.
// Every one of these, and the sheets' width (TEXTURE), the rims' wave (FLOW)
// and the ground haze (SPACE), is one normalized input (lib_shell) set in one
// block at the top of main and mapped there to its range; at rest each is the
// constant the band was tuned with. The distance to the nearest sheet is the
// field the grow hooks act on (SHELL_FIELD3), its glow carried beside it from
// the nearest copy: Spawn slides a second band of curtains down the flight,
// Fractal hangs curtains a third the size through the first, the layers open
// the near and far curtains. While a hook is on the march samples outside the
// curtains' slab too, where a copy may hang.
//
// ---- why the march is safe --------------------------------------------------
//
// This is an emission volume, not a surface. Each sheet is x = xs(y, z); the
// distance to it is bounded below by |x - xs| / sqrt(1 + L^2), L being the
// largest slope of xs, worked out per frame from the fold amplitudes and
// wavenumbers (gLn). The step is the larger of that sheet distance less the
// glow width and the distance to the vertical slab every curtain lives in,
// clamped to a minimum, so empty air is crossed fast and glow is sampled
// finely. A ray that leaves the slab heading away from it stops at once. The
// loop bound is a constant; the user's Detail (uSteps) is the budget. The
// snow is a flat plane hit analytically, its dunes only bend the normal.

#define AU_MAX_STEPS 80
#define AU_FAR 52.0
/** 2pi / 64: the z wavenumber unit that keeps the world periodic over the travel wrap. */
#define AU_KZ 0.09817477
#define AU_ZWRAP 64.0
/** Height of the curtains' lower rim at rest. */
#define AU_BASE 1.3
#define AU_GROUND -2.8

float gAmp;
float gMorphA;
float gTurb;
float gLn;
float gW;
float gH;
float gReach;
float gSpread;
float gRayC;
float gHue;
float gRayClock;
float gYLo;
float gYHi;
/** TONE: how far the curtains' rims are lifted (0 at rest). */
float gLift;
/** The rims' wave along each curtain (0.45 at rest: FLOW). */
float gRimA;
/** The glow of the sheets auSheets() last measured. */
vec3 gAuE;

/** The x of sheet `i` at height y, depth z: three folds along its length. */
float sheetX(int i, float y, float z) {
    float fi = float(i);
    float t = uTime;
    float c = (fi - 1.5) * gSpread;
    float ph = fi * 1.7;
    float f = 0.62 * sin(float(3 + i) * AU_KZ * z + t * 0.21 + ph)
            + 0.38 * sin(float(7 + 2 * i) * AU_KZ * z - t * 0.33 + 0.12 * y + ph * 2.3)
            + gMorphA * sin(float(13 + i) * AU_KZ * z + t * 0.47 + 0.2 * y + ph * 0.7);
    return c + gAmp * f;
}

/** The lower rim of sheet `i` at depth z. */
float sheetBase(int i, float z) {
    float fi = float(i);
    return AU_BASE + gRimA * sin(float(2 + i) * AU_KZ * z + uTime * 0.13 + fi * 2.1) - gReach + gLift;
}

/** Vertical rays along sheet `i`: 0..~1.6, mean ~1. */
float sheetRays(int i, float z) {
    float fi = float(i);
    float r = 0.5 * sin(float(29 + i) * AU_KZ * z + gRayClock * 1.1 + fi)
            + 0.3 * sin(float(53 + 2 * i) * AU_KZ * z - gRayClock * 1.7 + fi * 2.0)
            + 0.2 * sin(97.0 * AU_KZ * z + gRayClock * 2.3 + fi * 3.0)
            + gTurb * 0.35 * sin(151.0 * AU_KZ * z + uTime * 1.9 + fi * 5.0);
    float pat = clamp(0.5 + 0.5 * r, 0.0, 1.0);
    return mix(1.0, pat * pat * 2.2, gRayC);
}

/**
 * The four sheets at p: the distance to the nearest (the field the grow hooks act on, through
 * SHELL_FIELD3) and, in gAuE, the glow they give off there.
 */
float auSheets(vec3 p) {
    float dMin = 1e3;
    vec3 e = vec3(0.0);
    for (int k = 0; k < 4; k++) {
        float d = abs(p.x - sheetX(k, p.y, p.z)) / gLn;
        dMin = min(dMin, d);
        if (d < 3.0 * gW) {
            float v = p.y - sheetBase(k, p.z);
            float vu = max(v, 0.0);
            // A sharp rim at the bottom, then a long soft tail upward.
            float prof = smoothstep(-0.35, 0.05, v) * (exp(-vu / gH) + 0.6 * exp(-vu * 3.0));
            float g = exp(-(d * d) / (gW * gW));
            float dens = g * prof * sheetRays(k, p.z);
            float h = gHue + 0.11 * float(k) + 0.32 * clamp(vu / (gH * 2.0), 0.0, 1.0)
                    + 0.08 * sin(5.0 * AU_KZ * p.z + uTime * 0.05 + float(k));
            e += pal(h) * dens;
        }
    }
    gAuE = e;
    return dMin;
}

vec3 skyColour(vec3 rd) {
    float up = clamp(rd.y, -1.0, 1.0);
    vec3 col = mix(pal(gHue + 0.5) * 0.045, vec3(0.003, 0.005, 0.014), smoothstep(-0.05, 0.55, up));
    vec3 sp = rd * 150.0;
    float s = hashCell(ivec3(floor(sp)));
    float star = (1.0 - smoothstep(0.0, 0.3, length(fract(sp) - 0.5))) * step(0.996, s);
    col += vec3(0.75, 0.8, 0.9) * star * smoothstep(0.05, 0.3, up) * (0.6 + 0.4 * sin(uTime * 2.0 + s * 500.0));
    return col;
}

void main() {
    vec2 uv = view();

    float mid = sndLevel(SND_MID_ATT);
    float treb = sndLevel(SND_TREBLE_ATT);
    float level = sndLevel(SND_VOL_ATT);
    float build = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    float pitch = clamp(SND_HELD_NOTE_CHANGE, 0.0, 1.0);

    // ---- the shape inputs (lib_shell): each 0..1, 0.5 the tuned look --------------------
    float nPulse = shellKnob(0);                                   // PULSE    the glow
    float nTexture = shellKnob(1);                                 // TEXTURE  ray contrast, the sheets' width
    float nFlow = shellKnob(2);                                    // FLOW     the folds' and the rims' wave
    float nTone = shellKnob(3);                                    // TONE     how high the curtains hang, the hue
    float nBuild = shellKnob(4);                                   // BUILD    the tails' height
    float nSpace = shellKnob(5);                                   // SPACE    curtain spacing, the ground haze
    float nMorph = 0.5 + 0.5 * clamp(uMorph, 0.0, 1.0);            // Morph    the third fold
    float nWarp = 0.5 + 0.5 * clamp(uWarp, 0.0, 1.0);              // Warp     the folds' depth
    float nTurb = 0.5 + 0.5 * clamp(uTurbulence / 1.5, 0.0, 1.0);  // Turbulence the fine rays
    float gainK = shellMap(nPulse, 0.75, 1.0, 1.25);
    float rayTex = shellMap(nTexture, -0.3, 0.0, 0.3);
    float widthK = shellMap(nTexture, 0.2, 0.26, 0.34);
    float ampFlow = shellMap(nFlow, -0.4, 0.0, 0.4);
    gRimA = shellMap(nFlow, 0.3, 0.45, 0.6);
    float hueTone = shellMap(nTone, -0.25, 0.0, 0.25);
    gLift = shellMap(nTone, -1.3, 0.0, 1.3);
    float tailK = shellMap(nBuild, -0.8, 0.0, 0.8);
    gSpread = max(2.5, shellMap(nSpace, 3.6, 4.6, 5.6));
    float hazeK = shellMap(nSpace, 0.045, 0.035, 0.025);
    gMorphA = shellMap(nMorph, 0.12, 0.12, 0.42);
    float ampWarp = shellMap(nWarp, 0.0, 0.0, 0.8);
    gTurb = shellMap(nTurb, 0.0, 0.0, 1.5);

    gAmp = max(0.3, 1.0 + 0.9 * mid + ampWarp + ampFlow);
    // Largest slope of sheetX over any sheet (i = 3 has the highest wavenumbers).
    float lz = gAmp * AU_KZ * (0.62 * 6.0 + 0.38 * 13.0 + gMorphA * 16.0);
    float ly = gAmp * (0.38 * 0.12 + gMorphA * 0.2);
    gLn = sqrt(1.0 + lz * lz + ly * ly);
    gW = widthK + 0.14 * mid;
    gH = max(1.0, 2.6 + 0.9 * build + tailK);
    gReach = 0.8 * sndShaped(SND_LOW);
    gRayC = clamp(0.35 + 0.45 * treb + rayTex, 0.0, 1.0);
    gHue = 0.22 * pitch + hueTone;
    // Both clocks only ever advance, so a treble change speeds the shimmer
    // without jumping it.
    gRayClock = uSoundTimeSped.z * 0.6 + uTime * 0.15;
    gYLo = AU_BASE - gRimA - gReach - 0.4 + gLift;
    gYHi = AU_BASE + gRimA + 4.0 * gH + gLift;
    float gain = (0.45 + 0.9 * level) * gainK;

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    ro.z = mod(ro.z, AU_ZWRAP);
    // The layers are measured from the eye; a spawned band slides 4 down the flight.
    shellSetup3(ro, 3.0, 4.0, false);
    bool shellRest = shellIdle(SHELL_SPAWN, SHELL_FRACTAL, SHELL_FRONT, SHELL_BACK);

    float tGround = rd.y < -1e-3 ? (AU_GROUND - ro.y) / rd.y : 1e9;
    float tEnd = min(AU_FAR, tGround);

    // Per-pixel dither of the first sample, fixed in time, hides step banding.
    float t = 0.1 + 0.3 * hash13(vec3(gl_FragCoord.xy, 1.0));
    vec3 acc = vec3(0.0);
    for (int i = 0; i < AU_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (t > tEnd) break;
        vec3 p = ro + rd * t;
        // A hook that is on can hang a copy outside the slab, so the march then samples everywhere.
        if (shellRest && p.y < gYLo && rd.y <= 0.0) break;
        if (shellRest && p.y > gYHi && rd.y >= 0.0) break;
        float dSlab = shellRest ? max(gYLo - p.y, p.y - gYHi) : -1.0;
        // Outside the slab the step is the slab distance alone (dMin 0 never wins).
        float dMin = 0.0;
        vec3 e = vec3(0.0);
        if (dSlab <= 0.0) {
            dMin = SHELL_FIELD3(p, auSheets);
            e = gAuE;
        }
        // Fade near the eye so flying through a curtain is a veil, and haze far off.
        e *= smoothstep(0.4, 2.4, t) * exp(-t * 0.025);
        float dt = clamp(max(0.6 * (dMin - 2.5 * gW), dSlab), 0.07 + 0.012 * t, 1.6);
        acc += e * dt;
        t += dt;
    }

    vec3 sky = skyColour(rd);
    vec3 col = sky;
    if (tGround < AU_FAR) {
        vec3 gp = ro + rd * tGround;
        // Snow dunes in the normal only; the z wavenumbers are multiples of AU_KZ.
        float a1 = 0.9 * gp.x + 3.0 * AU_KZ * gp.z;
        float a2 = 0.37 * gp.x - 8.0 * AU_KZ * gp.z + 1.3;
        float dx = 0.12 * 0.9 * cos(a1) + 0.06 * 0.37 * cos(a2);
        float dz = 0.12 * 3.0 * AU_KZ * cos(a1) - 0.06 * 8.0 * AU_KZ * cos(a2);
        vec3 n = normalize(vec3(-dx, 1.0, -dz));
        // Pools of light under each curtain, in its colour.
        vec3 light = vec3(0.0);
        for (int k = 0; k < 4; k++) {
            float ox = gp.x - sheetX(k, AU_BASE, gp.z);
            light += pal(gHue + 0.11 * float(k)) * exp(-ox * ox * 0.05);
        }
        vec3 snow = vec3(0.55, 0.6, 0.72);
        float ndv = clamp(dot(n, -rd), 0.0, 1.0);
        float fres = pow(1.0 - ndv, 4.0);
        vec3 g = snow * light * (0.05 + 0.08 * n.y) * gain + sky * fres * 2.0;
        // Snow sparkle: sparse twinkling cells (z cells wrap at 256 = 64 * 4).
        vec2 sc = floor(vec2(gp.x, mod(gp.z, AU_ZWRAP)) * 4.0);
        float hs = hashCell(ivec3(int(sc.x), int(sc.y), 7));
        vec2 sf = fract(gp.xz * 4.0) - 0.5;
        float pt = 1.0 - smoothstep(0.0, 0.3, length(sf));
        float tw = 0.5 + 0.5 * sin(uTime * (5.0 + 9.0 * hs) + hs * 70.0);
        g += (light * 0.5 + 0.2) * step(0.975 - 0.02 * treb, hs) * pt * tw * (0.15 + 0.5 * treb) * exp(-tGround * 0.08);
        col = mix(g, sky, 1.0 - exp(-tGround * hazeK));
    }

    // Soft clip of the glow: edge-on folds saturate instead of blowing out.
    col += 1.0 - exp(-acc * 0.9 * gain);
    col *= 0.72 + 0.28 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
