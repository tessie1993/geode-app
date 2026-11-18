#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

// Lava spends Morph, Warp and Turbulence on its own wax (blend, wobble depth,
// wobble frequency), so view() must not bend the picture with them as well.
#define VIEW_OWNS_SHAPE
//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_shell
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3

// LAVA: the inside of an endless lava lamp, flown through. A molten pool lies
// on the floor, a cooler layer of wax hangs under the cap above, and between
// them blobs of wax rise and sink, one per 4 x 4 cell of the floor. The idea
// is the 2D style's, kept whole and given depth:
//
//  - Every blob owns a PERMANENT home: its cell, which it wanders around but
//    never leaves, so the lamp stays anchored while the eye moves through it.
//  - It rises and sinks on its own 18-40 s clock, cosine-eased so it detaches
//    from the pool and lands back in it gently, and stretches tall while it
//    moves, like real wax.
//  - It is born and dies IN PLACE through a smooth grow/dissolve envelope
//    (radius 0 -> full -> 0 over its 45-90 s life), so wax appears and goes
//    by morphing, never by popping.
//  - Blobs, pool and cap are joined by a smooth minimum, so a blob leaving the
//    pool pulls a neck of wax with it and two neighbours that meet merge.
//    The blend carries each blob's colour across the seam (sminMat), so the
//    merged wax is coloured as smoothly as it is shaped.
//  - The edges undulate like smoke underwater: a two-frequency wobble added
//    to each blob's distance.
//
// Light comes from the bulb under the pool: undersides and low wax glow hot,
// faces seen head-on show the translucent core, and the fog is the lamp's
// liquid, warmer looking down.
//
// The camera is the shared rig (camFly): the eye glides down +z between the
// pool and the cap (the rig's paths stay within |y| < 1.3; the pool is at
// -2.7, the cap at +3.0). The cell hash repeats every 16 cells (64 world
// units) along z, and the pool's and cap's z ripples have periods 16 and 32,
// so every z term divides 64, which divides the rig's 8192 travel wrap. The
// eye is rebased by 64, so the wrap and the rebase are both invisible.
//
// ---- audio ------------------------------------------------------------------
//
// Bass (SND_BASS_ATT)        the wax breathes: every blob swells up to +15 %
//                            and the pool's surface rises.
// Mid (SND_MID)              the blobs sway in their homes.
// Treble (SND_TREBLE_ATT)    the wobble's depth: it moves the OUTLINE, so a
//                            bright hi-hat makes the wax shiver without
//                            changing the frame's luminance.
// Volume (SND_VOL)           the body's brightness, 0.55 .. 1.
// Kick (SND_KICK_ENV)        lands on the RIM only: a fresnel contour on each
//                            blob, never the whole frame.
// Drop (SND_HELD_SURGE)      the lamp heats: the cores glow up on a hump, so
//                            it glides in and out.
// Pitch (SND_HELD_NOTE_CHANGE) slides the wax hue a little, as a hump.
// Knobs                      k0 PULSE the rim, k1 TEXTURE the wobble depth,
//                            k2 FLOW how far the blobs sway, k3 TONE where
//                            the wax lingers on its rise (a high melody holds
//                            the blobs up under the cap, a low one keeps them
//                            down by the pool; the travel itself is unchanged)
//                            and a small hue offset,
//                            k4 BUILD the core heat, k5 SPACE the fog (more
//                            space, thinner liquid). Each is read as an offset
//                            from 0.5, never as a clock rate.
// Morph widens the blend (more merging), Warp deepens the wobble, Turbulence
// raises its frequency.
// Every one of these, and the lamp's floor and cap (SPACE), the pool's ripple
// (FLOW) and how far rising wax stretches (BUILD), is one normalized input
// (lib_shell) set in one block at the top of main and mapped there to its
// range; at rest each is the constant the lamp was tuned with. The wax is the
// field the grow hooks act on (its shell wrapper): Spawn slides a second lamp's wax
// half a cell down the flight, Fractal grows blobs a third the size on the
// wax, the layers open the near and far lamp.
//
// ---- why the distance estimate is safe ------------------------------------
//
// A blob is a sphere with its y axis squashed by s <= 1 before the length,
// which makes it taller and keeps the field 1-Lipschitz. The wobble added to
// it has gradient at most amp * r * F * 1.06, so the joined field is divided
// by gLip = 1 + amp * LV_RMAX * F * 1.06 (the quadratic smin of L-Lipschitz
// fields is L-Lipschitz). A point evaluates its own cell and the three
// nearest neighbours (the 2 x 2 block it is closest to); every other blob's
// centre is at least one pitch (4) away on x or z, and a blob reaches at most
// 2.5 from its cell centre, so an unevaluated one is at least 1.5 away, less
// the blend's 0.7: capping the estimate at LV_CAP = 0.8 keeps every step
// short of it. The pool and the cap ripple with gradients under 0.2. A sphere
// around the eye is smoothly carved out of everything (smax can overestimate
// inside that cavity, hence the 0.85 step scale), so the eye is never in wax.

