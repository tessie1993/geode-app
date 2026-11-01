// The Lucid family's shared kit: a field that reads itself through itself.
//
// WHY THIS EXISTS
//
// Every Lucid style is the same idea seen through a different lens: take a
// noise field, use what it says at a point to decide WHERE to read it next,
// and do that a few levels deep. Each level folds the one beneath it, so a
// plain fbm turns into marbled, self-similar, slowly breathing tissue - the
// look people call psychedelic. The general technique is domain warping (Inigo
// Quilez's articles on it are the usual reference); what follows is this
// family's own formulation of it, written for a phone GPU.
//
// Twelve styles share one formulation so that one slider means one thing:
// "warp strength" is the same push in every Lucid style, "depth" is the same
// number of levels, and the Detail budget (uSteps) trims every one of them the
// same way. What makes the styles different is everything AROUND the nest -
// the mapping that feeds it, the recolour that reads it, and above all which
// sound moves what - and that lives in each style file, not here.
//
// STATELESS ON PURPOSE
//
// There is no previous-frame texture for these styles. Everything is computed
// per pixel, per frame, from procedural noise and the uniforms. That rules out
// feedback trails, and it is also what makes the family safe to export: the
// offscreen renderer draws frame N without having drawn frame N-1.
//
// BUDGET
//
// The nest costs (depth + 1) * 2 fbm reads, each `octaves` noise taps. The
// caps below are hard: depth <= 5, octaves <= 5, constant loop bounds with a
// break on the requested count, which is what GLSL ES wants and what keeps the
// compiler from unrolling a variable loop into nonsense. lucidBudget() picks
// the lean figure when the user's Detail budget is low (uSteps < 80), so a
// slow phone draws a softer picture instead of a slower one.
//
// INCLUDE ORDER
//
// After lib_scene_uniforms (lucidBudget and lucidView read its uniforms, and
// lucidView reads the style's `in vec2 vUv`). This file includes
// nothing itself: the native resolver does not de-duplicate includes, so a lib
// that pulled in lib_scene_uniforms would declare every uniform twice in any
// style that also includes it.
//
// PHOTOSENSITIVITY
//
// Nothing in here reads an audio uniform. The helpers that shape envelopes
// (lucidBump, lucidSeed) exist so that a style turns a hit into motion that
// EASES in, never a value that steps between two frames: a recursive warp
// amplifies any step into a full-field change, which is exactly what must not
// happen on these styles.

#define LUCID_TAU 6.2831853
#define LUCID_PI 3.14159265
/** Hard cap on nested-warp levels. Each level is two fbm reads. */
#define LUCID_MAX_DEPTH 5
/** Hard cap on fbm octaves. */
#define LUCID_MAX_OCTAVES 5

// Noise kinds for lucidNoise / lucidFbm / lucidNest. A style passes a literal,
// so after inlining the switch costs nothing.
#define LUCID_VALUE 0
#define LUCID_SIMPLEX 1
#define LUCID_RIDGED 2
#define LUCID_SINES 3
#define LUCID_CELLS 4

// ---- hashing ----------------------------------------------------------------
//
// Arithmetic hashes, no sin(): sin() of a large argument loses its fraction on
// mediump-leaning mobile GPUs and the noise turns into visible stripes.

float lucidHash(vec2 p) {
    vec3 q = fract(p.xyx * vec3(0.1453, 0.1621, 0.1379));
    q += dot(q, q.yzx + 27.71);
    return fract((q.x + q.y) * q.z);
}

vec2 lucidHash2(vec2 p) {
    vec3 q = fract(p.xyx * vec3(0.1453, 0.1621, 0.1379));
    q += dot(q, q.yzx + 27.71);
    return fract((q.xx + q.yz) * q.zy);
}

/** A hash of a counter (SND_CUT_COUNT, floor(SND_HIT_TIME)), 0..1. */
float lucidHash1(float n) {
    return lucidHash(vec2(n * 1.618, n * 0.371 + 5.13));
}

mat2 lucidRot(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, s, -s, c);
}

// ---- envelope shaping ---------------------------------------------------------

/**
 * A bump that RISES as an event envelope falls: 0 at the instant of the hit
 * (envelope 1), peak at envelope 0.5, back to 0 as it dies. lib_sound's
 * envelopes rise instantly; feeding one straight into a warp makes the whole
 * field jump on one frame. This one is continuous across the hit.
 */
float lucidBump(float env) {
    float e = clamp(env, 0.0, 1.0);
    return 4.0 * e * (1.0 - e);
}

/**
 * A per-section (or per-hit) random value that slides to its new value instead
 * of stepping. At the frame the count increments the envelope is 1, so the mix
 * still returns the PREVIOUS seed; as the envelope falls it slides to the new
 * one. Feed it SND_CUT_COUNT and SND_HELD_HARD_CUT, or floor(SND_HIT_TIME) and SND_ONSET_ENV.
 */
