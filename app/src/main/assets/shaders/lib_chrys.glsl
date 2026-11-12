// The Chrysanthemum family's shared flower geometry.
//
// Twelve styles draw flowers twelve different ways - flat mandalas, a
// sunflower disk, a rose window, a tunnel lined with petal scales, a marched
// flower head - and under every one of them sits the same small question:
// "how far is this point from a petal?". Answering it twelve times was
// twelve chances to answer it slightly differently, and a petal whose taper
// or cup meant one thing in one style and another thing next door would make
// the family read as a pile of unrelated shaders rather than as one garden.
// So the petal profile, the ring repetition, the golden-angle florets and the
// envelope shaping live here, once.
//
// Like lib_sdf3, no geometry helper here reads a uniform. Every one is pure
// geometry or pure shaping, so a style decides for itself what the music does
// to a petal, and this file never inherits an opinion about it. The one
// exception is the normalization shell at the bottom (chrysIn), which reads
// the scene params and the style knobs and nothing of the sound. Include it
// after lib_scene_uniforms. Every name carries a `chrys` prefix because a
// style is free to include lib_sdf3 or lib_scene_motion beside it, and GLSL
// has no namespaces.
//
// ---- the field ------------------------------------------------------------
//
// Every Chrysanthemum style defines ChrysHit chrysMap(vec3 p), its flowers at
// p, and then includes lib_chrys_field, which routes it through the family's
// one field (chrysField) and differentiates it (chrysNormal); the style
// marches, shades and takes normals through these alone. The field writes no
// global: a map hands back what it found (which part, the petal's u and v,
// its id...) in the hit, and each style says what its slots hold, so a
// normal's taps never clobber the hit's material. CHRYS_GROW in
// lib_chrys_field is the one line where Grow (lib_grow: Spawn, Fractal, Front
// and Back layer) and Formula will wrap the map once they land.
//
// ---- the normalization shell ----------------------------------------------
//
// Every shape variable of the family is read once, normalized, in chrysIn():
// one field of ChrysIn each, 0 at rest, -1..1 from a knob (its offset from
// 0.5, doubled) or the Twist slider, 0..1 from the other params (0 their
// default). Each style maps the ones it has to its own range in its setup
// block, through chrysSpan, once, into a named value the map and the shading
// read. The drivers are params and knobs only, every one a target of the
// reaction matrix, so the music moves any variable through the matrix; at the
// defaults every variable is exactly the constant it was.
//
//   variable  driver               what a style spends it on
//   rings     Morph                rings, layers, extra florets
//   chaos     Turbulence + Warp    each petal's own length, a frayed edge
//   bend      Warp                 a style's own bend of space (Tunnel's bore)
//   curl      Twist                the rings' spiral sweep
//   fray      k1 TEXTURE           how far the chaos tears the edges
//   width     k1 TEXTURE           a petal's width, a tube's or a line's
//   spin      k2 FLOW              how fast the rings turn with the drift
//   sway      k2 FLOW              a style's own wind or fall
//   reach     k5 SPACE             the camera's distance, the haze
//   glow      k4 BUILD             the glow, the light behind
//   emit      k0 PULSE             the emission, the kick's share
//   tone      k3 TONE              the palette position
//
// A petal or ring COUNT keeps the style's own number unless the style eases
// it (a count pops on every beat: lib_neon's rule).
//
// ---- the petal ------------------------------------------------------------
//
// A petal is described in its own frame: u runs 0 at the base to 1 at the
// tip, v runs across it, -1 at one edge to +1 at the other. Its half-width
// along u is one profile, chrysProfile(), with two knobs a style can drive:
//
//   cup  0..1  how pinched the base is. A cupped petal (cup near 1) stays
//              narrow where it tucks into the ring below and swells late, the
//              way an incurved chrysanthemum's petals do; cup near 0 is a
//              spoon that is wide almost from the root.
//   tip  0..1  how pointed the end is. 0 closes the tip round, 1 draws it
//              out into a quill.
//
// The distance it returns is a bound, not exact: arc length across the petal
// is measured as angle times radius, which is right at the centreline and
// drifts toward the edges. That is fine for a 2D mask, where only the sign
// and the first pixel or two of the gradient matter, and it is why the one
// marched style scales its steps down.
//
// ---- rings ----------------------------------------------------------------
//
// chrysPetal() draws a whole ring of `count` identical petals by folding the
// angle into one sector, so a ring costs the same as one petal however many
// petals it has. The price is that a petal cannot be wider than its sector
// (it would be clipped by the fold), so the half-width is clamped to half a
// sector; a style that wants overlap draws a second ring offset by half a
// sector (chrysRingOffset) rather than widening the first.
//
// ---- envelopes ------------------------------------------------------------
//
// lib_sound's events rise INSTANTLY and fall exponentially. Any monotonic
// function of one therefore snaps on the frame the event fires, which on a
// petal reads as a glitch, not as a bloom. chrysBump() is zero at both ends
// of the envelope, so a petal driven by it starts moving from rest, peaks
// while the envelope is half spent and settles back without a jump.

