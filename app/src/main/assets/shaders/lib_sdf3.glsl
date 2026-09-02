// The 3D signed-distance toolkit the raymarched fragment styles share.
//
// Nothing here reads a uniform. It is pure geometry, so a style can lift a
// primitive out, deform it and blend it without inheriting an opinion about
// what the music is doing.
//
// ---- RAYMARCH: the caps, and why they are caps ----------------------------
//
// Every loop in a marched style needs a COMPILE-TIME constant bound and an
// early break. The bound is what the driver unrolls against and what stops a
// pathological ray from hanging the compositor; the break is what makes the
// common ray cheap. A `while (true)` on a distance estimate that returns NaN
// (see smin's k guard) is an unrecoverable GPU hang, not a slow frame.
//
//   surface march     <= 128 steps   (MarchBudget.MAX_STEPS)
//   inner volume loop <= 32 steps    (a second loop inside a hit costs the
//                                     first loop's count every time it runs)
//   fbm3 octaves      <= 6           (FBM3_MAX_OCTAVES below)
//
// The RUNTIME budget is the uSteps uniform - MarchBudget.forDetail() driven by
// the user's Detail control - and it is what the body breaks on:
//
//     for (int i = 0; i < 128; i++) {
//         if (float(i) >= uSteps) break;
//         ...
//     }
//
// so Detail moves without a recompile while the bound stays constant.
//
// ---- SURFACE NORMALS ------------------------------------------------------
//
// There is no sdfNormal() here, and there cannot be: GLSL ES has no function
// pointers, so a shared normal helper has no way to call YOUR map(). Write
// these four lines against your own map() instead - the tetrahedron 4-tap,
// which costs 4 map() evaluations rather than the 6 a central difference
// needs, and stays symmetric so a flat surface reads flat:
//
//     vec3 normalAt(vec3 p, float eps) {
//         vec2 k = vec2(1.0, -1.0);
//         return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
//                        + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
//     }
//
// eps should track the pixel footprint, not be a constant: too small and the
// normal is quantization noise at grazing angles, too large and every edge
// rounds off. `eps = max(hitEpsilon, 0.0015 * distanceMarched)` is the usual
// shape.
//
// ---- LIPSCHITZ ------------------------------------------------------------
//
// A raymarch is only safe while the estimate NEVER overestimates the distance
// to the surface. Every deform below that is not a rigid motion (opTwist above
// all) breaks that: it stretches space, so the estimate measured in the
// deformed frame can exceed the true distance in the marched frame and the ray
// steps through thin geometry. Divide the estimate by a bound on the deform's
// Jacobian norm, the way hyperspace_frag divides map() by uLipschitz. Each
// helper below states its own bound.

// ---- smooth combination ---------------------------------------------------

/**
 * Smooth minimum - iq's CURRENT quadratic form.
 *
 * The old exponential/power smin OVERESTIMATED the distance near the blend,
 * which is exactly the failure a raymarch cannot survive: the ray takes a step
 * longer than the true clearance and walks through the seam. This one never
 * overestimates, so it is safe to march.
 *
 * It is NOT associative. smin(smin(a, b, k), c, k) != smin(a, smin(b, c, k), k)
 * - the first form blends a-b at full k and then blends that RESULT against c,
 * so c sees a surface that has already been rounded. Blend order is part of the
 * shape; pick one and keep it, or the model changes when someone reorders the
 * arguments to tidy them up.
 *
 * k is the blend radius in world units, and 0 is a hard min.
 */
float smin(float a, float b, float k) {
    // A zero k would divide 0 by 0 below. One NaN distance is not a bad pixel:
    // the ray never terminates, the loop runs to its cap on every pixel behind
    // it, and the frame time collapses. Clamped rather than branched because
    // the branch would be non-uniform across the quad.
    k = max(k, 1e-5) * 4.0;
    float h = max(k - abs(a - b), 0.0) / k;
    return min(a, b) - h * h * k * 0.25;
}