float lucidSeed(float count, float env) {
    float e = clamp(env, 0.0, 1.0);
    e = e * e * (3.0 - 2.0 * e);
    return mix(lucidHash1(count), lucidHash1(count - 1.0), e);
}

/** Pick `full` normally, `lean` when the user's Detail budget is low. Capped at 5. */
int lucidBudget(int full, int lean) {
    return min(uSteps < 80.0 ? lean : full, 5);
}

// ---- noise --------------------------------------------------------------------

/** Value noise, quintic fade, -1..1. The cheapest kind: four hashes. */
float lucidValue(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float a = lucidHash(i);
    float b = lucidHash(i + vec2(1.0, 0.0));
    float c = lucidHash(i + vec2(0.0, 1.0));
    float d = lucidHash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y) * 2.0 - 1.0;
}

/**
 * Simplex-lattice gradient noise, roughly -1..1. Three corners instead of
 * four and no axis-aligned grid in the result, which matters under a
 * kaleidoscope fold: value noise's square lattice shows up as a visible
 * plaid once it is mirrored eight ways.
 */
float lucidSimplex(vec2 p) {
    const float F = 0.36602540;
    const float G = 0.21132487;
    vec2 i = floor(p + (p.x + p.y) * F);
    vec2 a = p - i + (i.x + i.y) * G;
    vec2 o = a.x > a.y ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    vec2 b = a - o + G;
    vec2 c = a - 1.0 + 2.0 * G;
    vec3 h = max(0.5 - vec3(dot(a, a), dot(b, b), dot(c, c)), 0.0);
    h = h * h;
    h = h * h;
    vec3 ang = LUCID_TAU * vec3(lucidHash(i), lucidHash(i + o), lucidHash(i + 1.0));
    vec3 g = vec3(dot(a, vec2(cos(ang.x), sin(ang.x))),
                  dot(b, vec2(cos(ang.y), sin(ang.y))),
                  dot(c, vec2(cos(ang.z), sin(ang.z))));
    return 64.0 * dot(h, g);
}

/** Ridged: the creases of simplex noise, sharpened. -1..1, mostly near the top. */
float lucidRidged(vec2 p) {
    float r = 1.0 - abs(lucidSimplex(p));
    return r * r * 2.0 - 1.0;
}

/**
 * Three interfering sine plane waves. Not noise at all, which is the point: a
 * lattice warped through itself reads as woven or crystalline where a noise
 * nest reads as smoke.
 */
float lucidSines(vec2 p) {
    float s = sin(p.x * 1.31 + sin(p.y * 0.73));
    s += sin(dot(p, vec2(-0.66, 1.13)) + 1.9);
    s += sin(dot(p, vec2(-0.71, -1.07)) * 1.17 + 4.1);
    return s * 0.3333;
}

/** Cellular: x = distance to the nearest site, y = second nearest, z = the nearest cell's hash. */
vec3 lucidCells(vec2 p, float jitter) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float f1 = 8.0;
    float f2 = 8.0;
    float id = 0.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 g = vec2(float(x), float(y));
            vec2 r = g + lucidHash2(i + g) * jitter - f;
            float d = dot(r, r);
            if (d < f1) {
                f2 = f1;
                f1 = d;
                id = lucidHash(i + g + 17.0);
            } else if (d < f2) {
                f2 = d;
            }
        }
    }
    return vec3(sqrt(f1), sqrt(f2), id);
}

float lucidNoise(vec2 p, int kind) {
    if (kind == LUCID_SIMPLEX) return lucidSimplex(p);
    if (kind == LUCID_RIDGED) return lucidRidged(p);
    if (kind == LUCID_SINES) return lucidSines(p);
    if (kind == LUCID_CELLS) {
        vec3 c = lucidCells(p, 0.85);
        return clamp((c.y - c.x) * 2.4, 0.0, 1.0) * 2.0 - 1.0;
    }
    return lucidValue(p);
}

/** Octave rotation: an irrational-ish turn so octaves never line up on one axis. */
const mat2 LUCID_OCT_ROT = mat2(0.80, 0.60, -0.60, 0.80);

/** Normalised fbm, -1..1, up to LUCID_MAX_OCTAVES octaves. */
float lucidFbm(vec2 p, int kind, int octaves) {
    float sum = 0.0;
    float amp = 0.5;
    float norm = 0.0;
    for (int o = 0; o < LUCID_MAX_OCTAVES; o++) {
        if (o >= octaves) break;
        sum += amp * lucidNoise(p, kind);
        norm += amp;
        p = LUCID_OCT_ROT * p * 2.03 + vec2(13.7, 7.1);
        amp *= 0.5;
    }
    return sum / max(norm, 1e-4);
}

