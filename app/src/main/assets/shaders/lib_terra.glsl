// The Terrain family's shared core: one tileable noise, the heightfields built
// from it, and the pieces every landscape style needs to march, light and fog
// them.
//
// WHAT A TERRAIN STYLE IS
//
// A heightfield y = h(x, z) seen from above it. Eleven of the twelve styles
// march a ray against it (the twelfth draws the same field as a map), so the
// surface is never a mesh and never runs out: the eye flies over it on the
// rig's path (camFly) or circles one subject on it (camAround, spin 0).
//
// WHY THE NOISE TILES
//
// The rig's travel wraps at 8192 world units, and a flyover reads its height
// from world z. Two things go wrong out there unless the field tiles:
//
//   THE WRAP. When travel jumps from 8192 back to 0, an untiled field
//   teleports the whole landscape under the eye in one frame.
//
//   PRECISION. Eight thousand world units times a fifth octave's frequency is
//   tens of thousands of lattice cells; a highp float keeps eight bits of
//   fraction there and the fine octaves turn into stairs.
//
// So the world is tiled along z with TERRA_PERIOD (512, a power of two that
// divides 8192): terraWrap() folds z into one tile BEFORE anything else reads
// it, and every lattice the noise touches repeats with exactly that period.
// The value noise hashes its lattice corners modulo TERRA_CELLS (the tile in
// octave-0 cells), and the octaves are stacked with an INTEGER matrix (2, 1;
// -1, 2): it turns each octave by 26.6 degrees, which kills the axis-aligned
// grain of value noise, and because its entries are integers it maps a
// TERRA_CELLS-periodic lattice onto another one, so every octave tiles too.
// A non-integer rotation would not. A style that adds its own z-dependent
// term (a meandering river, a ripple) uses an integer number of cycles per
// tile for the same reason; the style says so where it does it. At 1.6 units
// a second the tile repeats after five minutes of straight flight, and the
// lateral wander of the path means it is never the same strip twice.
//
// THE MARCH (written in each style)
//
// GLSL ES has no function pointers, so each style writes its own loop
// against its own h(). The pattern, with the helpers below:
//
//   vec2 span = terraSlab(ro, rd, hMax, far);  // where the ray is below the highest peak
//   t = span.x; for (i < 128) { if (i >= uSteps) break; if (t > span.y) break;
//       dy = p.y - h(p.xz);
//       if (dy < terraEps(t)) { t = terraRefine(tPrev, dyPrev, t, dy); hit; break; }
//       tPrev = t; dyPrev = dy; t += terraStep(dy, t, k); }
//   if (no hit && t <= span.y) hit at t;  // budget ran out on a grazing ray: ground, not sky
//
// dy is a VERTICAL distance, not a distance to the surface, so it is safe only
// scaled down by the steepest slope the field has: k is about 1 / (1 + slope),
// 0.4-0.5 for rolling ground, 0.25-0.3 for cliffs. terraStep relaxes k with
// distance (a far miss is a sub-pixel miss), floors the step so a grazing ray
// still gets somewhere, and divides by the Chaos warp's stretch. A step that
// lands under the surface is pulled back by terraRefine, a linear
// interpolation between the last point above and the first below: one line
// that removes most of the banding k alone would leave.
//
// terraSlab is most of the frame's saving: every ray above the highest peak
// that points up is sky without a single height read, and a ray from high up
// starts marching where it crosses hMax, not at the eye. So hMax must be a
// true bound: each style computes it from the same bounded amplitudes it
// builds its field with.
//
// THE EYE
//
// camFly's eye rides the rig's path, which wanders about +-1.2 vertically.
// terraFly() keeps a third of that (the flight still bobs) and lifts the eye
// to a style-chosen height above the highest peak the style can ever make.
// Bass raises peaks in several styles; it does so through a clamped
// amplitude, so the eye is above the land for every input, and no style
// needs a per-pixel ground probe.
//
// THE NORMAL is central differences on h() (four height reads), through the
// TERRA_NORMAL macro: a macro because it has to call the style's own height
// function. The march may use fewer octaves than the normal: lighting the
// coarse surface with the fine normal is the cheap way to get detail.
//
// INCLUDE ORDER: last, after lib_scene_uniforms and lib_camera (terraFly). It
// reads uTime, uSpeed, uIntensity, uMorph, uSteps, uWarp, uTurbulence, uTwist,
// each only inside the helper that needs it. No audio uniform is read here:
// what the music moves is each style's business.