/**
 * smin that also reports WHICH side won, for colouring the seam.
 *
 * .x is exactly what smin() returns for the same arguments. .y is the blend
 * factor: 0 deep inside a, 1 deep inside b, 0.5 on the seam - so a style can
 * mix two materials with it and the join is coloured as smoothly as it is
 * shaped. Without this the seam takes whichever material won min() and a
 * smooth surface gets a hard colour edge across it.
 */
vec2 sminMat(float a, float b, float k) {
    k = max(k, 1e-5) * 4.0;
    float h = max(k - abs(a - b), 0.0) / k;
    float m = h * h * 0.5;
    float s = m * k * 0.5;
    return (a < b) ? vec2(a - s, m) : vec2(b - s, 1.0 - m);
}

/**
 * Smooth maximum - the polynomial smin reflected, which is what makes it the
 * matching operation rather than a different curve with the same name.
 *
 * Intersection and subtraction are built from it: smax(a, b, k) intersects,
 * smax(a, -b, k) subtracts b from a. Both UNDERESTIMATE nothing on the
 * surviving surface but can overestimate inside a subtracted cavity, which is
 * why a subtracted model wants a step scale below 1.
 */
float smax(float a, float b, float k) {
    return -smin(-a, -b, k);
}

// ---- primitives -----------------------------------------------------------

/** Exact. */
float sdSphere(vec3 p, float r) {
    return length(p) - r;
}

/**
 * Exact box of half-extents b, inside and out.
 *
 * The two terms split the two cases: outside, the distance is the length of
 * the positive part of the corner offset; inside, every component is negative
 * and the largest of them is the distance to the nearest face. Neither term is
 * correct alone and neither branches.
 */
float sdBox(vec3 p, vec3 b) {
    vec3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

/** Exact. t.x = ring radius about +y, t.y = tube radius. */
float sdTorus(vec3 p, vec2 t) {
    vec2 q = vec2(length(p.xz) - t.x, p.y);
    return length(q) - t.y;
}

/**
 * Exact octahedron of "radius" s (the distance from centre to each vertex).
 *
 * The cheap version of this shape is `(|x|+|y|+|z|-s) * 0.57735` - the plane
 * distance - which is correct on the faces and an OVERESTIMATE near the edges
 * and vertices, so a march grazing a corner steps through it. The folds below
 * pick the correct Voronoi region first; only the interior case can use the
 * plane form, and that is the early return.
 */
float sdOctahedron(vec3 p, float s) {
    p = abs(p);
    float m = p.x + p.y + p.z - s;
    vec3 q;
    if (3.0 * p.x < m) {
        q = p.xyz;
    } else if (3.0 * p.y < m) {
        q = p.yzx;
    } else if (3.0 * p.z < m) {
        q = p.zxy;
    } else {
        return m * 0.57735027;
    }
    float k = clamp(0.5 * (q.z - q.y + s), 0.0, s);
    return length(vec3(q.x, q.y - s + k, q.z - k));
}

// ---- deformations ---------------------------------------------------------

/**
 * Rotate about +y by an angle proportional to height: a twist of k radians per
 * world unit.
 *
 * NOT distance-preserving. A point at radius r from the axis is displaced by
 * up to k*r per unit of y, so the Jacobian norm is bounded by
 * sqrt(1 + (k*r)^2) over the region you actually march. Divide your estimate
 * by that bound (a constant computed from your model's bounding radius) or the
 * twist will let rays through the thin parts of the geometry.
 */
vec3 opTwist(vec3 p, float k) {
    float a = k * p.y;
    float c = cos(a);
    float s = sin(a);
    return vec3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);
}

/**
 * Round a shape by r: inflate the surface outward everywhere.
 *
 * Exact and Lipschitz-1 - it is a constant offset of the field, not a
 * deformation of space - so it costs a raymarch nothing. It is the cheapest
 * way to take the hardness off an edge; prefer it to a smin against a sphere.
 */
