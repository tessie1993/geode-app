// lib_shell: every style's shape as normalized inputs, and the four grow hooks
// (Spawn, Fractal, Front layer, Back layer) and the formula hook, applied to a
// style's own field from outside it.
//
// ---- the inputs ---------------------------------------------------------------
//
// A style's variables (scale, thickness, counts, angles, iteration count,
// twist, warp, density, layer offsets, glow ...) are each one normalized input
// n, 0..1, with 0.5 its rest. The style sets n from its params and the six
// knobs in one block at the top of main (each line names what drives it), and
// maps it to its range in the same place with shellMap:
//
//     n 0 -> lo, n 0.5 -> exactly rest (today's constant), n 1 -> hi.
//
// The knobs rest at exactly 0.5 while Note & key reactions are off, and the
// params a style folds in (Morph, Warp, Turbulence: 0 at their defaults) enter
// as 0.5 + 0.5 x param, so at the defaults every variable is the constant the
// style was tuned with and the picture at rest is unchanged. The music reaches
// these inputs only through the params and the knobs (the reaction matrix and
// Note & key): nothing here reads the sound frame (the owner's rule: no
// hardcoded reactions). Nothing here reads uTime either, so no clock times a
// live dial.
//
// ---- the hooks ----------------------------------------------------------------
//
//   SHELL_SPAWN    a second copy of the style's world. At 0 there is none; as it
//                  rises a copy slides off the first (along the axis a flown
//                  world travels, beside a subject built round the origin).
//   SHELL_FRACTAL  the world again at a third of its size, repeated, kept within
//                  a shell round the first's surface that thickens as it rises:
//                  smaller self-similar copies grow on the subject.
//   SHELL_FRONT    how far the near layer opens (up to ~18%) or draws in.
//   SHELL_BACK     the same for the far layer.
//   SHELL_FORMULA  a style's own formula choice (read by the style, not here).
//
// Each reads its Customize › Shape › Grow control (lib_scene_uniforms: uSpawn,
// uFractal, uFrontLayer, uBackLayer, uFormula; core/viz/ParamTable.inc), clamped
// to the control's range, so a route or a wave that overshoots never takes a
// hook past what it was tuned for. A definition made before this file is
// included (the style) wins over the default here. The hooks are read where
// the style names them (its wrappers below and SHELL_SLACK), so the functions
// here take the amounts as arguments.
//
// ---- the wrappers ---------------------------------------------------------------
//
// A style hands its field F to the hooks through a wrapper function of its
// own, written right after F, in one of three shapes (F named once in each,
// in a loop the compiler cannot unroll, so a style compiles F once per call
// site, as before the hooks: a field read once per copy, pasted inline, put
// the styles past the GPU's compile budget):
//
//     float fGrown(vec3 p) {                      // a distance field
//         shellStart3(p, SHELL_SPAWN, SHELL_FRACTAL, SHELL_FRONT, SHELL_BACK);
//         float r = 0.0;
//         for (int i = SHELL_FROM; i < 7; i++) {
//             if (shellPass3(i)) r = shellKeep3(i, f(shellAt3(i)));
//         }
//         return r;
//     }
//
// a density takes shellKeepDensity3 and six passes; a 2D pattern of type T
// takes shellStart2, shellPass2, shellAt2 (round the frame's centre, or round
// the eye on the ground: SHELL_ROUND2 or SHELL_GROUND2) and shellBlend2 over
// six passes. Pass 0 reads the first copy; passes 1 and 2 (Spawn's copy,
// Fractal's child) run only while their hook is on, passes 3, 4 and 5 (Life's
// births, lib_grow) only while each birth lives, and a distance's pass 6 (the
// nearest extra copy read again so the globals F leaves are its) only when an
// extra copy is nearest.
//
// THE DEFAULT IS THE OLD PICTURE: every control rests at exactly 0, and with
// every hook and Life at 0 a wrapper reads F once, at the point it was given, and
// returns what F returned; SHELL_SLACK(extent) is 0.0. No hook turns
// anything (the owner removed spin) or touches colour. The music reaches them
// only through their params (routes, waves, envelopes, knobs): nothing here
// reads the sound frame or a clock.
//
// ---- the shortcuts ------------------------------------------------------------
//
// A march takes shortcuts round its style's world: a bounding sphere it skips
// to, a slab it steps over, a floor or a ceiling it stops at, a height it gives
// up past, an escape ball. The hooks move the world past those (a copy slides
// off, children reach out of the surface, a layer opens), so each shortcut is
// widened by SHELL_SLACK(extent) while a hook is on, and is exactly what it was
// at rest. A bound the style's own field evaluates (inside F, at the moved read
// point) moves with the world and needs nothing.
//
// INCLUDE ORDER: after lib_scene_uniforms (uKnob). No prototypes: a style's
// field is written above its wrapper, and the wrapper above its first call.