#define LV_MAX_STEPS 96
#define LV_FAR 30.0
#define LV_TAU 6.2831853
/** Cell pitch on x and z, world units. */
#define LV_P 4.0
/** Cells along z before the hash repeats. */
#define LV_ZCELLS 16.0
/** The z rebase: LV_P * LV_ZCELLS, a power of two dividing 8192. */
#define LV_ZWRAP 64.0
/** The pool's and the cap's rest heights at rest (gFloor, gCeil: SPACE). */
#define LV_FLOOR -2.7
#define LV_CEIL 3.0
/** The largest blob radius: 0.95 at full bass swell, 1.15x. */
#define LV_RMAX 1.1
/** The cap on the estimate for blobs outside the 2 x 2 block (see the header). */
#define LV_CAP 0.8
#define LV_BUBBLE 0.9

/** Lava's own slow clock (the 2D style's t). */
float gT;
float gBass;
float gMid;
float gAmp;
float gFreq;
float gBlend;
float gSway;
float gLip;
/** TONE's bias of the rise curve: an exponent, 1 at rest. */
float gLinger;
/** The eye, z rebased into 0..64. */
vec3 gEye;
/** The lamp's floor and cap, the pool's ripple and the stretch of moving wax (see main's inputs). */
float gFloor;
float gCeil;
float gRipple;
float gStretch;
/** The hue of the wax lavaField found, for map(). */
float gLvHue;

/** One blob's (distance, hue): `cell` is the integer cell on x and z. */
vec2 blob(vec3 p, vec2 cell) {
    float cz = mod(cell.y, LV_ZCELLS);
    uint h = uhash3(uvec3(ivec3(int(cell.x), 7, int(cz))));
    float h0 = uhashUnit(h);
    uint hb = uhash(h);
    float h1 = uhashUnit(hb);
    uint hc = uhash(hb);
    float h2 = uhashUnit(hc);
    uint hd = uhash(hc);
    float h3 = uhashUnit(hd);
    float h4 = uhashUnit(uhash(hd));

    // Own slow clock: one full rise-and-sink takes 18-40 s, cosine eased.
    float period = mix(18.0, 40.0, h0);
    float u = fract(gT / period + h1 * 1.7);
    float yc = (gFloor - 0.35) + (gCeil - gFloor + 0.1) * pow(0.5 - 0.5 * cos(LV_TAU * u), gLinger);

    // Life envelope: dissolves to nothing and regrows in its own home.
    float lifePeriod = mix(45.0, 90.0, h2);
    float lp = fract(gT / lifePeriod + h0 * 2.3);
    float life = smoothstep(0.0, 0.14, lp) * (1.0 - smoothstep(0.82, 1.0, lp));
    float r = mix(0.55, 0.95, h3) * life * (1.0 + 0.10 * gBass);

    // PERMANENT home: the cell centre plus a fixed jitter; it wanders around
    // it (sway and mid) but never relocates.
    vec2 home = (cell + 0.5) * LV_P + (vec2(h1, h4) - 0.5) * 0.7;
    vec2 c = home
           + gSway * vec2(sin(gT * (0.15 + 0.25 * h0) + h0 * 31.0), cos(gT * (0.12 + 0.2 * h4) + h4 * 17.0))
           + vec2(gMid * 0.15 * sin(gT * 0.8 + h0 * 9.0), 0.0);

    vec3 q = vec3(p.x - c.x, p.y - yc, p.z - c.y);
    // Rising and sinking blobs stretch vertically like real wax.
    float s = mix(0.95, gStretch, abs(sin(LV_TAU * u)));
    float d = length(vec3(q.x, q.y * s, q.z)) - r;
    // Smoke-like surface, scaled by the radius so a dying blob calms with it.
    float w = 0.6 * sin(gFreq * q.y + gT * 0.9 + h0 * 13.0)
            + 0.4 * sin(gFreq * 0.8 * (q.x - q.z) - gT * 0.6 + h4 * 7.0);
    d += gAmp * r * w;
    return vec2(d, 0.15 + 0.7 * h3);
}