/**
 * What the field found at a point: the distance, the part (`mat`) and up to
 * eight numbers of the style's own in `a` and `b` (the petal's u, v, id...;
 * each style says which slot holds what). 0 where the style set nothing.
 */
struct ChrysHit {
    float d;
    float mat;
    vec4 a;
    vec4 b;
};

/** A hit at distance d with nothing found yet. */
ChrysHit chrysHit(float d) {
    return ChrysHit(d, 0.0, vec4(0.0), vec4(0.0));
}

#define CHRYS_TAU 6.2831853
#define CHRYS_PI 3.14159265
/** The golden angle, 2*pi*(1 - 1/phi): consecutive florets of a sunflower sit this far apart. */
#define CHRYS_GOLDEN 2.39996323
#define CHRYS_PHI 1.61803399

/** Counter-clockwise rotation by `a` radians. Named apart from lib_sdf3's rot2 so both can be included. */
mat2 chrysTurn(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, s, -s, c);
}

/** A cheap 2D -> 0..1 hash, arithmetic only (no sin: its precision varies too much across mobile GPUs). */
float chrysHash(vec2 p) {
    vec2 q = fract(p * vec2(0.1371, 0.1093) + vec2(0.311, 0.737));
    q += dot(q, q.yx + 27.71);
    return fract((q.x + q.y) * q.y * 1.37);
}

float chrysHash1(float x) {
    return chrysHash(vec2(x, x * 0.57 + 3.1));
}

/** Smooth value noise, 0..1. Lib-local so a style need not pull in lib_scene_motion for it. */
float chrysNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(chrysHash(i), chrysHash(i + vec2(1.0, 0.0)), f.x),
               mix(chrysHash(i + vec2(0.0, 1.0)), chrysHash(i + vec2(1.0, 1.0)), f.x), f.y);
}

// ---- chaos ----------------------------------------------------------------
//
// The Customize "Chaos" macro moves uTurbulence and uWarp. view() already
// turns both into a domain warp of the whole frame; these two give a flower
// its OWN irregularity on top, the kind a real bloom has: petals that are not
// all the same length and edges that are not clean. Both return exactly 0 at
// amount 0, so a style at its defaults draws the perfect flower.

/** A signed offset to add to a petal distance: a ragged, torn edge. `p` in petal space. */
float chrysFray(vec2 p, float amount) {
    if (amount <= 0.0) return 0.0;
    return amount * 0.02 * (chrysNoise(p * 38.0) + 0.5 * chrysNoise(p * 91.0) - 0.75);
}

/** -1..1 per petal, drifting slowly with `t`: scale a petal's length or angle by it. */
float chrysWobble(float id, float t) {
    float h = chrysHash1(id * 1.31 + 0.5);
    return sin(t * (0.4 + 0.5 * h) + h * CHRYS_TAU);
}

/**
 * Half-width of a petal at `u` (0 base, 1 tip), as a fraction of its length.
 * About 0.7 at its widest for cup = tip = 0.5; see the header for the knobs.
 */
float chrysProfile(float u, float cup, float tip) {
    float uc = clamp(u, 0.0, 1.0);
    float rise = pow(uc, 0.3 + 0.9 * cup);
    float fall = pow(1.0 - uc, 0.25 + 1.1 * tip);
    return 2.2 * rise * fall;
}

/**
 * One petal pointing along +y from the origin, `len` long, `width` its
 * relative half-width (0.3 is slender, 0.8 is a spoon).
 *
 * Returns (signed distance, u, v, 0): negative inside; u 0 at the base and 1
 * at the tip; v -1..1 across. For petals placed one by one (a lotus fan, a
 * falling petal, a scale on a wall) rather than in a ring.
 */