#ifndef SHELL_SPAWN
#define SHELL_SPAWN clamp(uSpawn, 0.0, 1.0)
#endif
#ifndef SHELL_FRACTAL
#define SHELL_FRACTAL clamp(uFractal, 0.0, 1.0)
#endif
#ifndef SHELL_FRONT
#define SHELL_FRONT clamp(uFrontLayer, -1.0, 1.0)
#endif
#ifndef SHELL_BACK
#define SHELL_BACK clamp(uBackLayer, -1.0, 1.0)
#endif
#ifndef SHELL_FORMULA
#define SHELL_FORMULA clamp(uFormula, 0.0, 1.0)
#endif

/**
 * Where a wrapper's pass loop starts: 0, but read from a uniform (Spawn rests at 0 and never goes below),
 * so the compiler cannot unroll the loop into a copy of the field per pass.
 */
#define SHELL_FROM min(int(uSpawn), 0)

// ---- the inputs ----------------------------------------------------------------

/** n (0..1) to its range, piecewise linear: 0 -> lo, 0.5 -> exactly rest, 1 -> hi. */
float shellMap(float n, float lo, float rest, float hi) {
    n = clamp(n, 0.0, 1.0);
    return n < 0.5 ? mix(lo, rest, 2.0 * n) : mix(rest, hi, 2.0 * n - 1.0);
}

/** Signed n (-1..1) to its range: -1 -> lo, 0 -> exactly rest, 1 -> hi. */
float shellMapS(float n, float lo, float rest, float hi) {
    return shellMap(0.5 + 0.5 * clamp(n, -1.0, 1.0), lo, rest, hi);
}

/** Style knob i (k0 PULSE, k1 TEXTURE, k2 FLOW, k3 TONE, k4 BUILD, k5 SPACE), 0..1; exactly 0.5 at rest. */
float shellKnob(int i) {
    return clamp(uKnob[i], 0.0, 1.0);
}

/** True while all four grow hooks are off: the field is evaluated once, as before. */
bool shellIdle(float spawn, float fractal, float front, float back) {
    return spawn == 0.0 && fractal == 0.0 && front == 0.0 && back == 0.0;
}

// ---- 3D: a distance field ------------------------------------------------------
//
// Where the layers are measured from: the eye of a flown world (shellSetup3 with
// around false), or the subject at the origin (around true). `near` is the
// depth (world units) where the front layer gives way to the back; `size` is the
// world's own feature size, which sets how far Spawn's copy slides, Fractal's
// cell and how far its children reach out of the surface.
//
// A style that never calls shellSetup3 takes the rig's frame (shellFrame3): a
// style flown with camFly grows round the eye on the rig's path, one turned
// round its subject with camAround round the origin, each with lib_camera's
// near depth (lib_grow's gGrowEye, gGrowNear, gGrowAround), so a flown world
// opens round the camera and not round a world origin it left long ago. A style
// that draws from a view of its own (no lib_camera call) grows round its origin.
// A style that moves its eye after camFly (into one period of a repeating
// world, or off the path) calls shellSetup3 with the eye it marches from.

