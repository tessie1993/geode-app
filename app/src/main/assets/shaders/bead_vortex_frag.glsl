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
//#include lib_touch
//#include lib_shell

// Bead Vortex: a flight down a tube whose walls are strands of segmented
// beads - crimson, chartreuse and violet - winding round the bore toward a
// white-hot core at the far end, through a purple haze of drifting motes.
// It was a flat 1/r tunnel mapping with a faked tube normal; now every bead is
// a real sphere on a real helix, lit by the core it winds toward.
//
// WHAT IS DRAWN. Two woven layers of helical strands about the bore. The inner
// layer (BV_N1 strands at radius BV_R1) is fatter and brighter and winds one
// way; the outer one (BV_N2 at BV_R2) is thinner, dimmer and winds the other,
// so the wall reads as woven rather than tiled. Each strand is a chain of
// spheres on a thin cord, so the necks between beads are real and shadowed.
// Behind both is a dark membrane (BV_R3), the gap colour the old style showed
// between the strands. The beads stream along their strands into the core.
//
// THE CAMERA is the shared rig (camFly), flown down +z. The bore's centre is
// the eye's own xy at the eye and eases to the world axis down the tunnel (as
// Fractal Tunnel does), so the rig's sideways wander bends the tube toward it
// instead of flying into the beads. Every z term tiles with period BV_ZWRAP
// (32, divides the rig's 8192 wrap): each layer winds a whole number of turns
// per 32, the beads sit every BV_PZ (128 per 32), the ripples, haze rings and
// mote slices all divide 32. So ro.z is rebased mod 32 exactly and invisibly.
// The style catalogue spins it (rate 1.4): that roll arrives through the rig.
//
// ---- audio: one sound, one property -----------------------------------------
//
// Flow (uFlowPhase) and the bass clock (uSoundTimeSped.x) stream the beads
//                      along their strands into the core. The weave turns on
//                      time alone (owner 2026-11-12, no style spins to the music).
// Kick (SND_KICK_ENV)  a swell runs down the tunnel: the beads it passes grow
//                      and glow, and the ring recedes as the envelope falls.
// Snare (SND_SNARE_ENV) the outer layer's rims light in the contrasting hue.
// Hat (SND_HAT_ENV)    the motes in the haze sparkle.
// Drop (SND_HELD_SURGE) the tube opens wide and the core flares, on a hump.
// Pitch (SND_HELD_NOTE_CHANGE) slides the bead hues, as a hump.
// SND_ENERGY_TREND     overall brightness, gently.
// Touch                swells the beads under the finger, as before.
//
// Params: Morph thickens the cord between beads until the chain is a smooth
// tube; Warp sways the bore in a slow helix; Turbulence ripples each strand's
// radius; Intensity scales everything that glows. At their defaults (0, 0, 0,
// 1) the look is the reference one.
//
// Knobs: k0 PULSE the kick swell, k1 TEXTURE the kernel ridges on each bead
// (a cob of corn, as the flat style faked), k2 FLOW the haze density, k3 TONE
// the depth between the two bead layers (a high melody pulls the inner layer
// in and pushes the outer one out, so the vortex opens into two shells you
// see between; a low one presses them into one dense wall; plus a small hue
// offset; the outer layer stays clear of the wall), k4 BUILD the core and
// halo, k5 SPACE the tube's width.
//
// ---- why the estimate is safe -----------------------------------------------
//
// A layer is measured in its own unrolled frame: arc round the bore, radius,
// and z, sheared so each strand is a straight line. Only the nearest strand
// and its nearest bead are measured, and the result is clamped to
// (spacing - |offset|) - cap, the least distance any neighbour could be,
// where cap = 0.46 of the smaller spacing bounds every bead and cord radius.
// The arc is taken at sqrt(r * R), within half a percent of the true chord.
// The bore bend, the Warp sway and the Turbulence ripple are not isometries
// but each moves the field by well under 0.15 per world unit, and the strands'
// own curvature is small at these radii; the march takes BV_STEP of the
// estimate to cover all of it. A sphere around the eye is carved out of
// everything, so the eye is never inside a bead.