vec4 chrysLeaf(vec2 q, float len, float width, float cup, float tip) {
    float u = q.y / max(len, 1e-4);
    float h = max(width * len * chrysProfile(u, cup, tip), 1e-4);
    float d = max(abs(q.x) - h, (abs(u - 0.5) - 0.5) * len);
    return vec4(d, u, q.x / h, 0.0);
}

/**
 * A ring of `count` petals around the origin, bases on the circle `r0`,
 * reaching out `len`. `curl` sweeps each petal along an Archimedean spiral
 * (radians per unit of radius), which is what turns a daisy into a dahlia.
 *
 * Returns (signed distance, u, v, petal id). The id is the sector index, not
 * yet wrapped: mod(id, count) before hashing it, since a curled ring keeps
 * counting past one turn.
 */
vec4 chrysPetal(vec2 p, float count, float r0, float len, float width, float curl, float cup, float tip) {
    float r = length(p);
    float a = atan(p.y, p.x) - curl * (r - r0);
    float sector = CHRYS_TAU / count;
    float id = floor(a / sector + 0.5);
    float across = (a - id * sector) * r;
    float u = (r - r0) / max(len, 1e-4);
    float h = width * len * chrysProfile(u, cup, tip);
    h = max(min(h, 0.49 * sector * r), 1e-4);
    float d = max(abs(across) - h, (abs(u - 0.5) - 0.5) * len);
    return vec4(d, u, across / h, id);
}

/** The angle to turn ring `ring` by so its petals sit in the gaps of the ring before it. */
float chrysRingOffset(float ring, float count) {
    return mod(ring, 2.0) * CHRYS_PI / count + ring * 0.11;
}

/**
 * Rotates p by an angle growing with the square of the distance past r0.
 * Applied before chrysPetal it hooks the tips without bending the bases: the
 * spider-mum curl, which chrysPetal's constant curl cannot draw.
 */
vec2 chrysHook(vec2 p, float k, float r0) {
    float e = max(length(p) - r0, 0.0);
    return chrysTurn(k * e * e) * p;
}

/**
 * Light falling on a petal at (u, v): a cupped petal turns its edges away,
 * the base sits in the shadow of the ring above it, and a faint vein runs
 * down the middle. 0..1, never 0, so a petal is always readable.
 */
float chrysPetalShade(vec4 pet, float cup) {
    float u = clamp(pet.y, 0.0, 1.0);
    float v = clamp(pet.z, -1.0, 1.0);
    float body = 1.0 - 0.55 * cup * v * v;
    float tuck = mix(0.45, 1.0, smoothstep(0.0, 0.4, u));
    float vein = 1.0 - 0.18 * exp(-abs(v) * 16.0) * (1.0 - u);
    return body * tuck * vein;
}

/** 0 at both ends of a lib_sound envelope, 1 halfway: a snap-free bloom. */
float chrysBump(float env) {
    return sin(CHRYS_PI * clamp(env, 0.0, 1.0));
}

/**
 * chrysBump with its peak moved: `lean` > 1 peaks late (early in the
 * envelope's life, since it decays), < 1 peaks early. Still 0 at both ends.
 */
float chrysBumpLean(float env, float lean) {
    float e = clamp(env, 0.0, 1.0);
    float peak = pow(lean / (lean + 1.0), lean) / (lean + 1.0);
    return pow(e, lean) * (1.0 - e) / peak;
}

/** A bounded soft glow around a distance `d` (>= 0 outside): 1 on the line, halving at `w`. */
float chrysGlow(float d, float w) {
    float x = max(d, 0.0) / max(w, 1e-5);
    return 1.0 / (1.0 + x * x);
}

/** A Gaussian core of radius `size` - the light a flower opens out of. */
float chrysCore(float r, float size) {
    float x = r / max(size, 1e-5);
    return exp(-x * x);
}