vec3 gShellEye = vec3(0.0);
float gShellNear = 3.0;
float gShellSize = 1.0;
bool gShellAround = true;
/** The way a flown world's spawned copy slides (unit): down the travel axis unless the style says otherwise. */
vec3 gShellAxis = vec3(0.0, 0.0, 1.0);
/** Whether the style said where the layers are measured from (shellSetup3); otherwise the rig's frame is taken. */
bool gShellSet = false;

/** Tell the hooks where the layers are measured from (once per pixel, in main, before the march). */
void shellSetup3(vec3 eye, float near, float size, bool around) {
    gShellEye = eye;
    gShellNear = near;
    gShellSize = size;
    gShellAround = around;
    gShellSet = true;
}

/**
 * The frame the hooks measure from, for a style that never called shellSetup3: the rig's, as lib_camera's
 * camFly or camAround left it (the eye riding the path; the subject at the origin, which an around frame
 * scales about), with the rig's near depth; the defaults above (the origin) for a style off the rig. Only
 * ever called while a hook is on, so the frame at rest is never read.
 */
void shellFrame3() {
    if (gShellSet || !gGrowSet) return;
    gShellEye = gGrowAround ? vec3(0.0) : gGrowEye;
    gShellNear = gGrowNear;
    gShellAround = gGrowAround;
}

/**
 * A flown world whose copies should slide some other way than down the travel axis (unit `axis`):
 * across, for a world of lanes that run along it. Call after shellSetup3.
 */
void shellAxis3(vec3 axis) {
    gShellAxis = axis;
}

// The evaluation in flight (a style's wrapper calls its field once per pass:
// GLSL has no function values, so these keep the rest between the passes).
vec3 gShellP;   // the point asked about
vec3 gShellQ;   // where the field is read: the point opened by the layers
float gShellK;  // the layers' distance factor (1 while both are 0)
float gShellPar; // the first copy's distance
float gShellD;  // the nearest so far
vec3 gShellWP;  // where the nearest extra copy was read
float gShellWin; // 1 when an extra copy is nearest: the field is read there again, last, so the
                 // globals a style's field leaves (material, cell) are the nearest copy's

/** 0 for the front layer, 1 for the back layer from twice `near` on. */
float shellDepth3(vec3 p) {
    return smoothstep(0.5 * gShellNear, 2.0 * gShellNear, length(p - gShellEye));
}

/** How far the layer holding p opens: 1 + 0.18 at full, 1 - 0.11 drawn in all the way; 1 while both are 0. */
float shellOpen3(vec3 p, float front, float back) {
    if (front == 0.0 && back == 0.0) return 1.0;
    float layer = mix(front, back, shellDepth3(p));
    layer *= layer < 0.0 ? 0.6 : 1.0;
    return 1.0 + 0.18 * layer * (0.9 + 0.1 * sin(dot(p, vec3(2.3, 1.7, 2.9))));
}

/** Start an evaluation at p: the opened read point and the distance factor. Returns 0. */
float shellBegin3(vec3 p, float front, float back) {
    shellFrame3();
    gShellP = p;
    gShellWin = 0.0;
    float k = shellOpen3(p, front, back);
    if (gShellAround) {
        // Space scaled about the subject: an exact scaling, distance times k.
        gShellQ = gShellEye + (p - gShellEye) / k;
        gShellK = k;
    } else {
        // Opened about the axis the eye rides (+z); along it space is unscaled, so min(k, 1) is the safe factor.
        vec3 r = p - gShellEye;
        gShellQ = gShellEye + vec3(r.xy / k, r.z);
        gShellK = min(k, 1.0);
    }
    return 0.0;
}

/** The first copy's distance d (read at gShellQ). Returns 0. */
float shellFirst3(float d) {
    gShellPar = d * gShellK;
    gShellD = gShellPar;
    return 0.0;
}

