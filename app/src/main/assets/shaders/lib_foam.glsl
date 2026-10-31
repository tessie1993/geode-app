// The Sphere Foam family: falls and orbits through a 3D Apollonian sphere
// packing, each style with its own material and its own ear.
//
// WHAT IS DRAWN. One prototype with no shape of its own, the plane y = 0, seen
// through a chain of alternating translations (repetition into a unit cell)
// and sphere inversions about the cell centre. The limit set of that group is
// spheres packed inside spheres at every scale. A bore (FOAM_BORE) or a ball
// (FOAM_BALL) cuts the packing open, so its surface shows rings of circles at
// every size with the cavities behind them, and a wedge fold gives the cut a
// kaleidoscope's mirrored symmetry. The translate/invert chain is the
// construction of Inigo Quilez's "Apollonian"; the code is written from the
// construction. This family grew out of PR #7, the shader Fractal Tunnel was
// first ported from; Fractal Tunnel keeps the rig's own flight and a bore that
// bends toward the eye, while here the camera RIDES a curving tunnelPath,
// banking into its bends, as #7 did.
//
// WHY THE WORLD IS PERIODIC IN 64
//
// The rig's travel wraps at 8192 and the sound clocks at 3600 s. The foam
// cell is 1 world unit along z, the path's lobes and the bore's twist are
// whole cycles per FOAM_PERIOD, and the cell hash is taken mod FOAM_CELLS, so
// the eye's z can be rebased into [0, 64) and the march runs near the origin.
// The fall rate is 64 * 90 / 3600 world units per clock second: the clock's
// wrap lands on a whole number of periods, so the fall never jumps.
//
// WHY THE ESTIMATE IS SAFE
//
// Repetition, the fold rotation, the wedge fold and the spin are isometries;
// each inversion is conformal with factor s / r2, floored. The estimate is the
// plane distance pulled back through those factors times FOAM_DE_FUDGE, and
// the march takes FOAM_STEP of it for the twist and the bend, which are not
// isometries. During a section change the wedge fold is a blend of two folds;
// each is 1-Lipschitz, so the blend is too.
//
// ---- the sound (foamSetup) --------------------------------------------------
//
// Several things answer different parts of the music at once, and motion rides
// clocks, so it slows in a quiet passage and never runs backwards:
//
// - Loudness: the fall. SND_CURVED_TIME (vol_att^2.5) adds to the rig's
//   travel, so a loud passage races down the bore and a quiet one drifts.
// - Harmony and brightness: the bubble size. A tonal (uHarmony), dark
//   (uMotionBright) sound opens the packing into few large spheres; a noisy,
//   bright one packs it dense and fine. The bass clock breathes it.
// - Flux: the writhe. SND_FLUX turns the packing inside itself between the
//   inversions, so a changing spectrum squirms and a held chord hangs still.
//   The turn stays under about 0.3 rad: a turned cell no longer tiles, and
//   past that the packing tears at the cell borders.
// - Drum hits: light pulses run outward through each cell's nested levels
//   (SND_HIT_TIME), and the circles where the cut meets a shell glow, brighter
//   on the level's own spectrum band.
// - Tension: SND_ENERGY_TREND above its steady 0.5 (a build) closes the bore
//   in; SND_HELD_SURGE (the drop) bursts it open on its envelope's bump.
// - Kick: SND_KICK_ENV swells the shells into beads.
// - Onsets: a train of light rings runs down the bore. Each drum hit
//   (SND_HIT_TIME, eased) pushes the train one ring away, SND_ONSET_ENV
//   flashes it, SND_PITCH_CLASS (gated by its confidence) colours it.
// - Spectrum by depth: each nesting level of the packing is lit by its own
//   band (sndSpectrum at the orbit trap), so a bassline lights the deep
//   shells and a hi-hat the near ones instead of everything at once.
// - Treble: the highlight tightens, a glassier bead without moving geometry.
// - Section change: the kaleidoscope's wedge count steps (3, 4, 5, 6, 8; two
//   neighbouring sections never share one), the packing slides to a new
//   seat and the palette turns, all morphed across SND_HELD_HARD_CUT.
// - Mid clock: the cross-section turns slowly about the bore.
// - Key: uKeyHue, eased, slides the palette.
//
// ---- the Customize macros ---------------------------------------------------
//
// Speed: uTime already runs at its rate, so it hurries the fold's sway and the
// slide. Complexity: uMorph leans the folds. Chaos: uWarp and uTurbulence wobble
// the bore's twist and slide the packing further. Energy: foamEnergy(), the
// gain on every in-shader reaction, 0..2. Colour: pal(). Space: the rig.
//
// INCLUDE ORDER: last, after lib_scene_uniforms (with VIEW_OWNS_SHAPE
// defined), lib_scene_motion, lib_camera, lib_palette, lib_scene_grade and
// lib_sdf3 (rot2, hash11, hash13, hashCell).

