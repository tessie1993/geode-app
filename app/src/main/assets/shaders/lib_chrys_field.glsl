// The Chrysanthemum family's one field: the part of lib_chrys that calls the
// style's chrysMap, which the style defines between the two includes, and the
// normal that differentiates it.
//
// Why a second file: no style in the app declares a function ahead of its
// body (style_reads_test: noStyleDeclaresAFunctionAhead), so the field that
// calls chrysMap comes after the style has defined it, as in Prism, Petal and
// the other families with a march include.
//
// Every Chrysanthemum style marches, shades and differentiates through
// chrysField and chrysNormal, never its map directly, so whatever wraps the
// field wraps the whole style once.
//
// INCLUDE ORDER: after lib_chrys and after the style's ChrysHit chrysMap(vec3 p).

/**
 * GROW AND LAYERS (lib_grow), the family's rules for Spawn, Fractal, Front and
 * Back layer, as the other families draw them: the layers open the flowers
 * about the subject at the origin (or about the axis a flown eye rides), Spawn
 * slides a second flowers off the first (1.0 along the way, or aside of a
 * subject), and Fractal grows the flowers a third the size, repeated every
 * 2.0, out of every surface within 0.2 at full. No turn and no colour.
 * Every map read sits behind its own control, and CHRYS_GROW takes the map alone
 * while all four rest, so the picture at rest is the old one, bit for bit.
 *
 * Formula: the family keeps one formula per style (no alternatives yet), so it
 * reads no uFormula; a style that grows a second joins the formula styles.
 */
ChrysHit chrysGrow(vec3 p) {
    // At rest the one pass reads the map at p and hands back what it found, bit for bit.
    bool idle = growIdle();
    float k = 1.0;
    vec3 q = p;
    float reach = 0.0;
    if (!idle) {
        growFrame();
        k = growOpen(p, 0.18);
        vec3 c = growCentre();
        q = c + growOpened(p - c, k);
        reach = growFractalReach(0.2);
    }
    // One map call, in a loop the compiler cannot unroll (it starts at a uniform): the first world, then
    // Spawn's copy and Fractal's child while each is on, then Life's three births (lib_grow) while each
    // lives. Inlined copies put a style past the GPU's compile budget, and WebGL refuses ?: on a struct,
    // so the nearer hit is chosen with an if.
    ChrysHit h;
    for (int i = min(int(uSpawn), 0); i < 6; i++) {
        if (i < 0 || (i > 0 && i < 3 && (idle || (i == 1 && !(uSpawn > 0.0)) || (i == 2 && !(reach > 0.0)))) || (i > 2 && !growBorn(i - 3))) continue;
        ChrysHit c = chrysMap(i == 0 ? q : (i == 1 ? q - growSpawnShift(vec3(0.0, 0.0, 1.0), 1.0) : (i == 2 ? growChildSpace(q, 3.0, 2.0) : growBirthAt(i - 3, p))));
        if (i == 0) {
            h = c;
            continue;
        }
        if (i == 2) c.d = growFractalChild(c.d / 3.0, h.d, reach);
        if (i > 2) c.d = growBirthDist(c.d);
        else if (gGrowSet) c.d = growClearEye(c.d, p, 0.6);
        if (c.d < h.d) h = c;
    }
    // Space scaled about a subject is an exact scaling (distance times k); opened about the eye's axis,
    // only across it, so min(k, 1) is the safe factor.
    if (!idle) h.d *= gGrowAround ? k : min(k, 1.0);
    return h;
}

/** The style's map F at p, grown: exactly F(p) while Spawn, Fractal and both layers rest. */
#define CHRYS_GROW(p, F) chrysGrow(p)

/** The family's field at world point p: the style's flowers, the distance and what was found there. It writes no global. */
ChrysHit chrysField(vec3 p) {
    return CHRYS_GROW(p, chrysMap);
}

/** The field's gradient at p, unit length: four taps of chrysField's distance. */
vec3 chrysNormal(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * chrysField(p + k.xyy * eps).d + k.yyx * chrysField(p + k.yyx * eps).d
                   + k.yxy * chrysField(p + k.yxy * eps).d + k.xxx * chrysField(p + k.xxx * eps).d);
}
