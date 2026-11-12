// The depth layers round a marched style (Shard, Prism and Fluor): a far
// fractal structure, a domain-warped haze between it and the eye, and the
// folds and the Formula pick that a style's own marched form is built from.
//
// WHY. A style used to be one marched form on black: one look, recoloured
// from style to style. Here every style has three layers that sit at their
// own depths, move their own way and hear the music through their own
// controls, and the Formula param changes the geometry of all three, not
// only its surface.
//
//   FOREGROUND  the style's own map, marched by lib_<family>_march. A style
//               builds it from strataFold (a kaleidoscope, a Mandelbox's
//               box and sphere fold, an Apollonian inversion or a Menger
//               cut) where it wants a fractal, its iterations gStrataIters.
//   MIDDLE      strataHaze: a gyroid-sheet haze, its domain warped twice
//               over (a sine warp on a sine warp), sampled STRATA_HAZE_N
//               times between the eye and the form. Light, no surface.
//   FAR         strataFar: a lattice of fractals (one to a STRATA_CELL cube)
//               sphere-traced in few steps beyond the form, lit by how fast
//               the trace closed in (a cheap occlusion) and glowing where
//               the ray passes close.
//
// THE FORMULA. uFormula (0..1) is three formulas: 0, 0.5 and 1 each draw
// one, and between them two narrow crossfades (strataFormula) mix the two
// neighbours' distances, never a pop. A style evaluates only the formulas
// with a share (strataUses), so outside the bands it pays for one. The far
// layer takes its fractal kind from the same pick (gStrataFarKinds).
//
// THE CONTROLS AND THE MUSIC. Each layer answers through its own:
//
//   foreground  gStrataIters: Detail and k1 TEXTURE grow the iterations,
//               fractional, the last fold blended in with mix(), so it
//               never pops. Morph and the family's Warp as before.
//   middle      Warp and k2 FLOW warp the haze; the mid band,
//               sndShaped(SND_MID) times SND_DRIVE, pushes the warp further
//               (motion only, never a colour or a brightness). Its drift
//               follows uFlowTime.
//   far         Detail and k4 BUILD grow its iterations; the high band,
//               sndShaped(SND_HIGH) times SND_DRIVE, breathes its folds'
//               offset (the shape, never the light). It drifts along the
//               way on its own clock. k5 SPACE sets its reach with the
//               family's far plane.
//
// Nothing here turns to the music (owner 2026-11-12): no sound read reaches
// an angle. Every clock is whole cycles per uTime wrap (STRATA_RATE), and
// every z period divides 16, the families' world period, so the wrapped eye
// never sees a seam.
//
// COST. The far trace takes 8 steps at the lowest Detail and STRATA_FAR_MAX
// (12) at the highest (fewer on a hot phone, since uSteps carries the cut),
// each one fractal of at most 4 folds (gStrataFarIters, 2..4); in a Formula
// crossfade band it traces twice. The haze takes STRATA_HAZE_N (6) samples of
// a few sines. A family skips the far trace where its form hides it. A
// style's own folds run gStrataIters (1..3) times, at most STRATA_MAX_FOLDS.
//
// INCLUDE ORDER: after lib_scene_uniforms, lib_scene_motion and lib_neon (it
// reads uFormula, uSteps, the knobs and the sound), before lib_<family>.
// strataSetup() runs once per pixel, before the style's map is first called.

#define STRATA_TAU 6.2831853
/** k whole cycles per uTime wrap (7100 s, ShaderScene). */
#define STRATA_RATE(k) (STRATA_TAU * float(k) / 7100.0)
#define STRATA_MAX_FOLDS 6
#define STRATA_FAR_MAX 12
#define STRATA_HAZE_N 6
/** The far lattice's cell, world units: divides 16, the families' world period. */
#define STRATA_CELL 8.0