/** The wax, pool and cap (its hue left in gLvHue): the field the grow hooks act on (map() hands it to its shell wrapper). */
float lavaField(vec3 p) {
    // The molten pool every blob is born from and returns to, breathing.
    float pool = gFloor + 0.1 * gBass
               + gRipple * sin(p.x * 0.7 + gT * 0.5) * sin(p.z * (LV_TAU / 16.0) + gT * 0.23);
    vec2 res = vec2(p.y - pool, 0.08);

    vec2 cell = floor(p.xz / LV_P);
    vec2 f = p.xz / LV_P - cell - 0.5;
    vec2 sd = vec2(f.x < 0.0 ? -1.0 : 1.0, f.y < 0.0 ? -1.0 : 1.0);
    vec2 b = blob(p, cell);
    vec2 m = sminMat(res.x, b.x, gBlend);
    res = vec2(m.x, mix(res.y, b.y, m.y));
    b = blob(p, cell + vec2(sd.x, 0.0));
    m = sminMat(res.x, b.x, gBlend);
    res = vec2(m.x, mix(res.y, b.y, m.y));
    b = blob(p, cell + vec2(0.0, sd.y));
    m = sminMat(res.x, b.x, gBlend);
    res = vec2(m.x, mix(res.y, b.y, m.y));
    b = blob(p, cell + sd);
    m = sminMat(res.x, b.x, gBlend);
    res = vec2(m.x, mix(res.y, b.y, m.y));

    // The cooler wax gathered under the cap.
    float cap = gCeil - 0.14 * sin(p.x * 0.6 - gT * 0.3) * cos(p.z * (LV_TAU / 32.0) + gT * 0.17);
    m = sminMat(res.x, cap - p.y, gBlend);
    res = vec2(m.x, mix(res.y, 0.62, m.y));

    gLvHue = res.y;
    return min(res.x / gLip, LV_CAP);
}

/** lavaField through lib_shell's hooks: exactly lavaField(p) at rest, and lavaField read once, in a loop the compiler cannot unroll. */
float lavaFieldGrown(vec3 p) {
    shellStart3(p, SHELL_SPAWN, SHELL_FRACTAL, SHELL_FRONT, SHELL_BACK);
    float r = 0.0;
    for (int i = SHELL_FROM; i < 7; i++) {
        if (shellPass3(i)) r = shellKeep3(i, lavaField(shellAt3(i)));
    }
    return r;
}

/** (distance, hue) of the whole lamp. */
vec2 map(vec3 p) {
    float d = lavaFieldGrown(p);
    d = smax(d, LV_BUBBLE - length(p - gEye), 0.3);
    return vec2(d, gLvHue);
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps).x + k.yyx * map(p + k.yyx * eps).x
                   + k.yxy * map(p + k.yxy * eps).x + k.xxx * map(p + k.xxx * eps).x);
}