/** Spawn's read point: a flown world's copy slides `size` along its axis at full Spawn, a subject's twin moves aside. */
vec3 shellSpawnAt(float spawn) {
    vec3 shift = gShellAround ? vec3(0.3 * gShellNear * spawn, 0.0, 0.0) : gShellAxis * (gShellSize * spawn);
    return gShellQ - shift;
}

/** Keep an extra copy's distance d (read at `at`) if it is nearer, cleared off a flown eye. Returns 0. */
float shellTake3(float d, vec3 at) {
    d *= gShellK;
    if (!gShellAround) d = max(d, 0.2 * gShellNear - length(gShellP - gShellEye));
    if (d < gShellD) {
        gShellD = d;
        gShellWP = at;
        gShellWin = 1.0;
    }
    return 0.0;
}

/** Fractal's read point: the world three times smaller, repeated every `size`, set half a cell off the parent. */
vec3 shellChildAt() {
    float cell = gShellSize;
    vec3 r = gShellQ + 0.5 * cell;
    r -= cell * floor(r / cell + 0.5);
    return r * 3.0;
}

/** Fractal's child distance d (read at shellChildAt), kept within a shell `size` / 2 deep at full Fractal. Returns 0. */
float shellTakeChild3(float d, float fractal) {
    float child = max(d / 3.0 * gShellK, gShellPar - 0.5 * gShellSize * fractal);
    return shellTake3(child / gShellK, shellChildAt());
}

// The hooks' amounts for the read in flight, and whether they all rest (a wrapper then reads its field
// once, at the point it was given).
float gShellSpawn = 0.0;
float gShellFractal = 0.0;
float gShellFront = 0.0;
float gShellBack = 0.0;
bool gShellIdle = true;

/** Keep the amounts for a wrapper's read. */
void shellAmounts(float spawn, float fractal, float front, float back) {
    gShellSpawn = spawn;
    gShellFractal = fractal;
    gShellFront = front;
    gShellBack = back;
    // Life's births are passes too: while any can live, the wrapper runs its passes.
    gShellIdle = shellIdle(spawn, fractal, front, back) && !(uLife > 0.0);
}

/** Start a wrapper's read of a distance (or a density) at p: the amounts, and the opened read point while a hook is on. */
void shellStart3(vec3 p, float spawn, float fractal, float front, float back) {
    shellAmounts(spawn, fractal, front, back);
    gShellP = p;
    gShellWin = 0.0;
    if (!gShellIdle) shellBegin3(p, front, back);
}

/**
 * Whether pass i runs: 0 always (the first copy); 1 Spawn's copy, 2 Fractal's child, 3..5 Life's births,
 * 6 the nearest extra copy again, each while it is on.
 */
bool shellPass3(int i) {
    if (i == 0) return true;
    if (i < 0 || gShellIdle) return false;
    if (i == 1) return gShellSpawn > 0.0;
    if (i == 2) return gShellFractal > 0.0;
    if (i < 6) return growBorn(i - 3);
    return gShellWin > 0.5;
}

/** Where the birth in flight is read (lib_grow's growBirthIn, from the unopened point in the hooks' frame). */
vec3 gShellBirth = vec3(0.0);

/** Where pass i reads the field: the point given while every hook rests. */
vec3 shellAt3(int i) {
    if (gShellIdle) return gShellP;
    if (i == 0) return gShellQ;
    if (i == 1) return shellSpawnAt(gShellSpawn);
    if (i == 2) return shellChildAt();
    if (i < 6) {
        gShellBirth = growBirthIn(i - 3, gShellP, gShellEye, gShellNear, gShellAround);
        return gShellBirth;
    }
    return gShellWP;
}

