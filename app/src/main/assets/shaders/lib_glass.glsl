// The Glass family's shared optics and the worlds its styles look into.
//
// WHAT GLASS IS, FOR A SHADER
//
// A glass surface does two things at once. Some light reflects, and how much is
// set by the angle: almost nothing face on, almost everything at grazing
// incidence (Schlick's Fresnel, glassFresnel). The rest bends by Snell's law
// into the body, loses colour by Beer-Lambert absorption the further it goes
// (glassBeer), and at the far wall splits again: part leaves, part reflects
// back inside. Past the critical angle nothing leaves at all - total internal
// reflection, the fire of a cut gem. GLSL's refract() returns a zero vector
// there; glassRefract() substitutes the mirror direction, which is where that
// light really goes.
//
// Everything here is written from the textbook equations, not from any
// particular shader.
//
// WHY THE FIRST VERSION LOOKED BASIC, AND WHAT CHANGED
//
// Glass has no colour of its own: it shows as distortion of what is behind it
// and as the shapes of the lights it reflects. The first library put every
// style in one dim studio - a grey-green floor grid and three weak panels - so
// every body read as a darker smudge of a dark room. This version gives the
// family five worlds (GLASS_ENV_*), each built around BRIGHT, SHAPED light
// sources (softboxes, neon tubes, a sun, shafts, city bokeh) against a dark
// field, because contrast inside the body is what the eye reads as "glass". It
// also adds the four things that sell a material at a glance:
//
//   ROUGHNESS WITHOUT SAMPLES. Frosted glass is clear glass that scatters each
//   ray over a cone. Ray tracers average many rays; we cannot. But every world
//   here is analytic - a sum of lights with soft edges - so a rough lookup is
//   the same world with every edge widened and every peak lowered to keep its
//   energy (GlassLook.rough). One lookup, any frost, no noise: the environment
//   is pre-filtered in closed form.
//
//   TWO BOUNCES. glassSphereTrace() follows the light through a sphere twice:
//   in, across, out (dispersed, three wavelengths), and the part the far wall
//   reflects across again and out. For a sphere every chord from a point has
//   the same length on the bounce, so the second pass is one more quadratic.
//   Marched styles take their second interior march only when the step budget
//   allows it (glassBounces()).
//
//   CHROMATIC CAUSTICS. The caustic net (glassWaves, glassCaustic) is the
//   reciprocal of the light map's Jacobian determinant. The determinant is a
//   polynomial in the focus depth over one Hessian, so three wavelengths focus
//   at three depths for the cost of one wave sum (glassCausticRGB): the net
//   splits into rainbow-edged lines, as it does on a real pool floor.
//
//   A FILMIC SHOULDER. Highlights through glass run far past 1.0, and a hard
//   clip turns them into flat white blots. glassTone() rolls them off so the
//   brightest streaks keep their shape and hue. It is a curve, not an exposure:
//   it never lifts the frame, so the flash budget is unchanged.
//
// THE BOUNCE AND SAMPLE BUDGET
//
// uSteps is the only quality signal a style receives: Detail times the
// Performance march scale (Saver 0.5, Balanced 0.75, High 1), never under 24.
// At the default Detail it is about 51 on Saver, 77 on Balanced, 102 on High.
// glassQuality() maps it to 0..1; glassBounces() turns it into one bounce (under
// GLASS_TWO_BOUNCE_STEPS) or two. Analytic styles take two from Balanced up;
// marched styles key their second interior march on glassMarchBounces(), which
// needs High.
//
// WHAT THE MUSIC DOES
//
// GlassEar (glassListen) reads lib_sound once per pixel into the handful of
// shaped values every style draws on, each meaning one physical thing so the
// family hears alike:
//
//   pan     SND_STEREO_PAN    the key light's yaw: the highlight walks with the mix
//   height  SND_PITCH_HEIGHT  the key light's elevation, eased by pitch confidence
//   sharp   SND_CENTROID      edge sharpness: bright mixes cut crisp highlights and
//                             wider dispersion, dark ones soften
//   frost   SND_FLATNESS      roughness and grain: noise frosts, tones clear
//   bass    SND_BASS_ATT      body swell (IOR, size, ripple height)
//   kick    SND_KICK_ENV      eased into a bump, for shoves that do not step
//   snare   SND_SNARE_ENV     flips, waves, cracks: each style's own gesture
//   hat     SND_HAT_ENV       glints and sparkle
//   width   SND_STEREO_WIDTH  how far the lights spread apart
//
// None of them drives the frame's overall brightness. SND_LEVEL is not read at
// all: global brightness on loudness is the flash budget's business.
//
// WHAT IS NOT HERE: the marcher loops and the normal. GLSL ES has no function
// pointers, so a shared march cannot call a style's own map(). Each marched
// style writes its outer loop (constant bound GLASS_MAX_STEPS, breaking on
// uSteps), its interior loop (GLASS_MAX_INNER, breaking on glassInnerSteps())
// and its tetrahedral normal itself, from glassEps and glassStepAO below.
//
// INCLUDE ORDER: after lib_scene_uniforms (uTime, uSpeed, uIntensity, uMorph,
// uWarp, uTurbulence, uSteps, and the SND_* names it pastes) and lib_palette
// (pal()). It includes nothing itself.
//
// GLSL ES NOTE: every smoothstep() here has ascending edges; descending edges
// are undefined behaviour and some Mali drivers return garbage for them.

#define GLASS_TAU 6.2831853
#define GLASS_PI 3.14159265
/** Compile-time ceiling on a marched style's outside march. The body breaks on uSteps. */
#define GLASS_MAX_STEPS 128
/** Compile-time ceiling on one interior march. The body breaks on glassInnerSteps(). */
#define GLASS_MAX_INNER 48
/** Ordinary crown glass. */
#define GLASS_IOR 1.5
/** uSteps from which an analytic style takes its second internal bounce (Balanced at default Detail is 77). */
#define GLASS_TWO_BOUNCE_STEPS 64.0
/** uSteps from which a marched style takes its second interior MARCH (High at default Detail is 102). */
#define GLASS_TWO_MARCH_STEPS 96.0

/** The worlds. A style picks one in its GlassLook. */
#define GLASS_ENV_STUDIO 0
#define GLASS_ENV_NEON 1
#define GLASS_ENV_DUSK 2
#define GLASS_ENV_DEEP 3
#define GLASS_ENV_VOID 4
#define GLASS_ENV_AURORA 5
#define GLASS_ENV_FRACTAL 6

// ---- the Customize macros ---------------------------------------------------
//
// At their defaults each is the identity: Speed (uSpeed 1) scales animation
// time, Complexity (uMorph 0) adds pieces or detail per style, Chaos (uWarp,
// uTurbulence 0) wobbles surfaces as if the glass were poured and not yet set,
// Energy (uIntensity 1) scales how far the music moves a style. Colour is pal();
// Space is the rig.