float opRound(float d, float r) {
    return d - r;
}

/**
 * Hollow a solid into a shell of half-thickness t.
 *
 * abs() of a signed field is the distance to the SURFACE rather than to the
 * solid, so this is exact wherever the original was - but the field now has a
 * crease on the old medial axis, and a ray that lands on it gets a normal that
 * is the average of two opposite faces. Keep t well under the shape's own
 * smallest radius.
 */
float opOnion(float d, float t) {
    return abs(d) - t;
}

// ---- rotations ------------------------------------------------------------

/**
 * 2D rotation by a radians.
 *
 * Spelled in the same order view() spells it - mat2(cos, -sin, sin, cos), which
 * in GLSL's column-major constructor is the clockwise rotation - so a style
 * that rotates a plane with this and one that rotates it inline agree about
 * which way is positive.
 */
mat2 rot2(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, -s, s, c);
}

/** Right-handed rotation about +x. Columns, GLSL order. */
mat3 rotX(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat3(1.0, 0.0, 0.0, 0.0, c, s, 0.0, -s, c);
}

/** Right-handed rotation about +y. */
mat3 rotY(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat3(c, 0.0, -s, 0.0, 1.0, 0.0, s, 0.0, c);
}

/** Right-handed rotation about +z. */
mat3 rotZ(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat3(c, s, 0.0, -s, c, 0.0, 0.0, 0.0, 1.0);
}

/**
 * Rodrigues: rotation by a radians about an arbitrary axis.
 *
 * A degenerate axis falls back to +y instead of normalizing a zero vector -
 * NaN in a rotation matrix propagates into the ray direction and kills every
 * step after it, and an axis that shrinks to zero is exactly what an
 * audio-driven or interpolated axis does at a crossing.
 */
mat3 rotAxis(vec3 axis, float a) {
    float len = length(axis);
    vec3 u = len > 1e-6 ? axis / len : vec3(0.0, 1.0, 0.0);
    float c = cos(a);
    float s = sin(a);
    float t = 1.0 - c;
    return mat3(
        t * u.x * u.x + c, t * u.x * u.y + s * u.z, t * u.x * u.z - s * u.y,
        t * u.x * u.y - s * u.z, t * u.y * u.y + c, t * u.y * u.z + s * u.x,
        t * u.x * u.z + s * u.y, t * u.y * u.z - s * u.x, t * u.z * u.z + c
    );
}

// ---- hashing and noise ----------------------------------------------------
//
// NOT fract(sin(dot(p, k))). That hash depends on sin() being garbage far from
// zero, and on a device that evaluates it at mediump - which GLSL ES 3.00
// permits, and which Mali does - the argument loses the low bits that WERE the
// hash, so it degenerates into visible bands and repeats. It also differs
// between drivers, so a field authored on one phone is a different field on
// another. The integer hash below has no such dependence: it is exact on any
// conforming ES 3.00 device because integer arithmetic is exact by definition.

/**
 * Wellons' "lowbias32" integer mixer - the xor-shift-multiply chain with the
 * lowest measured avalanche bias of the 3-round 32-bit mixers.
 */
uint uhash(uint x) {
    x ^= x >> 16u;
    x *= 0x7feb352du;
    x ^= x >> 15u;
    x *= 0x846ca68bu;
    x ^= x >> 16u;
    return x;
}

/**
 * A hashed uint to [0,1).
 *
 * The top 24 bits only: float has 24 mantissa bits, so dividing the full 32
 * would round and two neighbouring hashes could land on the same float. This
 * form is an exact division by a power of two and covers [0,1) uniformly.
 */
float uhashUnit(uint h) {
    return float(h >> 8u) * (1.0 / 16777216.0);
}