#define BV_TAU 6.2831853
#define BV_MAX_STEPS 96
#define BV_FAR 28.0
#define BV_STEP 0.75
/** The world repeats along z every this many units (divides 8192). */
#define BV_ZWRAP 32.0
/** Bead spacing along z: 128 beads per period. */
#define BV_PZ 0.25
#define BV_BEADS_PER_WRAP 128.0
#define BV_N1 18.0
#define BV_N2 26.0
#define BV_R1 1.6
#define BV_R2 2.15
#define BV_R3 2.7
/** Whole turns per BV_ZWRAP of each layer; opposite signs weave them. */
#define BV_T1 3.0
#define BV_T2 -2.0
#define BV_RB1 0.13
#define BV_RB2 0.10
#define BV_BEND_REACH 9.0
#define BV_BUBBLE 0.45

// ---- per-frame state, written in main(), read by map() ----------------------

vec3 gEye;
float gScale;
float gSpin;
float gFlow;
float gCord;
float gSway;
float gRipple;
float gRingZ;
float gRingAmt;
float gTouch;
/** k3 TONE: the inner and outer layers' radius scales (1, 1 at rest). */
vec2 gSpread = vec2(1.0);

// ---- reported back by map(): capture before the normal taps overwrite them --

/** 0 inner layer, 1 outer layer, 2 membrane. */
float gMat;
/** Strand id (mod its count) of the winning layer. */
float gStrand;
/** Phase within the bead, -0.5..0.5: 0 at the bead's widest. */
float gBeadPhase;
/** Angle about the strand axis, for the kernel ridges. */
float gAround;

/** The bore's centre at depth z: the eye's xy at the eye, the world axis far away, plus Warp's sway. */
vec2 boreCentre(float z) {
    float dz = (z - gEye.z) / BV_BEND_REACH;
    float w = z * (BV_TAU / BV_ZWRAP);
    return gEye.xy / (1.0 + dz * dz) + gSway * vec2(sin(w), cos(w));
}

/**
 * One woven layer: `n` strands at radius `r` winding `turns` whole turns per
 * BV_ZWRAP, beads of base radius `rb`. Writes the winner's details into the
 * gStrand / gBeadPhase / gAround outputs only when `take` beats `best`.
 */
float strandLayer(vec3 p, float rr, float a, float r, float n, float turns, float rb, float seed, float best, float matId) {
    float k = turns * (BV_TAU / BV_ZWRAP);
    float kR = k * r;
    float l = sqrt(1.0 + kR * kR);
    // Strand coordinate round the bore, sheared by the winding. A 32-unit rebase
    // shifts it by n * turns, a whole number of strands.
    float u = (a - k * p.z - gSpin * sign(turns)) * n / BV_TAU;
    float id = floor(u + 0.5);
    float arc = (u - id) * (BV_TAU / n) * sqrt(rr * r);
    float sp = (BV_TAU * r / n) / l;
    float pa = BV_PZ * l;
    float cap = 0.46 * min(sp, pa);
    float perp = arc / l;
    // Bead coordinate along the strand; a rebase shifts it by 128 beads.
    float b = p.z / BV_PZ + arc * kR / (l * l * BV_PZ) - gFlow;
    float bi = floor(b + 0.5);
    float al = (b - bi) * pa;
    float idm = mod(id, n);
    float hs = uhashUnit(uhash3(uvec3(uint(idm), 7u, uint(seed))));
    float hb = uhashUnit(uhash3(uvec3(uint(idm), uint(mod(bi, BV_BEADS_PER_WRAP)), uint(seed) + 11u)));
    float rz = (p.z - gRingZ) / 1.6;
    float ring = gRingAmt * exp(-rz * rz);
    float rad = rb * gScale * (0.85 + 0.3 * hs) * (0.9 + 0.2 * hb) * (1.0 + 0.3 * ring) * (1.0 + 0.15 * gTouch);
    rad = min(rad, cap);
    float radial = rr - r - gRipple * sin(p.z * (BV_TAU * 4.0 / BV_ZWRAP) + idm * 1.7);
    float dBead = length(vec3(al, perp, radial)) - rad;
    float dCord = length(vec2(perp, radial)) - rad * gCord;
    float d = smin(dBead, dCord, 0.03 * gScale);
    // No neighbour strand or bead can be nearer than this.
    d = min(d, min(sp - abs(perp), pa - abs(al)) - cap);
    if (d < best) {
        gMat = matId;
        gStrand = idm;
        gBeadPhase = b - bi;
        gAround = atan(radial, perp + 1e-6);
    }
    return d;
}