/** Animation time under the Speed macro. */
float glassTime() {
    return uTime * uSpeed;
}

/** The Energy macro as a gain on in-shader audio reaction, clamped so a high setting cannot fling a shape out of frame. */
float glassEnergy() {
    return clamp(uIntensity, 0.0, 2.0);
}

/** The Chaos macro, 0 at default, up to 2. */
float glassChaos() {
    return 0.6 * clamp(uWarp, 0.0, 2.0) + 0.4 * clamp(uTurbulence, 0.0, 2.0);
}

/** The Complexity macro, 0..1. */
float glassComplexity() {
    return clamp(uMorph, 0.0, 1.0);
}

// ---- quality -------------------------------------------------------------------

/** The step budget as 0..1: 0 at the 24-step floor, 1 at 128. Scales sample counts and detail. */
float glassQuality() {
    return clamp((uSteps - 24.0) / 104.0, 0.0, 1.0);
}

/** Internal bounces an analytic style may afford: 2 from Balanced up, 1 on Saver. */
int glassBounces() {
    return uSteps >= GLASS_TWO_BOUNCE_STEPS ? 2 : 1;
}

/** True when a marched style may take a second interior march (High, or Balanced with Detail raised). */
bool glassMarchBounces() {
    return uSteps >= GLASS_TWO_MARCH_STEPS;
}

// ---- envelope shaping --------------------------------------------------------
//
// lib_sound's envelopes rise instantly. Glass magnifies whatever is behind it,
// so a value that steps on one frame steps the whole refracted image with it.
// These turn a hit into motion that eases.

/** 0 at the hit (envelope 1), peak at envelope 0.5, 0 as it dies: continuous across the hit. */
float glassBump(float env) {
    float e = clamp(env, 0.0, 1.0);
    return 4.0 * e * (1.0 - e);
}

/**
 * A monotonic counter (SND_ONSET_COUNT, SND_CUT_COUNT) that GLIDES to its new value
 * instead of stepping. On the frame the count increments the envelope is 1, so
 * this still returns the previous count; as the envelope falls it slides the
 * last unit. Anything positioned by it moves one notch per event, smoothly.
 */
float glassGlide(float count, float env) {
    float e = clamp(env, 0.0, 1.0);
    return count - e * e * (3.0 - 2.0 * e);
}

// ---- the ear ---------------------------------------------------------------------

/** The music, shaped once per pixel into the physical meanings the header lists. */
struct GlassEar {
    float pan;
    float height;
    float sharp;
    float frost;
    float bass;
    float kick;
    float snare;
    float hat;
    float width;
    float trend;
};

GlassEar glassListen() {
    GlassEar e;
    float g = glassEnergy();
    e.pan = clamp(SND_STEREO_PAN, -1.0, 1.0) * g;
    e.height = mix(0.5, clamp(SND_PITCH_HEIGHT, 0.0, 1.0), clamp(SND_PITCH_CONFIDENCE, 0.0, 1.0));
    e.sharp = clamp(SND_CENTROID * 1.6 - 0.3, 0.0, 1.0);
    e.frost = clamp(SND_FLATNESS * 1.4 - 0.1, 0.0, 1.0) * min(g, 1.0);
    e.bass = clamp(0.6 * sndLevel(SND_BASS_ATT) + 0.4 * SND_BASS_PRESENCE, 0.0, 1.2) * g;
    e.kick = glassBump(SND_KICK_ENV) * g;
    e.snare = clamp(SND_SNARE_ENV, 0.0, 1.0) * g;
    e.hat = clamp(SND_HAT_ENV, 0.0, 1.0) * g;
    e.width = clamp(SND_STEREO_WIDTH * 2.0, 0.0, 1.0);
    e.trend = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    return e;
}

// ---- hashing and noise -------------------------------------------------------
//
// Arithmetic hashes, no sin(): sin() of a large argument loses its fraction on
// mediump-leaning GPUs and the noise turns into stripes.

float glassHash(vec2 p) {
    vec3 q = fract(p.xyx * vec3(0.1453, 0.1621, 0.1379));
    q += dot(q, q.yzx + 27.71);
    return fract((q.x + q.y) * q.z);
}

vec2 glassHash2(vec2 p) {
    vec3 q = fract(p.xyx * vec3(0.1453, 0.1621, 0.1379));
    q += dot(q, q.yzx + 27.71);
    return fract((q.xx + q.yz) * q.zy);
}

float glassHash3(vec3 p) {
    vec3 q = fract(p * vec3(0.1453, 0.1621, 0.1379));
    q += dot(q, q.yzx + 27.71);
    return fract((q.x + q.y) * q.z);
}

/** A hash of a counter, 0..1. */
float glassHash1(float n) {
    return glassHash(vec2(n * 1.618, n * 0.371 + 5.13));
}

/** A per-event random value that slides instead of stepping (see glassGlide). */
float glassSeed(float count, float env) {
    float e = clamp(env, 0.0, 1.0);
    e = e * e * (3.0 - 2.0 * e);
    return mix(glassHash1(count), glassHash1(count - 1.0), e);
}

/** 2D value noise, -1..1. Four hashes. */
float glassNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = glassHash(i);
    float b = glassHash(i + vec2(1.0, 0.0));
    float c = glassHash(i + vec2(0.0, 1.0));
    float d = glassHash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y) * 2.0 - 1.0;
}

/** 3D value noise, -1..1. Eight hashes: call it a few times per pixel, never per march step. */
float glassNoise3(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
    float a = mix(glassHash3(i), glassHash3(i + vec3(1.0, 0.0, 0.0)), u.x);
    float b = mix(glassHash3(i + vec3(0.0, 1.0, 0.0)), glassHash3(i + vec3(1.0, 1.0, 0.0)), u.x);
    float c = mix(glassHash3(i + vec3(0.0, 0.0, 1.0)), glassHash3(i + vec3(1.0, 0.0, 1.0)), u.x);
    float d = mix(glassHash3(i + vec3(0.0, 1.0, 1.0)), glassHash3(i + vec3(1.0, 1.0, 1.0)), u.x);
    return mix(mix(a, b, u.y), mix(c, d, u.y), u.z) * 2.0 - 1.0;
}

mat2 glassRot(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, s, -s, c);
}

/** Rotation about x by a.x, then y by a.y, then z by a.z: a tumbling body's orientation. */
mat3 glassEuler(vec3 a) {
    vec3 c = cos(a);
    vec3 s = sin(a);
    mat3 rx = mat3(1.0, 0.0, 0.0, 0.0, c.x, s.x, 0.0, -s.x, c.x);
    mat3 ry = mat3(c.y, 0.0, -s.y, 0.0, 1.0, 0.0, s.y, 0.0, c.y);
    mat3 rz = mat3(c.z, s.z, 0.0, -s.z, c.z, 0.0, 0.0, 0.0, 1.0);
    return rz * ry * rx;
}