/** Pass i's distance d kept: the nearest so far, with the layers' factor; d itself while every hook rests. */
float shellKeep3(int i, float d) {
    if (gShellIdle) return d;
    if (i == 0) {
        shellFirst3(d);
    } else if (i == 1) {
        shellTake3(d, shellSpawnAt(gShellSpawn));
    } else if (i == 2) {
        shellTakeChild3(d, gShellFractal);
    } else if (i < 6) {
        // A birth's distance is in world units already: the layers' factor shellTake3 applies is undone.
        shellTake3(growBirthDist(d) / gShellK, gShellBirth);
    }
    return gShellD;
}

// ---- 3D: the shortcuts ---------------------------------------------------------

/**
 * How much farther than `extent` the grown world can reach (world units), for a shortcut sized `extent`:
 * a bound's radius, a slab's half-height, a give-up height or distance, each measured from where the
 * layers are (the subject, or the eye's axis). Spawn's copy slides by its shift, Fractal's children
 * reach half a feature size out of the surface, and a layer opens what lies within the bound by up to
 * 18% (draws it in by 11%, which moves a bound off the centre toward it by as much); Life's births
 * reach growBirthSlack farther. Exactly 0.0 while all four hooks and Life are off, so `extent + slack`
 * is `extent`, bit for bit.
 */
float shellSlack(float extent, float spawn, float fractal, float front, float back) {
    float born = 0.0;
    if (uLife > 0.0) {
        shellFrame3();
        born = growBirthSlack(gShellNear, gShellAround);
    }
    if (shellIdle(spawn, fractal, front, back)) return born;
    shellFrame3();
    float shift = gShellAround ? 0.3 * gShellNear * spawn : gShellSize * spawn;
    float grown = shift + 0.5 * gShellSize * fractal;
    return born + grown + 0.18 * max(abs(front), abs(back)) * (abs(extent) + grown);
}

/** shellSlack with the hooks' amounts: add it to a march shortcut of size `extent` (0.0 at rest). */
#define SHELL_SLACK(extent) shellSlack((extent), SHELL_SPAWN, SHELL_FRACTAL, SHELL_FRONT, SHELL_BACK)

// ---- 3D: a density (a volume marched in fixed steps) ---------------------------
//
// For a style whose world is a density, not a distance (gas, a glow volume): the
// copies add their density instead of taking the nearer surface. Spawn's copy
// adds Spawn times its density; Fractal's children (the same third-size,
// repeated read point) add Fractal times theirs, but only inside the first's
// gas, so the finer copies grow in the clouds rather than filling the air; the
// layers open space as for a field.

/** Fractal's children's share: inside the first's gas, none in clear air. */
float shellDensityChild3(float d, float fractal) {
    return fractal * d * clamp(4.0 * gShellPar, 0.0, 1.0);
}

/**
 * Pass i's density d kept (passes 0..5; start with shellStart3): the first copy's, then Spawn times the
 * copy's, Fractal's children's share and each birth's density within its ball added; d itself while
 * every hook rests.
 */
float shellKeepDensity3(int i, float d) {
    if (gShellIdle) return d;
    if (i == 0) {
        gShellPar = d;
        gShellD = d;
    } else if (i == 1) {
        gShellD += gShellSpawn * d;
    } else if (i == 2) {
        gShellD += shellDensityChild3(d, gShellFractal);
    } else if (i < 6) {
        gShellD += d * growBirthMask();
    }
    return gShellD;
}

// ---- 2D: a pattern or colour ---------------------------------------------------
//
// For a field F(vec2) returning a float or a vec3 (or vec4) where more is
// brighter or denser: a brightness, a colour, a density. A style whose field is
// a distance hands its negation (or a glow of it). The layers are radial: the
// centre of the frame is the front layer (the subject), the edge the back.
//
// A pattern laid on the world's ground (a height map, a floor's lines, a sea)
// is read at world xz, far from any origin on a flown world, and rebased with
// the eye's wrap. Its layers are measured round the eye instead: the style
// says where the eye stands on that ground with shellSetup2 and reads the
// pattern round SHELL_GROUND2. Opening round a fixed origin would slide the
// whole ground under the eye and jump at every wrap.

