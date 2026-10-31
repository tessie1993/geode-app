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

// Fractal Tunnel: a fall down a bore through a foam of moving 3D fractals.
//
// WHAT IS DRAWN. One prototype with no shape of its own, the plane y = 0,
// seen through a chain of alternating translations (repetition into a unit
// cell) and sphere inversions about the cell centre. The limit set of that
// group is spheres packed inside spheres at every scale, the 3D Apollonian
// construction. A cylinder bored down the middle cuts the packing open, so the
// walls are rings of circles at every size with the cavities behind them
// showing through, and a wedge fold about the bore's axis gives the
// cross-section a kaleidoscope's mirrored symmetry. The translate/invert chain
// is the construction of Inigo Quilez's "Apollonian"; the code is written from
// the construction. This differs from Fractal Dive (a mandelbox-family fold
// with a straight corridor): here it is a sphere packing, wedge-mirrored,
// twisting down a bore that follows the camera and pinches on the kick.
//
// THE CAMERA is the shared rig (camFly). The bore's centre is the camera's own
// xy at the eye, easing to the world z axis down the tunnel, so the rig's
// sideways wander bends the tunnel toward it instead of flying into the wall.
// The world tiles along z every FT_PERIOD (64, divides the rig's 8192 wrap):
// the foam cell (1 world unit), the twist (one turn per period) and the cell
// hash (mod FT_CELLS) are all periodic in it, so ro.z is rebased exactly.
//
// TWO MATERIALS. The big shells of the packing are a translucent membrane:
// dark facing you, glowing at the rim in the complementary hue. The small
// deep beads (a large conformal scale) are a solid lit core. Light pulses
// travel outward through the nested levels of each cell, and the glow of the
// shells and the pulses is gathered along the ray, so the gaps have depth.
//
// ---- audio ------------------------------------------------------------------
//
// Each sound moves a different thing; motion rides clocks, so it slows in a
// quiet passage and never runs backwards:
//
// - SND_BASS_TIME streams the foam toward the eye (an extra fall on top of
//   the rig's flight) and breathes the inversion radius: spheres grow, split
//   and merge on the bass clock.
// - SND_MID_TIME turns the whole cross-section slowly about the bore.
// - SND_FLUX (sndLevel) WRITHES the packing: it turns the folds between the
//   inversions on two axes, and the turn compounds down the levels, so a busy,
//   changing spectrum sets the foam squirming while a held chord hangs still.
// - Spectral centroid (uMotionBright, the eased SND_CENTROID) sets the
//   inversion radius: a bright timbre packs the foam dense and fine, a dark
//   one opens it into few large spheres.
// - SND_SUB_ATT fattens the shells: sub-bass turns thin shells into beads.
// - SND_KICK_ENV sends a ring down the tunnel: a pinch of the bore that runs
//   away from the eye as the envelope falls, lit as it goes.
// - SND_SNARE_ENV steps the cross-section one wedge round, snapping and
//   settling. One wedge is the fold's own symmetry, so the step starts from a
//   pose identical to the last.
// - SND_HAT_ENV lights sparkles on the shells; SND_TREBLE_TIME twinkles them.
// - SND_HIT_TIME drives the light pulses outward through the nested levels.
// - SND_HELD_SURGE (a drop) opens the tunnel wide on its envelope's bump and
//   swells the gathered glow.
// - SND_CUT_COUNT re-seeds the foam on a hard cut (where the packing sits and
//   how its folds lean), eased across SND_HELD_HARD_CUT so it morphs, not pops.
// - SND_PITCH_CLASS, gated by SND_PITCH_CONFIDENCE, colours the pulses and the
//   kick ring; the key (uKeyHue, eased) slides the whole palette.
// - SND_ENERGY_TREND sets the brightness, gently.
//
// ---- why the estimate is safe -----------------------------------------------
//
// Repetition, the fold rotations, the wedge fold and the spin are isometries;
// each inversion is conformal with factor s / r2, floored. The estimate is the
// plane distance pulled back through those factors times FT_DE_FUDGE, and the
// march takes FT_STEP of it for the twist, the bore bend and the kick pinch,
// which are not isometries (each well under 1.3 Lipschitz).