// ---- the Customize macros ------------------------------------------------------
//
// The six Customize macros move standard scene params; a Lucid style answers
// them with no uniforms of its own. Speed is uSpeed on the clock, Energy is
// uIntensity on the audio amounts, Color is pal(), Space is uZoom/uRotation -
// those a style reads directly. The three below need a shared reading so they
// mean the same thing on all twelve styles.
//
// view() is NOT used by this family. It spends uMorph on a polar remap and
// uTurbulence/uWarp on a sin wobble of the domain - which, upstream of a
// recursive warp, is a second, cruder warp fighting the real one. lucidView()
// is view() minus those three (and minus the retired beat terms), so here
// uMorph can mean "more detail" and uTurbulence/uWarp can mean "a wilder
// nest" without the domain being bent twice. Everything else - pixelate,
// mirror, the drift ping-pong, rotation, sway, zoom with its phase, twist,
// tile, ripple - is kept verbatim so those sliders still mean one thing.

/**
 * view() for the Lucid family. `fold` false skips the user's kaleidoscope,
 * for styles that do their own fold and read uKaleido/uSymmetry themselves
 * (folding the plane twice would square the segment count).
 */
vec2 lucidView(bool fold) {
    vec2 uv = vUv * 2.0 - 1.0;
    uv.x *= uResolution.x / uResolution.y;
    if (uPixelate > 0.001) {
        float px = mix(1.0, 12.0, uPixelate) * 24.0;
        uv = floor(uv * px) / px;
    }
    if (uMirrorX > 0.5) uv.x = abs(uv.x);
    if (fold && uKaleido > 0.5 && uSymmetry >= 2.0) {
        float ang = atan(uv.y, uv.x);
        float rad = length(uv);
        float seg = 6.2831853 / uSymmetry;
        ang = abs(mod(ang, seg) - seg * 0.5);
        uv = vec2(cos(ang), sin(ang)) * rad;
    }
    float a = uRotation + uSway * 0.35 * sin(uTime * 0.7);
    uv = mat2(cos(a), -sin(a), sin(a), cos(a)) * uv;
    // Drift after the spin, so the rotation pivots on the screen centre.
    vec2 driftPhase = fract(vec2(uDriftX, uDriftY) * uTime * 0.025 + 0.25);
    uv += 1.0 - 2.0 * abs(2.0 * driftPhase - 1.0);
    gZoomRead = true;
    float z = uZoom * gZoomMag;
    uv /= max(z, 0.05);
    if (abs(uTwist) > 0.001) {
        float tr = length(uv) * uTwist * 2.0;
        uv = mat2(cos(tr), -sin(tr), sin(tr), cos(tr)) * uv;
    }
    if (uTile > 1.01) {
        uv = 1.0 - abs(mod(uv * uTile * 0.5 + 1.0, 4.0) - 2.0);  // mirrored: a hard wrap cracks
    }
    if (uRipple > 0.001) {
        float r = length(uv);
        uv *= 1.0 + uRipple * 0.15 * sin(r * 14.0 - uTime * 3.0 + sndLevel(SND_BASS) * 4.0);
    }
    return uv;
}

/**
 * Chaos: the multiplier on warp strength. 1 at the defaults (uTurbulence and
 * uWarp both 0), so the look at default is the tuned one.
 */
float lucidChaos() {
    return 1.0 + 0.9 * clamp(uTurbulence, 0.0, 2.0) + 0.7 * clamp(uWarp, 0.0, 2.0);
}

/**
 * Chaos, the irregularity half: a slow per-region jitter of the domain, 0 at
 * the defaults. Added to a nest's input so the folds stop being even.
 */
vec2 lucidJitter(vec2 p, float t) {
    float k = 0.25 * clamp(uTurbulence, 0.0, 2.0) + 0.2 * clamp(uWarp, 0.0, 2.0);
    if (k <= 0.0) return vec2(0.0);
    return k * vec2(lucidValue(p * 0.7 + t * 0.05), lucidValue(p * 0.7 + vec2(4.3, 1.9) - t * 0.04));
}

/**
 * Complexity: lucidBudget plus one level once uMorph passes 0.5, still capped
 * at 5. A step, not a blend - a nest level cannot be half there - but it only
 * ever moves when the user moves the slider.
 */
int lucidDetail(int full, int lean) {
    return min(lucidBudget(full, lean) + (uMorph >= 0.5 ? 1 : 0), 5);
}

// ---- the nest -------------------------------------------------------------------