/** Where a ground pattern's layers are measured from (the eye on the ground), and how far its front reaches. */
vec2 gShellCentre2 = vec2(0.0);
float gShellReach2 = 1.0;

/** Tell SHELL_GROUND2 where the eye stands on the ground (world units) and the reach: front within 0.3 x, back from 1.2 x. */
void shellSetup2(vec2 centre, float reach) {
    gShellCentre2 = centre;
    gShellReach2 = reach;
}

/** uv with the near and far bands round centre c zoomed by the layers, up to ~18%; uv itself while both are 0. */
vec2 shellOpen2(vec2 uv, vec2 c, float reach, float front, float back) {
    if (front == 0.0 && back == 0.0) return uv;
    vec2 r = uv - c;
    float layer = mix(front, back, smoothstep(0.3 * reach, 1.2 * reach, length(r)));
    layer *= layer < 0.0 ? 0.6 : 1.0;
    return c + r / (1.0 + 0.18 * layer);
}

/** Spawn's copy: mirrored across the vertical axis and slid aside by up to half a screen height. */
vec2 shellSpawn2(vec2 uv, float spawn) {
    return vec2(-uv.x, uv.y) - vec2(0.5 * spawn, 0.0);
}

/** Fractal's copy: the picture's centre at a third of its size, repeated and mirrored edge to edge. */
vec2 shellFold2(vec2 uv) {
    return 1.0 - abs(mod(uv * 3.0 + 1.0, 4.0) - 2.0);
}

/** An extra copy b laid over a by amount w: the brighter of the two, faded in. */
float shellBlend2(float a, float b, float w) { return mix(a, max(a, b), clamp(w, 0.0, 1.0)); }
vec3 shellBlend2(vec3 a, vec3 b, float w) { return mix(a, max(a, b), clamp(w, 0.0, 1.0)); }
vec4 shellBlend2(vec4 a, vec4 b, float w) { return mix(a, max(a, b), clamp(w, 0.0, 1.0)); }

/** A vec2 pattern's copy b laid over a by amount w, as the others. */
vec2 shellBlend2(vec2 a, vec2 b, float w) { return mix(a, max(a, b), clamp(w, 0.0, 1.0)); }

/** Start a wrapper's read of a 2D pattern: the amounts. */
void shellStart2(float spawn, float fractal, float front, float back) {
    shellAmounts(spawn, fractal, front, back);
}

/** Whether pass i runs: 0 always (the pattern, opened by the layers); 1 Spawn's copy, 2 Fractal's, 3..5 Life's births, each while it is on. */
bool shellPass2(int i) {
    if (i == 0) return true;
    if (i < 0 || gShellIdle) return false;
    if (i == 1) return gShellSpawn > 0.0;
    if (i == 2) return gShellFractal > 0.0;
    return i < 6 && growBorn(i - 3);
}

/** Where pass i reads the pattern given uv, its layers round c with reach r: uv itself while every hook rests. */
vec2 shellAt2(vec2 uv, int i, vec2 c, float r) {
    if (gShellIdle) return uv;
    if (i > 2) return growBirth2(i - 3, uv, c, r);
    vec2 o = shellOpen2(uv, c, r, gShellFront, gShellBack);
    if (i == 1) return shellSpawn2(o, gShellSpawn);
    if (i == 2) return shellFold2(o);
    return o;
}

/** How far pass i's copy is laid over the pattern so far: a birth by its ball (growBirth2 just read it). */
float shellWeight2(int i) {
    if (i > 2) return growBirthMask();
    return i == 1 ? gShellSpawn : gShellFractal;
}

/** A screen pattern's layers: round the frame's centre (uv aspect-corrected), the front within 0.3. */
#define SHELL_ROUND2 vec2(0.0), 1.0
/** A ground pattern's layers: round the eye where shellSetup2 says it stands. */
#define SHELL_GROUND2 gShellCentre2, gShellReach2
