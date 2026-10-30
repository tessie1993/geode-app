// The Glass family's shared optics: analytic intersections, Fresnel,
// refraction with a total-internal-reflection fallback, three-wavelength
// dispersion, a studio backdrop every style can look into, a caustic light
// net, and thin-film colour.
//
// WHAT GLASS IS, FOR A SHADER
//
// A glass surface does two things at once. Some of the light reflects, and
// how much is set by the angle: almost nothing face on, almost everything at
// grazing incidence. Schlick's approximation of Fresnel's equations gives that
// curve in one polynomial, and it is the single most important line in this
// family - without it glass reads as a flat tinted disc, with it the rim of
// every shape lights up the way a wine glass does on a table. The rest of the
// light bends by Snell's law into the body, travels through it (losing colour
// by Beer-Lambert absorption the further it goes), and bends again on the way
// out. GLSL's refract() does Snell; what it does not do is say anything useful
// when Snell has no solution. Leaving a dense medium at a steep angle there is
// no refracted ray - the light reflects back inside, which is total internal
// reflection and is exactly where a cut gem gets its fire. refract() returns a
// zero vector then, and a style that used it raw would sample the backdrop in
// direction (0,0,0) and paint black holes across its brightest facets.
// glassRefract() substitutes the mirror direction instead.
//
// All of that is textbook optics. Everything below is written from the
// equations, not from any particular shader.
//
// WHY ONE LIBRARY
//
// Twelve styles look through the same material, and a material that behaves
// differently in two styles reads as two materials. So the four things a viewer
// actually judges glass by live here and nowhere else:
//
//   THE FRESNEL CURVE. glassFresnel(), Schlick with the true f0 for the IOR,
//   so a higher IOR (a gem, a swollen orb) is visibly more reflective face on.
//
//   THE WORLD BEHIND IT. Glass is invisible against black: it only shows as
//   distortion of what is behind it. glassBackdrop() is one studio every 3D
//   style stands in - a palette-tinted sky gradient, a floor grid running to
//   the horizon (the grid is what makes refraction LEGIBLE: straight lines
//   that bend inside the body), and three soft light panels that become the
//   highlights and the long bright streaks on every rim. It is a function of
//   direction only, an environment at infinity, so looking into it costs a few
//   dot products and no march.
//
//   DISPERSION. Real glass bends blue more than red. glassIors() returns one
//   IOR per channel; a style traces red, green and blue separately and keeps
//   one channel of each. That is three backdrop samples - the ONLY multi-tap
//   this family allows itself, because it is the one that buys a visible
//   effect (rainbow fringes on edges) for its cost.
//
//   THE BOUNCE BUDGET. One refraction in, one exit. A marched body is marched
//   to its surface, refracted, marched through its interior once (the distance
//   field negated, so the ray walks to the far wall), and refracted out; a
//   ray that hits total internal reflection at the far wall takes the mirror
//   direction into the backdrop and stops there. Real glass keeps bouncing.
//   On a phone it does not: a second internal bounce doubles the frame, and at
//   the size these shapes are drawn it is not missed.
//
// THE CAUSTIC NET. Light refracted through a rippling surface lands unevenly:
// where neighbouring rays converge the floor is bright, where they spread it is
// dim. The exact brightness is the inverse of how much the surface's light map
// compresses area - the reciprocal of the determinant of its Jacobian. For a
// height field h the map is x -> x + f * grad h, whose Jacobian is I + f * H
// with H the Hessian of h. glassWaves() sums five travelling sine waves, so h,
// its slope and its Hessian are all closed form (a sine's second derivative is
// minus itself), and glassCaustic() turns the determinant into brightness. Where
// the determinant crosses zero the map folds over itself: those zero curves are
// the thin bright lines of a pool floor, and they close into a moving net with
// no texture and no noise at all. It is evaluated at the floor point rather
// than through the inverse map - an approximation that keeps the topology of
// the net exactly and costs nothing.
//
// THIN FILM. A soap film is a few hundred nanometres of water. Light reflecting
// off its front and back faces interferes, and which wavelengths cancel depends
// on the thickness, so the film shows bands of colour that follow its thickness.
// glassFilm() is the two-beam form, with the half-wave phase flip at the front
// face, so a film thinning to nothing goes BLACK rather than white - the dark
// patch at the top of a bubble just before it pops.
//
// WHAT IS NOT HERE: the marcher loops and the normal. GLSL ES has no function
// pointers, so a shared march cannot call a style's own map(). Each marched
// style writes its outer loop (constant bound GLASS_MAX_STEPS, breaking on
// uSteps), its interior loop (constant bound GLASS_MAX_INNER, breaking on
// glassInnerSteps()) and its four-tap tetrahedral normal itself, against its own
// map(), from glassEps and glassStepAO below.
//
// INCLUDE ORDER: after lib_scene_uniforms (reads uTime, uSpeed, uIntensity,
// uMorph, uWarp, uTurbulence, uSteps) and lib_palette (glassBackdrop calls
// pal()). It includes nothing itself, and reads no audio uniform: what moves
// with the music is each style's business.