#define FOAM_TAU 6.2831853
/** Compile-time ceiling on the march; the Detail budget (uSteps) breaks below it. */
#define FOAM_MAX_STEPS 128
#define FOAM_MAX_ITERS 9

/** The world repeats along z every this many units (divides 8192). */
#define FOAM_PERIOD 64.0
/** Foam units per world unit: the cell is 2 foam units, so 1 world unit. */
#define FOAM_FREQ 2.0
#define FOAM_CELLS 64.0
/** World units per second of SND_CURVED_TIME: 64 * 90 / 3600, see the header. */
#define FOAM_FALL_RATE 1.6
/** A rate completing k cycles per sound-clock wrap (3600 s), so sin(clock * FOAM_RATE(k)) never jumps. */
#define FOAM_RATE(k) (FOAM_TAU * (k) / 3600.0)
/** The same for uTime, which wraps at 7100 s (ShaderScene). uTime already runs at the Speed control's rate. */
#define FOAM_SCENE_RATE(k) (FOAM_TAU * (k) / 7100.0)

#define FOAM_INV_FLOOR 0.02
#define FOAM_SCALE_CAP 1.0e30
#define FOAM_TRAP_FAR 4.0
#define FOAM_DE_FUDGE 0.4
#define FOAM_STEP 0.8
#define FOAM_BORE_MARGIN 0.02
#define FOAM_EPS_PIXELS 1.2
#define FOAM_EPS_FLOOR 4.0e-4
#define FOAM_FAR 12.0
/** Ring-train spacing, world units along the bore (or radius, for the ball). */
#define FOAM_RING_GAP 2.2

#define FOAM_BORE 0
#define FOAM_BALL 1

float foamSat(float x) { return clamp(x, 0.0, 1.0); }

float foamEnergy() { return clamp(uIntensity, 0.0, 2.0); }

// ---- per-frame state: foamSetup() writes the defaults, a style may override --

int gFoamShape;
/** Bore radius (FOAM_BORE) or ball radius (FOAM_BALL), world units. */
float gFoamRadius;
/** Path lobe amplitude, world units; 0 is a straight bore. */
float gFoamBend;
/** Whole turns of the bore about its axis per FOAM_PERIOD (whole, or the period breaks). */
float gFoamTwist;
/** A periodic wobble on top of the twist, radians: Chaos's share, continuous where a whole-turn step would pop. */
float gFoamWobble;
float gFoamInvS;
float gFoamIters;
mat2 gFoamFold;
vec3 gFoamShift;
float gFoamSpin;
/** The wedge count this section, the last section's, and how much of the last is still showing (1 at the cut). */
float gFoamWedgesNew;
float gFoamWedgesOld;
float gFoamWedgeHold;
float gFoamShell;
/** Multiplier on the loudness fall: halves and whole numbers keep the clock wrap exact. */
float gFoamFall;

// The sound, shaped once, for the shading.
float gFoamKick;
float gFoamOnset;
float gFoamHats;
float gFoamSnare;
float gFoamSurgeBump;
float gFoamTreble;
float gFoamBass;
float gFoamSection;
float gFoamHue;
vec3 gFoamAccent;
/** Where the ring train sits: rings at gFoamRingPhase + k * FOAM_RING_GAP past the eye (or the centre). */
float gFoamRingPhase;
/** The light pulses' phase through the nesting levels, 0..1, on the drum-hit clock. */
float gFoamPulsePhase;