// ---- intersections -----------------------------------------------------------

/**
 * Ray against a sphere: (near, far) along rd, NOT clamped, so a style can tell
 * an eye inside the sphere (near < 0 < far) from one in front of it. far < 0
 * means a miss (both -1).
 */
vec2 glassSphere(vec3 ro, vec3 rd, vec3 c, float r) {
    vec3 oc = ro - c;
    float b = dot(oc, rd);
    float h = b * b - (dot(oc, oc) - r * r);
    if (h < 0.0) return vec2(-1.0);
    h = sqrt(h);
    return vec2(-b - h, -b + h);
}

/** Ray against the plane dot(p, n) = d: the hit distance, or -1 if the ray is parallel or the plane is behind it. */
float glassPlane(vec3 ro, vec3 rd, vec3 n, float d) {
    float den = dot(rd, n);
    if (abs(den) < 1e-5) return -1.0;
    float t = (d - dot(ro, n)) / den;
    return t > 0.0 ? t : -1.0;
}

// ---- optics ------------------------------------------------------------------

/** Schlick's Fresnel with the true face-on f0 for `ior` (4% crown glass, 17% diamond). */
float glassFresnel(float cosI, float ior) {
    float f0 = (1.0 - ior) / (1.0 + ior);
    f0 *= f0;
    float m = 1.0 - clamp(cosI, 0.0, 1.0);
    float m2 = m * m;
    return f0 + (1.0 - f0) * m2 * m2 * m;
}

/**
 * Snell refraction of incident `i` at a surface with normal `n` facing the
 * incoming side, eta = n_from / n_to. On total internal reflection refract()
 * returns zero; this returns the mirror direction instead.
 */
vec3 glassRefract(vec3 i, vec3 n, float eta) {
    vec3 r = refract(i, n, eta);
    return dot(r, r) < 1e-6 ? reflect(i, n) : r;
}

/** One IOR per channel: red bends least, blue most. `spread` 0.01 is crown glass, 0.05 a showy gem. */
vec3 glassIors(float ior, float spread) {
    return ior + spread * vec3(-1.0, 0.0, 1.0);
}

/** Beer-Lambert transmission after `dist` through a body whose colour is `tint`: the thick parts saturate, the thin stay clear. */
vec3 glassBeer(vec3 tint, float dist, float density) {
    return exp(-(1.0 - clamp(tint, 0.0, 1.0)) * dist * density);
}

/** Chaos on a normal: a slow, travelling sine wobble, as if the glass were still soft. Identity at 0. */
vec3 glassWobble(vec3 n, vec3 p, float amount) {
    if (amount <= 1e-4) return n;
    float t = glassTime() * 0.5;
    vec3 w = sin(p.yzx * 4.1 + vec3(t, 1.3 - 0.7 * t, 2.6 + 0.4 * t));
    w += 0.5 * sin(p.zxy * 7.3 - vec3(0.9 * t, 2.1, t));
    return normalize(n + amount * 0.16 * w);
}

/** Frost grain on a normal: fine 3D noise, `amount` 0..1 (0 skips it). The rough lookup does the blur; this adds the sugar. */
vec3 glassGrain(vec3 n, vec3 p, float amount) {
    if (amount <= 1e-3) return n;
    vec3 q = p * 38.0;
    vec3 g = vec3(glassNoise3(q), glassNoise3(q + 17.3), glassNoise3(q + 41.9));
    return normalize(n + amount * 0.1 * g);
}

/** Chaos on a marched surface: a distance offset to add to the field. 0 at the default macro. Divide the field by glassSurfaceLip(). */
float glassSurfaceWarp(vec3 p) {
    float amp = 0.035 * glassChaos();
    if (amp <= 1e-4) return 0.0;
    float t = glassTime() * 0.4;
    return amp * sin(p.x * 3.7 + t) * sin(p.y * 3.1 - 0.8 * t) * sin(p.z * 3.3 + 1.7 + 0.6 * t);
}

/** Bound on glassSurfaceWarp's gradient, plus one. */
float glassSurfaceLip() {
    return 1.0 + 0.035 * glassChaos() * 6.2;
}

/** A pure spectral ramp, s = 0 red to s = 1 violet (violet carries a little red, as it reads to the eye). */
vec3 glassSpectrum(float s) {
    float r = clamp(1.0 - abs(s - 0.05) * 3.2, 0.0, 1.0) + 0.45 * clamp(1.0 - abs(s - 1.0) * 4.0, 0.0, 1.0);
    float g = clamp(1.0 - abs(s - 0.42) * 3.0, 0.0, 1.0);
    float b = clamp(1.0 - abs(s - 0.80) * 3.2, 0.0, 1.0);
    return vec3(r, g, b);
}

/**
 * Reflected colour of a thin film `nm` nanometres thick, IOR `n`, lit at
 * incidence cosine `cosI` in air. Two-beam interference at 650/532/450 nm, with
 * the half-wave flip at the front face, so zero thickness is black. Tilting the
 * film shifts every band toward blue, which is why a bubble's rim is a
 * different colour from its face.
 */
vec3 glassFilm(float nm, float cosI, float n) {
    float sin2 = (1.0 - cosI * cosI) / (n * n);
    float cosT = sqrt(max(1.0 - sin2, 0.0));
    vec3 phase = GLASS_TAU * 2.0 * n * nm * cosT / vec3(650.0, 532.0, 450.0);
    return 0.5 - 0.5 * cos(phase);
}

/** The filmic shoulder: linear to about 0.6, then rolls off toward 1 per channel, keeping hue. Never lifts the frame. */
vec3 glassTone(vec3 c) {
    c = max(c, 0.0);
    vec3 k = 0.6 + 0.4 * (1.0 - exp(-(c - 0.6) / 0.4));
    return mix(c, k, step(0.6, c));
}

// ---- the worlds --------------------------------------------------------------

/**
 * How a style looks into its world. `env` picks one of GLASS_ENV_*; `hue`
 * offsets the palette; `yaw` and `height` aim the key light (radians, 0..1);
 * `rough` widens every light edge and lowers its peak to keep its energy (the
 * frosted lookup); `spread` pulls the side lights apart (stereo width); `glow`
 * scales the lights (Energy and slow trend, never SND_LEVEL).
 */
struct GlassLook {
    int env;
    float hue;
    float yaw;
    float height;
    float rough;
    float spread;
    float glow;
    float flow;
    float warp;
    float detail;
};

