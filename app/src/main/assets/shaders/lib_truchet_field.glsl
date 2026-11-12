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
    growFrame();
    float k = growOpen(p, 0.18);
    vec3 c = growCentre();
    vec3 q = c + growOpened(p - c, k);
    TruchetHit h = truchetMap(q);
    if (uSpawn > 0.0) {
        TruchetHit s = truchetMap(q - growSpawnShift(vec3(0.0, 0.0, 1.0), 1.0));
        if (gGrowSet) s.d = growClearEye(s.d, p, 0.6);
        if (s.d < h.d) h = s;
    }
    float reach = growFractalReach(0.2);
    if (reach > 0.0) {
        TruchetHit ch = truchetMap(growChildSpace(q, 3.0, 2.0));
        ch.d = growFractalChild(ch.d / 3.0, h.d, reach);
        if (gGrowSet) ch.d = growClearEye(ch.d, p, 0.6);
        if (ch.d < h.d) h = ch;
    }
    // Space scaled about a subject is an exact scaling (distance times k); opened about the eye's axis,
    // only across it, so min(k, 1) is the safe factor.
    h.d *= gGrowAround ? k : min(k, 1.0);
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
