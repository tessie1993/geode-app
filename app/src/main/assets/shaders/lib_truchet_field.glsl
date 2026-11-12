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
 * THE FIELD: the style's tiling at p, through the merge hook TRUCHET_GROW
 * (lib_grow's Spawn, Fractal and layers, once that branch merges; the map
 * itself today). .d is the distance the march steps by; the rest is what the
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