#define GLASS_TAU 6.2831853
#define GLASS_PI 3.14159265
/** Compile-time ceiling on a marched style's outside march. The body breaks on uSteps. */
#define GLASS_MAX_STEPS 128
/** Compile-time ceiling on the one interior march. The body breaks on glassInnerSteps(). */
#define GLASS_MAX_INNER 64
/** Ordinary crown glass. */
#define GLASS_IOR 1.5

// ---- the Customize macros ---------------------------------------------------
//
// The app's macros move standard scene params, so every Glass style answers
// them through these and at their defaults each is the identity: Speed (uSpeed
// 1) scales animation time, Complexity (uMorph 0) adds pieces or detail per
// style, Chaos (uWarp, uTurbulence 0) wobbles surfaces and normals as if the
// glass were poured and not yet set, Energy (uIntensity 1) scales how far the
// music moves a style. Colour is pal(); Space is the rig.

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

// ---- envelope shaping --------------------------------------------------------
//
// lib_react's envelopes rise instantly. Glass magnifies whatever is behind it,
// so a value that steps on one frame steps the whole refracted image with it.
// These turn a hit into motion that eases.

/** 0 at the hit (envelope 1), peak at envelope 0.5, 0 as it dies: continuous across the hit. */
float glassBump(float env) {
    float e = clamp(env, 0.0, 1.0);
    return 4.0 * e * (1.0 - e);
}

/**
 * A monotonic counter (uHitCount, uSectionCount) that GLIDES to its new value
 * instead of stepping. On the frame the count increments the envelope is 1, so
 * this still returns the previous count; as the envelope falls it slides the
 * last unit. Anything positioned by it moves one notch per event, smoothly.
 */
