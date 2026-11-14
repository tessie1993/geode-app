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
//   THE POLE IS +Y. The WHOLE styles orbit about +y (lib_camera's camAround
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
// WHAT IS NOT HERE: the marcher loop. GLSL ES has no function pointers, so a
// shared march cannot call a style's own map (which folds, slices, inverts or
// squashes space before it calls bulbDE). Each style defines BulbHit
// bulbMap(vec3 p) and then includes lib_bulb_field, which routes it through
// the family's one field (bulbField) and differentiates it (bulbNormal); the
// style writes the eight-line loop against bulbField, with bulbBound /
// bulbEps / bulbStepAO below as the pieces it is built from.
//
// THE FIELD writes no global: a map hands its orbit trap (and any part of
// its own) back in the BulbHit, so a normal's taps leave the hit's trap
// alone. BULB_GROW in lib_bulb_field is the one line where Grow (lib_grow:
// Spawn, Fractal, Front and Back layer) and Formula will wrap the map once
// they land.
//
// THE FAMILY GRID. Twelve power-n bulbs framed the same way read as one lumpy
// sphere twelve times at phone size: the eye ranks framing, silhouette, light
// and dark mass, material and motion before it ever notices the formula. So
// the family is a 3 x 4 grid over two structural axes, and no two members
// share both:
//
//               LIT              GLOW            LINE            FLAT TRAP
//   WHOLE    classic          glow            ink             prism
//   MACRO    chrome           lotus           contour         julia
//   INSIDE   cavern           shell           hybrid          slice
//
//   WHOLE   the whole body centred in the frame on camAround (a silhouette).
//   MACRO   the eye hovers a fraction of the radius off the skin and looks
//           across it (bulbAim), so the body's horizon cuts the frame and the
//           surface is a landscape, never a silhouette.
//   INSIDE  the eye is in the set's room: an inverted bulb, the hollow of a
//           shell, or a cut face that fills the frame.
//
//   LIT     normals, a key light and a material.
//   GLOW    emission accumulated along the ray or from a light inside; little
//           or no use of the normal.
//   LINE    ink: outlines, creases or iso-lines carry the drawing.
//   FLAT    the orbit trap is the colour, with no lighting at all (iq's trap
//           renders), only the free step-count occlusion.
//
// Each member also has its own palette (bulbRamp below, a cosine ramp the
// Director's pal() tints but never replaces), its own value key (light on
// dark or dark on light) and its own motion rule, so neighbours in a row or
// a column still differ in a third way.
//
// TRAPS. The DE functions hand back a vec4 orbit trap alongside the distance:
//   x  min |z|^2 over the orbit, starting point included (how close it came
//      to the centre: low in the creases, so it doubles as cheap occlusion)
//   y  min |z.y|  (distance to the equatorial plane: thin bands)
//   z  min length(z.xz)  (distance to the pole axis: filaments)
//   w  escape iteration / iterations, 0..1 (1 = never escaped), smoothed: the
//      iteration it escaped at, plus how far short of the bailout's next
//      power it was, so it changes continuously across the iterations
//
// THE SHADING PASS (lib_shade, included below). Escape colour: trap.w is the
// SMOOTHED escape count (bulbDE: the iteration the orbit left at, less how far
// past the bailout it had already run, so neighbouring escapes meet without a
// step), and a style moves its main colour along its own ramp by
// shadeEscape(trap.w), Escape colour (uEscapeColor) setting how far. Soft
// shadows: a LIT member with a key (Classic, Ink) or a lamp (Shell's sun)
// shadows it softly through lib_bulb_shade's bulbShadow, which reads the field
// and so comes after lib_bulb_field. Step glow: a surface member on a dark
// ground (Classic, Julia, Prism, Chrome, Contour, Shell, Slice) adds
// shadeStepGlow(used, uSteps) as light in its ramp, by Bloom; the pale-ground
// members (Ink, Hybrid, Lotus) already draw their step count as ink or folds,
// Glow is a volume with no surface to skim, and Cavern's rays run out of steps
// at its walls by design, so those add none.
//
// INCLUDE ORDER: after lib_scene_uniforms (uSteps, the macros, the knobs and
// lib_sound), lib_camera (bulbAim) and lib_palette (bulbRamp). What moves with
// the music is each style's business, but for one shared hand below: the
// six style knobs.