/** The Formula crossfades (strataFormula), set once per pixel by strataSetup. */
vec2 gStrataW;
/** The foreground's fold iterations (fractional), for a style that folds. */
float gStrataIters;
/** The far layer's fold iterations (fractional). */
float gStrataFarIters;
/** The far fractal's kind at Formula 0, 0.5 and 1: 0 kaleidoscope, 1 Mandelbox, 2 Apollonian, 3 Menger. */
vec3 gStrataFarKinds;
/** The haze's kind: 0 warped gyroid sheets, 1 the sheets folded into a kaleidoscope, 2 an inverted foam. */
float gStrataHazeKind;
/** How far the haze's domain is warped. */
float gStrataWarp;
/** The high band's breath of the far folds, 0..1.5. */
float gStrataBreath;
/** How bright each layer is, the style's own. */
float gStrataHazeGlow;
float gStrataFarGlow;

mat2 strataRot(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, -s, s, c);
}

/**
 * uFormula as two crossfades: x from the first formula to the second about
 * 0.25, y from the second to the third about 0.75, each across a band 0.16
 * wide, so 0, 0.5 and 1 are each one formula alone.
 */
vec2 strataFormula() {
    float f = clamp(uFormula, 0.0, 1.0);
    return vec2(smoothstep(0.17, 0.33, f), smoothstep(0.67, 0.83, f));
}

/** Whether formula i (0, 1 or 2) has a share in the picture now. The bands never overlap. */
bool strataUses(int i) {
    if (i == 0) return gStrataW.x < 1.0;
    if (i == 1) return gStrataW.x > 0.0 && gStrataW.y < 1.0;
    return gStrataW.y > 0.0;
}

/** The three formulas' values mixed by their shares; one with no share may hold anything. */
vec3 strataBlend(vec3 a, vec3 b, vec3 c) {
    return mix(mix(a, b, gStrataW.x), c, gStrataW.y);
}

vec2 strataBlend2(vec2 a, vec2 b, vec2 c) {
    return mix(mix(a, b, gStrataW.x), c, gStrataW.y);
}

/** Detail as 0..1 (uSteps 64..128); the hot-phone cut lowers it with the steps. */
float strataDetail() {
    return clamp((uSteps - 64.0) / 64.0, 0.0, 1.0);
}

/** A fractional iteration count from lo to hi: Detail moves half of it and `knob` (-0.5..0.5, neonKnob) the other half. */
float strataIterations(float lo, float hi, float knob) {
    return lo + (hi - lo) * (0.5 * strataDetail() + 0.5 + clamp(knob, -0.5, 0.5));
}

/**
 * The layers' setup: `far` the far fractal's kind at Formula 0, 0.5 and 1,
 * `haze` the haze's kind, and the shares of light each layer gives.
 */
void strataSetup(vec3 far, float haze, float hazeGlow, float farGlow) {
    gStrataW = strataFormula();
    gStrataFarKinds = far;
    gStrataHazeKind = haze;
    gStrataHazeGlow = hazeGlow;
    gStrataFarGlow = farGlow;
    gStrataIters = strataIterations(1.0, 3.0, neonKnob(1));
    gStrataFarIters = strataIterations(2.0, 4.0, neonKnob(4));
    // The sound, as the families hear it: the absolute drive sets how much, the band only the shape.
    float drive = clamp(SND_DRIVE, 0.0, 1.0);
    gStrataWarp = 0.35 + 0.5 * clamp(uWarp, 0.0, 1.0) + 0.4 * neonKnob(2) + 0.45 * sndShaped(SND_MID) * drive;
    gStrataBreath = sndShaped(SND_HIGH) * drive;
}

/** p's three coordinates sorted, largest first, with max and min (no branch per pair). */
vec3 strataSort(vec3 p) {
    p.xy = vec2(max(p.x, p.y), min(p.x, p.y));
    p.xz = vec2(max(p.x, p.z), min(p.x, p.z));
    p.yz = vec2(max(p.y, p.z), min(p.y, p.z));
    return p;
}