void main() {
    vec2 uv = view();

    // ---- the shape inputs (lib_shell): each 0..1, 0.5 the tuned look --------------------
    float nPulse = shellKnob(0);                                   // PULSE    the rim
    float nTexture = shellKnob(1);                                 // TEXTURE  the wobble's depth
    float nFlow = shellKnob(2);                                    // FLOW     the sway, the pool's ripple
    float nTone = shellKnob(3);                                    // TONE     where the wax lingers, the hue
    float nBuild = shellKnob(4);                                   // BUILD    the core heat, the stretch
    float nSpace = shellKnob(5);                                   // SPACE    the fog, the lamp's height
    float nMorph = 0.5 + 0.5 * clamp(uMorph, 0.0, 1.0);            // Morph    the blend
    float nWarp = 0.5 + 0.5 * clamp(uWarp, 0.0, 1.0);              // Warp     the wobble's depth
    float nTurb = 0.5 + 0.5 * clamp(uTurbulence, 0.0, 1.0);        // Turbulence the wobble's frequency
    float rimK = shellMap(nPulse, 0.5, 1.0, 1.5);
    float ampTex = shellMap(nTexture, -0.06, 0.0, 0.06);
    gSway = shellMap(nFlow, 0.11, 0.22, 0.33);
    // The pool's gradient stays under 0.2 (0.24 x 0.7).
    gRipple = shellMap(nFlow, 0.12, 0.18, 0.24);
    gLinger = exp2(shellMap(nTone, 1.2, 0.0, -1.2));
    float hueTone = shellMap(nTone, -0.05, 0.0, 0.05);
    float coreK = shellMap(nBuild, 0.6, 1.0, 1.4);
    // At most 1 (a squash of y), so the blob stays 1-Lipschitz.
    gStretch = shellMap(nBuild, 0.75, 0.62, 0.5);
    float fogK = shellMap(nSpace, 0.078, 0.06, 0.042);
    gFloor = shellMap(nSpace, -2.4, LV_FLOOR, -3.1);
    gCeil = shellMap(nSpace, 2.7, LV_CEIL, 3.4);
    gBlend = shellMap(nMorph, 0.35, 0.35, 0.7);
    float ampWarp = shellMap(nWarp, 0.0, 0.0, 0.08);
    gFreq = shellMap(nTurb, 3.5, 3.5, 5.0);

    gT = uTime * 0.55;
    gBass = sndLevel(SND_BASS_ATT);
    gMid = sndLevel(SND_MID);
    float treble = sndLevel(SND_TREBLE_ATT);
    float vol = clamp(sndLevel(SND_VOL), 0.0, 1.0);
    float kick = clamp(SND_KICK_ENV, 0.0, 1.0);
    float drop = clamp(SND_HELD_SURGE, 0.0, 1.0);
    float pitch = clamp(SND_HELD_NOTE_CHANGE, 0.0, 1.0);
    float dropBump = 4.0 * drop * (1.0 - drop);
    float pitchBump = 4.0 * pitch * (1.0 - pitch);

    gAmp = clamp(0.10 + 0.06 * treble + ampWarp + ampTex, 0.0, 0.32);
    gLip = 1.0 + gAmp * LV_RMAX * gFreq * 1.06;

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term tiles with a period dividing 64 (see the header), so this
    // rebase is exact and invisible, and z keeps its precision far down the
    // travel.
    ro.z = mod(ro.z, LV_ZWRAP);
    gEye = ro;
    // The layers are measured from the eye; a spawned lamp's wax slides half a cell down the flight.
    shellSetup3(ro, 3.0, 0.5 * LV_P, false);

    float t = 0.03;
    float hitT = -1.0;
    float used = 0.0;
    for (int i = 0; i < LV_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (t > LV_FAR) break;
        used = float(i);
        float d = map(ro + rd * t).x;
        float eps = 0.0008 * t + 0.0008;
        if (d < eps) {
            hitT = t;
            break;
        }
        t += max(d * 0.85, eps);
    }

    float hueShift = hueTone + 0.12 * pitchBump;
    // The lamp's liquid: faint glass ambience, warmer looking down at the bulb.
    float down = clamp(-rd.y, 0.0, 1.0);
    vec3 fogCol = pal(0.85 + hueShift) * 0.045 * (1.2 + 0.6 * rd.y)
                + pal(0.08 + hueShift) * (0.03 + 0.10 * down * down);
    vec3 col = fogCol;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, max(0.0015, 0.0012 * hitT));
        float hue = map(p).y + hueShift;
        float ndv = clamp(dot(n, -rd), 0.0, 1.0);
        float fres = pow(1.0 - ndv, 3.0);
        float ao = clamp(1.0 - used / max(uSteps, 1.0) * 1.1, 0.3, 1.0);
        // Hot near the bulb, cool under the cap.
        float heat = 1.0 - smoothstep(gFloor, gCeil, p.y);
        // Wrapped light from the bulb below, plus a dim key for the form.
        float under = 0.5 - 0.5 * n.y;
        float key = max(dot(n, normalize(vec3(-0.4, 0.7, -0.5))), 0.0);
        float spec = pow(max(dot(reflect(rd, n), normalize(vec3(0.0, -0.8, 0.6))), 0.0), 24.0);

        vec3 body = pal(hue) * (0.10 + 0.55 * under * (0.35 + 0.65 * heat) + 0.12 * key) * ao
                  * (0.55 + 0.45 * vol);
        // The translucent core: strongest face-on, where the wax is thickest
        // to the eye, and hotter low in the lamp and on a drop.
        vec3 core = pal(hue + 0.13) * pow(ndv, 2.0) * (0.15 + 0.4 * heat + 0.3 * dropBump)
                  * coreK;
        // The beat on the rim only: a thin fresnel contour.
        vec3 rim = vec3(1.0, 0.92, 0.75) * fres * (0.08 + 0.30 * kick) * rimK;
        col = body + core + rim + pal(hue + 0.05) * spec * 0.2 * heat;
        col = mix(col, fogCol, 1.0 - exp(-hitT * fogK));
    }

    col *= 0.75 + 0.3 * (1.0 - smoothstep(0.5, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
