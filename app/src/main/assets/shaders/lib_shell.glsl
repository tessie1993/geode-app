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
// included (the style) wins over the default here. The hooks are read only
// inside the SHELL_FIELD* and SHELL_SLACK macros, which expand where the style
// uses them, so the functions below take the amounts as arguments.
//
// THE DEFAULT IS THE OLD PICTURE: every control rests at exactly 0, and with
// every hook at 0, SHELL_FIELD3(p, F) is F(p), SHELL_FIELD2(uv, F) is F(uv) and
// SHELL_SLACK(extent) is 0.0, and nothing else is evaluated. No hook turns
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
// field is written above the line that hands it to SHELL_FIELD3 / SHELL_FIELD2.

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

// The evaluation in flight (SHELL_FIELD3 runs as one expression: GLSL has no
// function values, so the macro calls the style's field and these keep the rest).
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

/** The result, after the field was read once more where the nearest extra copy lies (the value is ignored). */
float shellRedo3(float ignored) {
    return gShellD;
}

/**
 * The style's field F (float F(vec3)) at p with the hooks applied: exactly F(p) while all are 0.
 * Every extra read sits behind its own amount (the ternary evaluates one branch), so the cost at rest
 * is one read, as before. p may be any expression; it is read once.
 */
#define SHELL_FIELD3(p, F) (shellIdle(SHELL_SPAWN, SHELL_FRACTAL, SHELL_FRONT, SHELL_BACK) ? F(p) : (shellBegin3((p), SHELL_FRONT, SHELL_BACK), shellFirst3(F(gShellQ)), (SHELL_SPAWN > 0.0 ? shellTake3(F(shellSpawnAt(SHELL_SPAWN)), shellSpawnAt(SHELL_SPAWN)) : 0.0), (SHELL_FRACTAL > 0.0 ? shellTakeChild3(F(shellChildAt()), SHELL_FRACTAL) : 0.0), (gShellWin > 0.5 ? shellRedo3(F(gShellWP)) : gShellD)))

// ---- 3D: the shortcuts ---------------------------------------------------------

/**
 * How much farther than `extent` the grown world can reach (world units), for a shortcut sized `extent`:
 * a bound's radius, a slab's half-height, a give-up height or distance, each measured from where the
 * layers are (the subject, or the eye's axis). Spawn's copy slides by its shift, Fractal's children
 * reach half a feature size out of the surface, and a layer opens what lies within the bound by up to
 * 18% (draws it in by 11%, which moves a bound off the centre toward it by as much). Exactly 0.0 while
 * all four hooks are off, so `extent + slack` is `extent`, bit for bit.
 */
float shellSlack(float extent, float spawn, float fractal, float front, float back) {
    if (shellIdle(spawn, fractal, front, back)) return 0.0;
    shellFrame3();
    float shift = gShellAround ? 0.3 * gShellNear * spawn : gShellSize * spawn;
    float grown = shift + 0.5 * gShellSize * fractal;
    return grown + 0.18 * max(abs(front), abs(back)) * (abs(extent) + grown);
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

/** The first copy's density, kept for the children's weight (the macro sequences it first with a comma). Returns it. */
float shellDensityFirst3(float d) {
    gShellPar = d;
    return d;
}

/** Fractal's children's share: inside the first's gas, none in clear air. */
float shellDensityChild3(float d, float fractal) {
    return fractal * d * clamp(4.0 * gShellPar, 0.0, 1.0);
}

/**
 * The style's density F (float F(vec3), 0 in clear air) at p with the hooks applied: exactly F(p)
 * while all are 0, read once; each extra read sits behind its own amount.
 */
#define SHELL_DENSITY3(p, F) (shellIdle(SHELL_SPAWN, SHELL_FRACTAL, SHELL_FRONT, SHELL_BACK) ? F(p) : (shellBegin3((p), SHELL_FRONT, SHELL_BACK), shellDensityFirst3(F(gShellQ)), gShellPar + (SHELL_SPAWN > 0.0 ? SHELL_SPAWN * F(shellSpawnAt(SHELL_SPAWN)) : 0.0) + (SHELL_FRACTAL > 0.0 ? shellDensityChild3(F(shellChildAt()), SHELL_FRACTAL) : 0.0)))

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
// pattern through SHELL_FIELD2W. Opening round a fixed origin would slide the
// whole ground under the eye and jump at every wrap.

/** Where a ground pattern's layers are measured from (the eye on the ground), and how far its front reaches. */
vec2 gShellCentre2 = vec2(0.0);
float gShellReach2 = 1.0;

/** Tell SHELL_FIELD2W where the eye stands on the ground (world units) and the reach: front within 0.3 x, back from 1.2 x. */
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

#define SHELL_F2_A(uv, c, r, F) F(shellOpen2((uv), c, r, SHELL_FRONT, SHELL_BACK))
#define SHELL_F2_B(uv, c, r, F) F(shellSpawn2(shellOpen2((uv), c, r, SHELL_FRONT, SHELL_BACK), SHELL_SPAWN))
#define SHELL_F2_C(uv, c, r, F) F(shellFold2(shellOpen2((uv), c, r, SHELL_FRONT, SHELL_BACK)))

/**
 * The style's pattern F (float, vec3 or vec4 F(vec2)) at uv with the hooks applied, the layers measured
 * round c with reach r: exactly F(uv) while all are 0, read once. Pass uv as a variable: it is named
 * more than once.
 */
#define SHELL_FIELD2_ROUND(uv, c, r, F) (shellIdle(SHELL_SPAWN, SHELL_FRACTAL, SHELL_FRONT, SHELL_BACK) ? F(uv) : (SHELL_SPAWN > 0.0 ? (SHELL_FRACTAL > 0.0 ? shellBlend2(shellBlend2(SHELL_F2_A(uv, c, r, F), SHELL_F2_B(uv, c, r, F), SHELL_SPAWN), SHELL_F2_C(uv, c, r, F), SHELL_FRACTAL) : shellBlend2(SHELL_F2_A(uv, c, r, F), SHELL_F2_B(uv, c, r, F), SHELL_SPAWN)) : (SHELL_FRACTAL > 0.0 ? shellBlend2(SHELL_F2_A(uv, c, r, F), SHELL_F2_C(uv, c, r, F), SHELL_FRACTAL) : SHELL_F2_A(uv, c, r, F))))

/** A screen pattern (uv about the frame's centre, aspect-corrected): the layers round the centre. */
#define SHELL_FIELD2(uv, F) SHELL_FIELD2_ROUND(uv, vec2(0.0), 1.0, F)

/** A ground pattern at world xz: the layers round the eye shellSetup2 named. */
#define SHELL_FIELD2W(xz, F) SHELL_FIELD2_ROUND(xz, gShellCentre2, gShellReach2, F)