#define TERRA_TAU 6.2831853
/** World-z tile. A power of two that divides the rig's 8192 travel wrap. */
#define TERRA_PERIOD 512.0
/** Octave-0 lattice cells per world unit: one hill every four units or so. */
#define TERRA_FREQ 0.25
/** TERRA_PERIOD * TERRA_FREQ: the tile in octave-0 cells. Keep it an integer. */
#define TERRA_CELLS 128.0
/** Compile-time ceiling on fbm octaves; a style's own count is a runtime fade below it. */
#define TERRA_MAX_OCT 5

/** The octave matrix (2, 1; -1, 2): integer, so it keeps the lattice's tile. Scales by sqrt(5) and turns 26.6 degrees. */
const mat2 TERRA_OCT = mat2(2.0, -1.0, 1.0, 2.0);

// ---- the Customize macros ---------------------------------------------------
//
// As in every family, neutral at their defaults: Speed (uSpeed) scales
// animation time, Complexity (uMorph) adds an octave (terraOctaves), Chaos
// (uWarp, uTurbulence, uTwist) warps the ground plane before the field reads
// it (terraChaos), Energy (uIntensity) scales how far the music moves the
// land, Colour is pal(), Space is the rig.

/** Animation time under the Speed macro. */
float terraTime() {
    return uTime * uSpeed;
}

/** The Energy macro as a gain on in-shader reaction, clamped so a high setting cannot lift a peak through the eye. */
float terraEnergy() {
    return clamp(uIntensity, 0.0, 2.0);
}

/**
 * Octaves for this frame, fractional: `hi` at normal Detail, up to one more
 * with Complexity, one fewer when the march budget is low (uSteps < 80).
 * Fractional because the last octave FADES in (terraFbm weights it by the
 * fraction), so dragging Complexity grows detail instead of popping it.
 * Call once in main(): a uniform-only decision.
 */
float terraOctaves(float hi) {
    float n = hi + clamp(uMorph, 0.0, 1.0) - (uSteps < 80.0 ? 1.0 : 0.0);
    return clamp(n, 2.0, float(TERRA_MAX_OCT));
}

/** World xz with z folded into one tile. Every height function starts here. */
vec2 terraWrap(vec2 xz) {
    return vec2(xz.x, mod(xz.y, TERRA_PERIOD));
}

// ---- noise ------------------------------------------------------------------
//
// Arithmetic hash, no sin(): sin() of a large argument loses its fraction on
// GPUs that lean on mediump, and the noise turns to stripes. The corner is
// wrapped modulo TERRA_CELLS first, which is what makes the field tile and
// also keeps the hash's input small.

float terraHash(vec2 i) {
    i = mod(i, TERRA_CELLS);
    vec3 q = fract(i.xyx * vec3(0.1193, 0.1307, 0.1427));
    q += dot(q, q.zxy + 23.17);
    return fract((q.x + q.z) * q.y);
}

/** A 0..1 hash of a 3D cell, for stars and per-cell scatter (no tiling). */
float terraHash3(vec3 p) {
    vec3 q = fract(p * vec3(0.1193, 0.1307, 0.1427));
    q += dot(q, q.zxy + 23.17);
    return fract((q.x + q.z) * q.y);
}

