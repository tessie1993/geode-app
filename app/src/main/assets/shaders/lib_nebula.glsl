// The Nebula family's shared kit: gas, dust, stars and the volume step that
// puts them on screen, for galaxies, remnants and clouds you fly through.
//
// WHAT A NEBULA IS, FOR A SHADER
//
// Everything in this family is a participating medium, not a surface. Light
// is EMITTED by hot or ionised gas (the glow), ABSORBED by cold dust (the dark
// lanes that silhouette against it) and, in the 3D styles, SCATTERED toward
// the eye by dust lit from a star. A 2D style fakes all three on one layer:
// an emission field, an absorption field multiplied over it, stars behind
// both. A 3D style integrates the same three along the ray, front to back,
// with nebVolStep() below. The look of the whole family comes from getting
// that one composition right, which is why it lives here rather than in
// twelve copies.
//
// WHY ONE LIBRARY
//
// Twelve styles draw gas. Each of the following is a bug that does not look
// like one until the style has been on a phone for a minute, so each is
// solved once:
//
//   NOISE THAT DOES NOT POP. The octave count is a FLOAT (nebOctaves): the
//   last octave fades in by its fractional part. The Complexity macro (uMorph)
//   and MotionField move uMorph continuously, and an integer octave count
//   stepped the fine detail in and out of the cloud on the music. The fbm
//   normalises by the weight it actually summed, so the mean stays at 0.5
//   however many octaves are in play: the Detail control trims texture, never
//   brightness.
//
//   STARS THAT DO NOT CRACK. nebStars() hashes one star per grid cell and draws
//   it within the cell, its halo included: the jitter shrinks as the star
//   grows so nothing ever reaches the cell border. A star field built that way
//   is continuous across every cell edge, which is what rule 5 of the house
//   style asks of any cell pattern. Each layer is ONE cell lookup, not a loop
//   over stars, so three layers cost three hashes and a few multiplies.
//
//   SPIRALS WITHOUT A SEAM. atan() jumps by 2*pi on the negative-x axis. A
//   log spiral is cos(m * (theta - ln r / tan(pitch))): with m a whole number
//   the jump is m whole turns and vanishes. nebSpiralWave() rounds its arm
//   count, so a style cannot open the seam by passing 2.5.
//
//   A FLIGHT THAT DOES NOT JUMP. The rig's z wraps at 8192 (lib_camera). The
//   3D value noise wraps its lattice z at NEB_Z_WRAP = 256 cells, so a field
//   sampled at world z times a power of two of at least 1/32 lands back on
//   the same lattice cell at the wrap, and octaves with lacunarity exactly 2
//   keep that true at every scale. nebWeb3 is periodic by construction.
//
// THE VOLUME BUDGET
//
// A volume style has ONE loop, bound NEB_VOL_MAX (96), breaking on uSteps
// (the Detail control) and on transmittance under NEB_TRANS_MIN. Inside it:
// at most four noise octaves per sample (nebVolOctaves), one density tap
// toward the light for shading (never a second march - nebLightTap), and a
// step that grows with distance (nebVolDt). nebVolDt also lengthens every
// step when the budget is low, so a short budget still reaches the far side
// of the cloud and draws it coarser instead of cutting it off. Start the march
// at a per-pixel dither (nebDither) so the step pattern reads as fine grain,
// not as contour bands.
//
// THE CUSTOMIZE MACROS
//
// The app's macros move standard scene params. Every Nebula style answers
// them through the helpers below, and at their defaults each is the identity:
// Speed is uSpeed on time (nebTime, nebFlow); Complexity is uMorph on octaves
// (nebOctaves); Chaos is uWarp and uTurbulence as a curl-like domain warp
// (nebWarp2, nebWarp3) - a curl field rather than a plain displacement
// because it swirls the gas without pinching it into hard bright knots; uTwist
// is applied by view() on the screen, which keeps it from swinging a
// fly-through cloud around the world origin far off the camera's path; Energy
// is uIntensity as a gain on in-shader audio (nebEnergy); Colour is pal();
// Space is view() for the flat styles and the rig for the marched ones.
//
// INCLUDE ORDER: last, after lib_scene_uniforms, lib_scene_motion (nebFlow and
// nebKeyShift read it) and lib_palette (the colour ramps call pal()). It
// includes nothing itself: the native resolver pastes includes again rather
// than skipping them, so a lib that pulled in another would redeclare it.
//
// PHOTOSENSITIVITY: no helper here reads a hit or event uniform. Twinkle is
// an argument, and it dims stars around their level rather than lifting the
// frame. What moves on a hit is each style's business, and the rule there is:
// move geometry and hue, keep full-frame brightness on uSwell.