GlassLook glassLook(int env, float hue, GlassEar e) {
    GlassLook l;
    l.env = env;
    l.hue = hue;
    l.yaw = 0.7 * e.pan;
    l.height = e.height;
    l.rough = 0.1 * e.frost * (1.0 - 0.6 * e.sharp);
    l.spread = e.width;
    l.glow = 0.9 + 0.1 * e.trend;
    // The living worlds move on the sound engine's curved clock (it runs faster in loud passages)
    // plus a slow floor, and fold harder on the snare.
    l.flow = 0.08 * SND_CURVED_TIME + 0.04 * glassTime();
    l.warp = 0.75 + 0.5 * e.snare + 0.25 * glassChaos();
    // Fractal iterations follow the quality tier: about 8 on Balanced, 11 on High, 6 on Saver.
    l.detail = 0.25 + 0.75 * glassQuality();
    return l;
}

/**
 * The key light's direction for a look: above and BEHIND the subject, a little
 * to the left, turned by yaw and raised by height. A back key is how glass is
 * lit: it rims the top edge (where Fresnel is strongest) and throws the
 * caustic forward onto the floor where the viewer sees it.
 */
vec3 glassKeyDir(GlassLook l) {
    float el = mix(0.45, 1.25, clamp(l.height, 0.0, 1.0));
    float az = -0.45 + l.yaw;
    return normalize(vec3(sin(az) * cos(el), sin(el), cos(az) * cos(el)));
}

/** A soft rectangular light panel centred on direction `c` (gnomonic projection: straight edges). 0..1. */
float glassPanel(vec3 d, vec3 c, vec2 halfSize, float soft) {
    float k = dot(d, c);
    if (k <= 0.05) return 0.0;
    vec3 r = normalize(cross(abs(c.y) > 0.95 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0), c));
    vec3 u = cross(c, r);
    vec2 q = vec2(dot(d, r), dot(d, u)) / k;
    vec2 e = abs(q) - halfSize;
    float dist = length(max(e, 0.0)) + min(max(e.x, e.y), 0.0);
    return 1.0 - smoothstep(-soft, soft, dist);
}

/** A round light of angular radius `r` around direction `c`, with a halo. `rough` widens it and lowers its peak. */
float glassDisc(vec3 d, vec3 c, float r, float rough) {
    float a = acos(clamp(dot(d, c), -1.0, 1.0));
    float w = r + rough;
    float core = (1.0 - smoothstep(w - 0.004 - rough, w + 0.004 + rough, a)) * (r * r) / (w * w);
    return core + 0.12 * exp(-a * a / (0.02 + 4.0 * rough * rough));
}

/** Energy kept when an edge of width `soft` is widened by `rough`: the frosted light is dimmer, never brighter. */
float glassKeep(float soft, float rough) {
    return soft / (soft + rough * 2.0);
}

/** A thin glowing tube along the great circle with normal `axis`, `width` radians wide. 0..1 peak. */
float glassTube(vec3 d, vec3 axis, float width, float rough) {
    float w = width + rough * 0.6;
    float x = dot(d, axis);
    return exp(-x * x / (w * w)) * width / w;
}

/**
 * STUDIO: lit the way glass is photographed - from BEHIND. A large graded
 * backdrop panel behind the subject in the palette's colour (bright-field: the
 * body shows dark outlines and an inverted panel), two tall white strips
 * behind it to the left and right that open apart with stereo width (they
 * refract through as the long bright streaks glass is recognised by), a hot
 * key softbox above the viewer that the music aims (the crown highlight), and a
 * dark glossy floor mirroring all of it.
 */
vec3 glassStudio(vec3 d, GlassLook l) {
    float s = l.rough;
    vec3 col = pal(l.hue + 0.55) * 0.025 + 0.006;
    vec3 dd = d;
    float mirror = 1.0;
    if (d.y < 0.0) {
        // The cyclorama's sweep: the backdrop's glow runs down onto the floor and fades toward the viewer,
        // so there is no horizon line, only light pooling at the foot of the wall.
        dd = vec3(d.x, -d.y, d.z);
        mirror = mix(0.25 * smoothstep(-0.6, 0.0, d.y), 1.0, smoothstep(-0.07, 0.0, d.y));
        vec2 f = d.xz / max(-d.y, 0.02);
        float pool = exp(-0.06 * dot(f - vec2(0.0, 9.0), f - vec2(0.0, 9.0)) * 0.12);
        float side = clamp(f.x * 0.1 + 0.5, 0.0, 1.0);
        col += mix(pal(l.hue + 0.05), pal(l.hue + 0.45), side) * 0.35 * pool * l.glow;
    }
    // The backdrop: a soft graded glow behind the subject, brightest just above the floor line.
    vec3 bc = normalize(vec3(0.0, 0.12, 1.0));
    float side = clamp(dd.x * 1.5 + 0.5, 0.0, 1.0);
    float ba = max(1.0 - dot(dd, bc), 0.0);
    float back = exp(-ba * (9.0 - 5.0 * s));
    vec3 light = mix(pal(l.hue + 0.05), pal(l.hue + 0.45), side) * (0.55 + 0.4 * smoothstep(0.0, 0.4, dd.y)) * back;
    float sx = 0.3 + 0.2 * l.spread + 0.06 * sin(l.flow * 3.0);
    light += mix(vec3(1.0), pal(l.hue + 0.6), 0.25) * 4.0 * glassPanel(dd, normalize(vec3(-sx, 0.1, 1.0)), vec2(0.035, 0.75), 0.012 + s) * glassKeep(0.012, s);
    light += mix(vec3(1.0), pal(l.hue + 0.2), 0.25) * 3.6 * glassPanel(dd, normalize(vec3(sx, 0.14, 1.0)), vec2(0.03, 0.7), 0.012 + s) * glassKeep(0.012, s);
    light += vec3(1.0, 0.95, 0.88) * 9.0 * glassPanel(dd, glassKeyDir(l), vec2(0.45, 0.22), 0.03 + s) * glassKeep(0.03, s);
    light += vec3(1.0, 0.97, 0.92) * 7.0 * glassPanel(dd, normalize(vec3(-0.35, 0.8, -0.5)), vec2(0.22, 0.14), 0.02 + s) * glassKeep(0.02, s);
    light += mix(vec3(1.0), pal(l.hue + 0.75), 0.5) * 2.0 * glassPanel(dd, normalize(vec3(1.0, 0.35, -0.5)), vec2(0.05, 0.5), 0.02 + s) * glassKeep(0.02, s);
    return col + light * l.glow * mirror;
}

/**
 * NEON: a dark room ringed by six tall tubes of saturated palette light, a ring
 * hung round the key light's direction, a dashed strip at eye level and a low
 * coloured haze, mirrored in a wet floor. Thin saturated lines are where
 * dispersion and thin-film colour are most legible.
 */
