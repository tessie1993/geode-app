// The Bulb family's shared core: the power-N spherical fractal, its Julia
// twin, and the handful of marcher pieces every bulb style needs.
//
// WHAT THE FRACTAL IS
//
// White and Nylander (2009) asked what "squaring" a 3D point could mean and
// answered in spherical coordinates: raise the radius to the power n and
// multiply both angles by n. Iterate z -> z^n + c from z = c, and the points c
// whose orbit stays bounded form the bulb. The distance estimate is the usual
// escape-time one, 0.5 * log(r) * r / dr, where dr is the running length of
// the orbit's derivative (Hart's argument, carried to 3D). That is public
// mathematics; everything below is written from it, not lifted from any
// particular shader.
//
// WHY ONE LIBRARY
//
// Twelve styles iterate the same map. Every one of them has to get the same
// four details right, and each of the four is a bug that does not look like
// one until the style is on a phone:
//
//   THE POLE IS +Y. Every style here orbits about +y (lib_camera's camAround
//   turns about +y), so the fractal's axis of symmetry is the axis the
//   camera turns around and a bulb never rolls through the frame.
//
//   THE AZIMUTH POWER IS ITS OWN NUMBER. atan(z, x) jumps by 2*pi on the
//   negative-x half plane. Multiplied by an integer the jump is a whole number
//   of turns and vanishes; multiplied by 8.4 it is a visible seam through the
//   body. So bulbPow() takes two powers: n for the radius and the polar angle
//   (any real, the polar angle has no wrap), m for the azimuth (keep it an
//   integer). A style that wants the power to "swell" moves n and leaves m.
//
//   THE DERIVATIVE BOUNDS BOTH. With two powers the Jacobian stretches radially
//   by n * r^(n-1) and tangentially by up to max(n, m) * r^(n-1), so dr grows
//   by max(n, m): taking only n would let the estimate OVERESTIMATE, and the
//   ray would step through the lobes whenever m > n.
//
//   THE BAILOUT DEPENDS ON THE POWER. r > 2 is plenty for power 8 (r^8 runs
//   away in one step), but power 2 escapes slowly and its estimate needs a
//   wider bail before log(r) means anything. bulbBail() widens it for low n.
//
// THE ITERATION BUDGET. Every bulb style spends uSteps march steps and up to
// BULB_MAX_ITERS fractal iterations per step: that product is the cost of the
// frame. bulbIters() drops two iterations when the user's Detail control is
// low (uSteps < 80), which on a power-8 bulb costs only the finest crust - and
// it is a runtime break against a constant bound, like the march itself.
//
// THE BOUND. Everything that ever belongs to a power-n bulb with n >= 3 lies
// within about 1.2 of the origin. bulbBound() intersects a sphere, so a ray
// that misses it costs one quadratic and no march at all, and a ray that
// hits starts marching at the sphere instead of at the eye. Most of a framed
// bulb's pixels are sky, so this is most of the frame's saving.
//
// WHAT IS NOT HERE: the marcher loop and the normal. GLSL ES has no function
// pointers, so a shared march cannot call a style's own map() (which folds,
// slices, inverts or squashes space before it calls bulbDE). Each style
// writes the eight-line loop and the four-tap tetrahedral normal itself,
// against its own map(), with bulbBound / bulbEps / bulbStepAO below as the
// pieces it is built from.
//
// TRAPS. The DE functions hand back a vec4 orbit trap alongside the distance:
//   x  min |z|^2 over the orbit, starting point included (how close it came
//      to the centre: low in the creases, so it doubles as cheap occlusion)
//   y  min |z.y|  (distance to the equatorial plane: thin bands)
//   z  min length(z.xz)  (distance to the pole axis: filaments)
//   w  escape iteration / iterations, 0..1 (1 = never escaped)
//
// INCLUDE ORDER: after lib_scene_uniforms (reads uSteps only). It reads no
// audio uniform: what moves with the music is each style's business.