/**
 * The nearest floret of a Vogel sunflower (floret k at radius spacing*sqrt(k),
 * angle k * golden angle) to p, without looping over florets.
 *
 * WHY THIS WORKS. Every floret satisfies F*theta - delta_F*u = 0 (mod 2*pi)
 * for any Fibonacci number F, where u = (r/spacing)^2 and delta_F is how far
 * F golden angles miss a whole number of turns. Each such equation is one
 * family of F spirals (the parastichies you see in a sunflower), and two
 * consecutive Fibonacci numbers give two families whose crossings are
 * EXACTLY the florets: the determinant of the pair is 2*pi, so the crossing
 * lattice has one point per floret. So: pick the pair whose spirals are the
 * visible ones at this radius (F near 2.37*sqrt(u), where neighbours are as
 * close across as along), read the pixel's two phases, try the four lattice
 * corners around it, and keep the closest. Four candidates instead of a
 * search over hundreds of indices.
 *
 * Returns (distance to the floret centre, floret index k, offset from the
 * centre x, y).
 */
vec4 chrysFloret(vec2 p, float spacing) {
    float r = length(p);
    float u = (r / spacing) * (r / spacing);
    float th = atan(p.y, p.x);
    // Fibonacci index n with F_n just below 2.37 sqrt(u); F_n by Binet.
    float n = floor(log(max(2.37 * sqrt(u) * 2.2360680, 2.0)) / log(CHRYS_PHI));
    n = max(n, 3.0);
    float f1 = floor(pow(CHRYS_PHI, n) / 2.2360680 + 0.5);
    float f2 = floor(pow(CHRYS_PHI, n + 1.0) / 2.2360680 + 0.5);
    float d1 = f1 * CHRYS_GOLDEN - CHRYS_TAU * floor(f1 * CHRYS_GOLDEN / CHRYS_TAU + 0.5);
    float d2 = f2 * CHRYS_GOLDEN - CHRYS_TAU * floor(f2 * CHRYS_GOLDEN / CHRYS_TAU + 0.5);
    float det = f2 * d1 - f1 * d2;
    vec2 s = vec2(f1 * th - d1 * u, f2 * th - d2 * u) / CHRYS_TAU;
    vec2 s0 = floor(s);
    vec4 best = vec4(1e9, 0.0, 0.0, 0.0);
    for (int j = 0; j < 4; j++) {
        vec2 ab = s0 + vec2(float(j - 2 * (j / 2)), float(j / 2));
        float ku = CHRYS_TAU * (f1 * ab.y - f2 * ab.x) / det;
        if (ku < -0.5) continue;
        float kt = CHRYS_TAU * (ab.y * d1 - ab.x * d2) / det;
        vec2 c = spacing * sqrt(max(ku, 0.0)) * vec2(cos(kt), sin(kt));
        vec2 off = p - c;
        float d = length(off);
        if (d < best.x) best = vec4(d, floor(ku + 0.5), off);
    }
    return best;
}

// ---- the normalization shell (see the header) -----------------------------------

/** Every shape variable, normalized: 0 at rest, -1..1 from a knob or Twist, 0..1 from Morph and the Chaos pair. */
struct ChrysIn {
    float rings;
    float chaos;
    float bend;
    float curl;
    float fray;
    float width;
    float spin;
    float sway;
    float reach;
    float glow;
    float emit;
    float tone;
};

/** Style knob i, normalized: its offset from 0.5 doubled, -1..1, exactly 0 while Note & key reactions are off. */
float chrysKnob(int i) {
    return 2.0 * (clamp(uKnob[i], 0.0, 1.0) - 0.5);
}

/** Each variable from its driver (the header's table): params and knobs only, no sound. */
ChrysIn chrysIn() {
    ChrysIn n;
    n.rings = clamp(uMorph, 0.0, 1.0);
    // The Chaos pair, 0..2 on their sliders, as 0..1.
    n.chaos = 0.5 * clamp(uTurbulence + uWarp, 0.0, 2.0);
    n.bend = clamp(uWarp, 0.0, 1.0);
    n.curl = clamp(uTwist, -1.0, 1.0);
    n.fray = chrysKnob(1);
    n.width = chrysKnob(1);
    n.spin = chrysKnob(2);
    n.sway = chrysKnob(2);
    n.reach = chrysKnob(5);
    n.glow = chrysKnob(4);
    n.emit = chrysKnob(0);
    n.tone = chrysKnob(3);
    return n;
}

/**
 * A normalized n to its variable's range: `rest` at 0, `lo` at -1, `hi` at 1, straight between.
 * Exactly `rest` at 0, so the rest look is the old one bit for bit. A 0..1 variable uses rest..hi.
 */
float chrysSpan(float n, float rest, float lo, float hi) {
    return rest + n * (n < 0.0 ? rest - lo : hi - rest);
}