vec3 glassNeon(vec3 d, GlassLook l) {
    float s = l.rough;
    vec3 dd = d;
    float wet = 1.0;
    if (d.y < 0.0) {
        dd = vec3(d.x, -d.y, d.z);
        wet = 0.3 * (0.6 + 0.4 * glassNoise(d.xz / max(-d.y, 0.05) * 1.3));
    }
    vec3 col = pal(l.hue + 0.7) * 0.05 * exp(-abs(d.y) * 3.0) + 0.004;
    // A soft lit wall behind the subject, so a body has something to bend.
    col += mix(pal(l.hue + 0.2), pal(l.hue + 0.75), clamp(dd.x + 0.5, 0.0, 1.0)) * 0.3 * exp(-max(1.0 - dot(dd, normalize(vec3(0.0, 0.15, 1.0))), 0.0) * 5.0) * wet;
    vec3 key = glassKeyDir(l);
    float ring = abs(acos(clamp(dot(dd, key), -1.0, 1.0)) - 0.4);
    float w = 0.014 + s * 0.5;
    vec3 light = mix(vec3(1.0), pal(l.hue), 0.7) * 3.0 * exp(-ring * ring / (w * w)) * 0.014 / w;
    // Six vertical tubes round the room; stereo width spreads them.
    float az = atan(dd.x, dd.z);
    float cell = GLASS_TAU / 6.0;
    float sp = 1.0 + 0.25 * l.spread;
    float a = az * sp;
    float id = floor(a / cell + 0.5);
    float off = (a - id * cell) * sqrt(max(1.0 - dd.y * dd.y, 0.0));
    float tw = 0.018 + s * 0.6;
    float tube = exp(-off * off / (tw * tw)) * 0.018 / tw * smoothstep(-0.05, 0.1, dd.y) * (1.0 - smoothstep(0.55, 0.75, dd.y));
    // The tubes chase round the room, one step per onset (eased), so the room dances.
    float chase = fract((id - glassGlide(SND_ONSET_COUNT, SND_ONSET_ENV)) / 6.0);
    light += mix(pal(l.hue + id * 0.17), vec3(1.0), 0.25) * (0.8 + 3.2 * smoothstep(0.6, 1.0, chase)) * tube;
    // The dashed eye-level strip.
    float dash = smoothstep(0.2, 0.3, abs(fract(az * 3.0) - 0.5));
    light += pal(l.hue + 0.5) * 1.1 * glassTube(dd, vec3(0.0, 1.0, 0.0), 0.01, s) * dash;
    return col + light * l.glow * wet;
}

/**
 * DUSK: a palette sunset - warm near the horizon, deep overhead - with the sun
 * the key aims, a band of far hills, and a dark sea below that the sun
 * glitters on.
 */
vec3 glassDusk(vec3 d, GlassLook l) {
    float s = l.rough;
    vec3 key = glassKeyDir(l);
    key = normalize(vec3(key.x, mix(0.04, 0.5, l.height), key.z));
    vec3 low = mix(pal(l.hue + 0.05), vec3(1.0, 0.6, 0.35), 0.4) * 0.5;
    vec3 high = pal(l.hue + 0.6) * 0.08 + vec3(0.01, 0.015, 0.04);
    vec3 col = mix(low, high, smoothstep(0.0, 0.3, d.y));
    float hill = 0.035 + 0.025 * glassNoise(vec2(atan(d.x, d.z) * 3.0, 0.0)) + 0.012 * glassNoise(vec2(atan(d.x, d.z) * 11.0, 2.0));
    col = mix(col, pal(l.hue + 0.75) * 0.05, smoothstep(hill - 0.004 - s * 0.1, hill + 0.004 + s * 0.1, -d.y + 2.0 * hill) * step(0.0, d.y));
    // An aurora curtain hanging over the far hills: folded ribbons that drift on the music's clock.
    float az = atan(d.x, d.z);
    float fold = az * 4.0 + 1.4 * sin(az * 2.3 + l.flow * 1.3) + 0.6 * sin(az * 7.0 - l.flow * 2.1) * l.warp;
    float curtain = pow(1.0 - abs(sin(fold)), 5.0) * smoothstep(0.05, 0.25, d.y) * (1.0 - smoothstep(0.55, 0.9, d.y));
    curtain *= 0.6 + 0.4 * sin(d.y * 30.0 + az * 3.0 + l.flow * 4.0);
    col += mix(pal(l.hue + 0.35), pal(l.hue + 0.6), smoothstep(0.2, 0.7, d.y)) * curtain * 1.4 * glassKeep(0.05, s);
    float sun = glassDisc(d, key, 0.04, s);
    col += vec3(1.0, 0.82, 0.6) * 4.0 * sun * l.glow;
    col += low * 0.6 * exp(-max(1.0 - dot(d, key), 0.0) * 5.0);
    if (d.y < 0.0) {
        vec3 sea = pal(l.hue + 0.62) * 0.03 + 0.005;
        vec3 m = reflect(d, vec3(0.0, 1.0, 0.0));
        vec2 f = d.xz / max(-d.y, 0.02);
        float ripple = 0.5 + 0.5 * glassNoise(f * vec2(0.6, 2.4));
        float glit = pow(max(dot(m, key), 0.0), mix(400.0, 40.0, s)) * (0.3 + 1.7 * ripple);
        col = sea + low * 0.12 + vec3(1.0, 0.75, 0.5) * glit * 2.5 * l.glow + low * 0.35 * smoothstep(-0.3, 0.0, d.y);
    }
    return col;
}

/**
 * DEEP: under water. A bright rippling surface overhead with its own caustic
 * shimmer, light shafts falling from the key's direction, blue-green falloff to
 * black below.
 */
vec3 glassDeep(vec3 d, GlassLook l) {
    float s = l.rough;
    vec3 water = mix(pal(l.hue + 0.5), vec3(0.1, 0.45, 0.6), 0.5);
    vec3 col = water * (0.02 + 0.22 * smoothstep(-0.6, 0.9, d.y));
    if (d.y < -0.05) {
        // The sea floor far below: pale sand catching the light, fading into the blue.
        vec2 f = d.xz / -d.y;
        float lit = 0.6 + 0.4 * glassNoise(f * 1.5 + glassTime() * 0.2);
        col += mix(vec3(0.7, 0.62, 0.48), water, 0.45) * 0.35 * lit * smoothstep(0.05, 0.6, -d.y);
    }
    if (d.y > 0.05) {
        vec2 f = d.xz / d.y;
        float t = glassTime();
        float n = glassNoise(f * 2.0 + t * 0.3) + 0.5 * glassNoise(f * 4.3 - t * 0.4);
        float sky = smoothstep(0.35, 0.9, d.y);
        col += mix(water, vec3(1.0), 0.6) * (0.4 + 0.5 * n) * sky * 1.3;
        float window = 1.0 - smoothstep(0.82 - s, 0.86 + s, 1.0 - d.y);
        col += vec3(0.9, 1.0, 1.0) * 1.2 * window * (0.6 + 0.4 * n);
    }
    vec3 key = glassKeyDir(l);
    vec3 shaftAxis = normalize(cross(key, vec3(0.0, 0.0, 1.0)) + vec3(0.0, 0.0, 0.001));
    float sh = 0.5 + 0.5 * sin(dot(d, shaftAxis) * 40.0 + 1.3 * sin(dot(d, shaftAxis) * 9.0));
    col += mix(water, vec3(1.0), 0.5) * 0.5 * sh * sh * pow(max(dot(d, key), 0.0), 3.0) * glassKeep(0.05, s) * l.glow;
    return col;
}