float glassGlide(float count, float env) {
    float e = clamp(env, 0.0, 1.0);
    return count - e * e * (3.0 - 2.0 * e);
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

/**
 * A per-event random value that slides instead of stepping: the hash of the
 * previous count at the instant of the event, easing to the new one as the
 * envelope falls.
 */
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

/** 3D value noise, -1..1. Eight hashes: call it once per pixel (a normal), never per march step. */
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

/**
 * Schlick's Fresnel: the reflected fraction at incidence cosine `cosI` for an
 * interface with relative IOR `ior`. f0 is the true face-on value for that IOR
 * (4% for crown glass, 17% for diamond), so a denser material looks denser.
 */
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
 * returns zero; this returns the mirror direction instead, which is where that
 * light actually goes.
 */
vec3 glassRefract(vec3 i, vec3 n, float eta) {
    vec3 r = refract(i, n, eta);
    return dot(r, r) < 1e-6 ? reflect(i, n) : r;
}

/** One IOR per channel: red bends least, blue most. `spread` 0.01 is crown glass, 0.04 a showy gem. */
vec3 glassIors(float ior, float spread) {
    return ior + spread * vec3(-1.0, 0.0, 1.0);
}

/** Beer-Lambert transmission after `dist` through a body whose colour is `tint`: the thick parts saturate, the thin stay clear. */
vec3 glassBeer(vec3 tint, float dist, float density) {
    return exp(-(1.0 - clamp(tint, 0.0, 1.0)) * dist * density);
}

/**
 * Chaos on a normal: a slow, travelling sine wobble, as if the glass were still
 * soft. 0 at the default macro, and the branch is skipped.
 */
vec3 glassWobble(vec3 n, vec3 p, float amount) {
    if (amount <= 1e-4) return n;
    float t = glassTime() * 0.5;
    vec3 w = sin(p.yzx * 4.1 + vec3(t, 1.3 - 0.7 * t, 2.6 + 0.4 * t));
    w += 0.5 * sin(p.zxy * 7.3 - vec3(0.9 * t, 2.1, t));
    return normalize(n + amount * 0.16 * w);
}

/**
 * Chaos on a marched surface: a distance offset to add to the field. 0 at the
 * default macro. Divide the field by glassSurfaceLip() after adding it.
 */
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

// ---- the studio --------------------------------------------------------------

/**
 * A soft rectangular light panel centred on direction `c`, measured in the
 * plane one unit along c (the gnomonic projection, so a panel keeps straight
 * edges however wide it is). 0..1.
 */
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

/**
 * The backdrop's floor at point `f` of its plane: a dim tinted ground with a
 * grid of lines two units apart, fading into haze with `dist` from the viewer
 * (which also widens the lines, roughly with the pixel footprint, so the far
 * grid greys out instead of shimmering). A style that puts a real floor under
 * its subject calls this so the floor it draws and the one in the backdrop are
 * the same floor.
 */
vec3 glassFloorGrid(vec2 f, float dist, float hue) {
    vec2 g = 0.5 - abs(fract(f * 0.5) - 0.5);
    float w = 0.015 + 0.012 * dist;
    float line = 1.0 - smoothstep(0.0, w, min(g.x, g.y));
    return pal(hue + 0.32) * 0.05 + pal(hue + 0.1) * line * 0.22 * exp(-dist * 0.11);
}

/**
 * The world every 3D Glass style stands in, as a function of direction only.
 * A palette-tinted sky, darker at the zenith; a floor grid one unit below the
 * eye's horizon running into haze (straight lines are what make refraction
 * legible); a warm key panel overhead and a little behind the viewer, a cool
 * strip on the left behind the subject and a tinted strip on the right in front
 * (those three become every highlight and rim streak); and a faint glow behind
 * the subject. `hue` offsets the palette.
 */
vec3 glassBackdrop(vec3 d, float hue) {
    float y = d.y;
    vec3 horizon = pal(hue + 0.46) * 0.16 + 0.04;
    vec3 zenith = pal(hue + 0.60) * 0.10;
    vec3 col = mix(horizon, zenith, smoothstep(0.0, 0.9, y));
    if (y < 0.0) {
        vec2 f = d.xz / max(-y, 0.02);
        col = mix(col, glassFloorGrid(f, length(f), hue), (1.0 - smoothstep(-0.12, 0.0, y)));
    }
    col += pal(hue + 0.12) * 0.12 * exp(-abs(y) * 10.0);
    col += vec3(1.0, 0.95, 0.88) * 1.35 * glassPanel(d, normalize(vec3(-0.35, 1.0, -0.45)), vec2(0.55, 0.32), 0.12);
    col += mix(vec3(1.0), pal(hue + 0.62), 0.45) * 1.05 * glassPanel(d, normalize(vec3(-1.0, 0.3, 0.55)), vec2(0.09, 0.75), 0.06);
    col += (pal(hue + 0.08) * 0.85 + 0.2) * glassPanel(d, normalize(vec3(1.0, 0.18, -0.35)), vec2(0.07, 0.6), 0.05);
    col += pal(hue + 0.18) * 0.16 * pow(max(dot(d, normalize(vec3(0.15, 0.25, 1.0))), 0.0), 6.0);
    return col;
}

/** A pure spectral ramp, s = 0 red to s = 1 violet (violet carries a little red, as it reads to the eye). */
vec3 glassSpectrum(float s) {
    float r = clamp(1.0 - abs(s - 0.05) * 3.2, 0.0, 1.0) + 0.45 * clamp(1.0 - abs(s - 1.0) * 4.0, 0.0, 1.0);
    float g = clamp(1.0 - abs(s - 0.42) * 3.0, 0.0, 1.0);
    float b = clamp(1.0 - abs(s - 0.80) * 3.2, 0.0, 1.0);
    return vec3(r, g, b);
}

// ---- spheres, end to end ------------------------------------------------------

/**
 * The one-bounce path through a solid glass sphere. `p` is the entry point,
 * `n` its (possibly wobbled) outward normal, `rd` the incoming ray. Refracts
 * in, crosses the chord to the far wall, refracts out (or takes the mirror
 * direction on total internal reflection, and stops). Returns the exit
 * direction; `chord` is the distance travelled inside.
 */
vec3 glassThroughSphere(vec3 p, vec3 n, vec3 rd, vec3 c, float r, float ior, out float chord) {
    vec3 d1 = glassRefract(rd, n, 1.0 / ior);
    vec3 oc = p - c;
    float b = dot(oc, d1);
    float h = b * b - (dot(oc, oc) - r * r);
    chord = max(-b + sqrt(max(h, 0.0)), 0.0);
    vec3 n2 = normalize(oc + d1 * chord);
    return glassRefract(d1, -n2, ior);
}

/**
 * The same path traced once per channel with glassIors(ior, spread), each
 * looking into the backdrop and keeping its own channel: a dispersed view
 * through the sphere. Three backdrop samples.
 */
vec3 glassSphereDispersed(vec3 p, vec3 n, vec3 rd, vec3 c, float r, float ior, float spread, float hue, out float chord) {
    vec3 iors = glassIors(ior, spread);
    float cr;
    float cg;
    float cb;
    vec3 er = glassThroughSphere(p, n, rd, c, r, iors.x, cr);
    vec3 eg = glassThroughSphere(p, n, rd, c, r, iors.y, cg);
    vec3 eb = glassThroughSphere(p, n, rd, c, r, iors.z, cb);
    chord = cg;
    return vec3(glassBackdrop(er, hue).r, glassBackdrop(eg, hue).g, glassBackdrop(eb, hue).b);
}

/**
 * Leaving a body at one surface, dispersed: `d` is the ray inside, `n` the
 * OUTWARD normal at the exit point. Three refractions out, three backdrop
 * samples; a channel that hits total internal reflection takes the mirror
 * direction. The exit-only half of glassSphereDispersed, for marched bodies.
 */
vec3 glassExitDispersed(vec3 d, vec3 n, float ior, float spread, float hue) {
    vec3 iors = glassIors(ior, spread);
    vec3 er = glassRefract(d, -n, iors.x);
    vec3 eg = glassRefract(d, -n, iors.y);
    vec3 eb = glassRefract(d, -n, iors.z);
    return vec3(glassBackdrop(er, hue).r, glassBackdrop(eg, hue).g, glassBackdrop(eb, hue).b);
}

// ---- marching ------------------------------------------------------------------

/** Interior march budget: half the user's Detail budget, never past GLASS_MAX_INNER. */
float glassInnerSteps() {
    return min(uSteps * 0.5, float(GLASS_MAX_INNER));
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
 * Five travelling sine waves on the plane, none parallel to another (their
 * directions step by the golden angle) and each moving at the deep-water rate
 * for its wavenumber (speed grows as the square root), so the surface never
 * repeats visibly. Returns x = height, yz = slope (grad h), w = the determinant
 * of the light map x -> x + focus * grad h (see the header): feed w to
 * glassCaustic. `chop` scales the amplitude, `focus` the depth the light falls
 * through before it lands.
 */
vec4 glassWaves(vec2 p, float t, float chop, float focus) {
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
    float det = (1.0 + focus * hs.x) * (1.0 + focus * hs.y) - focus * focus * hs.z * hs.z;
    return vec4(h, g, det);
}

/**
 * Caustic brightness from the light map's determinant: the reciprocal of its
 * area compression, softened by `sharp` (smaller is thinner, brighter lines).
 * 0..1; the zero crossings are the lines of the net.
 */
float glassCaustic(float det, float sharp) {
    return clamp(sharp / (abs(det) + sharp), 0.0, 1.0);
}

// ---- thin film ---------------------------------------------------------------------

/**
 * Reflected colour of a thin film `nm` nanometres thick, IOR `n`, lit at
 * incidence cosine `cosI` in air. Two-beam interference at 650/532/450 nm, with
 * the half-wave flip at the front face, so zero thickness is black. Thicker
 * films cycle through more bands; tilting the film (smaller cosI) shifts every
 * band toward blue, which is why a bubble's rim is a different colour from its
 * face.
 */
vec3 glassFilm(float nm, float cosI, float n) {
    float sin2 = (1.0 - cosI * cosI) / (n * n);
    float cosT = sqrt(max(1.0 - sin2, 0.0));
    vec3 phase = GLASS_TAU * 2.0 * n * nm * cosT / vec3(650.0, 532.0, 450.0);
    return 0.5 - 0.5 * cos(phase);
}