#define NEB_TAU 6.2831853
#define NEB_PI 3.14159265
/** Hard cap on 2D fbm octaves. */
#define NEB_MAX_OCT 5
/** Hard cap on octaves per volume sample. */
#define NEB_VOL_OCT 4
/** Star-field layers, compile-time ceiling. */
#define NEB_STAR_LAYERS 3
/** The volume loop's constant bound. A style breaks below it on uSteps. */
#define NEB_VOL_MAX 96
/** Transmittance under which a volume ray is done: what is behind cannot show. */
#define NEB_TRANS_MIN 0.02
/** Lattice wrap of the 3D value noise along z, in cells. Divides the rig's 8192 at any power-of-two scale >= 1/32. */
#define NEB_Z_WRAP 256.0

const mat2 NEB_ROT2 = mat2(0.8, 0.6, -0.6, 0.8);

// ---- hashing ------------------------------------------------------------------
//
// Arithmetic hashes, no sin(): sin of a large argument loses its fraction on
// mediump-leaning mobile GPUs and the noise turns into stripes.

float nebHash(vec2 p) {
    vec3 q = fract(p.xyx * vec3(0.1173, 0.1279, 0.1091));
    q += dot(q, q.yzx + 19.19);
    return fract((q.x + q.y) * q.z);
}

vec2 nebHash22(vec2 p) {
    vec3 q = fract(p.xyx * vec3(0.1173, 0.1279, 0.1091));
    q += dot(q, q.yzx + 19.19);
    return fract((q.xx + q.yz) * q.zy);
}

float nebHash3(vec3 p) {
    p = fract(p * vec3(0.1173, 0.1279, 0.1091));
    p += dot(p, p.zyx + 19.19);
    return fract((p.x + p.y) * p.z);
}

/** A hash of a counter (uHitCount, uSectionCount), 0..1. */
float nebHash1(float n) {
    return nebHash(vec2(n * 1.618, n * 0.413 + 3.7));
}

mat2 nebRot(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, s, -s, c);
}

// ---- the Customize macros -------------------------------------------------------

/** Animation time under the Speed macro. */
float nebTime() {
    return uTime * uSpeed;
}

/** Travel under the Speed macro: uFlowPhase only ever advances, loudness sets its rate. */
float nebFlow() {
    return uFlowPhase * uSpeed;
}

/** The Energy macro as a gain on in-shader audio, clamped so a high setting cannot fling gas off screen. */
float nebEnergy() {
    return clamp(uIntensity, 0.0, 2.0);
}

/** Chaos as one amount, 0 at the defaults. */
float nebChaos() {
    return 0.5 * clamp(uWarp, 0.0, 2.0) + 0.35 * clamp(uTurbulence, 0.0, 2.0);
}

/**
 * The octave count for this frame, as a float (the last octave fades in by
 * its fraction): `full` at the defaults, up to two more with Complexity, one
 * fewer when the Detail budget is low (uSteps < 80). Uniform-only: call it
 * once in main().
 */
float nebOctaves(float full) {
    float n = full + 2.0 * clamp(uMorph, 0.0, 1.0);
    n -= uSteps < 80.0 ? 1.0 : 0.0;
    return clamp(n, 1.0, float(NEB_MAX_OCT));
}

/** nebOctaves capped for a volume sample, never past NEB_VOL_OCT. */
float nebVolOctaves(float full) {
    return min(nebOctaves(full), float(NEB_VOL_OCT));
}

