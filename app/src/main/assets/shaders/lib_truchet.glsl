// The Truchet family's shared kit: tiles that join into paths.
//
// WHAT A TRUCHET TILING IS
//
// Sebastien Truchet (1704) took one square tile split along its diagonal into
// two colours and showed how many patterns its four rotations make. Cyril
// Stanley Smith (1987) swapped the diagonal for two quarter circles centred on
// opposite corners, each joining the midpoints of the two edges beside its
// corner. Rotate that tile at random cell by cell and the arcs still meet their
// neighbours' arcs at every edge midpoint, at right angles to the edge, so the
// plane fills with smooth endless paths nobody drew. That is public geometry;
// everything below is written from it, not lifted from any particular shader.
//
// WHY floor/fract IS LEGAL HERE
//
// The house rule is never to wrap the picture with a hard fract(): a cell grid
// cracks anything that is not continuous across its borders. A Truchet tile is
// built to be exactly that. Every tile in this file puts its curves through
// the same points on its border (edge midpoints, or the q / 1-q points on a
// triangle's edges) and leaves them square to the edge, so whichever tile a
// neighbour holds, the curve carries straight on. What is NOT continuous is
// anything a style derives per cell on its own - a per-cell brightness, a
// per-cell hue - and those are banned in this family for the same reason: they
// would draw the grid the arcs are hiding. Colour comes from a smooth field
// of position, or from the region two-colouring below, never from the cell id.
//
// THE TWO-COLOURING, AND WHY FLOW DIRECTION FOLLOWS PARITY
//
// Every lattice edge is crossed by exactly one curve (at its midpoint), so
// walking along the lattice from vertex to vertex, each step crosses one curve:
// the curves 2-colour the plane, and the colour of the region a lattice vertex
// lies in is just that vertex's checkerboard parity. A quarter tile's two corner
// caps both hold vertices of parity (cell parity + tile bit), its middle band
// holds the other two. TruchetArc.cap is that colour.
//
// Orient every arc so the colour-1 region is on its LEFT and the orientation is
// consistent along a whole path (the region on each side of a curve does not
// change along it). Work through the tile's flip and that rule reduces to the
// cell's own checkerboard parity alone: TruchetArc.s runs one way in even cells
// and the other way in odd ones, and dashes laid along s with a whole number of
// dashes per arc flow continuously from tile to tile. The hex tile has the same
// property through the honeycomb's two vertex classes; there the direction
// follows the tile bit instead.
//
// REWIRING ON ONSETS
//
// truchetRoll() decides, per cell, whether the newest onset (floor(uHitCount))
// re-rolled it. Each cell re-rolls on every k-th onset, k and its offset hashed
// from the cell, so a hit rewires a scattered subset of the maze rather than
// all of it or a visible band of it. A re-rolled cell eases from its old tile
// to its new one over the onset envelope (uHits.w, 0.22 s): squares and hexes
// turn by a quarter / sixth turn, which is the rotation that maps one tile onto
// the other, and the region colour cross-fades as they turn. The envelope is 1
// on the frame the count steps, so the first frame of a roll is still the old
// tile - nothing snaps. Brightness never moves with a hit; only geometry.
//
// THE CUSTOMIZE MACROS
//
// Speed: truchetTime() is uTime * uSpeed. Complexity: truchetMorph() is uMorph
// 0..1; each style spends it on detail of its own (more cells, more lanes, a
// deeper quadtree). Chaos: truchetWarp() bends the plane slowly BEFORE it is
// tiled (uWarp, uTurbulence; uTwist already twists it in view()), so the paths
// meander but stay joined. The warp's slope is kept under 1, so the plane never
// folds over itself and no cell is drawn twice. Energy: truchetEnergy() is
// uIntensity clamped 0..2, the gain on everything audio. Colour: pal().
// Space: view() (zoom, rotation, drift) or the camera rig. At their defaults
// every one of these is the identity.
//
// ANTI-ALIASING
//
// Lines are drawn against a pixel footprint measured in cell units:
// truchetPx(p) from fwidth of the CONTINUOUS coordinate, taken in uniform
// control flow at the top of main(). Never fwidth something after fract() or
// after an atan(): both jump, and the derivative across the jump is a
// one-pixel seam of blur.
//
// INCLUDE ORDER
//
// After lib_scene_uniforms (reads uTime, uSpeed, uIntensity, uMorph, uWarp,
// uTurbulence). It reads no audio uniform itself - styles hand in floor(
// uHitCount), uHits.w and their own band levels - and it includes nothing,
// because the include resolver does not de-duplicate.

