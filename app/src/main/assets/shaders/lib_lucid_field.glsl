// The Lucid family's one field: the part of lib_lucid that calls the
// style's lucidMap, which the style defines between the two includes, and the
// normal that differentiates it.
//
// Why a second file: no style in the app declares a function ahead of its
// body (style_reads_test: noStyleDeclaresAFunctionAhead), so the field that
// calls lucidMap comes after the style has defined it, as in Prism, Petal and
// the other families with a march include.
//
// Every Lucid style marches, shades and differentiates through
// lucidField and lucidNormal, never its map directly, so whatever wraps the
// field wraps the whole style once.
//
// INCLUDE ORDER: after lib_lucid and after the style's LucidHit lucidMap(vec3 p).

/**
 * GROW AND LAYERS (lib_grow), the family's rules for Spawn, Fractal, Front and
 * Back layer, as the other families draw them: the layers open the world
 * about the subject at the origin (or about the axis a flown eye rides), Spawn
 * slides a second world off the first (1.0 along the way, or aside of a
 * subject), and Fractal grows the world a third the size, repeated every
 * 2.0, out of every surface within 0.2 at full. No turn and no colour.
 * Every map read sits behind its own control, and LUCID_GROW takes the map alone
 * while all four rest, so the picture at rest is the old one, bit for bit.
 *
 * Formula: the family keeps one formula per style (no alternatives yet), so it
 * reads no uFormula; a style that grows a second joins the formula styles.
 */
LucidHit lucidGrow(vec3 p) {
    growFrame();
    float k = growOpen(p, 0.18);
    vec3 c = growCentre();
    vec3 q = c + growOpened(p - c, k);
    LucidHit h = lucidMap(q);
    if (uSpawn > 0.0) {
        LucidHit s = lucidMap(q - growSpawnShift(vec3(0.0, 0.0, 1.0), 1.0));
        if (gGrowSet) s.d = growClearEye(s.d, p, 0.6);
        if (s.d < h.d) h = s;
    }
    float reach = growFractalReach(0.2);
    if (reach > 0.0) {
        LucidHit ch = lucidMap(growChildSpace(q, 3.0, 2.0));
        ch.d = growFractalChild(ch.d / 3.0, h.d, reach);
        if (gGrowSet) ch.d = growClearEye(ch.d, p, 0.6);
        if (ch.d < h.d) h = ch;
    }
    // Space scaled about a subject is an exact scaling (distance times k); opened about the eye's axis,
    // only across it, so min(k, 1) is the safe factor.
    h.d *= gGrowAround ? k : min(k, 1.0);
    return h;
}

/** The style's map F at p, grown: exactly F(p) while Spawn, Fractal and both layers rest. */
#define LUCID_GROW(p, F) (growIdle() ? F(p) : lucidGrow(p))

/** The family's field at world point p: the style's flowers, the distance and what was found there. It writes no global. */
LucidHit lucidField(vec3 p) {
    return LUCID_GROW(p, lucidMap);
}

/** The field's gradient at p, unit length: four taps of lucidField's distance. */
vec3 lucidNormal(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * lucidField(p + k.xyy * eps).d + k.yyx * lucidField(p + k.yyx * eps).d
                   + k.yxy * lucidField(p + k.yxy * eps).d + k.xxx * lucidField(p + k.xxx * eps).d);
}
