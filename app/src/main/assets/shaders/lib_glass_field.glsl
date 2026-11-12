// The Glass family's field: the part of lib_glass that calls the style's
// glassMap, which a marched style defines between the two includes. Its outside
// march, interior march, normal and shading read the glass through here.
//
// Why a second file: no shader in the app declares a function ahead (a
// prototype) and calls it before its body (style_reads_test:
// noStyleDeclaresAFunctionAhead), so the field comes after the style's map, as
// lib_shard_march does for the Shard family.
//
// INCLUDE ORDER: after lib_glass, the style's `struct GlassHit { float d; ...
// }` and its `GlassHit glassMap(vec3 p)`. Gem and Prism, traced in closed
// form, have no map and do not include it.

/**
 * GROW AND LAYERS (lib_grow), the family's rules for Spawn, Fractal, Front and
 * Back layer, as the other families draw them: the layers open the glass
 * about the subject at the origin (or about the axis a flown eye rides), Spawn
 * slides a second glass off the first (1.0 along the way, or aside of a
 * subject), and Fractal grows the glass a third the size, repeated every
 * 2.0, out of every surface within 0.2 at full. No turn and no colour.
 * Every map read sits behind its own control, and GLASS_GROW (lib_glass) takes
 * the map alone while all four rest, so the picture at rest is the old one,
 * bit for bit.
 */
GlassHit glassGrow(vec3 p) {
    growFrame();
    float k = growOpen(p, 0.18);
    vec3 c = growCentre();
    vec3 q = c + growOpened(p - c, k);
    GlassHit h = glassMap(q);
    if (uSpawn > 0.0) {
        GlassHit s = glassMap(q - growSpawnShift(vec3(0.0, 0.0, 1.0), 1.0));
        if (gGrowSet) s.d = growClearEye(s.d, p, 0.6);
        if (s.d < h.d) h = s;
    }
    float reach = growFractalReach(0.2);
    if (reach > 0.0) {
        GlassHit ch = glassMap(growChildSpace(q, 3.0, 2.0));
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
 * THE FIELD: the style's glass at p, through GLASS_GROW (lib_grow's Spawn,
 * Fractal and layers, glassGrow above; the map itself while they rest). .d is the distance the march steps by; the rest is what the
 * style's shading reads there. Writes no global.
 */
GlassHit glassField(vec3 p) {
    return GLASS_GROW(p, glassMap);
}

/** The field's gradient at p by the tetrahedral difference, step e. */
vec3 glassNormal(vec3 p, float e) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * glassField(p + k.xyy * e).d + k.yyx * glassField(p + k.yyx * e).d
                   + k.yxy * glassField(p + k.yxy * e).d + k.xxx * glassField(p + k.xxx * e).d);
}