/**
 * VOID: black space with a few large palette nebula glows and a field of
 * point lights at infinity. For subjects floating in nothing; points become
 * sharp sparkles through the body, the glows its tint.
 */
vec3 glassVoid(vec3 d, GlassLook l) {
    float s = l.rough;
    vec3 key = glassKeyDir(l);
    vec3 col = vec3(0.004, 0.004, 0.008);
    col += pal(l.hue + 0.1) * 0.9 * pow(max(dot(d, key), 0.0), 4.0);
    col += pal(l.hue + 0.45) * 0.7 * pow(max(dot(d, normalize(vec3(0.8 + 0.3 * l.spread, -0.2, 0.5))), 0.0), 4.0);
    col += pal(l.hue + 0.8) * 0.6 * pow(max(dot(d, normalize(vec3(-0.7 - 0.3 * l.spread, -0.4, -0.3))), 0.0), 3.0);
    // Point lights at infinity on two lattices: a sparse bright one and a denser dim one.
    for (int k = 0; k < 2; k++) {
        float sc = k == 0 ? 9.0 : 23.0;
        vec3 q = d * sc;
        vec3 c = floor(q);
        float h = glassHash3(c + float(k) * 31.0);
        if (h > (k == 0 ? 0.55 : 0.75)) {
            vec3 o = vec3(glassHash3(c + 3.1), glassHash3(c + 7.7), glassHash3(c + 1.9)) - 0.5;
            float r = length(fract(q) - 0.5 - o * 0.5);
            float w = (k == 0 ? 0.05 : 0.07) + s * sc * 0.5;
            float base = k == 0 ? 7.0 : 2.0;
            col += mix(vec3(1.0), pal(h * 7.0 + l.hue), 0.45) * base * exp(-r * r / (w * w)) * pow((k == 0 ? 0.05 : 0.07) / w, 2.0);
        }
    }
    col += vec3(1.0, 0.95, 0.9) * 6.0 * glassDisc(d, key, 0.06, s) * l.glow;
    return col;
}

/**
 * The neon push: the references this family is drawn toward are blacklight art,
 * saturated colour on near black. This lifts a palette colour's saturation
 * (pushing it away from its own grey) without changing its hue, so any palette
 * the user picks still reads, just louder.
 */
vec3 glassVivid(vec3 c, float amount) {
    float g = dot(c, vec3(0.299, 0.587, 0.114));
    return max(mix(vec3(g), c, 1.0 + amount), 0.0);
}

/**
 * FRACTAL: the dense world. A folding fractal of the kind found by iterating
 * p -> |p| / dot(p, p) - c (each step folds space into the positive octant and
 * inverts it through the unit sphere, so structure repeats at every scale),
 * walked along the view direction a few depths at a time. Each iteration
 * leaves a trace where the orbit's radius changes sharply, and those traces
 * stack into glowing filaments, cells and spirals that fill the whole sphere
 * of view. Colour runs along the palette with the iteration count, pushed to
 * neon. c drifts on the music's clock and the snare kicks it, so the whole
 * structure breathes and reshapes; `detail` (from the quality tier) sets how
 * many iterations are spent. On High only (glassMarchBounces) it adds a third
 * fractal depth, spirals tiled inside the vortex, and a two-level cell web:
 * detail at every scale, at a cost Balanced phones are spared.
 */