#define TRUCHET_PI 3.14159265
#define TRUCHET_TAU 6.2831853
/** Pointy-top hexagon of unit centre spacing: centre-to-vertex radius, 1/sqrt(3). */
#define TRUCHET_HEX_R 0.57735027
/** Where a triangle tile's curves cross each edge: at q and 1-q of the side. */
#define TRUCHET_TRI_Q 0.36

// ---- hashing -------------------------------------------------------------------
//
// Integer hashing on the cell index. A cell id is a whole number, and an
// arithmetic float hash of a whole number loses its fraction on mediump-leaning
// GPUs and stripes; uint arithmetic is exact and GLSL ES 3.00 guarantees highp
// int in the fragment stage. The mixer is a xorshift-multiply avalanche
// (public-domain construction).

uint truchetMix(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

/** 0..1 from a cell id (whole numbers; the fraction is dropped). */
float truchetHash(vec2 id) {
    uvec2 q = uvec2(ivec2(floor(id)));
    uint h = truchetMix(q.x * 0x9e3779b9u ^ truchetMix(q.y + 0x632be5abu));
    return float(h >> 8) * (1.0 / 16777216.0);
}

/** 0..1 from a cell id and a third whole number (an epoch, a layer, a level). */
float truchetHash3(vec3 id) {
    uvec3 q = uvec3(ivec3(floor(id)));
    uint h = truchetMix(q.x * 0x9e3779b9u ^ truchetMix(q.y + 0x632be5abu ^ truchetMix(q.z * 0x85ebca6bu + 0x27d4eb2fu)));
    return float(h >> 8) * (1.0 / 16777216.0);
}

mat2 truchetRot(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, s, -s, c);
}

// ---- the Customize macros ---------------------------------------------------------

/** Animation time under the Speed macro. */
float truchetTime() {
    return uTime * uSpeed;
}

/** The Energy macro: gain on in-shader audio reaction, clamped so a high setting cannot tear the tiling. */
float truchetEnergy() {
    return clamp(uIntensity, 0.0, 2.0);
}

/** The Complexity macro, 0..1. */
float truchetMorph() {
    return clamp(uMorph, 0.0, 1.0);
}

/**
 * Chaos: a slow, low-frequency bend of the plane in CELL units, applied before
 * tiling so the paths meander but stay joined. The largest slope is about 0.9
 * at the top of both sliders (0.8 * 2 * 0.31 + 0.18 * 2 * 1.13), under 1, so
 * the map never folds. Identity at default.
 */
vec2 truchetWarp(vec2 p, float t) {
    float a = 0.8 * clamp(uWarp, 0.0, 2.0);
    float b = 0.18 * clamp(uTurbulence, 0.0, 2.0);
    if (a + b <= 1e-4) return p;
    p += a * vec2(sin(p.y * 0.31 + t * 0.21), sin(p.x * 0.29 - t * 0.17 + 1.3));
    p += b * vec2(sin(p.y * 1.13 - t * 0.37 + 2.1), sin(p.x * 1.07 + t * 0.33));
    return p;
}

/**
 * The Chaos amount alone, 0 at default, about 0..2.2: for a style whose plane
 * is periodic (a tunnel's angle) and must build its own wrap-safe bend.
 */
float truchetChaos() {
    return 0.8 * clamp(uWarp, 0.0, 2.0) + 0.3 * clamp(uTurbulence, 0.0, 2.0);
}

/**
 * Bass breathing: the multiplier on the cell scale. Slightly above 1 on a low
 * end, so the tiles grow toward the viewer - zooming in on the bass. Feed it a
 * slew-limited level (uBassSmooth), never a hit, so it breathes and never jumps.
 */
float truchetBreath(float bass) {
    return 1.0 / (1.0 + 0.14 * clamp(bass, 0.0, 1.5) * truchetEnergy());
}

/**
 * A smooth, slowly moving field of position, about -1..1, in cell units. This
 * is where a style's hue variation comes from: colour keyed to the cell id
 * would draw the very grid the arcs are hiding.
 */