// The shading pass's rules (soft shadows, step glow, escape colour).
//#include lib_shade

/** Compile-time ceiling on fractal iterations. A style's own count is a runtime break below this. */
#define BULB_MAX_ITERS 8

#define BULB_TAU 6.2831853

/** What the field found at a point: the distance, the orbit trap (see TRAPS) and the style's own part (each style says what). */
struct BulbHit {
    float d;
    vec4 trap;
    float mat;
};

// ---- the normalization shell ----------------------------------------------------
//
// Every shape variable of the family is read once, normalized, in bulbIn():
// one field of BulbIn each, 0 at rest, -1..1 from a knob (its offset from 0.5,
// doubled) or the Twist slider, 0..1 from the other params (0 their default).
// Each is mapped to its own range in one place: the shared ones in the helper
// below that spends them, a style's own in its setup block through bulbSpan,
// once. The drivers are scene params and style knobs only, every one a target
// of the reaction matrix, so the music moves any variable through the matrix;
// nothing in the shell reads the sound frame. At the defaults every variable
// is exactly the constant it was.
//
//   variable  driver               mapped to                         where
//   iters     Morph                + 0..2 fractal iterations          bulbIters
//   width     k1 TEXTURE           domain scale x1.03..x0.97          bulbWidthScale
//   power     k1 TEXTURE           +-0.5 on the radial power n        bulbPower
//   bend      Warp + k2 FLOW       0..0.05 of sine warp               bulbChaosAmp
//   stir      Turbulence           0..0.0525 more of it               bulbChaosAmp
//   twist     Twist                +-1.2 rad per unit up the pole     bulbTwist
//   reach     k5 SPACE             orbit x0.8..x1.2, eye +-0.06       bulbOrbit, bulbAim
//   emit      k0 PULSE             ramp gain x0.75..x1.25             bulbRamp
//   tone      k3 TONE              ramp position +-0.3                bulbRamp
//
// k4 BUILD has no shared hand (a style may spend it). The azimuth power m is a
// count (its seam needs a whole number) and stays the style's.

/** Every shape variable, normalized: 0 at rest. */
struct BulbIn {
    float iters;
    float width;
    float power;
    float bend;
    float stir;
    float twist;
    float reach;
    float emit;
    float tone;
};

/** Each variable from its driver (the table above): params and knobs only, no sound. A pure read of uniforms. */
BulbIn bulbIn() {
    float kTex = 2.0 * (clamp(uKnob[1], 0.0, 1.0) - 0.5);
    BulbIn n;
    n.iters = clamp(uMorph, 0.0, 1.0);
    n.width = kTex;
    n.power = kTex;
    n.bend = clamp(uWarp + 0.5 * (clamp(uKnob[2], 0.0, 1.0) - 0.5), -1.0, 1.0);
    n.stir = clamp(uTurbulence / 1.5, 0.0, 1.0);
    n.twist = clamp(uTwist, -1.0, 1.0);
    n.reach = 2.0 * (clamp(uKnob[5], 0.0, 1.0) - 0.5);
    n.emit = 2.0 * (clamp(uKnob[0], 0.0, 1.0) - 0.5);
    n.tone = 2.0 * (clamp(uKnob[3], 0.0, 1.0) - 0.5);
    return n;
}

/**
 * A normalized n to its variable's range: `rest` at 0, `lo` at -1, `hi` at 1, straight between.
 * Exactly `rest` at 0, so the rest look is the old one bit for bit. A 0..1 variable uses rest..hi.
 */
float bulbSpan(float n, float rest, float lo, float hi) {
    return rest + n * (n < 0.0 ? rest - lo : hi - rest);
}

/**
 * The fractal iteration count for this frame: `hi` at normal Detail, plus up
 * to two more as the Complexity macro (uMorph, 0 by default) rises, never
 * past BULB_MAX_ITERS; two fewer (never under 3) when the march budget is low.
 * Call once in main(), not per bulbMap: it is a uniform-only decision and the
 * driver hoists nothing.
 */