float bvField(vec3 p) {
    vec2 rel = p.xy - boreCentre(p.z);
    float rr = max(length(rel), 1e-4);
    float a = atan(rel.y, rel.x);
    float d = BV_R3 * gScale - rr;
    gMat = 2.0;
    gStrand = 0.0;
    gBeadPhase = 0.0;
    gAround = 0.0;
    d = min(d, strandLayer(p, rr, a, BV_R2 * gScale * gSpread.y, BV_N2, BV_T2, BV_RB2, 2.0, d, 1.0));
    d = min(d, strandLayer(p, rr, a, BV_R1 * gScale * gSpread.x, BV_N1, BV_T1, BV_RB1, 1.0, d, 0.0));
    d = max(d, BV_BUBBLE - length(p - gEye));
    return d;
}

/** bvField through lib_shell's hooks: exactly bvField(p) at rest, and bvField read once, in a loop the compiler cannot unroll. */
float bvFieldGrown(vec3 p) {
    shellStart3(p, SHELL_SPAWN, SHELL_FRACTAL, SHELL_FRONT, SHELL_BACK);
    float r = 0.0;
    for (int i = SHELL_FROM; i < 7; i++) {
        if (shellPass3(i)) r = shellKeep3(i, bvField(shellAt3(i)));
    }
    return r;
}

/**
 * The field every march, normal and occlusion tap reads: bvField, the style's
 * own, through lib_shell's hook (the identity until the shell drives it).
 */