// The camera, for the shading and the backdrop.
vec3 gFoamEye;
vec3 gFoamFwd;
float gFoamEpsPerT;

// ---- reported back by foamMap(): capture before the normal taps overwrite them --

/** min |q| per axis in .xyz, min r2 in .w, over the chain. */
vec4 gFoamOrb;
/** The foam's own distance before the cut. */
float gFoamD;
/** The cut surface's distance minus the foam's: about 0 on the rings where the cut meets a shell. */
float gFoamCut;
/** 1 where the foam is real, 0 deep inside the bore. */
float gFoamMask;
/** log2 of the conformal scale: the small deep beads have a large one. */
float gFoamScaleLog;
/** The outermost cell's hash. */
float gFoamCellH;

/** The tunnel axis at depth z: two lobes, whole cycles per period. */
vec2 foamPath(float z) {
    float w = FOAM_TAU / FOAM_PERIOD;
    return gFoamBend * vec2(sin(z * w * 3.0), 0.8 * sin(z * w * 2.0 + 1.3));
}

/** The wedge count for section `sec`: slot sec + 2 * (a hashed bit), mod 5, so neighbours always differ. */
float foamWedgesFor(float sec) {
    float s = mod(sec + 2.0 * floor(2.0 * hash11(sec * 1.37 + 0.5)), 5.0);
    int i = int(min(floor(s + 0.5), 4.0));
    if (i == 0) return 3.0;
    if (i == 1) return 4.0;
    if (i == 2) return 5.0;
    if (i == 3) return 6.0;
    return 8.0;
}

/** Where the packing sits in section `sec`, in foam units. */
vec2 foamSeatFor(float sec) {
    return 1.6 * vec2(hash11(sec * 2.91 + 7.0), hash11(sec * 0.73 + 3.0));
}

/**
 * Every default from the sound. Call once at the top of main() after view(),
 * then override what the style wants (gFoamShape, gFoamRadius, gFoamFall...).
 */
void foamSetup() {
    float e = foamEnergy();
    float morph = foamSat(uMorph);
    float chaos = clamp(uWarp, 0.0, 2.0) + clamp(uTurbulence, 0.0, 2.0);

    gFoamKick = foamSat(SND_KICK_ENV * e);
    gFoamOnset = foamSat(SND_ONSET_ENV * e);
    gFoamHats = foamSat(SND_HAT_ENV * e);
    gFoamSnare = foamSat(SND_SNARE_ENV * e);
    float surge = foamSat(SND_HELD_SURGE);
    gFoamSurgeBump = 4.0 * surge * (1.0 - surge) * min(e, 1.0);
    gFoamTreble = foamSat(sndShaped(SND_HIGH));
    gFoamBass = foamSat(sndShaped(SND_LOW));
    float flux = foamSat(sndLevel(SND_FLUX)) * e;
    float harmony = foamSat(uHarmony);
    float bright = foamSat(uMotionBright);
    float tension = smoothstep(0.5, 0.9, SND_ENERGY_TREND) * min(e, 1.0);

    // The section: wedge count, seat and palette, eased from the last one.
    float sec = floor(SND_CUT_COUNT + 0.5);
    float hold = smoothstep(0.0, 1.0, foamSat(SND_HELD_HARD_CUT));
    gFoamSection = sec;
    gFoamWedgeHold = hold;
    // The user's count rounded: a fractional wedge count tears a seam where atan jumps.
    float userWedges = floor(uSymmetry + 0.5);
    gFoamWedgesNew = (uKaleido > 0.5) ? userWedges : foamWedgesFor(sec);
    gFoamWedgesOld = (uKaleido > 0.5) ? userWedges : foamWedgesFor(sec - 1.0);
    vec2 seat = mix(foamSeatFor(sec), foamSeatFor(sec - 1.0), hold);

    gFoamShape = FOAM_BORE;
    gFoamRadius = 0.85 * (1.0 - 0.22 * tension) * (1.0 + 0.4 * gFoamSurgeBump);
    gFoamBend = 0.75;
    gFoamTwist = 2.0;
    gFoamWobble = 0.35 * chaos;
    gFoamFall = 1.0;
    // Tonal and dark opens the packing (larger s), noisy and bright packs it.
    // About 1.30 in silence (Fractal Tunnel's resting packing, where the cut shows round
    // cavities), 1.16 for noisy bright sound, 1.46 for a dark held chord.
    gFoamInvS = 1.16 + 0.16 * harmony + 0.14 * (1.0 - smoothstep(0.2, 0.75, bright))
              + 0.06 * sin(SND_BASS_TIME * FOAM_RATE(37.0));
    // Kept small: a turned cell no longer tiles, so a large turn shreds the
    // packing at every cell border into torn shards instead of round shells.
    gFoamFold = rot2(0.06 * sin(uTime * FOAM_SCENE_RATE(57.0)) + 0.08 * morph + 0.14 * flux);
    float slide = 0.4 * (1.0 + 0.5 * chaos);
    gFoamShift = vec3(seat + slide * vec2(sin(SND_MID_TIME * FOAM_RATE(61.0)), sin(uTime * FOAM_SCENE_RATE(42.0) + 2.0)), 0.0);
    gFoamSpin = SND_MID_TIME * FOAM_RATE(240.0);
    gFoamShell = 0.012 * (1.0 + 0.8 * gFoamKick);
    float detail = foamSat((uSteps - 64.0) / 64.0);
    // Detail only: it is a quality setting, not a live control. Complexity moving a whole
    // level would pop the packing as it crossed a threshold, so Complexity leans the fold instead.
    gFoamIters = 6.0 + floor(detail * 3.0 + 0.5);

    // Colour: the key slides the palette, each section turns it a little.
    gFoamHue = 0.5 * uKeyHue * foamSat(uKeyStrength) + 0.13 * mix(sec, sec - 1.0, hold);
    float conf = foamSat(SND_PITCH_CONFIDENCE);
    gFoamAccent = mix(mix(pal(gFoamHue + 0.5), pal(SND_PITCH_CLASS), conf), vec3(1.0), 0.3);
    gFoamRingPhase = FOAM_RING_GAP * fract(SND_HIT_TIME);
    gFoamPulsePhase = fract(SND_HIT_TIME * 0.35);
}