int bulbIters(int hi) {
    int extra = int(floor(bulbSpan(bulbIn().iters, 0.0, 0.0, 2.0) + 0.5));
    int n = min(hi + extra, BULB_MAX_ITERS);
    return uSteps < 80.0 ? max(n - 2, 3) : n;
}

// ---- the Customize macros ---------------------------------------------------
//
// The app's macros move standard scene params, not per-style uniforms, so
// every bulb answers them through these, the same way, and at their defaults
// each is the identity: Speed (1) scales animation time, Complexity
// (uMorph 0) adds iterations in bulbIters(), Chaos (uWarp, uTurbulence,
// uTwist 0) bends space before the fractal sees it, Energy (uIntensity 1)
// scales how far the music moves a style. Colour is pal(); Space is the rig.
//
// The six style knobs (uKnob), with the roles lib_flame, lib_opal and
// lib_hollow give them, each read as its offset from 0.5 (bulbIn), so with
// Note & key reactions off every term is exactly zero and every member draws
// its tuned picture: k0 PULSE the emission (bulbRamp's gain), k1 TEXTURE the
// body's thickness, a few percent (bulbChaos, its stretch in bulbChaosLip),
// and half a power on the radial power (bulbPower), k2
// FLOW added to Warp's bend (bulbChaosAmp), k3 TONE the palette position
// (bulbRamp), k5 SPACE the reach: the eye backs off the body (bulbAim, and the
// WHOLE members' orbit radius). k4 BUILD has no shared hand here yet.
//
// No sound band turns the body about its pole (owner 2026-11-12, no style
// spins to the music).

/** k1 TEXTURE as a scale on the fractal's domain: under 1 thickens the body (by about 3% at most). */
float bulbWidthScale() {
    return bulbSpan(bulbIn().width, 1.0, 1.03, 0.97);
}

/** The radial power n of a body whose rest power is n, k1 TEXTURE's swell on top (the azimuth power stays the style's). */
float bulbPower(float n) {
    return n + bulbSpan(bulbIn().power, 0.0, -0.5, 0.5);
}

/** Twist's turn about the pole, radians per unit up it. */
float bulbTwist() {
    return bulbSpan(bulbIn().twist, 0.0, -1.2, 1.2);
}

/** A WHOLE member's orbit radius r, k5 SPACE's reach on it. */
float bulbOrbit(float r) {
    return r * bulbSpan(bulbIn().reach, 1.0, 0.8, 1.2);
}

/** Animation time under the Speed macro. */
float bulbTime() {
    return uTime;  // already integrates Speed (ShaderScene::update)
}

/** The Energy macro as a gain on in-shader audio reaction, clamped so a high setting cannot fling geometry out of its bound. */
float bulbEnergy() {
    return clamp(uIntensity, 0.0, 2.0);
}

/** Domain-warp amplitude and frequency for the Chaos macro, k2 FLOW added to Warp's share, floored at none. */
float bulbChaosAmp() {
    BulbIn n = bulbIn();
    return bulbSpan(n.bend, 0.0, 0.0, 0.05) + bulbSpan(n.stir, 0.0, 0.0, 0.0525);
}
#define BULB_CHAOS_FREQ 3.5

/**
 * Chaos: a slow sine warp (uWarp, uTurbulence) and a twist about the pole
 * (uTwist) applied to p before the fractal. Neither is rigid, so the caller
 * divides its estimate by bulbChaosLip(). At default the branch is skipped
 * and p comes back untouched. Before them, k1 TEXTURE's scale (see the
 * macros above), identity at rest.
 */
vec3 bulbChaos(vec3 p) {
    p *= bulbWidthScale();
    float amp = bulbChaosAmp();
    if (amp > 1e-4) {
        float t = bulbTime() * 0.4;
        p += amp * sin(p.yzx * BULB_CHAOS_FREQ + vec3(t, 1.7 + 0.8 * t, 3.1 - 0.6 * t));
    }
    float tw = bulbTwist();
    if (abs(tw) > 1e-4) {
        float a = tw * p.y;
        float c = cos(a);
        float s = sin(a);
        p.xz = mat2(c, -s, s, c) * p.xz;
    }
    return p;
}