float truchetField(vec2 p, float t) {
    float a = sin(p.x * 0.23 + 1.7 * sin(p.y * 0.17 + t * 0.05) + t * 0.03);
    float b = sin(p.y * 0.21 - p.x * 0.07 - t * 0.04 + 1.9);
    return 0.5 * (a + b);
}

// ---- anti-aliasing --------------------------------------------------------------

/** One pixel in the units of p. Call in main() on the continuous coordinate, before any fract. */
float truchetPx(vec2 p) {
    return max(length(fwidth(p)) * 0.7071, 1e-5);
}

/** A crisp line of half-width hw about a distance d, one pixel of soft edge either side. */
float truchetLine(float d, float hw, float px) {
    return 1.0 - smoothstep(hw - px, hw + px, abs(d));
}

// ---- re-rolling on onsets ---------------------------------------------------------

/**
 * Which tile a cell holds now, and how far through a re-roll it is.
 * x = the old tile's hash, y = the new tile's hash, z = eased progress 0..1:
 * mix old to new by z. z reaches 1 as the onset envelope falls, so z == 1
 * with x != y is the new tile in full; x == y only once no re-roll is due.
 * `hits` = floor(uHitCount), `env` = uHits.w,
 * `period` = on average a cell re-rolls on one onset in `period`.
 */
vec3 truchetRoll(vec2 id, float hits, float env, float period) {
    float per = max(1.0, floor(period * (0.5 + truchetHash3(vec3(id, 91.0)))));
    float off = floor(truchetHash3(vec3(id, 37.0)) * per);
    // +0.5 keeps the division clear of whole numbers, where a GPU that
    // divides by multiplying with a reciprocal could floor the wrong way.
    float now = floor((hits + off + 0.5) / per);
    float before = floor((hits - 1.0 + off + 0.5) / per);
    float fresh = truchetHash3(vec3(id, now));
    if (now == before) return vec3(fresh, fresh, 1.0);
    float k = smoothstep(0.0, 0.8, 1.0 - clamp(env, 0.0, 1.0));
    return vec3(truchetHash3(vec3(id, before)), fresh, k);
}

// ---- the quarter-arc square tile ------------------------------------------------------

/** One arc of a tile, as seen from a pixel. */
struct TruchetArc {
    /** Signed distance to the nearest arc's centreline, cell units, negative on the cap (corner) side. */
    float d;
    /** Position along that arc, 0..1, in the path's consistent direction. */
    float s;
    /** The two-colouring: the cap side's colour, 0 or 1 (fractional while re-rolling). The band side is 1 - cap. */
    float cap;
    /** Unit direction from the arc's centre to the pixel (the gradient of d), screen frame. */
    vec2 n;
    /** Which arc of the tile, 0 or 1 (2 for the hex tile's third). */
    float lane;
};

/**
 * Smith's tile, settled. f is the position in the cell, -0.5..0.5; parity is
 * mod(cell.x + cell.y, 2); bit picks the diagonal the two arcs hug.
 * Bit 1 is drawn as bit 0 mirrored in x, so every quantity is computed once.
 */
TruchetArc truchetQuarter(vec2 f, float parity, float bit) {
    float flip = bit > 0.5 ? -1.0 : 1.0;
    vec2 g = vec2(f.x * flip, f.y);
    // Both corners seen through a half turn about them, so the angle runs
    // 0..pi/2 across the quarter in both and no atan ever meets its +-pi seam.
    vec2 w1 = vec2(0.5) - g;
    vec2 w2 = g + vec2(0.5);
    float l1 = length(w1);
    float l2 = length(w2);
    bool first = l1 < l2;
    vec2 w = first ? w1 : w2;
    float l = max(first ? l1 : l2, 1e-5);
    float a = atan(w.y, w.x) * (2.0 / TRUCHET_PI);
    TruchetArc r;
    r.d = l - 0.5;
    r.s = parity > 0.5 ? a : 1.0 - a;
    r.cap = abs(parity - bit);
    vec2 radial = (first ? -w1 : w2) / l;
    r.n = vec2(radial.x * flip, radial.y);
    r.lane = first ? 0.0 : 1.0;
    return r;
}

/**
 * The quarter tile at cell `id`, position f in the cell (-0.5..0.5), re-rolled
 * by onsets. Separate from the id so a style that wraps its cells (a tunnel's
 * angle, a fly-through's travel) can hand in the wrapped id; keep any wrap
 * period even, or the parity breaks at the wrap.
 */