// ---- the foam ---------------------------------------------------------------

float foam(vec3 q) {
    float scale = 1.0;
    gFoamOrb = vec4(FOAM_TRAP_FAR);
    vec3 cell = floor(0.5 * q + 0.5);
    gFoamCellH = hashCell(ivec3(int(cell.x), int(cell.y), int(mod(cell.z, FOAM_CELLS))));
    for (int i = 0; i < FOAM_MAX_ITERS; i++) {
        if (float(i) >= gFoamIters) break;
        q = 2.0 * fract(0.5 * q + 0.5) - 1.0;
        q.xy = gFoamFold * q.xy;
        float r2 = dot(q, q);
        gFoamOrb = min(gFoamOrb, vec4(abs(q), r2));
        float k = gFoamInvS / max(r2, FOAM_INV_FLOOR);
        q *= k;
        scale = min(scale * k, FOAM_SCALE_CAP);
    }
    gFoamScaleLog = log2(max(scale, 1.0));
    return FOAM_DE_FUDGE * abs(q.y) / scale;
}

/** The wedge fold into n wedges (below 2, none). c has length r, r > 0. */
vec2 foamWedge(vec2 c, float r, float n) {
    if (n < 2.0) return c;
    float seg = FOAM_TAU / n;
    float a = abs(mod(atan(c.y, c.x), seg) - seg * 0.5);
    return vec2(cos(a), sin(a)) * r;
}

/** This section's fold, blended from the last one's while the cut is held. */
vec2 foamFold(vec2 c, float r) {
    if (r < 1.0e-6) return c;
    vec2 f = foamWedge(c, r, gFoamWedgesNew);
    if (gFoamWedgeHold > 0.001) f = mix(f, foamWedge(c, r, gFoamWedgesOld), gFoamWedgeHold);
    return f;
}