float map(vec3 p) {
    return bvFieldGrown(p);
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

/** Crimson, olive-chartreuse or violet, toned toward the shade they sit in (the flat style's three). */
vec3 beadColour(float pick, float hue) {
    if (pick < 1.0) return pal(hue) * vec3(0.85, 0.45, 0.5);
    if (pick < 2.0) return pal(hue + 0.75) * vec3(0.85, 0.8, 0.35);
    return pal(hue + 0.20) * 0.9;
}

void main() {
    // Advected before anything is measured off it, so the vortex leans the way
    // the last spike aimed, as the flat style did.
    vec2 uv = fluidWarp(view(), 1.0, 0.04);

    float swell = clamp(SND_ENERGY_TREND * uSoundDrive, 0.0, 1.5);
    float kick = clamp(SND_KICK_ENV * uSoundDrive, 0.0, 1.0);
    float snare = clamp(SND_SNARE_ENV * uSoundDrive, 0.0, 1.0);
    float hat = clamp(SND_HAT_ENV * uSoundDrive, 0.0, 1.0);
    float drop = clamp(SND_HELD_SURGE, 0.0, 1.0);
    float pitch = clamp(SND_HELD_NOTE_CHANGE, 0.0, 1.0);
    float dropBump = 4.0 * drop * (1.0 - drop);
    float pitchBump = 4.0 * pitch * (1.0 - pitch);
    float glow = max(uIntensity, 0.0);

    // ---- the shell: this style's shape as normalized inputs ---------------
    //
    // Every tunable of the shape is a 0..1 input here, set from the params and
    // the six knobs and 0.5 at their defaults, and mapped to its range in this
    // one place: lib_shell's shellMap(n, lo, rest, hi) puts 0.5 exactly on the
    // value the style was tuned at, so the default look is unchanged, and the
    // shell's hooks can later fractal out, move and manipulate any of them.
    float nRing = clamp(uKnob[0], 0.0, 1.0);
    float nCob = clamp(uKnob[1], 0.0, 1.0);
    float nHaze = clamp(uKnob[2], 0.0, 1.0);
    float nLayers = clamp(uKnob[3], 0.0, 1.0);
    float nCore = clamp(uKnob[4], 0.0, 1.0);
    float nTube = clamp(uKnob[5], 0.0, 1.0);
    float nCord = 0.5 + 0.5 * clamp(uMorph, 0.0, 1.0);
    float nSway = 0.5 + 0.5 * clamp(uWarp, 0.0, 1.0);
    float nRipple = 0.5 + 0.5 * clamp(uTurbulence, 0.0, 1.0);
    // The kick ring's depth, the kernel ridges' depth, the haze, the inner and
    // outer layers' radii, the core's light, and the tube's width.
    float ringK = shellMap(nRing, 0.0, 1.0, 2.0);
    float cobDepth = shellMap(nCob, 0.0, 0.35, 0.7);
    float fogK = shellMap(nHaze, 0.045, 0.075, 0.105);
    float build = shellMap(nCore, 0.5, 1.0, 1.5);
    gScale = shellMap(nTube, 0.85, 1.0, 1.15) * (1.0 + 0.25 * dropBump);
    gSpread = vec2(shellMap(nLayers, 1.3, 1.0, 0.7), shellMap(nLayers, 0.85, 1.0, 1.15));
    // The cord between the beads (Morph), the strands' sway (Warp), the ripple (Turbulence).
    gCord = shellMap(nCord, 0.32, 0.32, 1.0);
    gSway = shellMap(nSway, 0.0, 0.0, 0.5);
    gRipple = shellMap(nRipple, 0.0, 0.0, 0.1);

    // The beads stream on integrated clocks, so a change of rate never slides
    // them; the weave's turn is uTime's alone, which follows Speed and never
    // the music (it used to ride the loudness clock too).
    float travel = uFlowPhase * 4.0 + uTime * 0.9;
    // Time alone, at the rate the loudness clock (uFlowPhase) used to add at a typical level.
    gSpin = uTime * 0.252;
    gFlow = travel * 1.2 + uSoundTimeSped.x * 0.8;
    gTouch = touchFalloff(uv, 0.5);
    gRingAmt = kick * ringK;

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term tiles with a period dividing 32 (see the header), so this
    // rebase is exact, and z keeps its precision far down the 8192 travel.
    ro.z = mod(ro.z, BV_ZWRAP);
    gEye = ro;
    // The grow hooks (lib_shell) open round this eye, in the rebased frame the world is read in.
    shellSetup3(ro, 3.0, 1.0, false);
    // The swell leaves just ahead of the eye and recedes as the envelope falls.
    gRingZ = ro.z + 2.0 + 16.0 * (1.0 - kick);

    float t = 0.03;
    float hitT = -1.0;
    float used = 0.0;
    float hitMat = 2.0;
    float hitStrand = 0.0;
    float hitPhase = 0.0;
    float hitAround = 0.0;
    for (int i = 0; i < BV_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (t > BV_FAR) break;
        used = float(i);
        float d = map(ro + rd * t);
        float eps = 0.0008 * t + 0.0005;
        if (d < eps) {
            hitT = t;
            hitMat = gMat;
            hitStrand = gStrand;
            hitPhase = gBeadPhase;
            hitAround = gAround;
            break;
        }
        t += max(d * BV_STEP, eps);
    }

    float hue = 0.12 * pitchBump + shellMap(nLayers, -0.15, 0.0, 0.15) + 0.25 * dropBump;
    vec3 coreDir = normalize(vec3(-ro.xy, 40.0));
    float ahead = max(dot(rd, coreDir), 0.0);
    float sceneT = hitT > 0.0 ? hitT : BV_FAR;

    // The haze: purple, ringed in depth like the flat style's, brightest down the bore.
    float zFar = ro.z + rd.z * sceneT;
    float rings = 0.5 + 0.5 * sin(zFar * (BV_TAU * 6.0 / BV_ZWRAP) - travel * 3.0);
    vec3 haze = (pal(0.24) * (0.24 + 0.28 * rings) + pal(0.36) * 0.2) * (0.55 + 0.6 * pow(ahead, 6.0));
    vec3 gapCol = vec3(0.01, 0.0, 0.02);
    vec3 col = haze;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, max(0.0015, 0.0012 * hitT));
        float ndv = clamp(dot(n, -rd), 0.0, 1.0);
        float fres = pow(1.0 - ndv, 3.0);
        float ao = clamp(1.0 - used / max(uSteps, 1.0) * 1.2, 0.25, 1.0);
        // Lit from the core: a point on the axis ahead, as the reference is.
        vec3 lc = normalize(vec3(boreCentre(p.z + 10.0) - p.xy, 10.0));
        float diff = max(dot(n, lc), 0.0);
        float spec = pow(max(dot(reflect(rd, n), lc), 0.0), 18.0);

        if (hitMat < 1.5) {
            uint hp = uhash3(uvec3(uint(hitStrand), 3u, uint(hitMat) + 5u));
            vec3 base = beadColour(floor(uhashUnit(hp) * 3.0), hue);
            // Kernel ridges round each bead, like a cob; the flat style's fake, now on a real bead.
            float cob = 1.0 - cobDepth * (0.5 + 0.5 * sin(hitAround * 9.0)) * (1.0 - abs(hitPhase) * 2.0);
            float bulge = 0.5 + 0.5 * cos(BV_TAU * hitPhase);
            vec3 c = base * (0.15 + 0.8 * diff) * cob;
            c += vec3(0.5) * spec * 0.35;
            c *= (0.45 + 0.55 * bulge) * ao;  // dark necks between beads
            if (hitMat > 0.5) {
                c *= 0.6;
                c += pal(hue + 0.5) * snare * fres * 0.9 * glow;
            }
            float rz = (p.z - gRingZ) / 1.6;
            float ring = gRingAmt * exp(-rz * rz);
            c += pal(hue + 0.08) * ring * 0.45 * glow * bulge;
            col = c;
        } else {
            col = mix(gapCol, haze * 0.35, 0.3 + 0.2 * rings) * (0.4 + 0.6 * diff);
        }
        col = mix(col, haze, 1.0 - exp(-hitT * fogK));
    } else {
        col = mix(gapCol, haze, 0.85);
    }

    // Motes: points on z slices every 2 units, one hashed candidate per 0.6-unit
    // cell, inside the inner layer. Slices sit at fixed world z (index wrapped
    // by 32), so they hold still under the rebase and parallax is true.
    if (rd.z > 0.08) {
        float base = floor(ro.z * 0.5) + 1.0;
        float pxk = 2.0 / (max(uCamFocal, 0.1) * uResolution.y);
        for (int i = 0; i < 6; i++) {
            float zi = (base + float(i)) * 2.0;
            float ti = (zi - ro.z) / rd.z;
            if (ti < 0.5 || ti > sceneT) continue;
            vec2 xy = ro.xy + rd.xy * ti;
            if (length(xy - boreCentre(zi)) > BV_R1 * gScale * gSpread.x - 0.25) continue;
            vec2 cell = floor(xy / 0.6);
            int slice = int(mod(zi, BV_ZWRAP) * 0.5 + 0.5);
            float h = hashCell(ivec3(int(cell.x), int(cell.y), slice));
            if (h < 0.55) continue;
            float h2 = hashCell(ivec3(int(cell.x), int(cell.y), slice + 97));
            vec2 c = (cell + 0.5 + (vec2(h, h2) - 0.5) * 0.6) * 0.6
                   + 0.08 * vec2(sin(uTime * 0.4 + h * 30.0), cos(uTime * 0.33 + h2 * 30.0));
            float r = 0.025 + ti * pxk;
            float m = (1.0 - smoothstep(0.0, r, length(xy - c))) * min(1.0, 0.025 / r);
            float tw = 0.5 + 0.5 * sin(uTime * (7.0 + 11.0 * h2) + h * 50.0);
            vec3 moteCol = mix(pal(0.5), pal(0.88), step(0.8, h2));
            col += moteCol * m * (0.5 + 1.6 * hat * tw) * exp(-ti * fogK);
        }
    }

    // The core: a small white-hot sun ringed like a sunflower, with a warm halo
    // that flares on a drop. Measured as the screen radius from the bore's end.
    float cosA = dot(rd, coreDir);
    float rs = sqrt(max(2.0 - 2.0 * cosA, 0.0)) * uCamFocal;
    float seen = hitT > 0.0 ? 0.3 : 1.0;
    float core = exp(-rs * rs * 260.0) * seen;
    float sunRings = 0.6 + 0.4 * sin(rs * 110.0 - travel * 2.0);
    float halo = exp(-rs * 11.0) * sunRings;
    col += (vec3(1.0, 0.96, 0.75) * core * 2.6 + vec3(1.0, 0.82, 0.4) * halo * 0.5 * (0.5 + 0.5 * seen))
         * build * (1.0 + 1.5 * dropBump) * glow;

    col *= 0.85 + 0.2 * swell;
    col *= 0.72 + 0.28 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