/**
 * The four folds (lib_strata's kinds), one step each; dr carries how much the
 * fold stretched space, so a distance taken after it divides by dr.
 *
 * 0 a kaleidoscope: the mirrors of the axes and of the plane x = y, then scaled
 *   by 2 away from the corner c (sixteen images a fold, not the full
 *   forty-eight, so a few folds stay a cluster and not a ball).
 */
vec3 strataKaleido(vec3 p, vec3 c, inout float dr) {
    p = abs(p);
    p.xy = vec2(max(p.x, p.y), min(p.x, p.y));
    dr *= 2.0;
    return 2.0 * p - c;
}

/** 1 a Mandelbox step: a box fold, a sphere fold, scaled by 2 about c. */
vec3 strataBoxFold(vec3 p, vec3 c, inout float dr) {
    p = clamp(p, -1.0, 1.0) * 2.0 - p;
    float m = clamp(1.0 / max(dot(p, p), 1.0e-4), 1.0, 4.0);
    dr = dr * 2.0 * m + 1.0;
    return 2.0 * m * p + c;
}

/** 2 an Apollonian step: back into the unit cell, then inverted in a sphere (c unused). */
vec3 strataInvert(vec3 p, inout float dr) {
    p = -1.0 + 2.0 * fract(0.5 * p + 0.5);
    float m = 1.3 / max(dot(p, p), 0.03);
    dr *= m;
    return p * m;
}

/** 3 a Menger step: the mirrors of the axes and the planes between them, scaled by 3, the middle cross cut. */
vec3 strataMenger(vec3 p, vec3 c, inout float dr) {
    p = strataSort(abs(p));
    p = 3.0 * p - 2.0 * c;
    p.z += 2.0 * c.z * step(p.z, -c.z);
    dr *= 3.0;
    return p;
}

/**
 * p folded `iters` times by kind k (strataKaleido .. strataMenger), each fold
 * turned by `turn` about z. The last fold is blended in by the fraction of
 * `iters` left, so the count moves smoothly. Never more than
 * STRATA_MAX_FOLDS. The kind is chosen once, outside the loop: one kind per
 * loop, so no fold pays for the others' branches.
 */
vec3 strataFold(vec3 p, float k, float iters, vec3 c, mat2 turn, inout float dr) {
    float n = min(iters, float(STRATA_MAX_FOLDS));
    if (k < 0.5) {
        for (int i = 0; i < STRATA_MAX_FOLDS; i++) {
            float f = clamp(n - float(i), 0.0, 1.0);
            if (f <= 0.0) break;
            float d1 = dr;
            vec3 q = strataKaleido(p, c, d1);
            q.xy = turn * q.xy;
            p = mix(p, q, f);
            dr = mix(dr, d1, f);
        }
    } else if (k < 1.5) {
        for (int i = 0; i < STRATA_MAX_FOLDS; i++) {
            float f = clamp(n - float(i), 0.0, 1.0);
            if (f <= 0.0) break;
            float d1 = dr;
            vec3 q = strataBoxFold(p, c, d1);
            q.xy = turn * q.xy;
            p = mix(p, q, f);
            dr = mix(dr, d1, f);
        }
    } else if (k < 2.5) {
        for (int i = 0; i < STRATA_MAX_FOLDS; i++) {
            float f = clamp(n - float(i), 0.0, 1.0);
            if (f <= 0.0) break;
            float d1 = dr;
            vec3 q = strataInvert(p, d1);
            q.xy = turn * q.xy;
            p = mix(p, q, f);
            dr = mix(dr, d1, f);
        }
    } else {
        for (int i = 0; i < STRATA_MAX_FOLDS; i++) {
            float f = clamp(n - float(i), 0.0, 1.0);
            if (f <= 0.0) break;
            float d1 = dr;
            vec3 q = strataMenger(p, c, d1);
            q.xy = turn * q.xy;
            p = mix(p, q, f);
            dr = mix(dr, d1, f);
        }
    }
    return p;
}