#define FT_TAU 6.2831853
#define FT_MAX_STEPS 128
#define FT_MAX_ITERS 9

/** The world repeats along z every this many units (divides 1024 and 8192). */
#define FT_PERIOD 64.0
/** Foam units per world unit: the cell is 2 foam units, so 1 world unit. */
#define FT_FREQ 2.0
/** Foam cells per period, for the cell hash; and foam units per period, for the stream wrap. */
#define FT_CELLS 64.0
#define FT_FOAM_SPAN 128.0

#define FT_CORRIDOR 0.9
#define FT_BORE_MARGIN 0.02
/** How far down the tunnel (world units) the bore eases from the eye's xy back to the axis. */
#define FT_BEND_REACH 9.0
#define FT_WEDGES 6.0

#define FT_INV_FLOOR 0.02
#define FT_SCALE_CAP 1.0e30
#define FT_TRAP_FAR 4.0
#define FT_DE_FUDGE 0.4
#define FT_STEP 0.75
#define FT_SHELL 0.012
#define FT_EPS_PIXELS 1.2
#define FT_EPS_FLOOR 4.0e-4
#define FT_FAR 12.0

// ---- per-frame state, written in main(), read by map() ----------------------

vec2 gEyeXY;
float gEyeZ;
float gCorridor;
float gInvS;
float gIters;
mat2 gFoldA;
mat2 gFoldB;
vec3 gShift;
float gSpin;
float gWedges;
float gShell;
float gRingZ;
float gRingAmt;

// ---- reported back by map(): capture before the normal taps overwrite them --

/** min |q| per axis in .xyz, min r2 in .w, over the chain. */
vec4 gOrb;
/** The foam's own distance before the bore cut. */
float gFoamD;
/** 1 where the foam is real, 0 deep inside the bore. */
float gFoamMask;
/** log2 of the conformal scale: small deep beads have a large one. */
float gScaleLog;
/** The outermost cell's hash. */
float gCellH;

/** The bore's centre at depth z: the eye's xy at the eye, the world axis far away. */
vec2 boreCentre(float z) {
    float dz = (z - gEyeZ) / FT_BEND_REACH;
    return gEyeXY / (1.0 + dz * dz);
}

float foam(vec3 q) {
    float scale = 1.0;
    gOrb = vec4(FT_TRAP_FAR);
    // The outermost cell, keyed mod FT_CELLS along z so it repeats with the world.
    vec3 cell = floor(0.5 * q + 0.5);
    gCellH = hashCell(ivec3(int(cell.x), int(cell.y), int(mod(cell.z, FT_CELLS))));
    for (int i = 0; i < FT_MAX_ITERS; i++) {
        if (float(i) >= gIters) break;
        q = 2.0 * fract(0.5 * q + 0.5) - 1.0;
        // The writhe: two turns between the folds, compounding down the levels.
        q.xy = gFoldA * q.xy;
        q.yz = gFoldB * q.yz;
        float r2 = dot(q, q);
        gOrb = min(gOrb, vec4(abs(q), r2));
        float k = gInvS / max(r2, FT_INV_FLOOR);
        q *= k;
        scale = min(scale * k, FT_SCALE_CAP);
    }
    gScaleLog = log2(max(scale, 1.0));
    return FT_DE_FUDGE * abs(q.y) / scale;
}