vec3 glassFractal(vec3 d, GlassLook l) {
    int iters = int(5.0 + 6.0 * clamp(l.detail, 0.0, 1.0));
    vec3 c = vec3(0.52, 0.42, 0.61) + 0.07 * vec3(sin(l.flow * 0.9), cos(l.flow * 0.7), sin(l.flow * 1.1 + 1.0)) + 0.05 * (l.warp - 0.75);
    vec3 col = vec3(0.0);
    float rough = clamp(l.rough * 6.0, 0.0, 1.0);
    // High adds a third, finer depth (glassMarchBounces: the same budget gate as the second interior march).
    int layers = glassMarchBounces() ? 3 : 2;
    for (int layer = 0; layer < 3; layer++) {
        if (layer >= layers) break;
        float depth = 0.55 + 0.45 * float(layer);
        vec3 p = d * (0.95 + 0.75 * depth) + vec3(0.13, 0.31, 0.2) * float(layer) + vec3(0.0, 0.0, l.flow * 0.05);
        // Bend space first so the folds come out as curves, not polygons.
        p += 0.22 * l.warp * sin(p.yzx * 3.1 + l.flow * vec3(1.0, 0.7, 1.3));
        float prev = 0.0;
        float acc = 0.0;
        vec3 tint = vec3(0.0);
        for (int i = 0; i < 11; i++) {
            if (i >= iters) break;
            p = abs(p) / dot(p, p) - c;
            float r = dot(p, p);
            float dr = abs(r - prev);
            // A glowing tube: a hot core and a wide halo round each trace.
            float edge = exp(-dr * mix(22.0, 6.0, rough)) + 0.25 * exp(-dr * mix(5.0, 2.0, rough));
            prev = r;
            acc += edge;
            tint += edge * pal(l.hue + float(i) * 0.15 + 0.33 * float(layer) + 0.1 * r);
        }
        col += glassVivid(tint, 0.9) * (layer == 0 ? 0.3 : (layer == 1 ? 0.16 : 0.1));
        // The membrane: a soft coloured haze where the orbit lingered, filling the gaps.
        col += glassVivid(tint, 0.5) * 0.02 * acc;
    }
    // Spirals: a log-spiral lattice round the key's direction, the eye of the vortex.
    vec3 key = glassKeyDir(l);
    vec3 ax = normalize(cross(key, vec3(0.0, 1.0, 0.001)));
    vec3 ay = cross(key, ax);
    vec2 sp = vec2(dot(d, ax), dot(d, ay));
    float sr = length(sp) + 1e-3;
    float sa = atan(sp.y, sp.x);
    float spiral = pow(0.5 + 0.5 * cos(sa * 3.0 + log(sr) * 9.0 - l.flow * 2.0), 8.0);
    col += glassVivid(pal(l.hue + 0.6 + log(sr) * 0.3), 0.6) * spiral * 1.2 * exp(-sr * 1.2) * (1.0 - rough);
    if (glassMarchBounces()) {
        // HIGH ONLY. Spirals inside spirals: the vortex's log-polar plane tiled into cells, a small
        // spiral turning in each, so the big spiral is made of little ones.
        vec2 lp = vec2(log(sr) * 2.2 - l.flow * 0.6, sa * 6.0 / GLASS_TAU);
        vec2 lc = floor(lp);
        vec2 lf = fract(lp) - 0.5;
        float lr = length(lf) + 1e-3;
        float la = atan(lf.y, lf.x);
        float small = pow(0.5 + 0.5 * cos(la * 2.0 + log(lr) * 7.0 + l.flow * 3.0 + glassHash(lc) * 6.0), 10.0);
        small *= 1.0 - smoothstep(0.25, 0.5, lr);
        col += glassVivid(pal(l.hue + glassHash(lc) * 0.8), 0.8) * small * 0.5 * exp(-sr * 0.8) * (1.0 - rough);
        // Cells inside cells: a two-level cell web on the view plane, its walls glowing, the inner
        // level finer and fainter, so every cell of the web holds its own smaller web.
        vec2 cp = vec2(atan(d.x, d.z) * 1.6, d.y * 3.0) + 0.1 * l.flow;
        for (int lev = 0; lev < 2; lev++) {
            float sc = lev == 0 ? 3.0 : 9.0;
            vec2 g = cp * sc;
            vec2 gi = floor(g);
            vec2 gf = fract(g);
            float d1 = 8.0;
            float d2 = 8.0;
            for (int y = -1; y <= 1; y++) {
                for (int x = -1; x <= 1; x++) {
                    vec2 o = vec2(float(x), float(y));
                    vec2 h = glassHash2(gi + o + float(lev) * 17.0);
                    vec2 pt = o + 0.5 + 0.4 * sin(l.flow * 0.7 + h * 6.2831);
                    float dd = length(pt - gf);
                    if (dd < d1) {
                        d2 = d1;
                        d1 = dd;
                    } else if (dd < d2) {
                        d2 = dd;
                    }
                }
            }
            float wall = exp(-(d2 - d1) * (lev == 0 ? 18.0 : 30.0));
            col += glassVivid(pal(l.hue + 0.2 + 0.3 * float(lev) + d1), 0.8) * wall * (lev == 0 ? 0.22 : 0.1) * (1.0 - rough);
        }
    }
    // Contrast: dark gaps, bright filaments, as blacklight art reads.
    col = col * col * 1.6 + col * 0.15;
    return col * l.glow;
}

/** The FRACTAL world as a flat picture: point `p` on a plane one unit in front of the eye. `rough` blurs it. */
vec3 glassFractal2(vec2 p, GlassLook l, float rough) {
    GlassLook r = l;
    r.rough = l.rough + rough;
    return glassFractal(normalize(vec3(p * 0.55, 1.0)), r);
}

/**
 * AURORA: a world made only of moving light. The view direction is bent by
 * four rounds of sine displacement, each feeding the last (the classic
 * iterated domain warp, here in closed form with no noise lookups so a glass
 * body can afford it five to fifteen times a pixel), and the warped point's
 * phase is drawn as filaments: thin bright ribbons of palette light folding
 * through a dim glow, with white-hot cores where they bunch. It flows on
 * GlassLook.flow and folds harder with GlassLook.warp; nothing in it is
 * static, so everything seen through glass in it is alive.
 */
vec3 glassAurora(vec3 d, GlassLook l) {
    vec3 q = d * 2.3;
    float w = l.warp;
    float f = l.flow;
    q += w * 0.55 * sin(q.zxy * 1.7 + f * vec3(1.0, 1.3, 0.7));
    q += w * 0.45 * sin(q.yzx * 2.3 - f * vec3(0.8, 0.6, 1.1) + 1.7);
    q += w * 0.35 * sin(q.zxy * 3.1 + f * vec3(0.5, 1.4, 0.9) + 3.1);
    q += w * 0.25 * sin(q.yzx * 4.3 - f * vec3(1.2, 0.4, 0.7) + 0.6);
    float ph = q.x + 1.3 * q.y + 0.7 * q.z;
    float ribbon = 1.0 - abs(sin(ph * 1.6));
    float rough = l.rough * 6.0;
    float fil = pow(ribbon, mix(9.0, 3.0, clamp(rough, 0.0, 1.0)));
    float core = pow(ribbon, 40.0) * (1.0 - clamp(rough, 0.0, 1.0));
    // A second, finer set of ribbons crossing the first, in the palette's other half.
    float ph2 = q.z - 0.8 * q.x + 0.5 * q.y;
    float ribbon2 = 1.0 - abs(sin(ph2 * 2.6 + f));
    float fil2 = pow(ribbon2, mix(14.0, 4.0, clamp(rough, 0.0, 1.0)));
    vec3 c = pal(l.hue + 0.12 * q.x + 0.09 * q.z + 0.05 * ph);
    vec3 c2 = pal(l.hue + 0.45 + 0.1 * q.y - 0.05 * ph2);
    float wash = 0.5 + 0.5 * sin(q.y * 1.3 + q.x * 0.7 + f);
    vec3 glow = mix(pal(l.hue + 0.5 + 0.1 * q.y), pal(l.hue + 0.2 + 0.1 * q.x), wash) * (0.04 + 0.1 * wash * wash);
    vec3 col = glow + c * fil * 1.4 + c2 * fil2 * 0.9 + mix(c, vec3(1.0), 0.6) * core * 2.5;
    // A brighter band where the key light sits, so the pan and pitch still steer it.
    col *= 0.6 + 0.8 * pow(max(dot(d, glassKeyDir(l)), 0.0), 2.0);
    return col * l.glow;
}

/** The world in direction `d`, as the look picks it. */
vec3 glassEnv(vec3 d, GlassLook l) {
    if (l.env == GLASS_ENV_NEON) return glassNeon(d, l);
    if (l.env == GLASS_ENV_DUSK) return glassDusk(d, l);
    if (l.env == GLASS_ENV_DEEP) return glassDeep(d, l);
    if (l.env == GLASS_ENV_VOID) return glassVoid(d, l);
    if (l.env == GLASS_ENV_AURORA) return glassAurora(d, l);
    if (l.env == GLASS_ENV_FRACTAL) return glassFractal(d, l);
    return glassStudio(d, l);
}

/** The same world, frosted by `extra` more roughness (a frosted face of an otherwise clear body). */
/**
 * The neon rim every translucent body wears, like the edge-lit shells of
 * blacklight art: a palette glow rising toward grazing incidence. Additive.
 */
