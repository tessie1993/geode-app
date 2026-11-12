// The Arbor family's one field: the part of lib_arbor that calls the style's
// arborMap, which the style defines between the two includes, and the normal
// that differentiates it.
//
// Why a second file: no style in the app declares a function ahead of its
// body (style_reads_test: noStyleDeclaresAFunctionAhead), so the field that
// calls arborMap comes after the style has defined it, as in Prism, Petal and
// the other families with a march include.
//
// Every Arbor style marches, shades and differentiates through arborField and
// arborNormal, never its map directly, so whatever wraps the field wraps the
// whole style once.
//
// INCLUDE ORDER: after lib_arbor and after the style's ArborHit arborMap(vec3 p).

/**
 * GROW AND FORMULA HOOK. Where lib_grow (Spawn, Fractal, Front and Back layer)
 * and uFormula will wrap the style's map F at p once they land: today the map
 * itself. Swap this one line at the merge.
 */
#define ARBOR_GROW(p, F) F(p)

/** The family's field at world point p: the style's world, its distance and what was found there. It writes no global. */
ArborHit arborField(vec3 p) {
    return ARBOR_GROW(p, arborMap);
}

/** The field's gradient at p, unit length: four taps of arborField's distance. */
vec3 arborNormal(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * arborField(p + k.xyy * eps).d + k.yyx * arborField(p + k.yyx * eps).d
                   + k.yxy * arborField(p + k.yxy * eps).d + k.xxx * arborField(p + k.xxx * eps).d);
}