/** Hashes the three coordinates into one value, mixing so the axes cannot cancel. */
uint uhash3(uvec3 q) {
    return uhash(q.x ^ uhash(q.y + 0x9e3779b9u) ^ uhash(q.z + 0x85ebca6bu));
}

/** Scalar hash to [0,1). Offset so that hash11(0.0) is not the hash of zero bits. */
float hash11(float p) {
    return uhashUnit(uhash(floatBitsToUint(p) + 0x9e3779b9u));
}

/**
 * vec3 to [0,1), hashed on the BIT PATTERN, so it is stable for any input
 * rather than only for lattice points.
 *
 * Lattice sampling (vnoise3 below) goes through the ivec3 form instead:
 * floor() can return -0.0, whose bits differ from +0.0, and one lattice corner
 * hashing two ways puts a seam through the origin.
 */
float hash13(vec3 p) {
    return uhashUnit(uhash3(floatBitsToUint(p)));
}

/** Lattice hash. int -> uint is two's complement, so negative cells stay distinct. */
float hashCell(ivec3 c) {
    return uhashUnit(uhash3(uvec3(c)));
}

/**
 * Value noise on the integer lattice, quintic-smoothed.
 *
 * Quintic (6t^5 - 15t^4 + 10t^3) rather than smoothstep's cubic because its
 * SECOND derivative is also zero at the cell boundary. A cubic leaves a
 * curvature discontinuity there, invisible in the noise itself and very
 * visible in anything that differentiates it - a normal built from an fbm
 * displacement shows the lattice as a grid of creases.
 */
float vnoise3(vec3 p) {
    vec3 f = floor(p);
    ivec3 i = ivec3(f);
    vec3 t = p - f;
    vec3 w = t * t * t * (t * (t * 6.0 - 15.0) + 10.0);

    float c000 = hashCell(i + ivec3(0, 0, 0));
    float c100 = hashCell(i + ivec3(1, 0, 0));
    float c010 = hashCell(i + ivec3(0, 1, 0));
    float c110 = hashCell(i + ivec3(1, 1, 0));
    float c001 = hashCell(i + ivec3(0, 0, 1));
    float c101 = hashCell(i + ivec3(1, 0, 1));
    float c011 = hashCell(i + ivec3(0, 1, 1));
    float c111 = hashCell(i + ivec3(1, 1, 1));

    float x00 = mix(c000, c100, w.x);
    float x10 = mix(c010, c110, w.x);
    float x01 = mix(c001, c101, w.x);
    float x11 = mix(c011, c111, w.x);
    return mix(mix(x00, x10, w.y), mix(x01, x11, w.y), w.z);
}

/** The compile-time ceiling on fbm3's loop. See the RAYMARCH note at the top. */
#define FBM3_MAX_OCTAVES 6

/**
 * Fractal sum of vnoise3, normalized to [0,1] whatever the octave count.
 *
 * Two details that are not decoration:
 *
 * - the lacunarity is 2.02, not 2. At exactly 2 every octave's lattice lands on
 *   the previous one's and the cell grid reinforces itself into visible axis-
 *   aligned structure. The offset is small enough that the octaves still read
 *   as one field and large enough that they never re-register.
 * - each octave is displaced by an irrational-looking offset for the same
 *   reason: without it the octaves share their zero crossings at the origin.
 *
 * The sum is divided by the amplitude total rather than by a constant, so
 * dropping octaves for Detail changes the DETAIL and not the brightness.
 */
float fbm3(vec3 p, int octaves) {
    float sum = 0.0;
    float amp = 0.5;
    float norm = 0.0;
    for (int i = 0; i < FBM3_MAX_OCTAVES; i++) {
        if (i >= octaves) break;
        sum += amp * vnoise3(p);
        norm += amp;
        p = p * 2.02 + vec3(37.13, 11.71, 53.29);
        amp *= 0.5;
    }
    return norm > 0.0 ? sum / norm : 0.0;
}