/** strataFold on a plane: u folded with z held at 0, which every kind keeps at 0. */
vec2 strataFold2(vec2 u, float k, float iters, vec2 c, mat2 turn, inout float dr) {
    return strataFold(vec3(u, 0.0), k, iters, vec3(c, 0.0), turn, dr).xy;
}

/**
 * A Menger sponge of half-size `size` about the origin, `iters` folds (fractional): its distance,
 * a bound. A style intersects or cuts its form with it, so the form is pierced at every scale.
 */
float strataSponge(vec3 p, float size, float iters) {
    float dr = 1.0 / size;
    vec3 q = strataFold(p * dr, 3.0, iters, vec3(1.0), mat2(1.0), dr);
    vec3 b = abs(q) - 1.0;
    return (length(max(b, 0.0)) + min(max(b.x, max(b.y, b.z)), 0.0)) / dr;
}

/**
 * The holes a Menger sponge of half-size `size` leaves in its cube, as a solid: its distance.
 * A form cut by them, max(d, -holes), is pierced inside the cube and whole outside it.
 */
float strataHoles(vec3 p, float size, float iters) {
    vec3 b = abs(p) - size;
    float box = length(max(b, 0.0)) + min(max(b.x, max(b.y, b.z)), 0.0);
    return max(box, -strataSponge(p, size, iters));
}

/**
 * Shells within shells: p brought into the band of the outermost shell, radius r0, by a geometric
 * repetition about the origin (each shell inside the last 1 / g its size, g over 1). `iters`
 * shells nest inside the first (fractional: the innermost grows in as `grow` goes 0 to 1). The
 * returned point is in the outer shell's units: a distance taken there times `s` is in world
 * units, and `bound` (world units) is how far a step may go before it crosses into the next band.
 */
vec3 strataNest(vec3 p, float r0, float g, float iters, out float s, out float grow, out float bound) {
    float l = max(length(p), 1.0e-4);
    float lg = log(g);
    float deep = ceil(max(iters, 0.0));
    float j = clamp(floor(log(l / r0) / lg + 0.5), -deep, 0.0);
    s = exp(j * lg);
    grow = j > -deep ? 1.0 : clamp(iters - deep + 1.0, 0.0, 1.0);
    // The band's walls, half a shell either side in log; the outermost and innermost are open.
    float hi = j < 0.0 ? r0 * s * sqrt(g) - l : 1.0e3;
    float lo = j > -deep ? l - r0 * s / sqrt(g) : 1.0e3;
    bound = max(min(hi, lo), 0.0) + 0.02 * s;
    return p / s;
}

/**
 * A fractal of kind k at p (fractal units, about -2..2), `iters` folds: x its
 * distance (a bound), y an orbit value for its hue. Each kind is kept inside
 * a ball of radius 1.9, so a lattice of them stays sparse:
 *
 * 0 a kaleidoscope dust: small balls on the corners of folded octahedra;
 * 1 a Mandelbox, its fold offset the point itself, a crumpled cube;
 * 2 an Apollonian ball: spheres within spheres, cut to a hollow ball;
 * 3 a Menger cube, half the ball's size, its crosses cut through.
 *
 * `breath` moves each kind's folds a little (the far layer's own breathing).
 */