TruchetArc truchetSquareAt(vec2 id, vec2 f, float hits, float env, float period) {
    float parity = mod(id.x + id.y, 2.0);
    vec3 roll = truchetRoll(id, hits, env, period);
    float bitOld = step(0.5, roll.x);
    float bitNew = step(0.5, roll.y);
    if (bitOld == bitNew || roll.z >= 1.0) return truchetQuarter(f, parity, bitNew);
    // A quarter turn maps one diagonal onto the other: turn the old tile by
    // progress * 90 degrees, and fade its colouring to the new tile's.
    float a = roll.z * TRUCHET_PI * 0.5;
    TruchetArc r = truchetQuarter(truchetRot(-a) * f, parity, bitOld);
    r.n = truchetRot(a) * r.n;
    r.cap = mix(r.cap, 1.0 - r.cap, roll.z);
    return r;
}

/** The quarter tiling of the plane p (cell units). */
TruchetArc truchetSquare(vec2 p, float hits, float env, float period) {
    return truchetSquareAt(floor(p), fract(p) - 0.5, hits, env, period);
}

/** Smooth two-tone fill from an arc: the cap colour inside, the band colour outside, one pixel of blend. */
float truchetTone(TruchetArc a, float px) {
    return mix(1.0 - a.cap, a.cap, 1.0 - smoothstep(-px, px, a.d));
}

// ---- the hexagonal three-arc tile ---------------------------------------------------------

/**
 * Hexagonal cells of unit centre spacing, pointy top (vertices at 30 + 60k
 * degrees, edge midpoints at 0 + 60k, 0.5 from the centre). xy = position from
 * the cell centre, zw = a whole-number id (the two offset lattices land on odd
 * and even numbers, so no two cells share one).
 */
vec4 truchetHexCell(vec2 p) {
    const vec2 s = vec2(1.0, 1.7320508);
    vec2 a = floor(p / s) + 0.5;
    vec2 b = floor(p / s + 0.5);
    vec2 ha = p - a * s;
    vec2 hb = p - b * s;
    return dot(ha, ha) < dot(hb, hb) ? vec4(ha, a * 2.0) : vec4(hb, b * 2.0);
}

/**
 * The hex tile, settled: three 120-degree arcs around alternate vertices, each
 * of radius half a side, joining the midpoints of the two edges at that vertex.
 * Bit 0 takes the vertices at 30/150/270 degrees, bit 1 those at 90/210/330 -
 * the same set turned half a turn, so bit 1 is bit 0 seen through a point
 * reflection (which keeps orientation). The vertex class IS the bit across the
 * whole honeycomb, so the region colour and the flow direction follow it.
 */
TruchetArc truchetHexTile(vec2 h, float bit) {
    float sg = bit > 0.5 ? -1.0 : 1.0;
    vec2 g = h * sg;
    const float r = TRUCHET_HEX_R * 0.5;
    vec2 d0 = vec2(0.8660254, 0.5);
    vec2 d1 = vec2(-0.8660254, 0.5);
    vec2 d2 = vec2(0.0, -1.0);
    vec2 v0 = g - d0 * TRUCHET_HEX_R;
    vec2 v1 = g - d1 * TRUCHET_HEX_R;
    vec2 v2 = g - d2 * TRUCHET_HEX_R;
    float l0 = dot(v0, v0);
    float l1 = dot(v1, v1);
    float l2 = dot(v2, v2);
    vec2 v = v0;
    vec2 dir = d0;
    float lane = 0.0;
    if (l1 < l0 && l1 <= l2) {
        v = v1;
        dir = d1;
        lane = 1.0;
    } else if (l2 < l0 && l2 < l1) {
        v = v2;
        dir = d2;
        lane = 2.0;
    }
    float l = max(length(v), 1e-5);
    // Angle about the vertex, measured from the direction back to the centre:
    // the arc spans -60..60 degrees there, far from atan's seam.
    vec2 u = -dir;
    float phi = atan(u.x * v.y - u.y * v.x, dot(u, v));
    float a = phi * (1.5 / TRUCHET_PI) + 0.5;
    TruchetArc res;
    res.d = l - r;
    res.s = bit > 0.5 ? a : 1.0 - a;
    res.cap = bit;
    res.n = v / l * sg;
    res.lane = lane;
    return res;
}