float foamMap(vec3 p) {
    float cutD;
    vec3 q;
    if (gFoamShape == FOAM_BALL) {
        // The packing inside a ball, folded about the y axis, spun on the mid clock.
        vec2 xz = rot2(gFoamSpin) * p.xz;
        cutD = length(p) - gFoamRadius;
        if (cutD > FOAM_BORE_MARGIN) {
            gFoamOrb = vec4(FOAM_TRAP_FAR);
            gFoamD = FOAM_TRAP_FAR;
            gFoamCut = 0.0;
            gFoamMask = 0.0;
            gFoamScaleLog = 0.0;
            return cutD;
        }
        xz = foamFold(xz, length(xz));
        q = vec3(xz.x, p.y, xz.y);
    } else {
        vec2 c = p.xy - foamPath(p.z);
        float zw = FOAM_TAU / FOAM_PERIOD * p.z;
        c = rot2(gFoamTwist * zw + gFoamWobble * sin(3.0 * zw) + gFoamSpin) * c;
        float r = length(c);
        cutD = gFoamRadius - r;
        if (cutD > FOAM_BORE_MARGIN) {
            gFoamOrb = vec4(FOAM_TRAP_FAR);
            gFoamD = FOAM_TRAP_FAR;
            gFoamCut = 0.0;
            gFoamMask = 0.0;
            gFoamScaleLog = 0.0;
            return cutD;
        }
        q = vec3(foamFold(c, r), p.z);
    }
    float d = foam(q * FOAM_FREQ + gFoamShift) / FOAM_FREQ - gFoamShell;
    gFoamD = d;
    gFoamCut = cutD - d;
    gFoamMask = 1.0 - smoothstep(0.0, 0.15, cutD);
    return max(d, cutD);
}

vec3 foamNormal(vec3 p, float e, vec3 fallback) {
    vec2 k = vec2(1.0, -1.0);
    vec3 g = k.xyy * foamMap(p + k.xyy * e) + k.yyx * foamMap(p + k.yyx * e)
           + k.yxy * foamMap(p + k.yxy * e) + k.xxx * foamMap(p + k.xxx * e);
    float len = length(g);
    return len > 1.0e-20 ? g / len : fallback;
}

// ---- the camera ---------------------------------------------------------------

/**
 * Rides the curving tunnelPath: the eye on the bore's axis at the rig's travel
 * plus the loudness fall, looking down the path's tangent and banking into its
 * bends; the rig's look (yaw, pitch, roll, shake, FOV) applies on top, read as
 * a ray in the path's frame.
 */
void foamCamera(vec2 uv, out vec3 ro, out vec3 rd) {
    float s = mod(uCamTravel + 0.8 * uCamDolly + FOAM_FALL_RATE * gFoamFall * SND_CURVED_TIME, FOAM_PERIOD);
    ro = vec3(foamPath(s), s);
    vec3 fwd = normalize(vec3(foamPath(s + 2.0), s + 2.0) - ro);
    float roll = -0.9 * fwd.x;
    vec3 up = vec3(sin(roll), cos(roll), 0.0);
    vec3 right = normalize(cross(up, fwd));
    rd = normalize(mat3(right, cross(fwd, right), fwd) * camRayDir(uv));
    gFoamEye = ro;
    gFoamFwd = fwd;
    gFoamEpsPerT = FOAM_EPS_PIXELS * 2.0 / (max(uResolution.y, 1.0) * max(uCamFocal, 0.3));
}

/** Orbits the ball (FOAM_BALL): the rig's object camera, one turn per 128 units of travel. */
void foamOrbitCamera(vec2 uv, float radius, out vec3 ro, out vec3 rd) {
    camAround(uv, radius, 8.0 * CAM_SPIN_UNIT, ro, rd);
    gFoamEye = ro;
    gFoamFwd = normalize(-ro);
    gFoamEpsPerT = FOAM_EPS_PIXELS * 2.0 / (max(uResolution.y, 1.0) * max(uCamFocal, 0.3));
}

// ---- the march ------------------------------------------------------------------

struct FoamHit {
    /** Hit distance, or -1. */
    float t;
    /** The last distance marched, hit or not: past FOAM_FAR the ray escaped. */
    float tEnd;
    float used;
    vec4 orb;
    float scaleLog;
    float cell;
    float cut;
    /** Glow gathered near the shells along the ray, and its trap-weighted sum. */
    float glow;
    float glowTrap;
    /** The ring train gathered along the ray. */
    float rings;
};