float map(vec3 p) {
    vec2 c = p.xy - boreCentre(p.z);
    c = rot2(FT_TAU / FT_PERIOD * p.z + gSpin) * c;
    float r = length(c);
    // The kick's ring: a pinch of the bore running away down the tunnel.
    float rz = (p.z - gRingZ) / 0.7;
    float radius = gCorridor * (1.0 - 0.32 * gRingAmt * exp(-rz * rz));
    float bore = radius - r;
    if (bore > FT_BORE_MARGIN) {
        gOrb = vec4(FT_TRAP_FAR);
        gFoamD = FT_TRAP_FAR;
        gFoamMask = 0.0;
        gScaleLog = 0.0;
        return bore;
    }
    if (gWedges >= 2.0 && r > 1.0e-6) {
        float seg = FT_TAU / gWedges;
        float a = atan(c.y, c.x);
        a = abs(mod(a, seg) - seg * 0.5);
        c = vec2(cos(a), sin(a)) * r;
    }
    vec3 q = vec3(c, p.z) * FT_FREQ + gShift;
    float d = foam(q) / FT_FREQ - gShell;
    gFoamD = d;
    gFoamMask = 1.0 - smoothstep(0.0, 0.15, bore);
    return max(d, bore);
}

vec3 foamNormal(vec3 p, float e, vec3 fallback) {
    vec2 k = vec2(1.0, -1.0);
    vec3 g = k.xyy * map(p + k.xyy * e) + k.yyx * map(p + k.yyx * e)
           + k.yxy * map(p + k.yxy * e) + k.xxx * map(p + k.xxx * e);
    float len = length(g);
    return len > 1.0e-20 ? g / len : fallback;
}

/** A light pulse travelling outward through a cell's nested levels; only some cells carry one. */
float pulse(float trapR, float cellH, float phase) {
    float carries = step(0.45, fract(cellH * 13.7));
    float w = fract(trapR * 2.2 - phase + cellH * 3.1);
    return carries * smoothstep(0.0, 0.05, w) * (1.0 - smoothstep(0.05, 0.24, w));
}

/** The foam's seed for hard-cut section `sec`: where the packing sits and how its folds lean. */
vec4 seedFor(float sec) {
    return vec4(hash11(sec * 1.37 + 0.5), hash11(sec * 2.91 + 7.0), hash11(sec * 0.73 + 3.0), hash11(sec * 4.13 + 1.0));
}