/**
 * A palette offset that follows the detected key, gated by how sure the
 * detector is. sin of the hue rather than the hue itself: uKeyHue is circular
 * and pal() is not periodic in 1 unless the hue span happens to be, so the
 * raw value would jump colour when the key crosses 0.
 */
float nebKeyShift(float amount) {
    return amount * uKeyStrength * sin(NEB_TAU * uKeyHue);
}

// ---- noise ---------------------------------------------------------------------

/** 2D value noise, smooth fade, 0..1. Four hashes. */
float nebValue2(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = nebHash(i);
    float b = nebHash(i + vec2(1.0, 0.0));
    float c = nebHash(i + vec2(0.0, 1.0));
    float d = nebHash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

/**
 * 2D gradient noise, quintic fade, about -1..1. Zero on every lattice point,
 * so its zero set is a web of smooth curves: that is what nebRidged2 sharpens
 * into filaments.
 */
float nebGrad2(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    vec2 ga = nebHash22(i) * 2.0 - 1.0;
    vec2 gb = nebHash22(i + vec2(1.0, 0.0)) * 2.0 - 1.0;
    vec2 gc = nebHash22(i + vec2(0.0, 1.0)) * 2.0 - 1.0;
    vec2 gd = nebHash22(i + vec2(1.0, 1.0)) * 2.0 - 1.0;
    float va = dot(ga, f);
    float vb = dot(gb, f - vec2(1.0, 0.0));
    float vc = dot(gc, f - vec2(0.0, 1.0));
    float vd = dot(gd, f - vec2(1.0, 1.0));
    return 1.6 * mix(mix(va, vb, u.x), mix(vc, vd, u.x), u.y);
}

/**
 * 3D value noise, 0..1. Eight hashes. The lattice wraps along z every
 * NEB_Z_WRAP cells (see "a flight that does not jump" above).
 */
float nebValue3(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
    float z0 = mod(i.z, NEB_Z_WRAP);
    float z1 = mod(i.z + 1.0, NEB_Z_WRAP);
    float a = nebHash3(vec3(i.x, i.y, z0));
    float b = nebHash3(vec3(i.x + 1.0, i.y, z0));
    float c = nebHash3(vec3(i.x, i.y + 1.0, z0));
    float d = nebHash3(vec3(i.x + 1.0, i.y + 1.0, z0));
    float e = nebHash3(vec3(i.x, i.y, z1));
    float g = nebHash3(vec3(i.x + 1.0, i.y, z1));
    float h = nebHash3(vec3(i.x, i.y + 1.0, z1));
    float k = nebHash3(vec3(i.x + 1.0, i.y + 1.0, z1));
    float lo = mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
    float hi = mix(mix(e, g, u.x), mix(h, k, u.x), u.y);
    return mix(lo, hi, u.z);
}

/**
 * fbm of value noise, 0..1 with mean 0.5 at any octave count. `oct` from
 * nebOctaves: a float, the last octave weighted by its fraction. Octaves turn
 * as they shrink so the lattice never lines up into a grid.
 */
float nebFbm2(vec2 p, float oct) {
    float s = 0.0;
    float n = 0.0;
    float a = 0.5;
    for (int i = 0; i < NEB_MAX_OCT; i++) {
        float w = clamp(oct - float(i), 0.0, 1.0);
        if (w <= 0.0) break;
        s += a * w * nebValue2(p);
        n += a * w;
        p = NEB_ROT2 * p * 2.03 + vec2(1.7, 9.2);
        a *= 0.5;
    }
    return s / max(n, 1e-4);
}

/**
 * Ridged gradient fbm, 0..1: each octave is (1 - |noise|)^2, a crest along
 * the noise's zero curves. Long thin filaments - shock fronts, the Veil.
 * Stretch p along one axis before the call and the crests run along it.
 */
float nebRidged2(vec2 p, float oct) {
    float s = 0.0;
    float n = 0.0;
    float a = 0.5;
    for (int i = 0; i < NEB_MAX_OCT; i++) {
        float w = clamp(oct - float(i), 0.0, 1.0);
        if (w <= 0.0) break;
        float v = 1.0 - abs(nebGrad2(p));
        s += a * w * v * v;
        n += a * w;
        p = NEB_ROT2 * p * 2.07 + vec2(3.1, -4.7);
        a *= 0.55;
    }
    return s / max(n, 1e-4);
}

/**
 * 3D fbm for a volume sample, 0..1 with mean 0.5. At most NEB_VOL_OCT
 * octaves whatever `oct` says. Lacunarity exactly 2 and no rotation between
 * octaves, so every octave keeps the z wrap (the decorrelating offset is a
 * constant and does not disturb it).
 */
float nebFbm3(vec3 p, float oct) {
    float s = 0.0;
    float n = 0.0;
    float a = 0.5;
    for (int i = 0; i < NEB_VOL_OCT; i++) {
        float w = clamp(oct - float(i), 0.0, 1.0);
        if (w <= 0.0) break;
        s += a * w * nebValue3(p);
        n += a * w;
        p = p * 2.0 + vec3(5.31, 1.73, 3.37);
        a *= 0.5;
    }
    return s / max(n, 1e-4);
}

// ---- Chaos: a curl-like domain warp -----------------------------------------------

/**
 * Chaos in 2D: displace p along the curl of a slow value-noise potential (the
 * gradient turned a quarter turn). A curl field is divergence-free, so the
 * gas swirls and shears instead of piling into bright knots. Three noise
 * taps, all skipped at the default.
 */
vec2 nebWarp2(vec2 p, float t) {
    float k = nebChaos();
    if (k <= 1e-4) return p;
    vec2 q = p * 1.1 + vec2(t * 0.05, -t * 0.04);
    const float e = 0.2;
    float n0 = nebValue2(q);
    float gx = nebValue2(q + vec2(e, 0.0)) - n0;
    float gy = nebValue2(q + vec2(0.0, e)) - n0;
    return p + k * 0.3 * vec2(gy, -gx) / e;
}

/**
 * Chaos in 3D: an analytic swirl in the same spirit. Each component depends
 * only on the other two coordinates, so the field is exactly divergence-free,
 * at six trig calls instead of the six noise taps a differenced curl would
 * cost inside a volume loop. Skipped at the default. (It does not wrap with
 * the rig's travel; at the default amount there is nothing to wrap.)
 */
vec3 nebWarp3(vec3 p, float t) {
    float k = nebChaos();
    if (k <= 1e-4) return p;
    vec3 q = p * 0.9 + vec3(0.0, t * 0.07, t * 0.05);
    return p + k * 0.35 * vec3(sin(q.y * 1.3) + cos(q.z * 1.1),
                               sin(q.z * 1.2) + cos(q.x * 1.4),
                               sin(q.x * 1.1) + cos(q.y * 1.3));
}

// ---- stars ---------------------------------------------------------------------------

/** Star colour from a 0..1 temperature: red-orange through white to blue. */
vec3 nebStarTint(float h) {
    vec3 cool = vec3(1.0, 0.70, 0.46);
    vec3 warm = vec3(1.0, 0.95, 0.88);
    vec3 hot = vec3(0.70, 0.82, 1.0);
    return h < 0.5 ? mix(cool, warm, h * 2.0) : mix(warm, hot, h * 2.0 - 1.0);
}

/**
 * A layered star field, additive colour.
 *
 *   p         plane coordinates (view() units, or nebSky() for a 3D style)
 *   density   cells per unit on the nearest layer; each further layer is finer
 *   layers    1..NEB_STAR_LAYERS
 *   drift     parallax offset: the nearest layer moves by all of it, further
 *             ones by less, so one travel vector reads as depth
 *   size      star size scale, 1 = normal
 *   fill      fraction of cells that hold a star (0..1); vary it across the
 *             plane for a cluster or a disk, evaluated where you like - the
 *             cut-off is soft, so a fill that varies does not slice a star
 *   twinkle   0..1, how far stars scintillate (pass the hats). Dims around
 *             the level rather than adding: no hit lifts the frame.
 *
 * One cell lookup per layer: the star and its halo stay within their cell
 * (see the header), so the field is continuous across every border.
 */
vec3 nebStars(vec2 p, float density, int layers, vec2 drift, float size, float fill, float twinkle, float t) {
    vec3 acc = vec3(0.0);
    float px = 2.0 / max(uResolution.y, 1.0);
    for (int i = 0; i < NEB_STAR_LAYERS; i++) {
        if (i >= layers) break;
        float fi = float(i);
        float depth = 1.0 / (1.0 + fi * 0.9);
        float scale = density * (1.0 + fi * 0.85);
        vec2 q = (p + drift * depth) * scale + fi * vec2(17.3, 41.9);
        vec2 cell = floor(q);
        vec2 f = q - cell - 0.5;
        vec2 h2 = nebHash22(cell);
        float h = nebHash(cell + 71.3);
        float on = 1.0 - smoothstep(fill - 0.02, fill, h);
        float rel = clamp(h / max(fill, 1e-3), 0.0, 1.0);
        float bright = mix(1.0, 0.07, sqrt(rel)) * depth;
        // At least a pixel wide, never so wide that the halo leaves the cell.
        float r = min(max(size * mix(0.10, 0.03, rel), px * scale * 0.9), 0.12);
        vec2 off = (h2 - 0.5) * 2.0 * max(0.45 - 2.5 * r, 0.0);
        float d = length(f - off);
        float core = max(1.0 - d / r, 0.0);
        float halo = max(1.0 - d / (2.5 * r), 0.0);
        float shape = core * core + 0.16 * halo * halo;
        float tw = 1.0 - clamp(twinkle, 0.0, 1.0) * 0.65 * (0.5 + 0.5 * sin(t * (4.0 + 6.0 * h2.y) + h * 40.0));
        tw *= 0.9 + 0.1 * sin(t * (0.7 + h2.x) + h * 17.0);
        acc += nebStarTint(h2.y) * shape * bright * on * tw;
    }
    return acc;
}

/** A sky-plane coordinate for a 3D style's background stars, continuous for any ray not looking straight back. */
vec2 nebSky(vec3 rd) {
    return rd.xy / (1.0 + max(rd.z, -0.9)) * 1.6;
}

// ---- colour ramps ----------------------------------------------------------------------

/** Saturate a colour without changing its peak: k = 0 leaves it alone. */
vec3 nebRich(vec3 c, float k) {
    c = max(c, 0.0);
    float peak = max(c.r, max(c.g, c.b));
    vec3 d = pow(c, vec3(1.0 + max(k, 0.0)));
    return d / max(max(d.r, max(d.g, d.b)), 1e-4) * peak;
}

/**
 * Emission colour of gas at palette position `t` and temperature `heat`
 * (0 cold, 1 white-hot). Emission lines are saturated, so the palette is
 * deepened first; heat then washes it toward white the way an overexposed
 * core does.
 */
vec3 nebEmission(float t, float heat) {
    vec3 c = nebRich(pal(t), 0.6);
    float h = clamp(heat, 0.0, 1.0);
    return mix(c, vec3(1.0, 0.96, 0.92), h * h * 0.8);
}

/**
 * Per-channel extinction of dust, for exp(-tau * nebAbsorption(t)). Real dust
 * reddens - it stops blue more than red - so the base is a reddening law; a
 * quarter of it is the complement of the palette, so the lanes take a tint
 * that belongs to the scene.
 */
vec3 nebAbsorption(float t) {
    return mix(vec3(0.75, 1.0, 1.3), 1.6 - pal(t), 0.25);
}

// ---- spiral arms ------------------------------------------------------------------------

/**
 * The log-spiral arm wave at p (galaxy plane, centre at the origin), -1..1:
 * cos(m * (theta - ln r / tan(pitch)) - phase). `arms` is rounded to a whole
 * number so the atan seam cancels; `pitch` is the arms' pitch angle in
 * radians (0.2 tight, 0.5 open); `phase` turns the PATTERN, not the frame.
 * Near the centre the arms wind infinitely tight, so the wave fades to 0
 * inside r = 0.1: cover that with a bulge.
 */
float nebSpiralWave(vec2 p, float arms, float pitch, float phase) {
    float m = max(floor(arms + 0.5), 1.0);
    float r = max(length(p), 1e-4);
    float th = atan(p.y, p.x);
    float w = cos(m * (th - log(r) / tan(pitch)) - phase);
    return w * smoothstep(0.02, 0.1, r);
}

/** Arm density 0..1 from the wave: `sharp` 1 is a broad sine, higher narrows the arms. */
float nebSpiral(vec2 p, float arms, float pitch, float phase, float sharp) {
    return pow(0.5 + 0.5 * nebSpiralWave(p, arms, pitch, phase), sharp);
}

// ---- the cosmic web ------------------------------------------------------------------------

/**
 * A cheap 3D cell-edge field: x = distance to the nearest edge of the unit
 * cubic lattice, y = distance to the nearest lattice vertex. Every point's
 * distance to its nearest grid plane is continuous, so both fields are
 * continuous across every cell face with no neighbour search - the price of
 * a true Voronoi (27 cells per tap) is out of reach inside a volume loop.
 * Warp q first and union two turned lattices and the regular grid reads as
 * a web of filaments meeting in knots. Both are true distances in q units,
 * so a march may skip empty space by them.
 */
vec2 nebWeb3(vec3 q) {
    vec3 m = 0.5 - abs(fract(q) - 0.5);
    float e = min(min(length(m.xy), length(m.yz)), length(m.xz));
    return vec2(e, length(m));
}

// ---- volume integration ------------------------------------------------------------------

/**
 * One front-to-back step through a medium of extinction `sigma` (per world
 * unit) whose in-scattered plus emitted radiance is `emit`, over length dt.
 * acc.rgb is the colour so far, acc.a the transmittance (start it at 1). The
 * step's opacity is 1 - exp(-sigma dt), exact for any step length, so a
 * longer step at low Detail does not change how opaque the cloud is.
 */
void nebVolStep(inout vec4 acc, vec3 emit, float sigma, float dt) {
    float a = 1.0 - exp(-max(sigma, 0.0) * dt);
    acc.rgb += acc.a * a * emit;
    acc.a *= 1.0 - a;
}

/**
 * The emissive form, for a medium that glows more than it blocks (filaments,
 * hot gas): emission per unit length added in full, extinction applied after.
 */
void nebVolEmit(inout vec4 acc, vec3 emission, float sigma, float dt) {
    acc.rgb += acc.a * emission * dt;
    acc.a *= exp(-max(sigma, 0.0) * dt);
}

/**
 * The step length at distance t: dt0 + t * grow, lengthened when the Detail
 * budget is short so the march still reaches the far side of the volume.
 */
float nebVolDt(float t, float dt0, float grow) {
    return (dt0 + t * grow) * clamp(96.0 / max(uSteps, 1.0), 0.75, 2.5);
}

/** A per-pixel 0..1 offset for the first step: banding becomes fine grain. */
float nebDither() {
    return nebHash(gl_FragCoord.xy);
}

/** Ray against a sphere: (enter, exit), enter clamped to 0; exit < 0 is a miss. */
vec2 nebSphere(vec3 ro, vec3 rd, vec3 centre, float radius) {
    vec3 oc = ro - centre;
    float b = dot(oc, rd);
    float h = b * b - (dot(oc, oc) - radius * radius);
    if (h < 0.0) return vec2(-1.0);
    h = sqrt(h);
    return vec2(max(-b - h, 0.0), -b + h);
}

/**
 * Fake lighting from ONE extra density tap toward the light: how much of the
 * light survives the material just ahead of the sample. A real light march
 * inside the volume loop would square its cost; one tap gets the bright rim
 * on the lit side and the dark core, which is most of what the eye reads.
 */
float nebLightTap(float densityTowardLight, float k) {
    return exp(-k * max(densityTowardLight, 0.0));
}

/**
 * Henyey-Greenstein phase, scaled so isotropic is 1. g > 0 scatters forward:
 * dust lit from behind glows at its rim, which is the whole look of a
 * backlit pillar.
 */
float nebPhase(float cosTheta, float g) {
    float d = 1.0 + g * g - 2.0 * g * cosTheta;
    return (1.0 - g * g) / max(d * sqrt(d), 1e-4);
}
