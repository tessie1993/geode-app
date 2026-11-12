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
 * THE FIELD: the style's glass at p, through the merge hook GLASS_GROW
 * (lib_grow's Spawn, Fractal and layers, once that branch merges; the map
 * itself today). .d is the distance the march steps by; the rest is what the
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