void main() {
    vec2 uv = view();

    float swell = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    float flux = clamp(sndLevel(SND_FLUX), 0.0, 1.0) * uIntensity;
    float kick = clamp(SND_KICK_ENV, 0.0, 1.0);
    float snare = clamp(SND_SNARE_ENV, 0.0, 1.0);
    float hats = clamp(SND_HAT_ENV, 0.0, 1.0);
    float surge = clamp(SND_HELD_SURGE, 0.0, 1.0);
    float surgeBump = 4.0 * surge * (1.0 - surge);
    float sub = clamp(sndLevel(SND_SUB_ATT), 0.0, 1.0);
    float bright = clamp(uMotionBright, 0.0, 1.0);

    // ---- the hard-cut seed, eased from the last section to this one ----------
    float sec = floor(SND_CUT_COUNT + 0.5);
    vec4 seed = mix(seedFor(sec), seedFor(sec - 1.0), smoothstep(0.0, 1.0, clamp(SND_HELD_HARD_CUT, 0.0, 1.0)));

    // ---- the foam this frame ---------------------------------------------------
    gWedges = (uKaleido > 0.5) ? uSymmetry : FT_WEDGES;
    float wedge = FT_TAU / max(gWedges, 1.0);
    gCorridor = FT_CORRIDOR * (1.0 + 0.45 * surgeBump);
    // Centroid packs the foam, the bass clock breathes it, the seed offsets it.
    gInvS = 1.08 + 0.26 * (1.0 - smoothstep(0.2, 0.75, bright)) + 0.07 * sin(SND_BASS_TIME * 0.21) + 0.06 * (seed.z - 0.5);
    // Untwisted at rest except for the seed's lean and a slow drift on the
    // loudness clock; flux adds the writhe on top.
    float lean = 0.18 * (seed.w - 0.5) + 0.10 * sin(SND_CURVED_TIME * 0.17);
    gFoldA = rot2(lean + 0.42 * flux * uMorph + 0.30 * flux);
    gFoldB = rot2(0.5 * lean + 0.16 * flux);
    // The stream wraps on the foam's own z span, so the cell hash wraps with it.
    gShift = vec3(1.6 * seed.x, 1.6 * seed.y, mod(SND_BASS_TIME * 0.9, FT_FOAM_SPAN));
    // Mid clock spin, wrapped on the wedge (invisible); the snare's one-wedge step on top.
    gSpin = mod(SND_MID_TIME * 0.12, wedge) + wedge * (1.0 - snare * snare);
    gShell = FT_SHELL * (1.0 + 2.2 * sub);
    float detail = clamp((uSteps - 64.0) / 64.0, 0.0, 1.0);
    gIters = 6.0 + floor(detail * 2.0 + 0.5);

    // ---- camera ----------------------------------------------------------------
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    ro.z = mod(ro.z, FT_PERIOD);
    gEyeXY = ro.xy;
    gEyeZ = ro.z;
    gRingZ = ro.z + 1.2 + 9.0 * (1.0 - kick);
    gRingAmt = kick;

    float epsPerT = FT_EPS_PIXELS * 2.0 / (max(uResolution.y, 1.0) * max(uCamFocal, 0.3));
    float t = 0.02 + hash13(vec3(gl_FragCoord.xy, 3.0)) * 0.02;

    // ---- colours -----------------------------------------------------------------
    float hue = 0.05 + 0.5 * uKeyHue * clamp(uKeyStrength, 0.0, 1.0);
    float pitchHue = SND_PITCH_CLASS;
    float conf = clamp(SND_PITCH_CONFIDENCE, 0.0, 1.0);
    vec3 accent = mix(pal(hue + 0.5), pal(pitchHue), conf);
    accent = mix(accent, vec3(1.0), 0.3);
    float phase = fract(SND_HIT_TIME * 0.35);

    // ---- march -------------------------------------------------------------------
    float hitT = -1.0;
    float used = 0.0;
    vec4 hitOrb = vec4(FT_TRAP_FAR);
    float hitScale = 0.0;
    float hitCell = 0.0;
    float glowSum = 0.0;
    float glowTrap = 0.0;
    float pulseSum = 0.0;
    float ringSum = 0.0;
    for (int i = 0; i < FT_MAX_STEPS; i++) {
        if (float(i) >= uSteps) break;
        used = float(i) + 1.0;
        vec3 p = ro + rd * t;
        float d = map(p);
        float eps = epsPerT * t + FT_EPS_FLOOR;
        if (d < eps) {
            hitT = t;
            used = float(i);
            hitOrb = gOrb;
            hitScale = gScaleLog;
            hitCell = gCellH;
            break;
        }
        float stepLen = max(d * FT_STEP, eps * 0.5);
        float fd = max(gFoamD, 0.0);
        float w = exp(-fd * 12.0) * stepLen * gFoamMask;
        float tr = sqrt(min(gOrb.w, FT_TRAP_FAR));
        glowSum += w;
        glowTrap += w * tr;
        pulseSum += exp(-fd * 30.0) * stepLen * gFoamMask * pulse(clamp(tr * 0.75, 0.0, 1.0), gCellH, phase);
        float rz = (p.z - gRingZ) / 0.35;
        ringSum += exp(-rz * rz) * exp(-fd * 8.0) * stepLen * gFoamMask;
        t += stepLen;
        if (t > FT_FAR) break;
    }

    // The light at the end of the tunnel: the bore converges on the world axis ahead.
    vec3 ahead = normalize(vec3(-ro.xy / FT_BEND_REACH, 1.0));
    float core = pow(clamp(dot(rd, ahead), 0.0, 1.0), 40.0);
    vec3 endLight = pal(hue + 0.62);
    vec3 fogCol = endLight * 0.08 + vec3(0.002, 0.002, 0.006);
    vec3 col = (t > FT_FAR) ? fogCol + endLight * 0.5 * core + vec3(core * core) * 0.4 : fogCol;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        float px = epsPerT * hitT + FT_EPS_FLOOR;
        vec3 n = foamNormal(p, px * 1.3, -rd);
        float ao = clamp(1.0 - 1.1 * max(used / max(uSteps, 1.0) - 0.3, 0.0), 0.18, 1.0);

        float trapR = clamp(sqrt(min(hitOrb.w, FT_TRAP_FAR)) * 0.75, 0.0, 1.0);
        float trapX = clamp(hitOrb.x * 1.4, 0.0, 1.0);
        float zHue = 0.15 * sin(p.z * (FT_TAU / FT_PERIOD) * 4.0);
        float h = hue + 0.5 * trapR + 0.2 * trapX + zHue;

        float head = clamp(dot(n, -rd), 0.0, 1.0);
        float back = clamp(dot(n, ahead), 0.0, 1.0);
        float fres = pow(1.0 - head, 3.0);

        // Membrane: dark facing you, glowing at the rim in the complementary hue.
        vec3 body = pal(h);
        vec3 rimCol = pal(h + 0.5);
        vec3 membrane = body * (0.03 + 0.10 * head + 0.12 * back) * ao + rimCol * fres * 1.1 * ao;
        // Core: a solid, lit bead with a hard highlight.
        vec3 coreBase = mix(pal(h + 0.12), vec3(1.0, 0.92, 0.85), 0.25);
        float spec = pow(clamp(dot(n, normalize(ahead - rd)), 0.0, 1.0), 28.0);
        vec3 solid = coreBase * (0.18 + 0.6 * head + 0.3 * back) * ao + vec3(spec) * 0.5 * ao;
        float coreMask = smoothstep(3.0, 6.5, hitScale);
        col = mix(membrane, solid, coreMask);

        // The pulse on the shell itself, and the kick ring as it passes.
        col += accent * pulse(trapR, hitCell, phase) * (1.0 - coreMask) * 1.1;
        float rz = (p.z - gRingZ) / 0.35;
        col += accent * exp(-rz * rz) * kick * 0.9;

        // Hat sparkles, hashed on the world so they sit on the shells.
        vec3 gp = p * 14.0;
        // z keyed mod one period (64 * 14 cells), so the rebase of ro.z never re-rolls them.
        vec3 gc = floor(gp);
        float g = hashCell(ivec3(int(gc.x), int(gc.y), int(mod(gc.z, FT_PERIOD * 14.0))) + ivec3(0, 0, int(hitCell * 977.0)));
        float tw = 0.5 + 0.5 * sin(FT_TAU * fract(SND_TREBLE_TIME * (0.6 + g) + g * 7.0));
        float pt = 1.0 - smoothstep(0.0, 0.3, length(fract(gp) - 0.5));
        col += mix(rimCol, vec3(1.0), 0.5) * step(0.93 - 0.12 * hats, g) * pt * tw * (0.04 + 1.5 * hats);

        col = mix(col, fogCol, 1.0 - exp(-hitT * 0.22));
    }

    // The glow gathered along the ray, coloured once.
    float norm = max(glowSum, 1.0e-6);
    float glowHue = hue + 0.5 * clamp((glowTrap / norm) * 0.75, 0.0, 1.0) + 0.25;
    col += pal(glowHue) * (1.0 - exp(-glowSum * 4.0)) * (0.16 + 0.22 * surgeBump);
    col += accent * (1.0 - exp(-pulseSum * 6.0)) * 0.7;
    col += accent * (1.0 - exp(-ringSum * 5.0)) * kick * 0.8;

    col *= 0.8 + 0.3 * swell;
    col = vec3(1.0) - exp(-max(col, 0.0) * 1.4);
    vec2 nd = vUv * 2.0 - 1.0;
    col *= 1.0 - 0.11 * dot(nd, nd);
    fragColor = vec4(grade(col), 1.0);
}