vec2 strataFractal(vec3 p, float k, float iters, float breath, mat2 turn) {
    float dr = 1.0;
    float d;
    vec3 q;
    if (k < 0.5) {
        q = strataFold(p, 0.0, iters, vec3(1.5, 1.5, 0.9 + breath), turn, dr);
        d = (length(q) - 0.9) / dr;
    } else if (k < 1.5) {
        q = p * 1.6;
        dr = 1.6;
        vec3 c = q;
        for (int i = 0; i < STRATA_MAX_FOLDS; i++) {
            float f = clamp(iters - float(i), 0.0, 1.0);
            if (f <= 0.0) break;
            vec3 b = clamp(q, -1.0, 1.0) * 2.0 - q;
            float m = clamp(1.0 / max(dot(b, b), 1.0e-4), 1.0, 4.0);
            vec3 n = -(1.7 + breath) * m * b + c;
            n.xy = turn * n.xy;
            q = mix(q, n, f);
            dr = mix(dr, dr * (1.7 + breath) * m + 1.0, f);
        }
        d = (length(q) - 2.0) / abs(dr);
    } else if (k < 2.5) {
        q = strataFold(p * (1.0 + 0.2 * breath), 2.0, iters, vec3(0.0), turn, dr);
        d = max(0.25 * abs(q.y) / dr, abs(length(p) - 1.5) - 0.35);
    } else {
        q = strataFold(p * 2.0, 3.0, iters, vec3(1.0, 1.0, 1.0 + breath), turn, dr);
        vec3 b = abs(q) - vec3(1.0);
        d = (length(max(b, 0.0)) + min(max(b.x, max(b.y, b.z)), 0.0)) / (2.0 * dr);
    }
    // Every kind kept in its ball, so the lattice's cells stand apart.
    d = max(d, length(p) - 1.9);
    return vec2(d, 0.12 * log2(max(dr, 1.0)) + 0.15 * length(q));
}

/** The far lattice at world point w, fractal kind k: x the distance (world units), y the orbit. */
vec2 strataFarField(vec3 w, float k) {
    // It drifts along the way on its own clock, a whole number of cells per uTime wrap.
    w.z += STRATA_CELL * 40.0 * uTime / 7100.0;
    vec3 cell = floor(w / STRATA_CELL + 0.5);
    vec3 q = (w - STRATA_CELL * cell) * (4.0 / STRATA_CELL);
    // Every cell's fractal turned its own way; along the way the cells repeat every two.
    float h = fract(sin(dot(vec3(cell.xy, mod(cell.z, 2.0)), vec3(127.1, 311.7, 74.7))) * 43758.5453);
    q.xz = strataRot(STRATA_TAU * h) * q.xz;
    // The folds breathe on their own clock, and the highs breathe them further.
    float breath = sin(uTime * STRATA_RATE(160) + STRATA_TAU * h) * (0.06 + 0.1 * gStrataBreath);
    vec2 f = strataFractal(q, k, gStrataFarIters, breath, strataRot(0.3 + 0.4 * h));
    return vec2(f.x * (STRATA_CELL / 4.0), f.y + h);
}

/** One far trace, kind k, from tFrom to tTo: x its light, y its hue, z how much of the ray it covers. */
vec3 strataFarTrace(vec3 ro, vec3 rd, float tFrom, float tTo, float k, float dither) {
    float n = mix(8.0, float(STRATA_FAR_MAX), strataDetail());
    float t = tFrom * (1.0 + 0.05 * dither);
    float glow = 0.0;
    float hue = 0.0;
    float cover = 0.0;
    for (int i = 0; i < STRATA_FAR_MAX; i++) {
        if (float(i) >= n || t > tTo) break;
        vec3 p = ro + rd * t;
        vec2 f = strataFarField(p, k);
        // It fades in from its near edge, so nothing pops in where the layer starts.
        float fade = smoothstep(tFrom, tFrom + 3.0, t) * exp(-1.5 * (t - tFrom) / (tTo - tFrom));
        // The ray passing close to the fractal catches its glow.
        float g = fade * 0.06 / (1.0 + 80.0 * f.x * f.x);
        glow += g;
        hue += g * f.y;
        float eps = 0.004 * t;
        if (f.x < eps) {
            // A shell lit by how many steps the trace took to close in: a grazing ray, along an
            // edge or into a fold, takes many and stays dark, a ray meeting a face takes few.
            float lit = fade * 0.3 * (1.0 - float(i) / n);
            glow += lit;
            hue += lit * f.y;
            cover = fade;
            break;
        }
        t += max(f.x, 0.01 * t);
    }
    return vec3(glow, hue / max(glow, 1.0e-5), cover);
}

