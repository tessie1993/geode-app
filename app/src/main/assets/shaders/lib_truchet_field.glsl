// The Truchet family's field: the part of lib_truchet that calls the style's
// truchetMap, which the style defines between the two includes. Every march,
// normal and shading read in the family goes through here.
//
// Why a second file: no shader in the app declares a function ahead (a
// prototype) and calls it before its body (style_reads_test:
// noStyleDeclaresAFunctionAhead), so the field comes after the style's map, as
// lib_shard_march does for the Shard family.
//
// INCLUDE ORDER: after lib_truchet, the style's `struct TruchetHit { float d;
// ... }` and its `TruchetHit truchetMap(vec3 p)`.

/**
 * GROW AND LAYERS (lib_grow), the family's rules for Spawn, Fractal, Front and
 * Back layer, as the other families draw them: the layers open the tiling
 * about the subject at the origin (or about the axis a flown eye rides), Spawn
 * slides a second tiling off the first (1.0 along the way, or aside of a
 * subject), and Fractal grows the tiling a third the size, repeated every
 * 2.0, out of every surface within 0.2 at full. No turn and no colour.
 * Every map read sits behind its own control, and TRUCHET_GROW (lib_truchet) takes
 * the map alone while all four rest, so the picture at rest is the old one,
 * bit for bit.
 */
TruchetHit truchetGrow(vec3 p) {
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
    // Spawn's copy and Fractal's child while each is on. Inlined copies put a style past the GPU's
    // compile budget, and WebGL refuses ?: on a struct, so the nearer hit is chosen with an if.
    TruchetHit h;
    for (int i = min(int(uSpawn), 0); i < 3; i++) {
        if (i < 0 || (i > 0 && (idle || (i == 1 && !(uSpawn > 0.0)) || (i == 2 && !(reach > 0.0))))) continue;
        TruchetHit c = truchetMap(i == 0 ? q : (i == 1 ? q - growSpawnShift(vec3(0.0, 0.0, 1.0), 1.0) : growChildSpace(q, 3.0, 2.0)));
        if (i == 0) {
            h = c;
            continue;
        }
        if (i == 2) c.d = growFractalChild(c.d / 3.0, h.d, reach);
        if (gGrowSet) c.d = growClearEye(c.d, p, 0.6);
        if (c.d < h.d) h = c;
    }
    // Space scaled about a subject is an exact scaling (distance times k); opened about the eye's axis,
    // only across it, so min(k, 1) is the safe factor.
    if (!idle) h.d *= gGrowAround ? k : min(k, 1.0);
    return h;
}

/**
 * THE FIELD: the style's tiling at p, through TRUCHET_GROW (lib_grow's Spawn,
 * Fractal and layers, truchetGrow above; the map itself while they rest). .d is the distance the march steps by; the rest is what the
 * style's shading reads there. Writes no global.
 */
TruchetHit truchetField(vec3 p) {
    return TRUCHET_GROW(p, truchetMap);
}

/** The field's gradient at p by the tetrahedral difference, step eps. */
vec3 truchetNormal(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * truchetField(p + k.xyy * eps).d + k.yyx * truchetField(p + k.yyx * eps).d
                   + k.yxy * truchetField(p + k.yxy * eps).d + k.xxx * truchetField(p + k.xxx * eps).d);
}