/** Value noise in lattice cells, 0..1, quintic fade (C2, so normals do not crease at cell edges). */
float terraNoise(vec2 q) {
    vec2 i = floor(q);
    vec2 f = q - i;
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float a = terraHash(i);
    float b = terraHash(i + vec2(1.0, 0.0));
    float c = terraHash(i + vec2(0.0, 1.0));
    float d = terraHash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

/**
 * Plain fbm of world xz (already wrapped), 0..1. `oct` from terraOctaves, may
 * be fractional. Gain 0.45 per octave: soft hills, dunes, the base of others.
 * Normalised by the weights it actually summed, so its mean holds near 0.5
 * whatever the octave count.
 */
float terraFbm(vec2 xz, float oct) {
    vec2 q = xz * TERRA_FREQ;
    float a = 1.0;
    float s = 0.0;
    float norm = 0.0;
    for (int i = 0; i < TERRA_MAX_OCT; i++) {
        float w = clamp(oct - float(i), 0.0, 1.0);
        if (w <= 0.0) break;
        s += a * w * terraNoise(q);
        norm += a * w;
        a *= 0.45;
        q = TERRA_OCT * q + vec2(13.0, 7.0);
    }
    return s / max(norm, 1e-4);
}

/**
 * Ridged multifractal of world xz (already wrapped), about 0..1. Each octave
 * folds the noise about its middle into a crest (1 - |2n - 1|, squared to
 * sharpen it) and is weighted by the octave above it, so fine ridges grow only
 * on the coarse ridges' backs and the valleys stay smooth: the look of eroded
 * mountains, from Musgrave's multifractal construction.
 */
float terraRidged(vec2 xz, float oct) {
    vec2 q = xz * TERRA_FREQ;
    float a = 1.0;
    float s = 0.0;
    float norm = 0.0;
    float prev = 1.0;
    for (int i = 0; i < TERRA_MAX_OCT; i++) {
        float w = clamp(oct - float(i), 0.0, 1.0);
        if (w <= 0.0) break;
        float n = 1.0 - abs(2.0 * terraNoise(q) - 1.0);
        n *= n;
        s += a * w * n * prev;
        norm += a * w;
        prev = clamp(n * 1.6, 0.0, 1.0);
        a *= 0.5;
        q = TERRA_OCT * q + vec2(29.0, 3.0);
    }
    return s / max(norm, 1e-4);
}

// ---- Chaos: a warp of the ground plane --------------------------------------

/** Warp amplitude in world units for the Chaos macro. */
float terraChaosAmp() {
    return 0.9 * uWarp + 0.6 * uTurbulence + 0.4 * abs(uTwist);
}

/**
 * Chaos: the ground plane pushed sideways by a slow noise field before the
 * height is read, so ridges bend and braid. uTwist turns the push (a quarter
 * turn at 1), which makes the bends swirl instead of shear. The push is read
 * at twice the octave-0 frequency, an integer multiple, so it tiles with the
 * field. At default the branch is skipped and xz comes back untouched.
 */
vec2 terraChaos(vec2 xz) {
    float amp = terraChaosAmp();
    if (amp < 1e-4) return xz;
    vec2 q = xz * (TERRA_FREQ * 2.0);
    float t = terraTime() * 0.06;
    vec2 w = vec2(terraNoise(q + vec2(t, 0.0)), terraNoise(q + vec2(5.0 - t, 9.0))) - 0.5;
    float a = uTwist * 1.5707963;
    w = mat2(cos(a), sin(a), -sin(a), cos(a)) * w;
    return xz + 2.0 * amp * w;
}

/** How much terraChaos steepens the field: the march divides its step by this. */
float terraChaosLip() {
    return 1.0 + 1.9 * terraChaosAmp();
}

// ---- the camera -------------------------------------------------------------

/**
 * camFly for a landscape: the eye `eye` units up, keeping a third of the
 * path's vertical wander, and the view tipped `tilt` (screen units, about
 * radians / focal) below the path's heading so the land fills the lower frame
 * and the horizon sits high.
 */
void terraFly(vec2 uv, float eye, float tilt, out vec3 ro, out vec3 rd) {
    camFly(uv - vec2(0.0, tilt), ro, rd);
    ro.y = eye + 0.35 * ro.y;
}

// ---- marching ---------------------------------------------------------------

/**
 * Where the ray is below hMax, clipped to `far`: (enter, exit). enter < 0
 * means it never is, and the pixel is sky with no march.
 */
vec2 terraSlab(vec3 ro, vec3 rd, float hMax, float far) {
    float t0 = 0.0;
    float t1 = far;
    if (ro.y > hMax) {
        if (rd.y >= 0.0) return vec2(-1.0);
        t0 = (hMax - ro.y) / rd.y;
    } else if (rd.y > 0.0) {
        t1 = min(t1, (hMax - ro.y) / rd.y);
    }
    return t0 < t1 ? vec2(t0, t1) : vec2(-1.0);
}

/** Hit tolerance at distance t: a pixel's footprint, doubled on a low Detail budget. */
float terraEps(float t) {
    return (uSteps < 80.0 ? 0.004 : 0.002) * t + 0.001;
}

/** The next step for vertical clearance dy at distance t; k is about 1 / (1 + steepest slope). */
float terraStep(float dy, float t, float k) {
    return max(dy * k * (1.0 + 0.02 * t) / terraChaosLip(), 0.004 + 0.003 * t);
}

/** The surface between the last sample above it (tPrev, dyPrev) and this one (t, dy), when dy went under. */
float terraRefine(float tPrev, float dyPrev, float t, float dy) {
    return dy < 0.0 ? tPrev + (t - tPrev) * dyPrev / max(dyPrev - dy, 1e-5) : t;
}

/** Occlusion from the step count: a ray that spent its budget was creeping along a slope or into a fold. */
float terraStepAO(float used) {
    return clamp(1.0 - used / max(uSteps, 1.0) * 0.9, 0.35, 1.0);
}

/**
 * The terrain normal by central differences on the style's height function H
 * at world xz, step e (grow it with distance: 0.002 * t + 0.01 or so).
 */
#define TERRA_NORMAL(H, xz, e) normalize(vec3(H((xz) - vec2((e), 0.0)) - H((xz) + vec2((e), 0.0)), 2.0 * (e), H((xz) - vec2(0.0, (e))) - H((xz) + vec2(0.0, (e)))))

// ---- sky, sun and fog -------------------------------------------------------

/** A sky: horizon colour rising into zenith colour, the sun as glow, halo and a soft-edged disc. */
vec3 terraSky(vec3 rd, vec3 sun, vec3 zenith, vec3 horizon, vec3 sunCol) {
    float h = clamp(rd.y, 0.0, 1.0);
    vec3 c = mix(horizon, zenith, pow(h, 0.45));
    float s = max(dot(rd, sun), 0.0);
    c += sunCol * (0.22 * pow(s, 6.0) + 0.5 * pow(s, 64.0) + 1.2 * smoothstep(0.9990, 0.9996, s));
    return c;
}

/** Distance fog toward fogCol, warmed toward the sun's colour when looking into it (the in-scatter). */
vec3 terraFog(vec3 col, vec3 rd, float t, float density, vec3 fogCol, vec3 sun, vec3 sunCol) {
    float f = 1.0 - exp(-t * density);
    vec3 fc = fogCol + sunCol * 0.3 * pow(max(dot(rd, sun), 0.0), 6.0);
    return mix(col, fc, f);
}

/** A field of stars for a dark sky: one hash per 3D cell of the direction, so no atan and no seam. */
float terraStars(vec3 rd, float density) {
    vec3 c = rd * 160.0;
    vec3 i = floor(c);
    float h = terraHash3(i);
    float on = step(1.0 - density, h);
    float d = length(c - i - 0.5);
    return on * (1.0 - smoothstep(0.05, 0.45, d)) * (0.4 + 0.6 * fract(h * 37.0));
}