/**
 * The far layer along one ray, from tFrom to tTo: x its light, y its hue,
 * z how much of the ray it covers. Its kind follows the Formula; in a
 * crossfade band both neighbours are traced and mixed.
 */
vec3 strataFar(vec3 ro, vec3 rd, float tFrom, float tTo, float dither) {
    bool late = gStrataW.y > 0.0;
    float w = late ? gStrataW.y : gStrataW.x;
    float ka = late ? gStrataFarKinds.y : gStrataFarKinds.x;
    float kb = late ? gStrataFarKinds.z : gStrataFarKinds.y;
    vec3 a = w < 1.0 ? strataFarTrace(ro, rd, tFrom, tTo, ka, dither) : vec3(0.0);
    vec3 b = w > 0.0 ? strataFarTrace(ro, rd, tFrom, tTo, kb, dither) : vec3(0.0);
    return mix(a, b, w);
}

/** The far lattice seen once along a reflection from p: x its light, y its hue (for a mirror's sky). */
vec2 strataGlance(vec3 p, vec3 r, float reach) {
    float k = gStrataW.y > 0.5 ? gStrataFarKinds.z : (gStrataW.x > 0.5 ? gStrataFarKinds.y : gStrataFarKinds.x);
    vec2 f = strataFarField(p + r * reach, k);
    return vec2(0.5 / (1.0 + 40.0 * f.x * f.x), f.y);
}

/** The haze's density at world point w: x the density, y its hue. */
vec2 strataHazeField(vec3 w) {
    // Along the way one cycle every 8 units, and every sine of q.z a whole multiple of it, so the
    // haze repeats every 8 (dividing the world's 16) and the wrapped eye sees no seam. It drifts
    // toward the eye on the Speed-scaled flow clock.
    vec3 q = w * vec3(0.75, 0.75, 0.7853982);
    q.z -= 0.25 * uFlowTime;
    // The domain warped twice over: a sine warp of a sine warp.
    vec3 s = sin(q.yzx * vec3(1.3, 1.0, 1.3) + vec3(0.0, 1.7, 3.1) + uTime * STRATA_RATE(90));
    q += gStrataWarp * s;
    q += 0.5 * gStrataWarp * sin(q.zxy * vec3(2.0, 2.1, 2.1) + vec3(2.3, 0.4, 5.0));
    float sheet;
    if (gStrataHazeKind < 0.5) {
        sheet = abs(dot(sin(q), cos(q.yzx)));
    } else if (gStrataHazeKind < 1.5) {
        vec3 k = strataSort(abs(sin(q)) * 2.0);
        sheet = abs(dot(sin(k * 2.2), cos(k.yzx * 2.2)));
    } else {
        vec3 k = -1.0 + 2.0 * fract(q * vec3(0.15, 0.15, 0.1591549) + 0.5);
        float m = 1.0 / max(dot(k, k), 0.1);
        sheet = abs(length(k * m) - 1.6) / m;
    }
    return vec2(exp(-5.0 * sheet), 0.2 * sin(q.z) + 0.3 * s.x);
}

/** The middle layer along one ray from tFrom to tTo: x its light, y its hue. */
vec2 strataHaze(vec3 ro, vec3 rd, float tFrom, float tTo, float dither) {
    float dt = max(tTo - tFrom, 0.0) / float(STRATA_HAZE_N);
    float glow = 0.0;
    float hue = 0.0;
    for (int i = 0; i < STRATA_HAZE_N; i++) {
        float t = tFrom + (float(i) + dither) * dt;
        vec2 h = strataHazeField(ro + rd * t);
        // Thin by the eye, so flying through it never fills the frame.
        float g = h.x * smoothstep(0.2, 1.5, t) * exp(-0.12 * t);
        glow += g;
        hue += g * h.y;
    }
    return vec2(glow * dt * 0.1, hue / max(glow, 1.0e-5));
}