/**
 * The family's core: a field read through itself `depth` levels deep.
 *
 * Evaluated inside-out. The deepest level is the coarsest and slowest: it is
 * read at p scaled down by sqrt(2) per level, turned by its own angle and
 * drifting along its own direction at its own rate. Its two-channel answer
 * displaces where the level above it reads, by `strength`, and so on up to
 * the surface. The levels drift at different speeds and in different
 * directions, so the folds slide over each other rather than translating as
 * one sheet, which is what keeps it from reading as a scrolling texture.
 *
 * Returns xy = the surface displacement (about -1..1), z = the field read at
 * the fully displaced point, w = how far the displacement moved per level on
 * average (a "fold energy" a recolour can use to find the creases).
 */
vec4 lucidNest(vec2 p, int depth, float strength, float t, int kind, int octaves) {
    vec2 w = vec2(0.0);
    float travel = 0.0;
    for (int i = 0; i < LUCID_MAX_DEPTH; i++) {
        if (i >= depth) break;
        float level = float(depth - 1 - i);
        float scale = exp2(-0.5 * level);
        vec2 drift = vec2(cos(1.7 * level + 0.4), sin(2.3 * level + 1.1)) * t * (0.09 + 0.04 * level);
        vec2 q = lucidRot(0.61 * level + 0.3) * (p * scale) + strength * w + drift;
        vec2 nw = vec2(lucidFbm(q, kind, octaves), lucidFbm(q + vec2(5.2, -3.7), kind, octaves));
        travel += length(nw - w);
        w = nw;
    }
    float s = lucidFbm(p + strength * w + vec2(1.7, 9.2), kind, octaves);
    return vec4(w, s, travel / max(float(depth), 1.0));
}

// ---- mappings ---------------------------------------------------------------------

/** Kaleidoscope: fold the angle into one mirrored wedge of `segments`. */
vec2 lucidFold(vec2 p, float segments) {
    float seg = LUCID_TAU / max(segments, 1.0);
    float a = atan(p.y, p.x);
    a = abs(mod(a + 0.5 * seg, seg) - 0.5 * seg);
    return vec2(cos(a), sin(a)) * length(p);
}

/** (radius, angle). */
vec2 lucidPolar(vec2 p) {
    return vec2(length(p), atan(p.y, p.x));
}

/** (log radius, angle): scaling the plane becomes sliding along x, which is how an endless zoom is made. */
vec2 lucidLogPolar(vec2 p) {
    return vec2(log(max(length(p), 1e-5)), atan(p.y, p.x));
}

/** Hexagonal tiling of unit spacing. xy = position inside the cell (centre 0), zw = the cell's id. */
vec4 lucidHex(vec2 p) {
    const vec2 s = vec2(1.0, 1.7320508);
    vec4 c = floor(vec4(p, p - vec2(0.5, 1.0)) / s.xyxy) + 0.5;
    vec4 h = vec4(p - c.xy * s, p - (c.zw + 0.5) * s);
    return dot(h.xy, h.xy) < dot(h.zw, h.zw) ? vec4(h.xy, c.xy) : vec4(h.zw, c.zw + 0.5);
}

/** Distance from a hex cell's centre to its edge, 0.5 at the edge midpoints. */
float lucidHexEdge(vec2 l) {
    l = abs(l);
    return max(dot(l, vec2(0.5, 0.8660254)), l.x);
}

// ---- recolour -----------------------------------------------------------------------

/**
 * Rotate the channel order by `turn` (wraps every 3): 0 = rgb, 1 = gbr,
 * 2 = brg, eased in between. A channel swap is the cheapest way to make a
 * field that reads as one hue suddenly read as its complement; easing it
 * makes that a drift instead of a cut.
 */
vec3 lucidChannelTurn(vec3 c, float turn) {
    float k = mod(turn, 3.0);
    float f = fract(k);
    f = f * f * (3.0 - 2.0 * f);
    vec3 a = c;
    vec3 b = c.gbr;
    if (k >= 2.0) {
        a = c.brg;
        b = c;
    } else if (k >= 1.0) {
        a = c.gbr;
        b = c.brg;
    }
    return mix(a, b, f);
}

/** The cubic contrast S-curve: 0 leaves c alone, 1 is the full Hermite curve. */
vec3 lucidCubic(vec3 c, float amount) {
    c = clamp(c, 0.0, 1.0);
    return mix(c, c * c * (3.0 - 2.0 * c), amount);
}

/** Cubing: crushes the mids toward black so only the crests of the field stay lit. */
vec3 lucidCube(vec3 c) {
    c = max(c, 0.0);
    return c * c * c;
}

/** A thin-film interference ramp: thickness in wavelengths -> rainbow, the soap/oil look. */
vec3 lucidFilm(float thickness) {
    return 0.5 + 0.5 * cos(LUCID_TAU * (thickness + vec3(0.0, 0.33, 0.67)));
}