/** The hex tiling of the plane p, re-rolled by onsets (a sixth of a turn maps one tile onto the other). */
TruchetArc truchetHex(vec2 p, float hits, float env, float period) {
    vec4 c = truchetHexCell(p);
    vec3 roll = truchetRoll(c.zw, hits, env, period);
    float bitOld = step(0.5, roll.x);
    float bitNew = step(0.5, roll.y);
    if (bitOld == bitNew || roll.z >= 1.0) return truchetHexTile(c.xy, bitNew);
    float a = roll.z * TRUCHET_PI / 3.0;
    TruchetArc r = truchetHexTile(truchetRot(-a) * c.xy, bitOld);
    r.n = truchetRot(a) * r.n;
    r.cap = mix(r.cap, 1.0 - r.cap, roll.z);
    return r;
}

// ---- the triangle tile -------------------------------------------------------------------

/** A cell of the unit triangular lattice: its corners counter-clockwise, and a whole-number id. */
struct TruchetTri {
    vec2 a;
    vec2 b;
    vec2 c;
    vec3 id;
};

/**
 * The triangle holding p. Lattice basis (1, 0) and (1/2, sqrt3/2); each
 * rhombus of the skewed grid splits into a lower and an upper triangle. The
 * upper one's corners are listed as the lower's turned half a turn, so both
 * run counter-clockwise.
 */
TruchetTri truchetTriCell(vec2 p) {
    float bj = p.y / 0.8660254;
    float ai = p.x - 0.5 * bj;
    vec2 cell = floor(vec2(ai, bj));
    vec2 fr = vec2(ai, bj) - cell;
    float up = step(1.0, fr.x + fr.y);
    vec2 o = vec2(cell.x + 0.5 * cell.y, cell.y * 0.8660254);
    const vec2 e1 = vec2(1.0, 0.0);
    const vec2 e2 = vec2(0.5, 0.8660254);
    TruchetTri t;
    if (up > 0.5) {
        t.a = o + e1 + e2;
        t.b = o + e2;
        t.c = o + e1;
    } else {
        t.a = o;
        t.b = o + e1;
        t.c = o + e2;
    }
    t.id = vec3(cell, up);
    return t;
}

/**
 * The triangle tile. Every edge is crossed at q and 1-q of its length, square
 * to the edge, so any tile meets any neighbour. Variant 0: a radius-q arc
 * round each corner. Variant k (1..3): corner k holds two arcs (radius q and
 * 1-q), and the edge facing it a half circle about its midpoint (radius
 * 1/2 - q) - the six crossing points again, paired another way.
 *
 * Every lattice edge is crossed twice, so every lattice vertex lies in the same
 * colour of the curves' two-colouring, and a point's colour is the parity of
 * the curves between it and corner a. Returns x = distance to the nearest
 * curve (lattice units), y = that colour 0/1, z = which curve (0..2).
 */
vec3 truchetTriTile(TruchetTri t, vec2 p, float variant) {
    const float q = TRUCHET_TRI_Q;
    vec2 c0 = t.a;
    vec2 c1 = t.b;
    vec2 c2 = t.c;
    vec3 rad = vec3(q);
    if (variant > 0.5) {
        vec2 k = variant < 1.5 ? t.a : (variant < 2.5 ? t.b : t.c);
        vec2 o1 = variant < 1.5 ? t.b : (variant < 2.5 ? t.c : t.a);
        vec2 o2 = variant < 1.5 ? t.c : (variant < 2.5 ? t.a : t.b);
        c0 = k;
        c1 = k;
        c2 = 0.5 * (o1 + o2);
        rad = vec3(q, 1.0 - q, 0.5 - q);
    }
    vec3 l = vec3(length(p - c0), length(p - c1), length(p - c2));
    vec3 lr = vec3(length(t.a - c0), length(t.a - c1), length(t.a - c2));
    vec3 dd = abs(l - rad);
    vec3 inside = step(l, rad);
    vec3 ref = step(lr, rad);
    float tone = mod(dot(abs(inside - ref), vec3(1.0)), 2.0);
    float d = min(dd.x, min(dd.y, dd.z));
    float which = d == dd.x ? 0.0 : (d == dd.y ? 1.0 : 2.0);
    return vec3(d, tone, which);
}

/** Which triangle variant a hash picks: the plain tile 40% of the time, each corner variant 20%. */
float truchetTriVariant(float h) {
    return h < 0.4 ? 0.0 : min(1.0 + floor((h - 0.4) * 5.0), 3.0);
}