vec3 glassRim(float cosI, float hue, float strength) {
    float r = 1.0 - clamp(cosI, 0.0, 1.0);
    return glassVivid(pal(hue + 0.3 * r), 0.8) * (r * r * 1.2 + pow(r, 8.0) * 2.0) * strength;
}

vec3 glassEnvRough(vec3 d, GlassLook l, float extra) {
    GlassLook r = l;
    r.rough = l.rough + extra;
    return glassEnv(d, r);
}

/** The world seen dispersed: three directions, one channel each. */
vec3 glassEnv3(vec3 dr, vec3 dg, vec3 db, GlassLook l) {
    return vec3(glassEnv(dr, l).r, glassEnv(dg, l).g, glassEnv(db, l).b);
}

// ---- glints ------------------------------------------------------------------------

/**
 * Hi-hat sparkle: four-point stars on a 3D lattice of surface points, most
 * cells empty, each lit cell flaring when the hat hits and its own random
 * phase lines up. `amount` is the hat envelope; `p` a surface point; `scale`
 * the lattice density. Additive, 0..~2.
 */
float glassGlint(vec3 p, float scale, float amount) {
    if (amount <= 0.02) return 0.0;
    vec3 q = p * scale;
    vec3 c = floor(q);
    float h = glassHash3(c);
    if (h < 0.86) return 0.0;
    vec3 f = fract(q) - 0.5;
    float r = length(f);
    float star = exp(-r * r * 90.0) + 0.5 * exp(-abs(f.x * f.y) * 900.0 - r * 6.0);
    float phase = 0.5 + 0.5 * sin(glassTime() * 3.0 + h * 40.0);
    return star * amount * (0.4 + phase) * 2.0;
}

// ---- spheres, end to end ------------------------------------------------------

/**
 * Light through a solid sphere (centre c, radius r) entering at `p` with
 * outward normal `n` from incoming ray `rd`, refracted with `ior`, dispersed by
 * `spread` at the exit, looking into the world `l`. Up to `bounces` internal
 * passes: the first exit is traced per wavelength; the share the far wall
 * reflects crosses again (same chord length, a sphere's symmetry) and leaves
 * through one more refraction. `tint` and `density` colour the path.
 */
vec3 glassSphereTrace(vec3 p, vec3 n, vec3 rd, vec3 c, float r, float ior, float spread, vec3 tint, float density, int bounces, GlassLook l) {
    vec3 d1 = glassRefract(rd, n, 1.0 / ior);
    vec3 oc = p - c;
    float b = dot(oc, d1);
    float chord = max(-b + sqrt(max(b * b - (dot(oc, oc) - r * r), 0.0)), 0.0);
    vec3 q = p + d1 * chord;
    vec3 n2 = normalize(q - c);
    vec3 iors = glassIors(ior, spread);
    vec3 er = glassRefract(d1, -n2, iors.x);
    vec3 eg = glassRefract(d1, -n2, iors.y);
    vec3 eb = glassRefract(d1, -n2, iors.z);
    vec3 absorb = glassBeer(tint, chord, density);
    float fExit = glassFresnel(max(dot(d1, n2), 0.0), ior);
    vec3 col = glassEnv3(er, eg, eb, l) * absorb * (1.0 - fExit);
    if (bounces > 1) {
        vec3 d2 = reflect(d1, n2);
        vec3 q2 = q + d2 * chord;
        vec3 n3 = normalize(q2 - c);
        vec3 e2 = glassRefract(d2, -n3, ior);
        col += glassEnv(e2, l) * absorb * absorb * fExit * (1.0 - glassFresnel(max(dot(d2, n3), 0.0), ior)) * 1.6;
    }
    return col;
}

// ---- marching ------------------------------------------------------------------

/** Interior march budget: under half the user's Detail budget, never past GLASS_MAX_INNER. */
float glassInnerSteps() {
    return min(uSteps * 0.4, float(GLASS_MAX_INNER));
}

/** Hit epsilon at distance t: about a pixel's footprint, doubled at low Detail. Also the normal epsilon. */
float glassEps(float t) {
    return (uSteps < 80.0 ? 0.002 : 0.001) * t + 0.0004;
}

/** Occlusion from the step count: a ray that spent most of its budget crept along a crease. */
float glassStepAO(float used) {
    return clamp(1.0 - used / max(uSteps, 1.0) * 1.2, 0.25, 1.0);
}

// ---- water and caustics ----------------------------------------------------------

/**
 * Five travelling sine waves on the plane, directions a golden angle apart,
 * each at the deep-water speed for its wavenumber, so the surface never
 * repeats visibly. Returns x = height, yz = slope; `hess` gets the Hessian
 * (xx, yy, xy), from which glassCausticRGB builds the light map's determinant
 * at any focus depth. `chop` scales the amplitude.
 */
vec3 glassWaves(vec2 p, float t, float chop, out vec3 hess) {
    float h = 0.0;
    vec2 g = vec2(0.0);
    vec3 hs = vec3(0.0);
    float k = 1.0;
    for (int i = 0; i < 5; i++) {
        float fi = float(i);
        float a = 2.39996 * fi + 0.6;
        vec2 dir = vec2(cos(a), sin(a));
        float amp = chop * 0.32 / (k * k);
        float ph = k * dot(dir, p) + sqrt(k) * 1.6 * t + fi * 1.7;
        float s = sin(ph);
        float c = cos(ph);
        h += amp * s;
        g += amp * k * c * dir;
        hs -= amp * k * k * s * vec3(dir.x * dir.x, dir.y * dir.y, dir.x * dir.y);
        k *= 1.43;
    }
    hess = hs;
    return vec3(h, g);
}

/** The light map's determinant at focus depth f: det(I + f H). */
float glassDet(vec3 hess, float f) {
    return (1.0 + f * hess.x) * (1.0 + f * hess.y) - f * f * hess.z * hess.z;
}

/** Caustic brightness from a determinant, softened by `sharp` (smaller is thinner, brighter lines). 0..1. */
float glassCaustic(float det, float sharp) {
    return clamp(sharp / (abs(det) + sharp), 0.0, 1.0);
}

/**
 * The caustic net in three wavelengths: red focuses deepest, blue shallowest
 * (`spread` is the fractional difference), so the lines split into thin
 * rainbows where they fold. One Hessian, three determinants.
 */
vec3 glassCausticRGB(vec3 hess, float focus, float spread, float sharp) {
    return vec3(
        glassCaustic(glassDet(hess, focus * (1.0 + spread)), sharp),
        glassCaustic(glassDet(hess, focus), sharp),
        glassCaustic(glassDet(hess, focus * (1.0 - spread)), sharp));
}