/** The ring train's density at p: a soft band every FOAM_RING_GAP past the eye (bore) or the centre (ball). */
float foamRingAt(vec3 p) {
    float x = gFoamShape == FOAM_BALL ? length(p) : p.z - gFoamEye.z;
    float u = (x - gFoamRingPhase) / FOAM_RING_GAP;
    float w = (fract(u + 0.5) - 0.5) * FOAM_RING_GAP / 0.22;
    return step(0.0, u) * exp(-w * w);
}

FoamHit foamMarch(vec3 ro, vec3 rd, float far) {
    FoamHit h;
    h.t = -1.0;
    h.used = 0.0;
    h.orb = vec4(FOAM_TRAP_FAR);
    h.scaleLog = 0.0;
    h.cell = 0.0;
    h.cut = 0.0;
    h.glow = 0.0;
    h.glowTrap = 0.0;
    h.rings = 0.0;
    float t = 0.02 + hash13(vec3(gl_FragCoord.xy, 3.0)) * 0.02;
    for (int i = 0; i < FOAM_MAX_STEPS; i++) {
        if (float(i) >= uSteps) break;
        h.used = float(i) + 1.0;
        vec3 p = ro + rd * t;
        float d = foamMap(p);
        float eps = gFoamEpsPerT * t + FOAM_EPS_FLOOR;
        if (d < eps) {
            h.t = t;
            h.used = float(i);
            h.orb = gFoamOrb;
            h.scaleLog = gFoamScaleLog;
            h.cell = gFoamCellH;
            h.cut = gFoamCut;
            break;
        }
        float stepLen = max(d * FOAM_STEP, eps * 0.5);
        float fd = max(gFoamD, 0.0);
        float w = exp(-fd * 12.0) * stepLen * gFoamMask;
        h.glow += w;
        h.glowTrap += w * sqrt(min(gFoamOrb.w, FOAM_TRAP_FAR));
        h.rings += foamRingAt(p) * exp(-fd * 8.0) * stepLen * gFoamMask;
        t += stepLen;
        if (t > far) break;
    }
    h.tEnd = t;
    return h;
}

// ---- shading ----------------------------------------------------------------------

/** The family's colour: pal() turned by the key and the section. */
vec3 foamColor(float x) {
    return pal(x + gFoamHue);
}