/** Compile-time ceiling on fractal iterations. A style's own count is a runtime break below this. */
#define BULB_MAX_ITERS 8

#define BULB_TAU 6.2831853

/**
 * The fractal iteration count for this frame: `hi` at normal Detail, plus up
 * to two more as the Complexity macro (uMorph, 0 by default) rises, never
 * past BULB_MAX_ITERS; two fewer (never under 3) when the march budget is low.
 * Call once in main(), not per map(): it is a uniform-only decision and the
 * driver hoists nothing.
 */
int bulbIters(int hi) {
    int extra = int(floor(clamp(uMorph, 0.0, 1.0) * 2.0 + 0.5));
    int n = min(hi + extra, BULB_MAX_ITERS);
    return uSteps < 80.0 ? max(n - 2, 3) : n;
}

// ---- the Customize macros ---------------------------------------------------
//
// The app's macros move standard scene params, not per-style uniforms, so
// every bulb answers them through these, the same way, and at their defaults
// each is the identity: Speed (uSpeed 1) scales animation time, Complexity
// (uMorph 0) adds iterations in bulbIters(), Chaos (uWarp, uTurbulence,
// uTwist 0) bends space before the fractal sees it, Energy (uIntensity 1)
// scales how far the music moves a style. Colour is pal(); Space is the rig.

/** Animation time under the Speed macro. */
float bulbTime() {
    return uTime * uSpeed;
}

/** The Energy macro as a gain on in-shader audio reaction, clamped so a high setting cannot fling geometry out of its bound. */
float bulbEnergy() {
    return clamp(uIntensity, 0.0, 2.0);
}

/** Domain-warp amplitude and frequency for the Chaos macro. */
float bulbChaosAmp() {
    return 0.05 * uWarp + 0.035 * uTurbulence;
}
#define BULB_CHAOS_FREQ 3.5

/**
 * Chaos: a slow sine warp (uWarp, uTurbulence) and a twist about the pole
 * (uTwist) applied to p before the fractal. Neither is rigid, so the caller
 * divides its estimate by bulbChaosLip(). At default the branch is skipped
 * and p comes back untouched.
 */
vec3 bulbChaos(vec3 p) {
    float amp = bulbChaosAmp();
    if (amp > 1e-4) {
        float t = bulbTime() * 0.4;
        p += amp * sin(p.yzx * BULB_CHAOS_FREQ + vec3(t, 1.7 + 0.8 * t, 3.1 - 0.6 * t));
    }
    if (abs(uTwist) > 1e-4) {
        float a = uTwist * 1.2 * p.y;
        float c = cos(a);
        float s = sin(a);
        p.xz = mat2(c, -s, s, c) * p.xz;
    }
    return p;
}

/** Bound on bulbChaos's stretch, for a body within about 1.5 of the pole: divide the estimate by it. */
float bulbChaosLip() {
    return (1.0 + bulbChaosAmp() * BULB_CHAOS_FREQ * 1.8) * (1.0 + abs(uTwist) * 1.2 * 1.5);
}

/** Escape radius for power n: 2 from power 4 up, widening to 4 at power 2. */
float bulbBail(float n) {
    return clamp(6.0 - n, 2.0, 4.0);
}

/**
 * z raised to the power (n, m): radius and polar angle by n, azimuth by m,
 * pole on +y. `phase` is added to (polar, azimuth) after the multiply - a
 * per-iteration offset that morphs the body rather than merely turning it.
 * `r` is length(z), already guarded away from zero by the caller.
 */
vec3 bulbPow(vec3 z, float r, float n, float m, vec2 phase) {
    float polar = acos(clamp(z.y / r, -1.0, 1.0)) * n + phase.x;
    float azim = atan(z.z, z.x + 1e-12) * m + phase.y;
    float s = sin(polar);
    return pow(r, n) * vec3(s * cos(azim), cos(polar), s * sin(azim));
}