/** Bound on bulbChaos's stretch, for a body within about 1.5 of the pole: divide the estimate by it. The width's scale over 1 (a thinning) stretches too. */
float bulbChaosLip() {
    return (1.0 + bulbChaosAmp() * BULB_CHAOS_FREQ * 1.8) * (1.0 + abs(bulbTwist()) * 1.5) * max(bulbWidthScale(), 1.0);
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
 * The escape count of an orbit first found past the bailout at iteration i with radius r, power n, smoothed:
 * from i (it had just run past bail^n, so it all but left a step sooner) to i + 1 (it had only just passed
 * bail), so trap.w changes continuously from one escape iteration to the next and an escape colour never bands.
 */
float bulbSmoothEscape(int i, float r, float bail, float n) {
    float over = log2(max(log(r) / log(bail), 1.0)) / log2(max(n, 1.5));
    return float(i) + 1.0 - clamp(over, 0.0, 1.0);
}

/**
 * The bulb: distance from p to the set of c whose orbit under z^(n,m) + c stays
 * bounded. Negative inside. `iters` from bulbIters().
 */
float bulbDE(vec3 p, float n, float m, vec2 phase, int iters, out vec4 trap) {
    n = bulbPower(n);
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
            esc = bulbSmoothEscape(i, r, bail, n);
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
    // The azimuth keeps the rest power (a whole number, no seam); the radial power swells (bulbPower).
    float m = n;
    n = bulbPower(n);
    float grow = max(n, m);
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
            esc = bulbSmoothEscape(i, r, bail, n);
            break;
        }
        dr = grow * pow(r, n - 1.0) * dr;
        z = bulbPow(z, r, n, m, vec2(0.0)) + c;
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

/**
 * The MACRO framing: the eye at `eye`, looking at `target`, with the rig's
 * basis riding on top so its look, banking and shake still apply (the same
 * composition Cavern uses for its loop). Put the eye a fraction of the radius
 * off the skin and the target on the skin a little to one side, and the body's
 * horizon crosses the frame instead of the whole silhouette sitting in it.
 * The up vector swaps to +x near the pole so the frame never degenerates.
 */
void bulbAim(vec2 uv, vec3 eye, vec3 target, out vec3 ro, out vec3 rd) {
    vec3 fwd = normalize(target - eye);
    vec3 up = abs(fwd.y) > 0.96 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
    // k5 SPACE backs the eye off along the look (or eases it in), 0.06 at most.
    ro = eye - fwd * bulbSpan(bulbIn().reach, 0.0, -0.06, 0.06);
    rd = normalize(bulbFrame(fwd, up) * (uCamBasis * vec3(uv, uCamFocal)));
}

// ---- the member's own palette --------------------------------------------------
//
// Every bulb used to colour through the one global pal(), which the Director
// and the key hue drive, so at any moment the whole family wore one palette.
// Now each member owns a cosine ramp (a + b cos(2 pi (c t + d)), iq's
// palette form) and the Director's palette TINTS it by BULB_TINT rather than
// replacing it: the live look still answers the auto-director and the user's
// palette chips, but twelve bulbs never share one palette at once. The
// user's Hue shift (uColorShift) turns the member's own ramp too.

/** How much of the Director's pal() is mixed into a member's own ramp (the recipe's 0.3 - 0.5). */
#define BULB_TINT 0.4

/** A member's own cosine ramp at t, tinted toward pal(t); k3 TONE moves t, k0 PULSE is its gain. */
vec3 bulbRamp(float t, vec3 a, vec3 b, vec3 c, vec3 d) {
    BulbIn n = bulbIn();
    t += bulbSpan(n.tone, 0.0, -0.3, 0.3);
    vec3 own = clamp(a + b * cos(BULB_TAU * (c * (t + uColorShift) + d)), 0.0, 1.0);
    return mix(own, pal(t), BULB_TINT) * bulbSpan(n.emit, 1.0, 0.75, 1.25);
}