/** The light at the end of the bore (or behind the ball), and the dim wash round it. */
vec3 foamBackdrop(vec3 rd) {
    float c = foamSat(dot(rd, gFoamFwd));
    float core = pow(c, 40.0);
    vec3 endLight = foamColor(0.62);
    // Rays fanning out of the light, turning slowly on the mid clock; they
    // swell with the drop. Off the axis only: atan has no angle on it.
    vec3 side = rd - gFoamFwd * c;
    vec3 ax = normalize(cross(gFoamFwd, abs(gFoamFwd.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
    vec3 ay = cross(gFoamFwd, ax);
    float rays = 0.0;
    if (dot(side, side) > 1.0e-8) {
        float a = atan(dot(side, ay), dot(side, ax));
        float spin = SND_MID_TIME * FOAM_RATE(120.0);
        rays = pow(0.5 + 0.5 * cos(a * 9.0 + spin), 6.0) * 0.6 + pow(0.5 + 0.5 * cos(a * 23.0 - 1.7 * spin), 10.0) * 0.4;
    }
    float halo = pow(c, 8.0);
    return endLight * (0.06 + 0.5 * core + halo * rays * (0.35 + 0.5 * gFoamSurgeBump)) + vec3(core * core) * 0.4;
}

struct FoamSurf {
    vec3 p;
    vec3 n;
    vec3 rd;
    float t;
    float ao;
    /** The closest approach to a cell centre (the nesting level), 0..1. */
    float trapR;
    float trapX;
    float scaleLog;
    float cell;
    float cut;
    /** The nesting level's own spectrum band, 0..1.5. */
    float lit;
    /** One pixel at the hit, world units. */
    float px;
    float head;
    float fres;
};

FoamSurf foamSurface(vec3 ro, vec3 rd, FoamHit h) {
    FoamSurf s;
    s.t = h.t;
    s.p = ro + rd * h.t;
    s.rd = rd;
    s.px = gFoamEpsPerT * h.t + FOAM_EPS_FLOOR;
    s.n = foamNormal(s.p, s.px * 1.3, -rd);
    s.ao = clamp(1.0 - 1.1 * max(h.used / max(uSteps, 1.0) - 0.3, 0.0), 0.18, 1.0);
    s.trapR = foamSat(sqrt(min(h.orb.w, FOAM_TRAP_FAR)) * 0.75);
    s.trapX = foamSat(h.orb.x * 1.4);
    s.scaleLog = h.scaleLog;
    s.cell = h.cell;
    s.cut = h.cut;
    s.lit = sndSpectrum(s.trapR);
    s.head = foamSat(dot(s.n, -rd));
    s.fres = pow(1.0 - s.head, 3.0);
    return s;
}

/** A light pulse running outward through a cell's nested levels; only some cells carry one. */
float foamPulse(float trapR, float cellH) {
    float carries = step(0.4, fract(cellH * 13.7));
    float w = fract(trapR * 2.2 - gFoamPulsePhase + cellH * 3.1);
    return carries * smoothstep(0.0, 0.05, w) * (1.0 - smoothstep(0.05, 0.24, w));
}

/**
 * The flair every look shares: the ring train on the surface, light pulses
 * through the nesting levels, a glowing line where the cut meets each shell
 * (the circles that make the bore wall read as spheres), and the hats'
 * sparkles. sparkleCol tints the sparkles and the cut lines.
 */
vec3 foamAccents(FoamSurf s, vec3 sparkleCol, float sparkleGain) {
    vec3 col = gFoamAccent * foamRingAt(s.p) * (0.15 + 0.85 * gFoamOnset);
    col += gFoamAccent * foamPulse(s.trapR, s.cell) * (0.35 + 0.5 * gFoamKick) * (1.0 - smoothstep(4.0, 7.0, s.scaleLog));
    float w = 2.0 * s.px;
    float cutLine = (1.0 - smoothstep(0.0, w, abs(s.cut))) + 0.35 * exp(-abs(s.cut) / (6.0 * w));
    col += mix(foamColor(0.3 + 0.5 * s.trapR), sparkleCol, 0.4) * cutLine * (0.25 + 0.45 * s.lit) * sparkleGain;
    vec3 gp = s.p * 14.0;
    vec3 gc = floor(gp);
    // z keyed mod one period (64 * 14 cells), so the rebase of the eye's z never re-rolls them.
    float g = hashCell(ivec3(int(gc.x), int(gc.y), int(mod(gc.z, FOAM_PERIOD * 14.0))) + ivec3(0, 0, int(s.cell * 977.0)));
    float tw = 0.5 + 0.5 * sin(FOAM_TAU * fract(SND_TREBLE_TIME * (0.6 + g) + g * 7.0));
    float pt = 1.0 - smoothstep(0.0, 0.3, length(fract(gp) - 0.5));
    col += sparkleCol * step(0.93 - 0.12 * gFoamHats, g) * pt * tw * (0.04 + 1.5 * gFoamHats) * sparkleGain;
    return col;
}

/** Pearl and chrome: the big shells mirror the end light, the deep beads are solid nacre. */
vec3 foamPearl(FoamSurf s) {
    float back = foamSat(dot(s.n, gFoamFwd));
    vec3 nacre = mix(vec3(0.93, 0.89, 0.85), foamColor(0.1 + 0.35 * s.trapR + 0.15 * s.trapX), 0.35);
    vec3 sheen = foamColor(0.55 + 0.5 * s.head + 0.3 * s.trapR);
    vec3 r = reflect(s.rd, s.n);
    float spec = pow(foamSat(dot(r, gFoamFwd)), 16.0 + 70.0 * gFoamTreble);
    // Lace: the orbit traps banded, so the nested levels show as fine rings on the nacre.
    float lace = 0.75 + 0.25 * smoothstep(0.3, 0.7, abs(fract(s.trapR * 6.0 + s.trapX * 2.0) - 0.5) * 2.0);
    vec3 col = nacre * lace * (0.10 + 0.5 * s.head + 0.3 * back) * (0.55 + 0.6 * s.lit) * s.ao;
    col += sheen * s.fres * 0.6 * s.ao;
    float chrome = 1.0 - smoothstep(2.0, 5.0, s.scaleLog);
    col = mix(col, foamBackdrop(r) * 1.8 + sheen * s.fres * 0.4, 0.55 * chrome);
    col += vec3(spec) * (0.5 + 0.4 * gFoamTreble) * s.ao;
    return col + foamAccents(s, vec3(1.0), 1.0);
}

/** Soap: thin-film colour that shifts with the viewing angle, over clear bubbles. */
vec3 foamSoap(FoamSurf s) {
    float thick = 0.3 + 0.7 * fract(s.trapR * 1.7 + s.cell * 3.1);
    float path = thick * (1.0 + 1.5 * (1.0 - s.head));
    vec3 film = 0.5 + 0.5 * cos(FOAM_TAU * (2.0 * path + vec3(0.0, 0.33, 0.67) + gFoamHue));
    float spec = pow(foamSat(dot(reflect(s.rd, s.n), gFoamFwd)), 24.0 + 60.0 * gFoamTreble);
    vec3 col = film * (0.05 + 0.75 * s.fres + 0.2 * s.lit) * s.ao;
    col += foamBackdrop(reflect(s.rd, s.n)) * 0.5 * s.fres;
    col += vec3(spec) * 0.7;
    return col + foamAccents(s, film, 0.6);
}

/** Neon cut rings: a dark foam where only the circles the cut makes of each shell glow. */
vec3 foamNeon(FoamSurf s) {
    float w = 2.5 * s.px;
    float ring = 1.0 - smoothstep(0.0, w, abs(s.cut));
    float halo = exp(-abs(s.cut) / (8.0 * w));
    vec3 tube = foamColor(0.15 + 0.6 * s.trapR + 0.2 * s.trapX);
    vec3 col = tube * (ring * 1.6 + halo * 0.35) * (0.6 + 0.6 * s.lit);
    col += foamColor(0.6) * s.fres * s.fres * 0.25;
    col += vec3(0.01, 0.01, 0.02) * s.head;
    return col + foamAccents(s, tube, 0.5);
}

/** Fog toward the end light, then the glow and the rings gathered along the ray. */
vec3 foamAtmosphere(vec3 col, FoamHit h, float fogRate, float glowGain) {
    vec3 fog = foamColor(0.62) * 0.1;
    if (h.t > 0.0) col = mix(col, fog, 1.0 - exp(-h.t * fogRate));
    float norm = max(h.glow, 1.0e-6);
    float glowHue = 0.25 + 0.5 * foamSat((h.glowTrap / norm) * 0.75);
    col += foamColor(glowHue) * (1.0 - exp(-h.glow * 4.0)) * (0.14 + 0.22 * gFoamSurgeBump) * glowGain;
    col += gFoamAccent * (1.0 - exp(-h.rings * 5.0)) * (0.15 + 0.75 * gFoamOnset);
    return col;
}

/** Tone map, screen-space vignette and the user's grade. */
vec4 foamFinish(vec3 col) {
    col *= 0.85 + 0.25 * foamSat(SND_INTENSITY);
    col = vec3(1.0) - exp(-max(col, 0.0) * 1.4);
    vec2 nd = vUv * 2.0 - 1.0;
    col *= 1.0 - 0.11 * dot(nd, nd);
    return vec4(grade(col), 1.0);
}

/** The whole bore pipeline for a style that only picks a look: camera, march, surface, atmosphere. */
vec3 foamBoreFrame(vec2 uv, int look, out FoamHit h) {
    vec3 ro;
    vec3 rd;
    foamCamera(uv, ro, rd);
    h = foamMarch(ro, rd, FOAM_FAR);
    vec3 col = h.tEnd > FOAM_FAR ? foamBackdrop(rd) : foamColor(0.62) * 0.1;
    if (h.t > 0.0) {
        FoamSurf s = foamSurface(ro, rd, h);
        if (look == 0) col = foamPearl(s);
        else if (look == 1) col = foamNeon(s);
        else col = foamSoap(s);
    }
    return col;
}