/**
 * The bulb: distance from p to the set of c whose orbit under z^(n,m) + c stays
 * bounded. Negative inside. `iters` from bulbIters().
 */
float bulbDE(vec3 p, float n, float m, vec2 phase, int iters, out vec4 trap) {
    vec3 z = p;
    float dr = 1.0;
    float r = max(length(z), 1e-6);
    float grow = max(n, m);
    float bail = bulbBail(n);
    float esc = float(iters);
    trap = vec4(dot(p, p), abs(p.y), length(p.xz), 1.0);
    for (int i = 0; i < BULB_MAX_ITERS; i++) {
        if (i >= iters) break;
        r = max(length(z), 1e-6);
        if (r > bail) {
            esc = float(i);
            break;
        }
        dr = grow * pow(r, n - 1.0) * dr + 1.0;
        z = bulbPow(z, r, n, m, phase) + p;
        trap.x = min(trap.x, dot(z, z));
        trap.y = min(trap.y, abs(z.y));
        trap.z = min(trap.z, length(z.xz));
    }
    r = max(length(z), 1e-6);
    trap.w = esc / float(max(iters, 1));
    return 0.5 * log(r) * r / dr;
}

/**
 * The Julia twin: c is fixed and the orbit starts at p, so the derivative has
 * no +1 (d/dp of a constant c is zero). A Julia bulb is as large as c lets
 * it be; keep |c| under about 0.8 and it stays inside a 1.4 bound.
 */
float bulbJuliaDE(vec3 p, vec3 c, float n, int iters, out vec4 trap) {
    vec3 z = p;
    float dr = 1.0;
    float r = max(length(z), 1e-6);
    float bail = bulbBail(n);
    float esc = float(iters);
    trap = vec4(dot(p, p), abs(p.y), length(p.xz), 1.0);
    for (int i = 0; i < BULB_MAX_ITERS; i++) {
        if (i >= iters) break;
        r = max(length(z), 1e-6);
        if (r > bail) {
            esc = float(i);
            break;
        }
        dr = n * pow(r, n - 1.0) * dr;
        z = bulbPow(z, r, n, n, vec2(0.0)) + c;
        trap.x = min(trap.x, dot(z, z));
        trap.y = min(trap.y, abs(z.y));
        trap.z = min(trap.z, length(z.xz));
    }
    r = max(length(z), 1e-6);
    trap.w = esc / float(max(iters, 1));
    return 0.5 * log(r) * r / max(dr, 1e-6);
}

/**
 * Ray against the bounding sphere: (enter, exit) along rd, enter clamped to 0
 * for an eye inside. exit < 0 means the ray misses and needs no march.
 */
vec2 bulbBound(vec3 ro, vec3 rd, vec3 centre, float radius) {
    vec3 oc = ro - centre;
    float b = dot(oc, rd);
    float h = b * b - (dot(oc, oc) - radius * radius);
    if (h < 0.0) return vec2(-1.0);
    h = sqrt(h);
    return vec2(max(-b - h, 0.0), -b + h);
}

/**
 * Hit epsilon at distance t: roughly a pixel's footprint, doubled at low
 * Detail so a short budget stops on the crust instead of running out inside a
 * crease. Also the right normal epsilon.
 */
float bulbEps(float t) {
    return (uSteps < 80.0 ? 0.0016 : 0.0008) * t + 0.0003;
}

/**
 * Occlusion from the step count: a ray that spent most of its budget was
 * crawling along a fold, which is where the creases are. Free, and on a bulb
 * it reads better than a two-tap AO that costs two more fractal evaluations.
 */
float bulbStepAO(float used) {
    return clamp(1.0 - used / max(uSteps, 1.0) * 1.3, 0.18, 1.0);
}

/** A camera basis (right, up, forward) looking along `fwd`; compose with uCamBasis to keep the rig's look. */
mat3 bulbFrame(vec3 fwd, vec3 up) {
    vec3 right = normalize(cross(up, fwd));
    return mat3(right, cross(fwd, right), fwd);
}
